/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Album Roulette, built in (2.2.0): the spin, from the Album Roulette 1.3.1 app (its timings, easing, winner choice,
 * covers and gestures kept as they were), now a screen of mq_ui (SCR_ROULETTE_SPIN) behind the vinyl landing page.
 *
 *   I feel lucky! (roulette.c) -> this screen: the roulette wheel while up to 37 albums are prepared (a background
 *   thread, roulette_backend.c), then the spin starts by itself: covers race past and settle on a winner.
 *   Tap Play! or the cover: the album plays in track order and the cover zooms into Music.
 *   Hold the cover: another spin (the prepared albums are reused, the last three winners left out).
 *   Back (or Exit) returns to the landing page; leaving stops any preparation still running.
 * Colours and fonts come from the theme; Play! stays the white roulette ball in every theme. */
#include "screens.h"
#include "theme.h"
#include "config.h"
#include "musicdb.h"
#include "roulette_backend.h"
#include "lvgl/src/core/lv_obj_event_private.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roulette_rim.h"

LV_FONT_DECLARE(font_inter_bold_22)
LV_FONT_DECLARE(font_inter_medium_16)
LV_FONT_DECLARE(font_inter_medium_20)
enum { LANDING, SPINNING, SETTLING, WINNER, REDO, STARTING, ZOOMING, LEFT };
enum { FRAME_MS = 33, SPIN_MS = 8000, REVEAL_MS = 220, SETTLE_MS = 320, REDO_MS = 150, ZOOM_MS = 650 };
static int stage = LEFT, winning, ready_seen, settled, revealing;
static float spinner_phase;
static int slot_image[3] = { -1, -1, -1 };
static uint32_t started, opened, ready_at;
static float finish, settle_from;
static mdb_album_key_t recent[3];
static int recent_count;
static lv_style_transition_dsc_t press_transition;
static const lv_style_prop_t press_properties[] = { LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT, 0 };
static lv_anim_t text_scroll;
static pthread_t loader;
static int loader_joinable;
static lv_obj_t *ball_btn;
static lv_obj_t *root_obj, *landing, *spin, *status_label, *discs[3], *title, *artist, *ball, *hint, *fade, *orbit_ball, *selection_mark;
static lv_obj_t *error_panel, *error_text;
static lv_timer_t *anim_timer;
static lv_image_dsc_t images[POOL];
static lv_image_dsc_t rim;
static uint32_t *themed_rim;
static lv_point_t orbit[360];

static void plain(lv_obj_t *o){ lv_obj_remove_style_all(o); lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE); }
static lv_obj_t *label(lv_obj_t *parent, const char *s, int x, int y, int w, const lv_font_t *font, lv_color_t color){
    lv_obj_t *o = lv_label_create(parent); lv_label_set_text(o, s); lv_obj_set_pos(o, x, y); lv_obj_set_width(o, w);
    lv_obj_set_style_text_font(o, font, 0); lv_obj_set_style_text_color(o, color, 0); lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    return o;
}
static void button_feedback(lv_obj_t *o){
    lv_obj_set_style_transform_width(o, 0, 0); lv_obj_set_style_transform_height(o, 0, 0);
    lv_obj_set_style_transform_width(o, -2, LV_STATE_PRESSED); lv_obj_set_style_transform_height(o, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transition(o, &press_transition, 0);
}
/* a pill in the theme's own colours: primary (accent) or quiet (surface) */
static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, int primary, lv_event_cb_t cb){
    lv_obj_t *o = lv_button_create(parent); plain(o); lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, h / 2, 0);
    lv_obj_set_style_bg_color(o, primary ? TC(ACCENT_PRIMARY) : TC(SURFACE_RAISED), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, primary ? TC(PRIMARY_PRESSED) : TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    if(!primary){ lv_obj_set_style_border_width(o, 1, 0); lv_obj_set_style_border_color(o, TC(BORDER), 0); }
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = label(o, text, 0, 0, w, primary ? TF(UI_20) : TF(UI_18), primary ? TC(ON_ACCENT) : TC(TEXT_PRIMARY)); lv_obj_center(l);
    button_feedback(o);
    return o;
}
static float clamp01(float p){ return p < 0 ? 0 : p > 1 ? 1 : p; }
/* Zero velocity and acceleration at both ends; no abrupt fade/zoom edges. */
static float smooth(float p){ p = clamp01(p); return p * p * p * (p * (p * 6 - 15) + 10); }
static void winner_text(void){
    /* Restart the reading pause at the beginning of each new winner/error retry. */
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP); lv_label_set_long_mode(artist, LV_LABEL_LONG_CLIP);
    lv_label_set_text(title, albums[winning].key.album);
    lv_label_set_text(artist, albums[winning].key.owner[0] ? albums[winning].key.owner : "Unknown artist");
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL); lv_label_set_long_mode(artist, LV_LABEL_LONG_SCROLL);
}
static void winner_opacity(lv_opa_t opacity){
    lv_obj_set_style_opa(title, opacity, 0); lv_obj_set_style_opa(artist, opacity, 0); lv_obj_set_style_opa(ball, opacity, 0);
}
static void show_error(const char *s){
    lv_label_set_text(error_text, s); lv_obj_remove_flag(error_panel, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(error_panel);
}
static void exit_cb(lv_event_t *e){ (void)e; screen_back(); }
static void set_images(void){
    for(int i = 0; i < album_count; i++){
        lv_image_cache_drop(&images[i]);  /* Descriptors are reused after a re-roll. */
        images[i].header.magic = LV_IMAGE_HEADER_MAGIC; images[i].header.cf = LV_COLOR_FORMAT_ARGB8888;
        images[i].header.w = DISC; images[i].header.h = DISC; images[i].header.stride = DISC * 4;
        images[i].data_size = DISC * DISC * 4; images[i].data = (const uint8_t *)albums[i].pixels;
    }
}
/* Reuse three sprites. Only change source when crossing to another album. */
static void strip(float position){
    int center = (int)position; float fraction = position - center;
    for(int k = 0; k < 3; k++){
        int ordinal = center + k - 1, index = ((ordinal % album_count) + album_count) % album_count;
        if(slot_image[k] != index){ lv_image_set_src(discs[k], &images[index]); slot_image[k] = index; }
        lv_obj_set_pos(discs[k], (360 - DISC) / 2 + (int)lroundf((k - 1 - fraction) * (DISC + 16)), 0);
    }
}
static int same_album(const mdb_album_key_t *a, const mdb_album_key_t *b){
    return a->owner_null == b->owner_null && !strcmp(a->album, b->album) && !strcmp(a->owner, b->owner);
}
static int choose_winner(void){
    int options[POOL], count = 0, exclude = recent_count;
    if(exclude >= album_count) exclude = album_count - 1;
    for(int i = 0; i < album_count; i++){
        int seen = 0; for(int j = 0; j < exclude; j++) if(same_album(&albums[i].key, &recent[j])){ seen = 1; break; }
        if(!seen) options[count++] = i;
    }
    return count ? options[random_below((uint32_t)count)] : 0;
}
static void remember_winner(void){
    const mdb_album_key_t *key = &albums[winning].key;
    if(recent_count && same_album(key, &recent[0])) return;
    for(int i = 2; i > 0; i--) recent[i] = recent[i - 1];
    recent[0] = *key;
    if(recent_count < 3) recent_count++;
}
static void start_spin(void){
    if(atomic_load(&prepared) != 1 || album_count < 1 || album_count > POOL) return;
    /* Re-roll the prepared pool without decoding covers again. */
    for(int i = album_count - 1; i > 0; i--){ int j = (int)random_below(i + 1); album_t t = albums[i]; albums[i] = albums[j]; albums[j] = t; }
    finish = POOL - 1; winning = ((int)finish) % album_count;
    int chosen = choose_winner(); album_t selected = albums[chosen]; albums[chosen] = albums[winning]; albums[winning] = selected;
    set_images(); revealing = 0;
    lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(artist, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(fade, LV_OBJ_FLAG_HIDDEN); lv_obj_set_style_opa(fade, 0, 0);
    lv_obj_add_flag(error_panel, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_state(ball_btn, LV_STATE_DISABLED); winner_opacity(255);
    for(int i = 0; i < 3; i++){
        slot_image[i] = -1; lv_obj_remove_flag(discs[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(discs[i], LV_OBJ_FLAG_CLICKABLE); lv_image_set_scale(discs[i], 256); lv_obj_set_y(discs[i], 0);
    }
    stage = SPINNING; started = lv_tick_get();
    lv_obj_add_flag(landing, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(spin, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(selection_mark, LV_OBJ_FLAG_HIDDEN); lv_obj_set_style_opa(selection_mark, 255, 0);
    lv_label_set_text(hint, "Let chance choose."); lv_timer_set_period(anim_timer, FRAME_MS); strip(0);
}
static void playback_failed(const char *message){
    stage = WINNER; revealing = 0; winner_opacity(255); winner_text();
    lv_obj_add_flag(discs[1], LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_state(ball_btn, LV_STATE_DISABLED);
    lv_timer_set_period(anim_timer, 250); show_error(message);
}
/* The winner, in track order, through mq_ui's own album plan. A rare exact-playlist fallback must keep the disc and
 * track order, or nothing is played (never quietly a different order). */
static int play_winner(char *error, int cap){
    mdb_plan_t plan; memset(&plan, 0, sizeof plan);
    if(!mdb_album_plan(&albums[winning].key, &plan)){ snprintf(error, cap, "The album changed or is gone.\nTry another spin."); return 0; }
    if(plan.count < MIN_TRACKS){ mdb_plan_free(&plan); snprintf(error, cap, "The album now has fewer\nthan 4 songs."); return 0; }
    if(plan.list_type == 5){
        int count = plan.count, *order = malloc((size_t)count * sizeof *order);
        if(!order){ mdb_plan_free(&plan); snprintf(error, cap, "Not enough memory."); return 0; }
        memcpy(order, plan.ids, (size_t)count * sizeof *order);
        int ok = mdb_plan_materialize(&plan) && count == plan.count && !memcmp(order, plan.ids, (size_t)count * sizeof *order);
        free(order);
        if(!ok){ mdb_plan_free(&plan); snprintf(error, cap, "This album can't be\nplayed in track order."); return 0; }
    }
    cfg_set_int("work_mode", 0); ui_set_workmode(0);   /* in order, as an album should */
    int ok = ui_play_plan(&plan, plan.ids[0]);
    mdb_plan_free(&plan);
    if(!ok) snprintf(error, cap, "The player is busy.\nTap Retry.");
    return ok;
}
static void play_cb(lv_event_t *e){
    (void)e; if(stage != WINNER) return;
    lv_obj_add_flag(error_panel, LV_OBJ_FLAG_HIDDEN);
    stage = STARTING; revealing = 0; winner_opacity(255); lv_obj_remove_flag(discs[1], LV_OBJ_FLAG_CLICKABLE); lv_obj_add_state(ball_btn, LV_STATE_DISABLED);
    char error[160];
    if(!play_winner(error, sizeof error)){ playback_failed(error); return; }
    stage = ZOOMING; started = lv_tick_get();
    lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(artist, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP); lv_label_set_long_mode(artist, LV_LABEL_LONG_CLIP);
    lv_obj_remove_flag(fade, LV_OBJ_FLAG_HIDDEN); lv_timer_set_period(anim_timer, FRAME_MS);
}
static void retry_cb(lv_event_t *e){ (void)e; lv_obj_add_flag(error_panel, LV_OBJ_FLAG_HIDDEN); stage = WINNER; play_cb(NULL); }
/* Use SHORT_CLICKED so releasing after a long press never starts playback. */
static void cover_cb(lv_event_t *e){
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_HIT_TEST){
        lv_hit_test_info_t *hit = lv_event_get_param(e); lv_area_t area;
        lv_obj_get_coords(lv_event_get_target(e), &area);
        int dx = 2 * (hit->point->x - area.x1) + 1 - DISC, dy = 2 * (hit->point->y - area.y1) + 1 - DISC;
        hit->res = hit->res && dx * dx + dy * dy <= DISC * DISC; return;
    }
    if(stage != WINNER || !lv_obj_has_flag(error_panel, LV_OBJ_FLAG_HIDDEN)) return;
    if(code == LV_EVENT_SHORT_CLICKED) play_cb(e);
    else if(code == LV_EVENT_LONG_PRESSED){
        stage = REDO; revealing = 0; started = lv_tick_get();
        lv_obj_remove_flag(discs[1], LV_OBJ_FLAG_CLICKABLE); lv_obj_add_state(ball_btn, LV_STATE_DISABLED);
        lv_obj_remove_flag(fade, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(fade); lv_obj_set_style_opa(fade, 0, 0);
        lv_timer_set_period(anim_timer, FRAME_MS);
    }
}
static void winner(void){
    stage = WINNER; strip(finish); remember_winner(); lv_obj_add_flag(selection_mark, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(discs[1], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(discs[0], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(discs[2], LV_OBJ_FLAG_HIDDEN);
    winner_text();
    lv_obj_remove_flag(title, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(artist, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(ball, LV_OBJ_FLAG_HIDDEN); lv_label_set_text(hint, "");
    revealing = 1; started = lv_tick_get(); winner_opacity(0); lv_timer_set_period(anim_timer, FRAME_MS);
}
static void zoom(float p){
    p = clamp01(p); float e = smooth(p);
    lv_image_set_scale(discs[1], (int)lroundf(256 + e * 150));
    lv_obj_set_y(discs[1], (int)lroundf((180 - DISC / 2) * e));
    lv_obj_set_style_opa(fade, (int)lroundf(255 * smooth((p - 0.15f) / 0.85f)), 0);
}
static void place_orbit(float degrees){
    while(degrees >= 360) degrees -= 360;
    while(degrees < 0) degrees += 360;
    spinner_phase = degrees;
    int a = (int)degrees, b = (a + 1) % 360; float f = degrees - a;
    int x = (int)lroundf(orbit[a].x + (orbit[b].x - orbit[a].x) * f);
    int y = (int)lroundf(orbit[a].y + (orbit[b].y - orbit[a].y) * f);
    lv_obj_set_pos(orbit_ball, x - 5, y - 5);
}
/* Leaving the screen: stop a preparation still running (its thread is collected on the next visit) */
static void left(void){
    stage = LEFT; atomic_store(&cancelled, 1);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP); lv_label_set_long_mode(artist, LV_LABEL_LONG_CLIP);
    if(anim_timer) lv_timer_pause(anim_timer);
}
static void timer(lv_timer_t *unused){
    (void)unused; uint32_t now = lv_tick_get();
    if(stage != LEFT && stage != ZOOMING && screen_current() != SCR_ROULETTE_SPIN){ left(); return; }
    if(stage == LANDING){
        int ready = atomic_load(&prepared);
        if(!ready_seen && ready){
            ready_seen = ready; ready_at = now; settle_from = spinner_phase;
            if(ready > 0){ char s[80]; snprintf(s, sizeof s, "%d album%s.\nOne lucky pick.", album_count, album_count == 1 ? "" : "s"); lv_label_set_text(status_label, s); }
            else { lv_obj_set_style_text_font(status_label, &font_inter_medium_16, 0); lv_label_set_text(status_label, load_error); }
        }
        if(!ready){ place_orbit(((now - opened) % 2400) * 360.0f / 2400);
            int n = atomic_load(&progress); if(n > 0){ char s[48]; snprintf(s, sizeof s, "Shuffling the shelf...\n%d covers", n); lv_label_set_text(status_label, s); } }
        else if(!settled){
            float t = (now - ready_at) / 750.0f; if(t >= 1){ t = 1; settled = 1; }
            float q = 1 - t; place_orbit(settle_from + (360 - settle_from) * (1 - q * q * q));
        }
        if(ready > 0 && settled) start_spin();                                     /* tapped I feel lucky: it goes */
        else if(ready < 0 && settled) lv_timer_set_period(anim_timer, 250);
    } else if(stage == SPINNING){
        float t = (now - started) / (float)SPIN_MS;
        if(t >= 1){
            strip(finish); stage = SETTLING; started = now; lv_obj_add_flag(discs[0], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(discs[2], LV_OBJ_FLAG_HIDDEN); return;
        }
        float q = 1 - t; strip(finish * (1 - q * q * q));
    } else if(stage == SETTLING){
        float p = clamp01((now - started) / 220.0f), q = 1 - p;
        lv_obj_set_x(discs[1], (360 - DISC) / 2 - (int)lroundf(3 * 16 * p * p * q * q));
        lv_obj_set_style_opa(selection_mark, (int)lroundf(255 * (1 - smooth(((now - started) - 220.0f) / 100))), 0);
        if(now - started >= SETTLE_MS) winner();
    } else if(stage == REDO){                                                       /* held cover: through black, spin again */
        float p = clamp01((now - started) / (float)REDO_MS);
        lv_obj_set_style_opa(fade, (int)lroundf(255 * smooth(p < 0.5f ? p * 2 : (1 - p) * 2)), 0);
        if(p >= 0.5f && stage == REDO && lv_obj_has_flag(title, LV_OBJ_FLAG_HIDDEN) == false){
            lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(artist, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP); lv_label_set_long_mode(artist, LV_LABEL_LONG_CLIP);
        }
        if(p >= 1){ lv_obj_add_flag(fade, LV_OBJ_FLAG_HIDDEN); start_spin(); }
    } else if(stage == WINNER && revealing){
        float p = (now - started) / (float)REVEAL_MS; winner_opacity((int)lroundf(255 * smooth(p)));
        if(p >= 1){ revealing = 0; lv_timer_set_period(anim_timer, 250); }
    } else if(stage == ZOOMING){
        float p = (now - started) / (float)ZOOM_MS; zoom(p);
        if(p >= 1){ left(); screen_show(SCR_NOWPLAYING); }                          /* the cover becomes Music */
    }
}
void roulette_spin_create(lv_obj_t *root){
    root_obj = root;
    lv_style_transition_dsc_init(&press_transition, press_properties, lv_anim_path_ease_out, 90, 0, NULL);
    lv_anim_init(&text_scroll); lv_anim_set_delay(&text_scroll, 1100);
    lv_anim_set_playback_delay(&text_scroll, 1100); lv_anim_set_repeat_delay(&text_scroll, 1100);
    lv_anim_set_repeat_count(&text_scroll, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
}
/* Built on each visit, so it always wears the current theme */
static void build(void){
    lv_obj_clean(root_obj);
    lv_obj_set_style_bg_color(root_obj, TC(CANVAS), 0);
    landing = lv_obj_create(root_obj); plain(landing); lv_obj_set_size(landing, 360, 360);
    rim.header.magic = LV_IMAGE_HEADER_MAGIC; rim.header.cf = LV_COLOR_FORMAT_XRGB8888; rim.header.w = 360; rim.header.h = 360;
    rim.header.stride = 360 * 4; rim.data_size = sizeof rim_pixels;
    if(!themed_rim) themed_rim = malloc(sizeof rim_pixels);
    if(themed_rim){                                                                /* the wheel, its centre in the screen colour */
        memcpy(themed_rim, rim_pixels, sizeof rim_pixels); uint32_t canvas = 0xff000000u | (lv_color_to_u32(TC(CANVAS)) & 0xffffffu);
        for(int y = 0; y < 360; y++) for(int x = 0; x < 360; x++){ int dx = x - 180, dy = y - 180; if(dx * dx + dy * dy < 145 * 145) themed_rim[y * 360 + x] = canvas; }
        lv_image_cache_drop(&rim);
    }
    rim.data = (const uint8_t *)(themed_rim ? themed_rim : rim_pixels);
    lv_obj_t *wheel = lv_image_create(landing); lv_image_set_src(wheel, &rim);
    for(int i = 0; i < 360; i++){ float a = i * 6.283185307f / 360; orbit[i].x = 180 + (int)lroundf(146 * sinf(a)); orbit[i].y = 180 - (int)lroundf(146 * cosf(a)); }
    orbit_ball = lv_obj_create(landing); plain(orbit_ball); lv_obj_set_size(orbit_ball, 10, 10); lv_obj_set_style_radius(orbit_ball, 5, 0);
    lv_obj_set_style_bg_color(orbit_ball, TC(FIXED_MEDIA_WHITE), 0); lv_obj_set_style_bg_opa(orbit_ball, 255, 0);
    lv_obj_set_style_border_width(orbit_ball, 1, 0); lv_obj_set_style_border_color(orbit_ball, TC(BORDER_STRONG), 0); place_orbit(0);
    label(landing, "ALBUM", 60, 75, 240, TF(UI_28), TC(TEXT_PRIMARY)); label(landing, "ROULETTE", 40, 106, 280, TF(UI_28), TC(TEXT_PRIMARY));
    status_label = label(landing, "Shuffling the shelf...", 65, 148, 230, &font_inter_medium_20, TC(TEXT_SECONDARY));
    button(landing, "Exit", 121, 250, 118, 42, 0, exit_cb);
    spin = lv_obj_create(root_obj); plain(spin); lv_obj_set_size(spin, 360, 360); lv_obj_add_flag(spin, LV_OBJ_FLAG_HIDDEN);
    for(int i = 0; i < 3; i++){ discs[i] = lv_image_create(spin); lv_image_set_pivot(discs[i], DISC / 2, DISC / 2); lv_image_set_antialias(discs[i], true); }
    selection_mark = lv_obj_create(spin); plain(selection_mark); lv_obj_remove_flag(selection_mark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(selection_mark, 177, 7); lv_obj_set_size(selection_mark, 6, 14); lv_obj_set_style_radius(selection_mark, 3, 0);
    lv_obj_set_style_bg_color(selection_mark, TC(FIXED_MEDIA_WHITE), 0); lv_obj_set_style_bg_opa(selection_mark, 255, 0);
    lv_obj_set_style_border_width(selection_mark, 1, 0); lv_obj_set_style_border_color(selection_mark, TC(BORDER_STRONG), 0);
    lv_obj_add_flag(discs[1], LV_OBJ_FLAG_ADV_HITTEST); lv_obj_add_event_cb(discs[1], cover_cb, LV_EVENT_ALL, NULL);
    title = label(spin, "", 57, 274, 246, TF(UI_26), TC(TEXT_PRIMARY)); lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL);
    artist = label(spin, "", 85, 306, 190, TF(UI_22), TC(TEXT_SECONDARY)); lv_label_set_long_mode(artist, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_anim(title, &text_scroll, 0); lv_obj_set_style_anim(artist, &text_scroll, 0);
    lv_obj_set_style_anim_duration(title, lv_anim_speed_clamped(24, 1600, 60000), 0);
    lv_obj_set_style_anim_duration(artist, lv_anim_speed_clamped(24, 1600, 60000), 0);
    /* Play!: the roulette ball, white with black type in every theme. The type sits beside the ball, not in it,
     * so no theme's button styling can recolour it; `ball` (the pair) is what shows, hides and fades. */
    ball = lv_obj_create(spin); plain(ball); lv_obj_set_pos(ball, 144, DISC - 72 - 6); lv_obj_set_size(ball, 72, 72);
    lv_obj_remove_flag(ball, LV_OBJ_FLAG_CLICKABLE);
    ball_btn = lv_obj_create(ball); plain(ball_btn); lv_obj_set_size(ball_btn, 72, 72);
    lv_obj_add_flag(ball_btn, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(ball_btn, 6); lv_obj_set_style_radius(ball_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ball_btn, TC(FIXED_MEDIA_WHITE), 0); lv_obj_set_style_bg_opa(ball_btn, 255, 0);
    lv_obj_set_style_bg_opa(ball_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ball_btn, 1, 0); lv_obj_set_style_border_color(ball_btn, TC(FIXED_MEDIA_BLACK), 0); lv_obj_set_style_border_opa(ball_btn, LV_OPA_70, 0);
    lv_obj_add_flag(ball_btn, LV_OBJ_FLAG_USER_4);                                 /* the theme kit leaves the ball alone */
    lv_obj_add_event_cb(ball_btn, play_cb, LV_EVENT_CLICKED, NULL);
    button_feedback(ball_btn);
    lv_obj_t *bl = lv_label_create(ball); lv_label_set_text(bl, "Play!");
    lv_obj_set_style_text_font(bl, &font_inter_bold_22, 0); lv_obj_set_style_text_color(bl, TC(FIXED_MEDIA_BLACK), 0); lv_obj_center(bl);
    lv_obj_remove_flag(bl, LV_OBJ_FLAG_CLICKABLE);
    hint = label(spin, "", 40, 283, 280, &font_inter_bold_22, TC(TEXT_SECONDARY));
    lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(artist, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);
    fade = lv_obj_create(root_obj); plain(fade); lv_obj_set_size(fade, 360, 360);
    lv_obj_set_style_bg_color(fade, TC(FIXED_MEDIA_BLACK), 0); lv_obj_set_style_bg_opa(fade, 255, 0); lv_obj_set_style_opa(fade, 0, 0);
    lv_obj_add_flag(fade, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(fade, LV_OBJ_FLAG_CLICKABLE);
    error_panel = lv_obj_create(root_obj); plain(error_panel); lv_obj_set_size(error_panel, 360, 360);
    lv_obj_set_style_bg_color(error_panel, TC(CANVAS), 0); lv_obj_set_style_bg_opa(error_panel, 255, 0);
    label(error_panel, "Couldn't start the album", 35, 80, 290, &font_inter_bold_22, TC(TEXT_PRIMARY));
    error_text = label(error_panel, "", 55, 124, 250, &font_inter_medium_16, TC(TEXT_SECONDARY));
    button(error_panel, "Retry", 90, 209, 180, 44, 1, retry_cb); button(error_panel, "Exit", 121, 266, 118, 40, 0, exit_cb);
    lv_obj_add_flag(error_panel, LV_OBJ_FLAG_HIDDEN);
}
/* The landing page's I feel lucky!: a fresh sample of albums, the wheel while they're prepared, then the spin */
void roulette_spin_open(void){
    if(loader_joinable){ atomic_store(&cancelled, 1); pthread_join(loader, NULL); loader_joinable = 0; }
    for(int i = 0; i < POOL; i++) lv_image_cache_drop(&images[i]);
    roulette_reset();
    build();
    stage = LANDING; ready_seen = 0; settled = 0; revealing = 0; opened = lv_tick_get();
    if(!anim_timer) anim_timer = lv_timer_create(timer, FRAME_MS, NULL);
    lv_timer_set_period(anim_timer, FRAME_MS); lv_timer_resume(anim_timer);
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 512 * 1024);
    loader_joinable = !pthread_create(&loader, &attr, prepare_albums, NULL);
    pthread_attr_destroy(&attr);
    if(!loader_joinable){ snprintf(load_error, sizeof load_error, "Couldn't start.\nTry again."); atomic_store(&prepared, -1); }
    screen_show(SCR_ROULETTE_SPIN);
}
