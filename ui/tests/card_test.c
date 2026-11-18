/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "card.h"
#include "rmguard.h"
#include "srcswitch.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CARD_TEST_ROOT
#error "the Makefile points CARD_TEST_ROOT at a temp dir"
#endif

static int g_umount_busy, g_umount_errno, g_umount_rc, g_umount_calls;
static int g_lazy_calls, g_lazy_errno, g_lazy_rc;
static int g_mount_calls, g_mount_ok_on, g_mount_errno, g_mount_exports;
static char g_mount_fs[8][16], g_mount_data[8][32];
static int g_naps, g_sends, g_send_mode, g_send_rc;
static int g_launch_status, g_fw_direct, g_source_mode, g_writable, g_coldplug_writable;
static int g_begin_calls, g_end_calls;

static void make_gadget(const char *udc, const char *lun);

static int fake_umount(const char *target){
    assert(!strcmp(target, CARD_MNT));
    g_umount_calls++;
    if(g_umount_busy > 0){ g_umount_busy--; errno = EBUSY; return -1; }
    errno = g_umount_errno;
    return g_umount_rc;
}
static int fake_umount_lazy(const char *target, int flags){
    (void)flags;
    assert(!strcmp(target, CARD_MNT));
    g_lazy_calls++;
    errno = g_lazy_errno;
    return g_lazy_rc;
}
static int fake_mount(const char *src, const char *tgt, const char *fstype, unsigned long flags, const void *data){
    (void)flags;
    assert(!strcmp(src, CARD_DEV) && !strcmp(tgt, CARD_MNT));
    int i = g_mount_calls++;
    assert(i < 8);
    snprintf(g_mount_fs[i], sizeof g_mount_fs[i], "%s", fstype);
    snprintf(g_mount_data[i], sizeof g_mount_data[i], "%s", data ? (const char *)data : "");
    if(g_mount_ok_on == i){
        if(g_mount_exports) make_gadget("musb-hdrc\n", "/dev/mmcblk0p1\n");
        return 0;
    }
    errno = g_mount_errno;
    return -1;
}
static void fake_wait(void){ g_naps++; }
static int fake_send_source(int mode){ g_sends++; g_send_mode = mode; return g_send_rc; }
static int fake_launch_status(void){ return g_launch_status; }
static int fake_fw_direct(void){ return g_fw_direct; }
static int fake_source_mode(void){ return g_source_mode; }
static int fake_writable(void){ return g_writable; }
static int fake_coldplug_begin(void){ g_begin_calls++; return g_coldplug_writable; }
static void fake_coldplug_end(void){ g_end_calls++; }

static const struct card_env env = {
    .umount = fake_umount, .umount_lazy = fake_umount_lazy, .mount = fake_mount, .unmount_wait = fake_wait,
    .send_source = fake_send_source, .launch_status = fake_launch_status, .fw_direct_mount = fake_fw_direct,
    .source_mode = fake_source_mode, .writable = fake_writable,
    .coldplug_begin = fake_coldplug_begin, .coldplug_end = fake_coldplug_end,
};

static void knobs_zero(void){
    g_umount_busy = 0; g_umount_errno = EBUSY; g_umount_rc = -1; g_umount_calls = 0;
    g_lazy_calls = 0; g_lazy_errno = EBUSY; g_lazy_rc = -1;
    g_mount_calls = 0; g_mount_ok_on = -1; g_mount_errno = ENODEV; g_mount_exports = 0;
    memset(g_mount_fs, 0, sizeof g_mount_fs); memset(g_mount_data, 0, sizeof g_mount_data);
    g_naps = g_sends = g_send_mode = 0; g_send_rc = 0;
    g_launch_status = 1; g_fw_direct = 1; g_source_mode = 0; g_writable = 1; g_coldplug_writable = 1;
    g_begin_calls = g_end_calls = 0;
    card_set_env(&env);
}

static void wipe(void){
    char c[500];
    snprintf(c, sizeof c, "rm -rf %s && mkdir -p %s/proc %s", CARD_TEST_ROOT, CARD_TEST_ROOT, CARD_MNT);
    assert(system(c) == 0);
}
static void write_text(const char *path, const char *content){
    FILE *f = fopen(path, "wb"); assert(f);
    fputs(content, f);
    assert(fclose(f) == 0);
}
static void write_under_root(const char *rel, const char *content){
    char p[400]; snprintf(p, sizeof p, "%s/%s", CARD_TEST_ROOT, rel);
    write_text(p, content);
}
static void write_uptime(const char *content){ write_under_root("proc/uptime", content); }
static void write_mounts(const char *content){ write_under_root("proc/mounts", content); }
static void mkdir_under_root(const char *rel){
    char m[500]; snprintf(m, sizeof m, "mkdir -p %s/%s", CARD_TEST_ROOT, rel);
    assert(system(m) == 0);
}
static void write_sector_file(const unsigned char *b){
    FILE *f = fopen(CARD_DEV, "wb"); assert(f);
    assert(fwrite(b, 1, 512, f) == 512);
    assert(fclose(f) == 0);
}
static void make_exfat(unsigned char *b){
    memset(b, 0, 512);
    memcpy(b + 3, "EXFAT   ", 8);
}
static void make_fat32(unsigned char *b){
    memset(b, 0, 512);
    b[11] = 0x00; b[12] = 0x02;
    b[13] = 8;
    b[14] = 32; b[15] = 0;
    memcpy(b + 82, "FAT32   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
}
static void make_gadget(const char *udc, const char *lun){
    if(udc){
        char p[400]; snprintf(p, sizeof p, "%s/UDC", CARD_GADGET_DIR);
        mkdir_under_root("gadget");
        write_text(p, udc);
    }
    if(lun){
        char p[400]; snprintf(p, sizeof p, "%s/functions/mass_storage.0/lun.0/file", CARD_GADGET_DIR);
        mkdir_under_root("gadget/functions/mass_storage.0/lun.0");
        write_text(p, lun);
    }
}
static int log_has(const char *needle){
    char p[400]; snprintf(p, sizeof p, "%s/coldplug.log", CARD_TEST_ROOT);
    FILE *f = fopen(p, "r");
    if(!f) return 0;
    char b[4096]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
    return strstr(b, needle) != NULL;
}
static int journal_has(const char *needle){
    FILE *f = fopen(BOOTLOG_JOURNAL_PATH, "r");
    if(!f) return 0;
    char b[8192]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
    return strstr(b, needle) != NULL;
}
static int uevent_is_add(void){
    char b[16] = {0};
    FILE *f = fopen(CARD_UEVENT, "r"); if(!f) return 0;
    size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
    return !strcmp(b, "add\n");
}
static void write_proc_process(long pid, const char *comm, const char *path_entry){
    char m[500]; snprintf(m, sizeof m, "mkdir -p %s/proc/%ld", CARD_TEST_ROOT, pid);
    assert(system(m) == 0);
    char p[500];
    snprintf(p, sizeof p, "%s/proc/%ld/comm", CARD_TEST_ROOT, pid);
    write_text(p, comm);
    snprintf(p, sizeof p, "%s/proc/%ld/environ", CARD_TEST_ROOT, pid);
    FILE *f = fopen(p, "wb"); assert(f);
    char buf[512]; int n = snprintf(buf, sizeof buf, "PATH=%s", path_entry);
    assert(n > 0 && n < (int)sizeof buf);
    assert(fwrite(buf, 1, (size_t)n + 1, f) == (size_t)n + 1);
    assert(fclose(f) == 0);
}
static void publish_guard_dir(void){
    size_t len = 0;
    const char *script = rmguard_script(&len);
    char m[500]; snprintf(m, sizeof m, "mkdir -p %s/guard", CARD_TEST_ROOT);
    assert(system(m) == 0);
    char p[500]; snprintf(p, sizeof p, "%s/guard/rm", CARD_TEST_ROOT);
    FILE *f = fopen(p, "wb"); assert(f);
    assert(fwrite(script, 1, len, f) == len);
    assert(fclose(f) == 0);
    assert(chmod(p, 0755) == 0);
}

static void mounted_reads_the_real_table(void){
    wipe(); knobs_zero();
    write_mounts("/dev/other /somewhere vfat rw 0 0\n");
    assert(!card_mounted());
    char line[500];
    snprintf(line, sizeof line, "%s %s vfat rw,relatime 0 0\n", CARD_DEV, CARD_MNT);
    write_mounts(line);
    assert(card_mounted());
    snprintf(line, sizeof line, "%s %s vfat rw,relatime 0 0\n", CARD_DEV_WHOLE, CARD_MNT);
    write_mounts(line);
    assert(card_mounted());
    snprintf(line, sizeof line, "%s /elsewhere vfat rw 0 0\n", CARD_DEV);
    write_mounts(line);
    assert(!card_mounted());
}

static void exported_fails_closed_on_gadget_state(void){
    wipe(); knobs_zero();
    assert(!card_exported_to_host());
    make_gadget("musb-hdrc\n", "/dev/mmcblk0p1\n");
    assert(card_exported_to_host());
    wipe(); knobs_zero();
    make_gadget("  \n", "/dev/mmcblk0p1\n");
    assert(card_exported_to_host());
    wipe(); knobs_zero();
    make_gadget("  \n", NULL);
    assert(!card_exported_to_host());
    mkdir_under_root("gadget/functions/ffs.0");
    assert(!card_exported_to_host());
    mkdir_under_root("gadget/functions/mass_storage.0");
    assert(card_exported_to_host());
    wipe(); knobs_zero();
    make_gadget(NULL, "/dev/mmcblk0p1\n");
    assert(card_exported_to_host());
}

static void host_confirmed_needs_bound_and_backed(void){
    wipe(); knobs_zero();
    assert(!card_host_confirmed());
    make_gadget("musb-hdrc\n", "/dev/mmcblk0\n");
    assert(card_host_confirmed());
    wipe(); knobs_zero();
    make_gadget("  \n", "/dev/mmcblk0\n");
    assert(!card_host_confirmed());
    wipe(); knobs_zero();
    make_gadget("musb-hdrc\n", "/dev/other\n");
    assert(!card_host_confirmed());
}

static void player_guarded_checks_the_real_environ(void){
    wipe(); knobs_zero();
    publish_guard_dir();
    char guardpath[400]; snprintf(guardpath, sizeof guardpath, "%s/guard", CARD_TEST_ROOT);
    assert(!card_player_guarded());
    write_proc_process(101, "mq_player\n", guardpath);
    assert(card_player_guarded());
    write_proc_process(202, "sh\n", guardpath);
    assert(card_player_guarded());
    write_proc_process(303, "mq_player\n", "/bin");
    assert(!card_player_guarded());
    wipe(); knobs_zero();
    publish_guard_dir();
    write_proc_process(404, "mq_player\n", guardpath);
    assert(card_player_guarded());
}

static void unmount_takes_the_not_mounted_fast_path(void){
    wipe(); knobs_zero();
    g_umount_errno = EINVAL;
    assert(card_unmount() == 0);
    assert(g_umount_calls == 1 && g_sends == 0 && g_naps == 0);
    g_umount_errno = ENOENT;
    assert(card_unmount() == 0);
    assert(g_umount_calls == 2 && g_sends == 0 && g_naps == 0);
    assert(journal_has("card not mounted before shutdown or restart"));
}

static void unmount_asks_the_player_and_retries_until_free(void){
    wipe(); knobs_zero();
    unsigned char b[512]; make_exfat(b); write_sector_file(b);
    g_umount_busy = 2;
    g_umount_rc = 0;
    assert(card_unmount() == 0);
    assert(g_umount_calls == 3 && g_sends == 1 && g_send_mode == SRCSWITCH_MODE_USB_STORAGE);
    assert(g_naps == 2 && g_lazy_calls == 0);
    assert(journal_has("card unmounted after player release in 2 tries"));
}

static void unmount_falls_back_to_a_lazy_detach(void){
    wipe(); knobs_zero();
    g_umount_busy = 100;
    g_lazy_rc = 0;
    assert(card_unmount() == 1);
    assert(g_umount_calls == 41 && g_naps == 40 && g_lazy_calls == 1);
    assert(journal_has("card lazily detached"));
}

static void unmount_reports_a_still_busy_card(void){
    wipe(); knobs_zero();
    g_umount_busy = 100;
    g_lazy_rc = -1; g_lazy_errno = EBUSY;
    assert(card_unmount() == -1);
    assert(g_umount_calls == 41 && g_naps == 40 && g_lazy_calls == 1);
    assert(journal_has("card unmount failed"));
}

static void step_setup(const unsigned char *sector){
    wipe(); knobs_zero();
    write_uptime("10.0 100.0\n");
    write_mounts("");
    write_text(CARD_UEVENT, "");
    if(sector) write_sector_file(sector);
}

static void mount_prefers_the_detected_fstype(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    g_mount_ok_on = 0;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 1);
    assert(g_mount_calls == 1 && !strcmp(g_mount_fs[0], "exfat"));
    assert(!strcmp(g_mount_data[0], "iocharset=utf8"));
    assert(g_begin_calls == 1 && g_end_calls == 1 && g_umount_calls == 0);
    assert(!uevent_is_add());
    assert(log_has("SD direct-mounted (V2.40 exfat)"));
}

static void mount_uses_vfat_first_when_detected(void){
    unsigned char b[512]; make_fat32(b);
    step_setup(b);
    g_mount_ok_on = 0;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 1);
    assert(g_mount_calls == 1 && !strcmp(g_mount_fs[0], "vfat"));
    assert(log_has("SD direct-mounted (V2.40 vfat)"));
}

static void mount_retries_without_options_only_on_einval(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    g_mount_ok_on = 1;
    g_mount_errno = EINVAL;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 1);
    assert(g_mount_calls == 2);
    assert(!strcmp(g_mount_fs[0], "exfat") && !strcmp(g_mount_data[0], "iocharset=utf8"));
    assert(!strcmp(g_mount_fs[1], "exfat") && !strcmp(g_mount_data[1], ""));
}

static void mount_falls_back_to_the_other_fstype(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    g_mount_ok_on = 1;
    g_mount_errno = ENODEV;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 1);
    assert(g_mount_calls == 2);
    assert(!strcmp(g_mount_fs[0], "exfat") && !strcmp(g_mount_fs[1], "vfat"));
    assert(log_has("SD direct-mounted (V2.40 vfat)"));
}

static void mount_failure_keeps_the_worker_going(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    g_mount_ok_on = -1;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 0);
    assert(g_begin_calls == 1 && g_end_calls == 1);
    assert(nudged == 0 && !uevent_is_add());
    assert(log_has("SD direct-mount failed errno="));
}

static void a_late_attempt_falls_back_to_the_uevent_nudge(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    g_mount_ok_on = -1;
    int nudged = 0;
    assert(card_coldplug_step(9, 1, &nudged) == 0);
    assert(!uevent_is_add() && nudged == 0 && g_mount_calls == 2);
    assert(card_coldplug_step(10, 1, &nudged) == 0);
    assert(uevent_is_add() && nudged == 1 && g_mount_calls == 2);
    assert(g_end_calls == 2);
    assert(log_has("direct mount failed -> falling back"));
    assert(card_coldplug_step(11, 1, &nudged) == 0);
    assert(nudged == 1 && g_mount_calls == 2 && g_end_calls == 3);
}

static void nudge_only_firmware_never_direct_mounts(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    int nudged = 0;
    assert(card_coldplug_step(0, 0, &nudged) == 0);
    assert(g_mount_calls == 0 && uevent_is_add() && nudged == 1);
    assert(card_coldplug_step(1, 0, &nudged) == 0);
    assert(g_mount_calls == 0);
}

static void a_closed_or_foreign_card_never_mounts(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    g_coldplug_writable = 0;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 0);
    assert(g_mount_calls == 0 && g_begin_calls == 1 && g_end_calls == 1);
    step_setup(b);
    g_source_mode = 3;
    assert(card_coldplug_step(0, 1, &nudged) == 0);
    assert(g_mount_calls == 0);
}

static void post_mount_export_rolls_the_mount_back(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    g_mount_ok_on = 0;
    g_mount_exports = 1;
    g_umount_rc = 0;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 1);
    assert(g_umount_calls == 1 && g_begin_calls == 1 && g_end_calls == 1);
    assert(log_has("post-mount export detected -> unmounted, host owns card"));
}

static void post_mount_export_detaches_lazily_when_busy(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    g_mount_ok_on = 0;
    g_mount_exports = 1;
    g_umount_busy = 1;
    g_lazy_rc = 0;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 1);
    assert(g_umount_calls == 1 && g_lazy_calls == 1);
    assert(log_has("post-mount export detected -> unmounted, host owns card"));
}

static void post_mount_export_warns_when_both_fail(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    g_mount_ok_on = 0;
    g_mount_exports = 1;
    g_umount_busy = 1;
    g_lazy_rc = -1;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 1);
    assert(g_umount_calls == 1 && g_lazy_calls == 1);
    assert(log_has("WARNING: post-mount export + unmount BOTH failed"));
}

static void an_elapsed_window_gives_up(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    write_uptime("200.0 400.0\n");
    g_mount_ok_on = 0;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 1);
    assert(g_mount_calls == 0 && g_end_calls == 1);
    assert(log_has("V2.40 SD one-shot: window elapsed or card exported"));
}

static void an_exported_card_never_direct_mounts(void){
    unsigned char b[512]; make_exfat(b);
    step_setup(b);
    make_gadget("musb-hdrc\n", "/dev/mmcblk0p1\n");
    g_mount_ok_on = 0;
    int nudged = 0;
    assert(card_coldplug_step(0, 1, &nudged) == 0);
    assert(g_mount_calls == 0 && g_umount_calls == 0);
}

int main(void){
    mounted_reads_the_real_table();
    exported_fails_closed_on_gadget_state();
    host_confirmed_needs_bound_and_backed();
    player_guarded_checks_the_real_environ();
    unmount_takes_the_not_mounted_fast_path();
    unmount_asks_the_player_and_retries_until_free();
    unmount_falls_back_to_a_lazy_detach();
    unmount_reports_a_still_busy_card();
    mount_prefers_the_detected_fstype();
    mount_uses_vfat_first_when_detected();
    mount_retries_without_options_only_on_einval();
    mount_falls_back_to_the_other_fstype();
    mount_failure_keeps_the_worker_going();
    a_late_attempt_falls_back_to_the_uevent_nudge();
    nudge_only_firmware_never_direct_mounts();
    a_closed_or_foreign_card_never_mounts();
    post_mount_export_rolls_the_mount_back();
    post_mount_export_detaches_lazily_when_busy();
    post_mount_export_warns_when_both_fail();
    an_elapsed_window_gives_up();
    an_exported_card_never_direct_mounts();
    wipe();
    puts("card_test: ok");
    return 0;
}
