/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sdio.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ipc.h"
#include "session.h"
#ifndef SD_IO_FAULT_MARKER
#define SD_IO_FAULT_MARKER "/tmp/.diskos_sd_io_fault"
#endif
#ifndef SD_EXPORT_MARKER
#define SD_EXPORT_MARKER "/tmp/.diskos_sd_export_intent"
#endif
#define SD_PHASE_MS 20000
#define SD_GATE_POLLS 60

typedef enum { SD_LOCAL, SD_DRAIN, SD_WAIT_HOST, SD_HOST, SD_WAIT_LOCAL, SD_UNKNOWN } sd_phase_t;

static const struct sd_env *SDE;
void sd_set_env(const struct sd_env *env){ SDE = env; }
static uint32_t sd_now(void){ return SDE->now(); }

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_sd_mode_mu = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_users;
static int g_allowed, g_fault, g_ready;
static int g_sd_writable = 1;
static int g_sd_writers;
static _Atomic int g_sd_hold = 0;
static _Atomic int g_launch_verdict = -1;
static _Atomic int g_sd_quiesce = 0;
static sd_phase_t g_sd_phase = SD_LOCAL;
static uint32_t g_sd_deadline;
static int g_sd_return_mode;
static int g_sd_reissue;
static int g_gate_waited, g_gate_started;

static void fault_note(void){ g_fault = g_fault || access(SD_IO_FAULT_MARKER, F_OK) == 0; }
static void set_writable(int v){
    pthread_mutex_lock(&g_mu);
    g_sd_writable = v;
    pthread_mutex_unlock(&g_mu);
}
static void card_close(void){
    pthread_mutex_lock(&g_mu);
    g_allowed = 0; g_sd_writable = 0;
    fault_note();
    pthread_mutex_unlock(&g_mu);
}
static void card_lost(void){ card_close(); g_sd_phase = SD_UNKNOWN; }

static int media_local(void){
    if(atomic_load(&g_launch_verdict) != 1) return 0;
    return !atomic_load(&g_sd_hold) && !SDE->exported_to_host() && SDE->media_mounted();
}
int sd_io_begin(void){
    pthread_mutex_lock(&g_mu);
    int ok = g_allowed && g_ready && media_local();
    if(ok) g_users++;
    pthread_mutex_unlock(&g_mu);
    return ok;
}
void sd_io_end(void){
    pthread_mutex_lock(&g_mu);
    assert(g_users > 0);
    g_users--;
    pthread_mutex_unlock(&g_mu);
}
int sd_write_begin(void){
    pthread_mutex_lock(&g_mu);
    int ok = g_sd_writable;
    if(ok) g_sd_writers++;
    pthread_mutex_unlock(&g_mu);
    if(!ok) return 0;
    if(SDE->exported_to_host()){
        pthread_mutex_lock(&g_mu);
        g_sd_writers--;
        pthread_mutex_unlock(&g_mu);
        return 0;
    }
    return 1;
}
void sd_write_end(void){
    pthread_mutex_lock(&g_mu);
    g_sd_writers--;
    pthread_mutex_unlock(&g_mu);
}
int sd_drained(void){
    pthread_mutex_lock(&g_mu);
    int ok = g_users == 0 && g_sd_writers == 0;
    pthread_mutex_unlock(&g_mu);
    return ok;
}
static int sd_io_resume(void){
    pthread_mutex_lock(&g_mu);
    int ok = !g_fault && g_users == 0 && g_ready && media_local();
    if(ok) g_allowed = 1;
    pthread_mutex_unlock(&g_mu);
    return ok;
}
void sd_io_fault(void){
    pthread_mutex_lock(&g_mu);
    g_fault = 1; g_allowed = 0;
    int fd = open(SD_IO_FAULT_MARKER, O_WRONLY|O_CREAT|O_CLOEXEC, 0600);
    if(fd >= 0) close(fd);
    pthread_mutex_unlock(&g_mu);
}
int sd_io_healthy(void){
    pthread_mutex_lock(&g_mu);
    int ok = !g_fault;
    pthread_mutex_unlock(&g_mu);
    return ok;
}
int sd_io_allowed(void){
    pthread_mutex_lock(&g_mu);
    int ok = g_allowed;
    pthread_mutex_unlock(&g_mu);
    return ok;
}
int sd_writable(void){
    pthread_mutex_lock(&g_mu);
    int ok = g_sd_writable;
    pthread_mutex_unlock(&g_mu);
    return ok;
}

void sd_quiesce_begin(void){
    atomic_store(&g_sd_quiesce, 1);
    card_close();
}
void sd_quiesce_end(void){
    atomic_store(&g_sd_quiesce, 0);
    if(g_sd_phase == SD_LOCAL && !g_sd_hold){ set_writable(1); sd_io_resume(); }
}

void sd_hold(void){ g_sd_hold = 1; }
int  sd_held(void){ return g_sd_hold; }
static void sd_revoke(void){
    atomic_store(&g_launch_verdict, 0);
    g_sd_hold = 1;
    card_lost();
}
sd_gate_t sd_gate_poll(int launch_status){
    int v = launch_status;
    if(v < 0 && !g_gate_started){
        if(++g_gate_waited < SD_GATE_POLLS) return SD_GATE_WAIT;
        v = 0;
    }
    if(v == 1){
        if(g_gate_started) return SD_GATE_OPEN;
        g_gate_started = 1;
        atomic_store(&g_launch_verdict, 1);
        return SD_GATE_OPENED;
    }
    sd_revoke();
    return SD_GATE_DENIED;
}

int sd_local_playback_allowed(void){
    return atomic_load(&g_launch_verdict) == 1 && g_sd_phase == SD_LOCAL && !g_sd_hold && sd_io_healthy();
}
int sd_switch_failed(void){ return g_sd_phase == SD_UNKNOWN || g_sd_hold; }
int sd_switch_pending(void){
    return g_sd_phase == SD_DRAIN || g_sd_phase == SD_WAIT_HOST || g_sd_phase == SD_WAIT_LOCAL;
}
const char *sd_switch_refusal(void){
    if(sd_switch_pending()) return "Storage is switching";
    if(g_sd_phase == SD_UNKNOWN || g_sd_hold || !sd_io_healthy()) return "SD access is held this boot";
    if(atomic_load(&g_launch_verdict) != 1) return "SD access is not ready yet";
    return NULL;
}
int sd_host_owned(void){ return g_sd_phase == SD_HOST; }
void sd_restart_deadline(void){ if(sd_switch_pending()) g_sd_deadline = sd_now() + SD_PHASE_MS; }

int sd_coldplug_begin(void){
    pthread_mutex_lock(&g_sd_mode_mu);
    return sd_writable();
}
void sd_coldplug_end(void){ pthread_mutex_unlock(&g_sd_mode_mu); }

static int storage_mark(char phase, int mode){
    char tmp[] = SD_EXPORT_MARKER ".XXXXXX";
    int fd = mkstemp(tmp); if(fd < 0) return 0;
    char b[8]; int n = snprintf(b, sizeof b, "%c %d\n", phase, mode);
    int ok = write(fd, b, (size_t)n) == n && fsync(fd) == 0;
    if(close(fd) != 0) ok = 0;
    if(!ok || rename(tmp, SD_EXPORT_MARKER) != 0){ unlink(tmp); return 0; }
    return 1;
}
static void storage_unknown(const char *why){
    card_lost();
    fprintf(stderr, "storage: %s; SD access remains held\n", why); fflush(stderr);
    SDE->toast("Storage state uncertain - reboot device");
}

int sd_export_begin(void){
    if(g_sd_phase == SD_HOST) return 0;
    if(SDE->scan_active()){ SDE->toast("Let the library scan finish first"); return -1; }
    if(!SDE->player_guarded()){ SDE->toast("Storage protection is unavailable"); return -1; }
    if(!media_local()){ SDE->toast("SD card is not ready"); return -1; }
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 200000000L; if(ts.tv_nsec >= 1000000000L){ ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    if(pthread_mutex_timedlock(&g_sd_mode_mu, &ts) != 0){ SDE->toast("SD card is busy - try again"); return -1; }
    if(!storage_mark('D', 3)){ pthread_mutex_unlock(&g_sd_mode_mu); SDE->toast("Couldn't prepare USB storage"); return -1; }
    card_close(); session_set_source_mode(3);
    g_sd_phase = SD_DRAIN; g_sd_deadline = sd_now() + SD_PHASE_MS;
    SDE->art_cancel();
    pthread_mutex_unlock(&g_sd_mode_mu);
    return 0;
}
int sd_return_begin(int mode){
    if(!storage_mark('R', mode)){ SDE->toast("Couldn't leave USB storage"); return -1; }
    g_sd_return_mode = mode; g_sd_phase = SD_WAIT_LOCAL;
    g_sd_deadline = sd_now() + SD_PHASE_MS;
    if(SDE->send_source(mode) < 0){ storage_unknown("return request failed"); return -1; }
    return 0;
}

static void storage_resume_local(void){
    if(unlink(SD_EXPORT_MARKER) != 0 && errno != ENOENT){ storage_unknown("cannot clear handoff state"); return; }
    if(!sd_io_resume()){ storage_unknown("local mount changed during return"); return; }
    set_writable(1); g_sd_phase = SD_LOCAL; session_set_source_mode(g_sd_return_mode);
    session_save_source(g_sd_return_mode);
    g_sd_reissue = 0;
    SDE->card_returned();
    fprintf(stderr, "storage: local mount confirmed; SD readers and artwork cache resumed\n"); fflush(stderr);
    SDE->toast("Restarting to reload the card...");
    SDE->restart();
}
void sd_tick(void){
    if(g_sd_hold || atomic_load(&g_sd_quiesce)) return;
    if(!sd_io_healthy()){
        if(g_sd_phase != SD_UNKNOWN) storage_unknown("artwork decoder did not exit");
        return;
    }
    if(g_sd_phase == SD_LOCAL){
        if(!sd_io_allowed() && media_local()){
            if(sd_io_resume()) SDE->scan_if_library_empty();
        }
        return;
    }
    if(g_sd_phase == SD_DRAIN){
        if(sd_drained()){
            if(!storage_mark('E', 3)){ storage_unknown("cannot record export request"); return; }
            g_sd_phase = SD_WAIT_HOST; g_sd_deadline = sd_now() + SD_PHASE_MS;
            if(SDE->playing() && ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0){ storage_unknown("pause request failed"); return; }
            if(SDE->send_source(3) < 0) storage_unknown("export request failed");
            return;
        }
        if((int32_t)(sd_now() - g_sd_deadline) >= 0){
            storage_unknown("SD readers did not finish");
        }
    } else if(g_sd_phase == SD_WAIT_HOST){
        if(SDE->host_confirmed() && !SDE->media_mounted()){
            if(!storage_mark('H', 3)){ storage_unknown("cannot record completed export"); return; }
            g_sd_phase = SD_HOST;
            g_sd_reissue = 0;
            fprintf(stderr, "storage: export confirmed with no local mount\n"); fflush(stderr);
        } else if((int32_t)(sd_now() - g_sd_deadline) >= 0) storage_unknown("export not confirmed");
        else if(g_sd_reissue && atomic_load(&g_launch_verdict) == 1 && SDE->send_source(3) == 0) g_sd_reissue = 0;
    } else if(g_sd_phase == SD_WAIT_LOCAL){
        if(media_local()){ storage_resume_local(); return; }
        if((int32_t)(sd_now() - g_sd_deadline) >= 0) storage_unknown("return mount not confirmed");
        else if(g_sd_reissue && atomic_load(&g_launch_verdict) == 1 && SDE->send_source(g_sd_return_mode) == 0)
            g_sd_reissue = 0;
    }
}

void sd_init(void){
    pthread_mutex_lock(&g_mu);
    assert(g_users == 0);
    g_ready = 1;
    g_allowed = 0;
    fault_note();
    pthread_mutex_unlock(&g_mu);
    FILE *f = fopen(SD_EXPORT_MARKER, "r");
    if(f){
        char phase = 0; int mode = 0;
        int n = fscanf(f, "%c %d", &phase, &mode); fclose(f);
        set_writable(0);
        if(n == 2 && (phase == 'H' || phase == 'E') && SDE->host_confirmed() && !SDE->media_mounted()){
            g_sd_phase = storage_mark('H', 3) ? SD_HOST : SD_UNKNOWN; session_set_source_mode(3);
        } else if(n == 2 && phase == 'E' && mode == 3){
            g_sd_phase = SD_WAIT_HOST; session_set_source_mode(3); g_sd_reissue = 1;
            g_sd_deadline = sd_now() + SD_PHASE_MS;
        } else if(n == 2 && phase == 'R' && mode >= 0 && mode < 3){
            g_sd_phase = SD_WAIT_LOCAL; g_sd_return_mode = mode; session_set_source_mode(3); g_sd_reissue = 1;
            g_sd_deadline = sd_now() + SD_PHASE_MS;
        } else if(n == 2 && phase == 'D' && !SDE->exported_to_host()){
            if(unlink(SD_EXPORT_MARKER) == 0){ g_sd_phase = SD_LOCAL; set_writable(1); }
            else g_sd_phase = SD_UNKNOWN;
        } else g_sd_phase = SD_UNKNOWN;
    } else if(errno != ENOENT || SDE->exported_to_host()){
        g_sd_phase = SD_UNKNOWN; set_writable(0);
    }
    if(g_sd_hold){ g_sd_phase = SD_UNKNOWN; set_writable(0); }
    if(g_sd_phase == SD_LOCAL && !g_sd_hold) sd_io_resume();
}
