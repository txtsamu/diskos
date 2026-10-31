/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "dacvol.h"
#include "config.h"
#include "ipc.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREF_LEVEL      "usb_audio_vol"
#define PREF_PREV       "dacvol_prev"
#define PREF_SAFE       "dac_safe_start"
#define PREF_ENABLE     "usb_hw_volume"
#define PREF_KEY_STEP   "dacvol_key_step"
#define SAFE_START_PCT  60
#define PROBE_MS        4000
#define MIX_MS          3000
#define PLAYER_TRY_MS   2000
#define ASSERT_RETRY_MS 400
#define ASSERT_TRIES    3
#define KEY_WINDOW_MS   700
#define PERSIST_MS      1500
#define SETTLE_MS       300
#define LEAVE_RETRY_MS  500
#define LEAVE_GIVEUP_MS 15000
#define RECOVER_MS      1000
#define KEY_BURST       32
#define LOG_CODES       6

enum { ST_IDLE, ST_PROBE, ST_MIX, ST_ACTIVE, ST_FALLBACK, ST_LEAVE };

static const struct dacvol_env *E;
static struct {
    int st;
    uint32_t t0;
    dacvol_ctl_t ctl;
    int level, hold, ref, prev, orig, card, pending_user;
    int need_assert, tries, maxvol_tried;
    unsigned seen_seq;
    uint32_t last_assert, last_key, persist_due, rec_t;
    int persist_dirty, keys_open, announce, announce_level;
    int leave_target, leave_player_done, leave_mix_done, leave_after_fallback;
    uint32_t leave_t0, leave_retry;
    int logged_codes[LOG_CODES], n_codes;
} S;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...){
    char b[160];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if(E->log) E->log(b);
}
static uint32_t now_ms(void){ return E->now(); }
static int clampi(int v, int lo, int hi){ return v < lo ? lo : v > hi ? hi : v; }

static int user_cap(void){
    int c = E->user_cap ? E->user_cap() : VOL_MAX;
    return clampi(c, 0, VOL_MAX);
}
static int safe_cap(void){
    int pct = clampi(cfg_get_int(PREF_SAFE, SAFE_START_PCT), 0, 100);
    return VOL_MAX * pct / 100;
}
static int key_step(void){ return clampi(cfg_get_int(PREF_KEY_STEP, 1), 1, 10); }
static int hold_for(int level){ return level <= 0 ? 0 : VOL_MAX; }

int dacvol_map(const dacvol_ctl_t *ctl, int level){
    if(!ctl || ctl->max <= ctl->min) return 0;
    level = clampi(level, 0, VOL_MAX);
    long span = (long)ctl->max - ctl->min;
    return ctl->min + (int)((span * level + VOL_MAX / 2) / VOL_MAX);
}

static int cdb_of(const char *s){
    double v = 0;
    if(sscanf(s, "%lf", &v) != 1) return DACVOL_NO_DB;
    return (int)lround(v * 100.0);
}
static int ends_with(const char *s, const char *suffix){
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && !strcmp(s + n - m, suffix);
}
static int ctl_score(const dacvol_ctl_t *c){
    if(!ends_with(c->name, "Playback Volume") || c->max <= c->min || c->channels < 1 || c->channels > 8) return 0;
    return !strncmp(c->name, "PCM", 3) ? 2 : 1;
}
int dacvol_parse_contents(const char *text, dacvol_ctl_t *out){
    dacvol_ctl_t cur, best;
    int in_block = 0, cur_ok = 0, best_score = 0;
    memset(&best, 0, sizeof best);
    memset(&cur, 0, sizeof cur);
    const char *p = text;
    while(p && *p){
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[256];
        if(len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len); line[len] = 0;
        p = eol ? eol + 1 : NULL;
        if(!strncmp(line, "numid=", 6)){
            if(in_block && cur_ok){ int s = ctl_score(&cur); if(s > best_score){ best = cur; best_score = s; } }
            memset(&cur, 0, sizeof cur);
            cur.db_min_cdb = cur.db_max_cdb = DACVOL_NO_DB;
            in_block = 1; cur_ok = 0;
            cur.numid = atoi(line + 6);
            char *n = strstr(line, "name='");
            if(n && strstr(line, "iface=MIXER")){
                n += 6;
                char *q = strrchr(n, '\'');
                if(q && q > n && (size_t)(q - n) < sizeof cur.name){ memcpy(cur.name, n, (size_t)(q - n)); cur.name[q - n] = 0; cur_ok = 1; }
            }
        } else if(in_block && cur_ok){
            char *t = strstr(line, "type=");
            if(t && line[0] == ' ' ){
                if(strncmp(t, "type=INTEGER,", 13)) cur_ok = 0;
                char *a = strstr(line, "access=");
                if(!a || strncmp(a + 7, "rw", 2)) cur_ok = 0;
                char *v = strstr(line, "values="); if(v) cur.channels = atoi(v + 7);
                char *mn = strstr(line, ",min="); if(mn) cur.min = atoi(mn + 5);
                char *mx = strstr(line, ",max="); if(mx) cur.max = atoi(mx + 5);
            } else if(strstr(line, ": values=")){
                cur.cur = atoi(strstr(line, ": values=") + 9);
            } else if((t = strstr(line, "dBminmax-min="))){
                cur.db_min_cdb = cdb_of(t + 13);
                char *m = strstr(t, ",max="); if(m) cur.db_max_cdb = cdb_of(m + 5);
            }
        }
    }
    if(in_block && cur_ok){ int s = ctl_score(&cur); if(s > best_score){ best = cur; best_score = s; } }
    if(!best_score) return -1;
    if(out) *out = best;
    return 0;
}

static void mark_dirty(void){ S.persist_dirty = 1; S.persist_due = now_ms() + PERSIST_MS; }
static void flush_level(void){
    if(!S.persist_dirty) return;
    S.persist_dirty = 0;
    if(cfg_get_int(PREF_LEVEL, -1) != S.level) cfg_set_int(PREF_LEVEL, S.level);
}

static void set_level(int level, int announce){
    level = clampi(level, 0, user_cap());
    if(level == S.level && !announce) return;
    if(level != S.level){
        S.level = level;
        E->mixer_set(dacvol_map(&S.ctl, level));
        mark_dirty();
    }
    int h = hold_for(level);
    if(h != S.hold){ S.hold = h; S.need_assert = 1; S.tries = 0; }
    if(announce){ S.announce = 1; S.announce_level = level; }
}

static void keys_off(void){ if(S.keys_open){ if(E->key_active) E->key_active(0); S.keys_open = 0; } }

static void begin_leave(int after_fallback, int player_target){
    keys_off();
    flush_level();
    S.st = ST_LEAVE;
    S.leave_t0 = now_ms(); S.leave_retry = 0;
    S.leave_after_fallback = after_fallback;
    S.leave_target = player_target;
    S.leave_player_done = S.prev < 0 && cfg_get_int(PREF_PREV, -1) < 0;
    S.leave_mix_done = S.orig < 0;
}

static void go_fallback(const char *why){
    say("dacvol: fallback to the Player volume (%s)", why);
    S.st = ST_FALLBACK;
}
static void abort_untouched(const char *why){
    cfg_set_int(PREF_PREV, -1);
    S.prev = S.orig = -1;
    if(E->player_maxvol_restore) E->player_maxvol_restore();
    go_fallback(why);
}

static void enter_probe(void){
    memset(&S, 0, sizeof S);
    S.st = ST_PROBE; S.t0 = now_ms();
    S.prev = S.orig = -1; S.pending_user = -1;
    S.card = E->usb_card ? E->usb_card() : -1;
    if(S.card < 0 || E->mixer_probe(S.card) != 0){ go_fallback("no card or amixer not startable"); return; }
}

static int player_volume(unsigned *seq){
    track_state_t st; ipc_get_state(&st);
    if(seq) *seq = st.volume_seq;
    return st.volume_seq ? st.volume : -1;
}

static void start_hold(void){
    int cap_full = user_cap() < VOL_MAX;
    unsigned seq;
    int cur = player_volume(&seq);
    int marker = cfg_get_int(PREF_PREV, -1);
    int prev = marker >= 0 ? marker : cur;
    if(prev < 0) prev = safe_cap();
    prev = clampi(prev, 0, VOL_MAX);
    int saved = cfg_get_int(PREF_LEVEL, -1);
    int base = saved >= 0 ? saved : prev;
    int level = clampi(base, 0, VOL_MAX);
    int cap = safe_cap();
    int capped = level > cap;
    if(capped) level = cap;
    if(S.pending_user >= 0) level = S.pending_user;
    level = clampi(level, 0, user_cap());
    S.prev = prev; S.orig = S.ctl.cur; S.level = level;
    S.hold = hold_for(level); S.ref = S.hold;
    S.seen_seq = seq;
    cfg_set_int(PREF_PREV, prev);
    say("dacvol: attach card%d '%s' raw %d..%d (%d..%d cdB) was %d; start level %d/%d (prev %d, saved %d%s), hold Player at %d",
        S.card, S.ctl.name, S.ctl.min, S.ctl.max, S.ctl.db_min_cdb, S.ctl.db_max_cdb, S.ctl.cur, level, VOL_MAX, prev, saved,
        capped ? ", safe-start cap applied" : "", S.hold);
    if(cap_full && E->player_maxvol) E->player_maxvol(VOL_MAX);
    E->mixer_set(dacvol_map(&S.ctl, level));
    S.st = ST_MIX; S.t0 = now_ms();
}

static void tick_probe(void){
    if(!E->usb_ready()){ S.st = ST_IDLE; return; }
    dacvol_ctl_t c;
    int rc = E->mixer_probe_poll(&c);
    if(rc == DACVOL_PROBE_PENDING){
        if(now_ms() - S.t0 >= PROBE_MS) go_fallback("mixer probe timed out");
        return;
    }
    if(rc == DACVOL_PROBE_NOCTL){ go_fallback("the DAC has no usable playback volume control"); return; }
    if(rc != DACVOL_PROBE_OK){ go_fallback("amixer failed"); return; }
    if(c.max <= c.min || c.channels < 1){ go_fallback("unusable volume range"); return; }
    S.ctl = c;
    start_hold();
}

static void send_hold(void){
    S.last_assert = now_ms();
    E->player_send(S.hold);
}

static void tick_mix(void){
    if(!E->usb_ready()){ begin_leave(0, -1); return; }
    int ms = E->mixer_state();
    if(ms == DACVOL_MIX_FAILED){ abort_untouched("mixer write failed"); return; }
    if(ms == DACVOL_MIX_BUSY){
        if(now_ms() - S.t0 >= MIX_MS) abort_untouched("mixer write timed out");
        return;
    }
    if(E->player_send(S.hold) != 0){
        if(now_ms() - S.t0 >= MIX_MS + PLAYER_TRY_MS){ begin_leave(1, -1); say("dacvol: Player not reachable, restoring"); }
        return;
    }
    S.last_assert = now_ms();
    S.need_assert = 1; S.tries = 1;
    S.st = ST_ACTIVE; S.last_key = 0;
    if(S.pending_user >= 0 && S.pending_user != S.level) set_level(S.pending_user, 0);
}

static void on_key(int code, int value){
    if(value < 1) return;
    int dir = code == DACVOL_KEY_UP ? 1 : code == DACVOL_KEY_DOWN ? -1 : 0;
    if(!dir){
        for(int i = 0; i < S.n_codes; i++) if(S.logged_codes[i] == code) return;
        if(S.n_codes < LOG_CODES){ S.logged_codes[S.n_codes++] = code; say("dacvol: key code %d seen (not a volume key)", code); }
        return;
    }
    if(E->keys_adjust_volume && !E->keys_adjust_volume()) return;
    S.last_key = now_ms() ? now_ms() : 1;
    set_level(S.level + dir * key_step(), 1);
}

static void note_player(void){
    unsigned seq;
    int v = player_volume(&seq);
    if(seq == S.seen_seq) return;
    S.seen_seq = seq;
    if(v < 0) return;
    if(v == S.hold){ S.ref = v; S.need_assert = 0; S.tries = 0; return; }
    int delta = v - S.ref;
    S.ref = v;
    int keyed = S.last_key && now_ms() - S.last_key < KEY_WINDOW_MS;
    int adjusts = !E->keys_adjust_volume || E->keys_adjust_volume();
    int settling = now_ms() - S.last_assert < SETTLE_MS;
    if(!keyed && !settling && adjusts && delta){
        say("dacvol: Player volume moved by %d with no key event; applying it to the DAC", delta);
        set_level(S.level + delta, 1);
    }
    S.need_assert = 1; S.tries = 0;
}

static void drive_assert(void){
    if(!S.need_assert) return;
    unsigned seq;
    int v = player_volume(&seq);
    if(v == S.hold && seq){ S.need_assert = 0; S.tries = 0; return; }
    if(now_ms() - S.last_assert < ASSERT_RETRY_MS) return;
    if(S.tries >= ASSERT_TRIES){
        if(!S.maxvol_tried && E->player_maxvol){
            S.maxvol_tried = 1; S.tries = 0;
            say("dacvol: Player stays at %d, raising its maximum volume", v);
            E->player_maxvol(VOL_MAX);
            send_hold();
            S.tries = 1;
            return;
        }
        S.need_assert = 0;
        if(v >= 0 && v != S.hold){ say("dacvol: Player holds %d, not %d: output is not bit-perfect", v, S.hold); S.hold = v; S.ref = v; }
        else if(v < 0) say("dacvol: Player volume unverified (no report)");
        return;
    }
    send_hold();
    S.tries++;
}

static void tick_active(void){
    if(!E->usb_ready()){ begin_leave(0, -1); return; }
    if(!S.keys_open){ if(E->key_active) E->key_active(1); S.keys_open = 1; }
    if(E->key_poll){
        int code, value;
        for(int i = 0; i < KEY_BURST && E->key_poll(&code, &value); i++) on_key(code, value);
    }
    note_player();
    int cap = user_cap();
    if(S.level > cap) set_level(cap, 1);
    if(E->mixer_state() == DACVOL_MIX_FAILED){
        say("dacvol: mixer write failed, falling back to the Player volume %d", S.level);
        begin_leave(1, S.level);
        return;
    }
    drive_assert();
    if(S.persist_dirty && (int32_t)(now_ms() - S.persist_due) >= 0) flush_level();
}

static int leave_volume(void){
    int target = S.leave_target >= 0 ? S.leave_target : (S.prev >= 0 ? S.prev : cfg_get_int(PREF_PREV, -1));
    if(target < 0) target = safe_cap();
    return clampi(target, 0, VOL_MAX);
}
static void tick_leave(void){
    uint32_t now = now_ms();
    if(!S.leave_player_done && (!S.leave_retry || now - S.leave_retry >= LEAVE_RETRY_MS)){
        S.leave_retry = now ? now : 1;
        int target = leave_volume();
        if(E->player_send(target) == 0){
            S.leave_player_done = 1;
            cfg_set_int(PREF_PREV, -1);
            if(E->player_maxvol_restore) E->player_maxvol_restore();
            say("dacvol: left, Player volume back to %d", target);
        }
    }
    if(S.leave_player_done && !S.leave_mix_done){
        if(E->usb_card && E->usb_card() >= 0) E->mixer_set(S.orig);
        S.leave_mix_done = 1;
    }
    int give_up = now - S.leave_t0 >= LEAVE_GIVEUP_MS;
    if(give_up && !S.leave_player_done) say("dacvol: Player volume not restored; the next start repairs it");
    if((S.leave_player_done && S.leave_mix_done) || give_up){
        S.st = (S.leave_after_fallback && E->usb_ready()) ? ST_FALLBACK : ST_IDLE;
    }
}

static void tick_idle(void){
    if(!E->usb_ready()){
        int marker = cfg_get_int(PREF_PREV, -1);
        if(marker >= 0 && (!S.rec_t || now_ms() - S.rec_t >= RECOVER_MS)){
            S.rec_t = now_ms() ? now_ms() : 1;
            if(E->player_send(clampi(marker, 0, VOL_MAX)) == 0){
                cfg_set_int(PREF_PREV, -1);
                if(E->player_maxvol_restore) E->player_maxvol_restore();
                say("dacvol: repaired the Player volume to %d after an unclean exit", marker);
            }
        }
        return;
    }
    if(cfg_get_int(PREF_ENABLE, 1) == 0) return;
    enter_probe();
}

void dacvol_init(const struct dacvol_env *env){
    E = env;
    memset(&S, 0, sizeof S);
    S.prev = S.orig = S.pending_user = -1;
}

void dacvol_tick(void){
    if(!E) return;
    switch(S.st){
    case ST_IDLE:     tick_idle(); break;
    case ST_PROBE:    tick_probe(); break;
    case ST_MIX:      tick_mix(); break;
    case ST_ACTIVE:   tick_active(); break;
    case ST_FALLBACK: if(!E->usb_ready()){ S.st = ST_IDLE; } break;
    case ST_LEAVE:    tick_leave(); break;
    }
}

int dacvol_release_now(int current){
    if(!E) return current;
    if(S.st == ST_MIX || S.st == ST_ACTIVE) begin_leave(0, -1);
    if(S.st != ST_LEAVE) return current;
    int target = leave_volume();
    tick_leave();
    return target;
}

int dacvol_owns_volume(void){ return E && (S.st == ST_MIX || S.st == ST_ACTIVE); }

int dacvol_set_user(int level){
    if(!E) return 0;
    level = clampi(level, 0, VOL_MAX);
    switch(S.st){
    case ST_PROBE: S.pending_user = level; return 0;
    case ST_MIX:   S.pending_user = level; return 1;
    case ST_ACTIVE: set_level(level, 0); return 1;
    case ST_LEAVE:
        if(S.leave_player_done) return 0;
        S.leave_target = level; return 1;
    default: return 0;
    }
}

int dacvol_effective_volume(int player_volume_now){
    return (E && S.st == ST_ACTIVE) ? S.level : player_volume_now;
}

int dacvol_take_change(int *level){
    if(!E || !S.announce) return 0;
    S.announce = 0;
    if(level) *level = S.announce_level;
    return 1;
}
