/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "tama.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define EGG_HATCH_MIN 3
#define TICK_MS       600
#define ADV_TICKS     50
#define SAVE_GAP_S    60

typedef struct { int y; const char *row; } patch_t;
typedef struct {
    const char *const *rows;
    const patch_t *b;
    int bdx, bdy;
} sprite_t;

static const char *const R_EGG[] = {
    "......####......",
    "....########....",
    "...##########...",
    "..############..",
    "..##..########..",
    "..##..########..",
    ".##############.",
    ".#########..###.",
    ".#########..###.",
    "################",
    "################",
    "###..###########",
    "###..###########",
    "################",
    "################",
    ".##############.",
    ".##############.",
    "..############..",
    "...##########...",
    "....########....", NULL };
static const patch_t P_EGG[] = { {0, NULL} };
static const patch_t P_EGG_CRACK[] = {
    {3, "..######.#####.."},
    {4, "..##..#.######.."},
    {5, "..##..##.#####.."},
    {6, ".######.#######."},
    {7, ".#####.##..####."},
    {8, ".####.#####..##."},
    {0, NULL} };

static const char *const R_BOTA[] = {
    "......####......",
    "....########....",
    "..############..",
    ".##############.",
    ".###EE####EE###.",
    ".###ee####ee###.",
    "################",
    "################",
    "################",
    ".##############.",
    "..############..",
    "....########....", NULL };
static const patch_t P_BOTA[] = { {8, "#######..#######"}, {0, NULL} };

static const char *const R_KORO[] = {
    "..##............##..",
    ".####..........####.",
    ".####..........####.",
    ".####..........####.",
    "..################..",
    "..################..",
    ".##################.",
    ".####EE######EE####.",
    ".####ee######ee####.",
    ".##################.",
    ".####..........####.",
    ".######......######.",
    ".##################.",
    "..################..",
    "...####......####...", NULL };
static const patch_t P_KORO[] = { {0, "...##..........##..."}, {1, "..###..........###.."}, {0, NULL} };

static const char *const R_AGU[] = {
    "..........#######.......",
    "........###########.....",
    ".......#############....",
    ".......######EE#########",
    ".......######ee#########",
    ".......#################",
    ".......##########.......",
    ".......###############..",
    "........##############..",
    "......###############...",
    ".....##############..##.",
    "....###############..##.",
    "##..##############.#....",
    "###.#############.......",
    ".###############........",
    "..##############........",
    "....############........",
    "....####....####........",
    "....####....####........",
    "...#####....#####.......",
    "...######...######......",
    "...#.#.#...#.#.#........",
    NULL };
static const patch_t P_AGU[] = { {11, "##..###############..##."}, {12, "#...##############.#...."}, {0, NULL} };

static const char *const R_NUME[] = {
    ".######........######.",
    "########......########",
    "###EE###......###EE###",
    "###ee###......###ee###",
    "########......########",
    ".######........######.",
    "....##..........##....",
    "....##..........##....",
    "....##..........##....",
    "...################...",
    ".####################.",
    "######################",
    "######..........######",
    "#####............#####",
    "######..........######",
    "######################",
    "######################",
    ".####################.",
    "##..####..##..####..##", NULL };
static const patch_t P_NUME[] = { {0, "..######........######"}, {1, ".########......#######"}, {0, NULL} };

static const char *const R_TYR[] = {
    "..............#########.",
    ".............###########",
    ".............####EE#####",
    ".............####ee#####",
    ".............###########",
    ".............###########",
    ".............#.#.#.#.#.#",
    ".............#..........",
    ".............#.#.#.#.#.#",
    ".............###########",
    "............############",
    "...........#############",
    "..........###########.#.",
    "........#############.#.",
    "....###################.",
    "..#####################.",
    "###################.....",
    ".##################.....",
    "......####....####......",
    "......####....####......",
    "......####....####......",
    "......######..######....",
    NULL };
static const patch_t P_TYR[] = { {7, ".............#.#.#.#.#.#"}, {0, NULL} };

static const char *const R_GREY[] = {
    "..............#.......#.",
    "..............##....###.",
    ".............###########",
    ".............####EE#####",
    ".............####ee#####",
    ".............###########",
    ".............#.#.#.#.#.#",
    ".............#..........",
    ".............#.#.#.#.#.#",
    ".............###########",
    "............############",
    "...........####.###.####",
    "..........###.###.###.#.",
    "........####.###.####.#.",
    "....####.###.##.#######.",
    "..####.###.###.########.",
    "###.###.###.#######.....",
    ".##################.....",
    "......####....####......",
    "......####....####......",
    "......####....####......",
    "......######..######....",
    NULL };
static const patch_t P_GREY[] = { {7, ".............#.#.#.#.#.#"}, {0, NULL} };

static const char *const R_GONE[] = {
    "....########....",
    "..############..",
    ".##############.",
    ".######..######.",
    ".######..######.",
    ".####......####.",
    ".####......####.",
    ".######..######.",
    ".######..######.",
    ".######..######.",
    ".##############.",
    ".##############.",
    ".##############.",
    ".##############.",
    "################",
    "################", NULL };
static const patch_t P_GONE[] = { {0, NULL} };

static const sprite_t SP_EGG   = { R_EGG,  P_EGG,  1, 0 };
static const sprite_t SP_BOTA  = { R_BOTA, P_BOTA, 0, -1 };
static const sprite_t SP_KORO  = { R_KORO, P_KORO, 0, -1 };
static const sprite_t SP_AGU   = { R_AGU,  P_AGU,  0, -1 };
static const sprite_t SP_NUME  = { R_NUME, P_NUME, 1, 0 };
static const sprite_t SP_TYR   = { R_TYR,  P_TYR,  0, -1 };
static const sprite_t SP_GREY  = { R_GREY, P_GREY, 0, -1 };
static const sprite_t SP_GONE  = { R_GONE, P_GONE, 0, 0 };

static const char *const O_HEART[] = { ".##.##.", "#######", "#######", ".#####.", "..###..", "...#...", NULL };
static const char *const O_DROP[]  = { "..#..", "..#..", ".###.", "#####", "#####", ".###.", NULL };
static const char *const O_PLUS[]  = { "..#..", "..#..", "#####", "..#..", "..#..", NULL };
static const char *const O_Z[]     = { "#####", "...#.", "..#..", ".#...", "#####", NULL };
static const char *const O_ZS[]    = { "###", ".#.", "###", NULL };
static const char *const O_MEAT[]  = { ".####..", "######.", "######.", ".####..", "..##...", ".#.#...", "#...#..", NULL };
static const char *const O_POOP[]  = { "..#..", ".###.", ".###.", "#####", NULL };

#define SCENE_W 40
#define SCENE_H 28
#define CELL    4
#define BOX_X   8
#define BOX_Y   3
#define STATS_W 300
#define STATS_H 14

enum { B_FEED, B_PLAY, B_CLEAN, B_SLEEP, B_HEAL, B_NEW, B_COUNT };
enum { PN_NONE, PN_FEED, PN_GAME };

static tama_t   s_t;
static int      s_have;
static int      s_vis;
static int      s_prev_stage = -1;
static int      s_frame, s_ticks;
static int      s_dirty;
static time_t   s_saved_at;
static lv_timer_t *s_timer;
static lv_obj_t *s_name, *s_sub, *s_scene, *s_stats, *s_row, *s_btn[B_COUNT], *s_btnlbl[B_COUNT];
static lv_obj_t *s_lab[3];
static lv_draw_buf_t *s_scene_buf, *s_stats_buf;
static unsigned char s_cell[SCENE_H][SCENE_W];
static lv_obj_t *s_panel, *s_ptitle, *s_pa, *s_pb, *s_palbl, *s_pblbl, *s_pinfo;
static int      s_pmode = PN_NONE, s_g_round, s_g_win, s_g_loss;

static uint32_t px32(lv_color_t c){
    lv_color32_t q = lv_color_to_32(c, LV_OPA_COVER);
    uint32_t v; memcpy(&v, &q, sizeof v);
    return v;
}
static void fill_rect(lv_draw_buf_t *b, int x, int y, int w, int h, uint32_t px){
    for(int yy = y; yy < y + h; yy++){
        if(yy < 0 || yy >= (int)b->header.h) continue;
        uint32_t *row = (uint32_t *)((uint8_t *)b->data + (size_t)yy * b->header.stride);
        for(int xx = x; xx < x + w; xx++) if(xx >= 0 && xx < (int)b->header.w) row[xx] = px;
    }
}
static void clear_buf(lv_draw_buf_t *b){ memset(b->data, 0, (size_t)b->header.stride * b->header.h); }

static int nrows(const char *const *r){ int n = 0; while(r[n]) n++; return n; }

static void cell_set(int x, int y){ if(x >= 0 && x < SCENE_W && y >= 0 && y < SCENE_H) s_cell[y][x] = 1; }

static void draw_rows(const char *const *rows, const patch_t *patch, const patch_t *patch2, int x0, int y0, int shut){
    for(int y = 0; rows[y]; y++){
        const char *row = rows[y];
        for(const patch_t *p = patch; p && p->row; p++) if(p->y == y) row = p->row;
        for(const patch_t *p = patch2; p && p->row; p++) if(p->y == y) row = p->row;
        for(int x = 0; row[x]; x++)
            if(row[x] == '#' || (row[x] == 'E' && shut)) cell_set(x0 + x, y0 + y);
    }
}

static const sprite_t *sprite_of(const tama_t *t){
    switch(t->stage){
    case TAMA_EGG:   return &SP_EGG;
    case TAMA_BABY:  return &SP_BOTA;
    case TAMA_CHILD: return &SP_KORO;
    case TAMA_TEEN:  return &SP_AGU;
    case TAMA_ADULT: return t->form == 1 ? &SP_TYR : t->form == 2 ? &SP_GREY : &SP_NUME;
    default:         return &SP_GONE;
    }
}
static const char *char_name(const tama_t *t){
    switch(t->stage){
    case TAMA_EGG:   return "Digi-Egg";
    case TAMA_BABY:  return "Botamon";
    case TAMA_CHILD: return "Koromon";
    case TAMA_TEEN:  return "Agumon";
    case TAMA_ADULT: return t->form == 1 ? "Tyrannomon" : t->form == 2 ? "Greymon" : "Numemon";
    default:         return "gone";
    }
}

static void put_overlay(const char *const *o, int x, int y){ draw_rows(o, NULL, NULL, x, y, 0); }

static void draw_scene(void){
    memset(s_cell, 0, sizeof s_cell);
    const tama_t *t = &s_t;
    const sprite_t *sp = sprite_of(t);
    tama_mood_t m = tama_mood(t);
    int b = s_frame;
    int shut = (m == TAMA_MOOD_ASLEEP || m == TAMA_MOOD_SLEEPY);
    int low = (m == TAMA_MOOD_SAD || m == TAMA_MOOD_SICK || m == TAMA_MOOD_ASLEEP || m == TAMA_MOOD_SLEEPY);
    int n = nrows(sp->rows), w = (int)strlen(sp->rows[0]);
    int x0 = BOX_X + (24 - w) / 2, y0 = BOX_Y + 24 - n;
    int crack = t->stage == TAMA_EGG && t->stage_min >= EGG_HATCH_MIN - 1;
    if(b){
        if(low) y0 += 1;
        else { x0 += sp->bdx; y0 += sp->bdy; }
        if(m == TAMA_MOOD_SICK) x0 += 1;
    }
    draw_rows(sp->rows, b ? sp->b : NULL, crack ? P_EGG_CRACK : NULL, x0, y0, shut);

    switch(m){
    case TAMA_MOOD_HAPPY:  put_overlay(O_HEART, 33, b ? 1 : 0); break;
    case TAMA_MOOD_HUNGRY: put_overlay(O_MEAT, 32, 1); break;
    case TAMA_MOOD_SAD:    put_overlay(O_DROP, 34, b ? 3 : 1); break;
    case TAMA_MOOD_SLEEPY: put_overlay(O_ZS, 34, 3); break;
    case TAMA_MOOD_SICK:   put_overlay(O_PLUS, 34, 1); put_overlay(O_DROP, 1, b ? 4 : 2); break;
    case TAMA_MOOD_ASLEEP: put_overlay(O_Z, 33, b ? 1 : 3); put_overlay(O_ZS, 29, b ? 0 : 1); break;
    default: break;
    }
    for(int i = 0; t->stage != TAMA_EGG && t->stage != TAMA_GONE && i < t->poop && i < 4; i++)
        put_overlay(O_POOP, i < 2 ? 34 : 1, 23 - (i & 1) * 5);

    clear_buf(s_scene_buf);
    uint32_t px = px32(TC(TEXT_PRIMARY));
    for(int y = 0; y < SCENE_H; y++)
        for(int x = 0; x < SCENE_W; x++)
            if(s_cell[y][x]) fill_rect(s_scene_buf, x * CELL, y * CELL, CELL, CELL, px);
    lv_obj_invalidate(s_scene);
}

static void draw_stats(void){
    clear_buf(s_stats_buf);
    const int v[3] = { s_t.hunger, s_t.happy, s_t.energy };
    uint32_t on = px32(ui_current_accent()), warn = px32(TC(STATUS_WARNING)), off = px32(TC(CONTROL_TRACK));
    for(int g = 0; g < 3; g++){
        int pips = (v[g] + 19) / 20;
        if(pips > 5) pips = 5;
        for(int i = 0; i < 5; i++)
            fill_rect(s_stats_buf, g * 100 + 40 + i * 10, 2, 8, 10, i < pips ? (pips <= 1 ? warn : on) : off);
    }
    lv_obj_invalidate(s_stats);
}

static void fmt_age(char *out, int cap, long min){
    if(min < 60) snprintf(out, cap, "%ldm", min);
    else if(min < 24 * 60) snprintf(out, cap, "%ldh %ldm", min / 60, min % 60);
    else snprintf(out, cap, "%ldd %ldh", min / (24 * 60), (min / 60) % 24);
}

static void set_hidden(lv_obj_t *o, int hide){
    if(hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_all(void){
    const tama_t *t = &s_t;
    char age[24], line[64];
    fmt_age(age, sizeof age, t->age_min);
    lv_label_set_text(s_name, char_name(t));
    if(t->stage == TAMA_GONE) snprintf(line, sizeof line, "lived %s", age);
    else if(t->stage == TAMA_EGG){
        long left = EGG_HATCH_MIN - t->stage_min;
        snprintf(line, sizeof line, "egg  hatches in %ldm", left < 1 ? 1 : left);
    } else snprintf(line, sizeof line, "%s  %s  %s", tama_stage_name(t->stage), age, tama_mood_name(tama_mood(t)));
    lv_label_set_text(s_sub, line);

    int gone = t->stage == TAMA_GONE, egg = t->stage == TAMA_EGG;
    set_hidden(s_row, egg);
    for(int i = 0; i < B_COUNT; i++) set_hidden(s_btn[i], 0);
    set_hidden(s_btn[B_NEW], !gone);
    for(int i = B_FEED; i <= B_SLEEP; i++) set_hidden(s_btn[i], gone);
    set_hidden(s_btn[B_HEAL], gone || !t->sick);
    lv_label_set_text(s_btnlbl[B_SLEEP], t->asleep ? "Wake" : "Sleep");
    set_hidden(s_stats, gone);
    for(int i = 0; i < 3; i++) set_hidden(s_lab[i], gone);
    draw_stats();
    draw_scene();
}

static void note_stage_change(void){
    if(s_prev_stage >= 0 && s_t.stage != s_prev_stage && s_vis){
        if(s_prev_stage == TAMA_EGG && s_t.stage == TAMA_BABY) ui_toast("It hatched!");
        else if(s_t.stage != TAMA_GONE && s_t.stage > s_prev_stage){
            char m[48]; snprintf(m, sizeof m, "Evolved into %s!", char_name(&s_t)); ui_toast(m);
        }
    }
    s_prev_stage = s_t.stage;
}

static void save_now(void){
    if(tama_save(&s_t, TAMA_FILE) == 0){ s_dirty = 0; s_saved_at = time(NULL); }
}
static void changed(void){ s_dirty = 1; save_now(); }

static void load_state(void){
    if(tama_load(&s_t, TAMA_FILE) != 0){
        tama_new(&s_t, time(NULL), (unsigned)time(NULL));
        save_now();
    }
    s_have = 1;
    s_prev_stage = -1;
}

static void panel_close(void){ s_pmode = PN_NONE; set_hidden(s_panel, 1); }

static void panel_game_text(const char *result){
    char b[64];
    snprintf(b, sizeof b, "%s%sRound %d of 3   %d - %d", result ? result : "", result ? "\n" : "", s_g_round + 1, s_g_win, s_g_loss);
    lv_label_set_text(s_pinfo, b);
}

static const char *rc_msg(tama_rc rc, const char *nothing){
    switch(rc){
    case TAMA_REFUSED_ASLEEP:  return "Asleep";
    case TAMA_REFUSED_FULL:    return "Not hungry";
    case TAMA_REFUSED_DEAD:    return s_t.stage == TAMA_EGG ? "Still an egg" : "Gone";
    case TAMA_REFUSED_NOTHING: return nothing;
    case TAMA_REFUSED_TIRED:   return "Too tired";
    default:                   return NULL;
    }
}
static void act_result(tama_rc rc, const char *nothing){
    const char *m = rc_msg(rc, nothing);
    if(m) ui_toast(m);
    else changed();
    refresh_all();
}

static void panel_open(int mode){
    s_pmode = mode;
    if(mode == PN_FEED){
        lv_label_set_text(s_ptitle, "Feed");
        lv_label_set_text(s_palbl, "Meal"); lv_label_set_text(s_pblbl, "Snack");
        lv_label_set_text(s_pinfo, "Snacks are fun but fattening");
    } else {
        s_g_round = s_g_win = s_g_loss = 0;
        lv_label_set_text(s_ptitle, "Which hand?");
        lv_label_set_text(s_palbl, "Left"); lv_label_set_text(s_pblbl, "Right");
        panel_game_text(NULL);
    }
    set_hidden(s_panel, 0);
    lv_obj_move_foreground(s_panel);
}

static void panel_pick_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED || s_pmode == PN_NONE) return;
    int side = (int)(intptr_t)lv_event_get_user_data(e);
    if(s_pmode == PN_FEED){
        tama_rc rc = side == 0 ? tama_feed_meal(&s_t) : tama_feed_snack(&s_t);
        panel_close();
        act_result(rc, "");
        return;
    }
    int hit = (int)tama_rand(&s_t, 2) == side;
    if(hit) s_g_win++; else s_g_loss++;
    s_g_round++;
    if(s_g_win >= 2 || s_g_loss >= 2){
        int won = s_g_win > s_g_loss;
        tama_rc rc = tama_play(&s_t, won);
        panel_close();
        if(rc == TAMA_OK) ui_toast(won ? "You won!" : "Not this time");
        act_result(rc, "");
        return;
    }
    panel_game_text(hit ? "Hit!" : "Miss");
    s_dirty = 1;
}
static void panel_close_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) panel_close(); }

static void act_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED || s_pmode != PN_NONE) return;
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    tama_advance(&s_t, time(NULL));
    switch(id){
    case B_FEED:
        if(s_t.asleep){ ui_toast("Asleep"); break; }
        panel_open(PN_FEED);
        return;
    case B_PLAY: {
        tama_t probe = s_t;
        tama_rc rc = tama_play(&probe, 0);
        if(rc != TAMA_OK){ ui_toast(rc_msg(rc, "") ? rc_msg(rc, "") : "Not now"); break; }
        panel_open(PN_GAME);
        return;
    }
    case B_CLEAN: act_result(tama_clean(&s_t), "Nothing to clean"); return;
    case B_SLEEP: act_result(tama_set_asleep(&s_t, !s_t.asleep), "Nothing to do"); return;
    case B_HEAL:  act_result(tama_heal(&s_t), "Not sick"); return;
    case B_NEW:
        tama_new(&s_t, time(NULL), (unsigned)time(NULL) ^ 0x9e3779b9u);
        s_prev_stage = s_t.stage;
        changed();
        refresh_all();
        ui_toast("A new egg");
        return;
    }
    refresh_all();
}

extern int ui_screen_backlight_on(void);

static void tick_cb(lv_timer_t *t){
    (void)t;
    if(!s_vis || !s_have) return;
    if(++s_ticks >= ADV_TICKS){
        s_ticks = 0;
        tama_advance(&s_t, time(NULL));
        s_dirty = 1;
        if(time(NULL) - s_saved_at >= SAVE_GAP_S) save_now();
        note_stage_change();
        refresh_all();
        return;
    }
    if(!ui_screen_backlight_on()) return;
    s_frame ^= 1;
    draw_scene();
}

void tama_visible(int on){
    if(on){
        if(!s_have) load_state();
        tama_advance(&s_t, time(NULL));
        s_dirty = 1;
        s_vis = 1; s_frame = 0; s_ticks = 0;
        note_stage_change();
        panel_close();
        if(!s_timer) s_timer = lv_timer_create(tick_cb, TICK_MS, NULL);
        refresh_all();
    } else {
        s_vis = 0;
        if(s_timer){ lv_timer_delete(s_timer); s_timer = NULL; }
        panel_close();
        if(s_have && s_dirty) save_now();
    }
}

void tama_open(void){
    load_state();
    tama_advance(&s_t, time(NULL));
    s_dirty = 1;
    s_prev_stage = s_t.stage;
    refresh_all();
    screen_show(SCR_TAMA);
}

static lv_obj_t *mk_btn(lv_obj_t *parent, const char *text, int w, int h, lv_event_cb_t cb, void *ud, const char *tag, lv_obj_t **lbl){
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(RAISED_PRESSED), LV_STATE_PRESSED);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    ui_on(b, cb, LV_EVENT_CLICKED, ud, tag, UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, TF(UI_14), 0);
    lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_center(l);
    if(lbl) *lbl = l;
    return b;
}

static lv_obj_t *mk_label(lv_obj_t *root, const lv_font_t *f, lv_color_t c, int y, int w){
    lv_obj_t *l = lv_label_create(root);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static lv_obj_t *mk_canvas(lv_obj_t *root, lv_draw_buf_t **buf, int w, int h, int x, int y){
    *buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, 0);
    lv_obj_t *c = lv_canvas_create(root);
    lv_canvas_set_draw_buf(c, *buf);
    lv_obj_set_pos(c, x, y);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    clear_buf(*buf);
    return c;
}

void tama_create(lv_obj_t *root){
    ui_header(root, "Pet");

    s_name = mk_label(root, TF(UI_18), TC(TEXT_PRIMARY), 60, 280);
    s_sub  = mk_label(root, TF(UI_12), TC(TEXT_MUTED), 82, 280);

    s_scene = mk_canvas(root, &s_scene_buf, SCENE_W * CELL, SCENE_H * CELL, 100, 98);

    s_stats = mk_canvas(root, &s_stats_buf, STATS_W, STATS_H, 30, 212);
    static const char *const names[3] = { "Food", "Fun", "Rest" };
    for(int i = 0; i < 3; i++){
        s_lab[i] = lv_label_create(root);
        lv_label_set_text(s_lab[i], names[i]);
        lv_obj_set_style_text_font(s_lab[i], TF(UI_12), 0);
        lv_obj_set_style_text_color(s_lab[i], TC(TEXT_SECONDARY), 0);
        lv_obj_set_pos(s_lab[i], 30 + i * 100, 212);
    }

    s_row = lv_obj_create(root);
    lv_obj_remove_style_all(s_row);
    lv_obj_set_size(s_row, 290, 44);
    lv_obj_set_pos(s_row, 35, 232);
    lv_obj_set_style_pad_column(s_row, 4, 0);
    lv_obj_set_flex_flow(s_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_row, LV_OBJ_FLAG_SCROLLABLE);
    static const char *const bl[B_COUNT] = { "Feed", "Play", "Clean", "Sleep", "Heal", "New egg" };
    static const char *const bt[B_COUNT] = { "tama.feed", "tama.play", "tama.clean", "tama.sleep", "tama.heal", "tama.newegg" };
    for(int i = 0; i < B_COUNT; i++)
        s_btn[i] = mk_btn(s_row, bl[i], i == B_NEW ? 130 : 50, 44, act_cb, (void *)(intptr_t)i, bt[i], &s_btnlbl[i]);

    s_panel = lv_obj_create(root);
    lv_obj_remove_style_all(s_panel);
    kit_keep(s_panel);
    lv_obj_set_size(s_panel, 250, 172);
    lv_obj_set_pos(s_panel, 55, 96);
    lv_obj_set_style_radius(s_panel, !strcmp(theme_kit()->id, "terminal") ? 0 : 20, 0);
    lv_obj_set_style_bg_color(s_panel, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_panel, TC(BORDER_STRONG), 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    s_ptitle = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_ptitle, TF(UI_16), 0);
    lv_obj_set_style_text_color(s_ptitle, TC(TEXT_PRIMARY), 0);
    lv_obj_align(s_ptitle, LV_ALIGN_TOP_MID, 0, 10);
    s_pa = mk_btn(s_panel, "", 110, 56, panel_pick_cb, (void *)(intptr_t)0, "tama.panel.a", &s_palbl);
    s_pb = mk_btn(s_panel, "", 110, 56, panel_pick_cb, (void *)(intptr_t)1, "tama.panel.b", &s_pblbl);
    lv_obj_set_pos(s_pa, 10, 40); lv_obj_set_pos(s_pb, 130, 40);
    lv_obj_set_style_bg_color(s_pa, TC(SURFACE), 0); lv_obj_set_style_bg_color(s_pb, TC(SURFACE), 0);
    s_pinfo = lv_label_create(s_panel);
    lv_obj_set_width(s_pinfo, 230);
    lv_obj_set_style_text_align(s_pinfo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_pinfo, TF(UI_12), 0);
    lv_obj_set_style_text_color(s_pinfo, TC(TEXT_SECONDARY), 0);
    lv_obj_align(s_pinfo, LV_ALIGN_TOP_MID, 0, 98);
    lv_obj_t *x = mk_btn(s_panel, "Cancel", 110, 38, panel_close_cb, NULL, "tama.panel.close", NULL);
    lv_obj_set_pos(x, 70, 130);
    lv_obj_set_ext_click_area(x, 4);
    lv_obj_set_style_bg_color(x, TC(SURFACE), 0);
    set_hidden(s_panel, 1);
}
