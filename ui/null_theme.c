/* SPDX-License-Identifier: GPL-3.0-or-later
 * Null: shared pixel system, persistent horizontal menu, Music and vertical QS.
 * All player/radio/library actions use existing diskOS dispatchers. No
 * background I/O here.
 */
#include "null_theme.h"
#include "config.h"
#include "ipc.h"
#include "null_menu.h"
#include "null_transport_icons.h"
#include "quicksettings.h"
#include "scanner.h"
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int disco_catalog_load(void);
const char *disco_catalog_id(int);
const char *disco_catalog_name(int);
void disco_catalog_open(int);
void quicksettings_null_action(int id, int hold);
extern const lv_font_t null_pixel_14;
static lv_font_t pixel14;
static lv_obj_t *home, *bg, *wash, *title, *artist, *album, *seek, *fill, *playhead, *times, *mode,
    *buttons[3], *icons[3], *gesture, *hints[3];
static lv_obj_t *nav, *band, *edge, *edge_line[5];
static lv_obj_t *clock_box, *music_clock, *music_status;
void disco_track_overlay_open(void);
void dhome_overlay_close(void);
int disco_track_overlay_visible(void);
static lv_point_precise_t edge_points[3];
static null_menu_t *menu;
static char names[10][24];
static const char *name_ptrs[10];
static lv_obj_t *qroot, *qpanel, *qcover, *qart, *qpp, *qprev, *qnext, *qclose, *qbright, *qicons[3],
    *qfields[8], *qcaptions[8];
static int qids[8], qcount;
static const void *last_sharp;
static lv_image_dsc_t pixel_dsc;
static uint8_t *pixel_buf;
static int nav_open, nav_value, have, playing, qs_open, seek_drag, ctl_shown = 1, wake_guard,
                                                                   last_ctl_mode = -1, last_ctl_visible = -1;
static long seek_duration;
static lv_point_t seek_start, gz_start, edge_start;
static uint32_t gz_t0, gz_last, last_touch;
static int edge_drag;
static lv_timer_t *clock_timer;
static char last_title[192], last_artist[192], last_album[192], last_times[64];
static int left(void) { return cfg_get_int("null_side", 0) == 1; }
static lv_text_align_t alignment(void)
{
    return cfg_get_int("null_text_align", 0) == 1 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
}
static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static void keep_tree(lv_obj_t *o)
{
    kit_keep(o);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        keep_tree(lv_obj_get_child(o, i));
}
static void text_set(lv_obj_t *l, const char *s)
{
    if (l && strcmp(lv_label_get_text(l), s ? s : ""))
        lv_label_set_text(l, s ? s : "");
}
static lv_obj_t *text(lv_obj_t *p, const char *s, int x, int y, int w, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, s);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
static void icon_replace(lv_obj_t *p, lv_obj_t **old, int x, int y, int kind, lv_color_t c)
{
    if (*old) {
        lv_obj_delete(*old);
        *old = NULL;
    }
    *old = null_transport_icon_create(p, x, y, (null_transport_icon_t)kind, c);
    keep_tree(*old);
}
static void controls_paint(void);
static void transport_event(lv_event_t *e)
{
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *b = lv_event_get_current_target(e);
    lv_obj_t *i = lv_obj_get_child(b, 0);
    if (c == LV_EVENT_PRESSED) {
        last_touch = lv_tick_get();
        if (i)
            null_transport_icon_set_colour(i, TC(TEXT_PRIMARY));
    } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
        if (i)
            null_transport_icon_set_colour(i, which == 1 && playing ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY));
    } else if (c == LV_EVENT_CLICKED) {
        static const char *cmd[] = {"0201000C0002", "0201000C0000", "0201000C0001"};
        if (ui_transport_command(cmd[which]) == 0 && which == 1) {
            playing = ui_pp_icon_playing(ui_is_playing());
            controls_paint();
        }
    }
}
static lv_obj_t *transport_button(lv_obj_t *p, int x, int y, int size, int which, lv_obj_t **icon)
{
    lv_obj_t *b = box(p, x, y, size, size);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    *icon = null_transport_icon_create(b, (size - 22) / 2, (size - 22) / 2,
                                       which == 0   ? NULL_ICON_PREVIOUS
                                       : which == 2 ? NULL_ICON_NEXT
                                       : playing    ? NULL_ICON_PAUSE
                                                    : NULL_ICON_PLAY,
                                       which == 1 && playing ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY));
    ui_on(b, transport_event, LV_EVENT_CLICKED, (void *)(intptr_t)which,
          which == 0   ? "np.prev"
          : which == 1 ? "np.pp"
                       : "np.next",
          UI_CORE);
    lv_obj_add_event_cb(b, transport_event, LV_EVENT_PRESSED, (void *)(intptr_t)which);
    lv_obj_add_event_cb(b, transport_event, LV_EVENT_RELEASED, (void *)(intptr_t)which);
    lv_obj_add_event_cb(b, transport_event, LV_EVENT_PRESS_LOST, (void *)(intptr_t)which);
    return b;
}
static void controls_paint(void)
{
    static int was = -1;
    if (was != playing) {
        was = playing;
        if (buttons[1])
            icon_replace(buttons[1], &icons[1], 13, 13, playing ? NULL_ICON_PAUSE : NULL_ICON_PLAY,
                         playing ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY));
        if (qpp)
            icon_replace(qpp, &qicons[1], 13, 13, playing ? NULL_ICON_PAUSE : NULL_ICON_PLAY,
                         playing ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY));
    }
    if (qcover) {
        lv_obj_set_style_shadow_opa(qcover, playing ? 85 : 0, 0);
    }
}
static void mode_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_np_mode_next();
        last_touch = lv_tick_get();
    }
}
static void track_event(lv_event_t *e)
{
    if (!have)
        return;
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        screen_show(SCR_NOWPLAYING);
        ui_np_fsart_open();
    } else if (lv_event_get_code(e) == LV_EVENT_CLICKED && !screen_press_was_long())
        disco_track_overlay_open();
}
static void seek_event(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    if (!in)
        return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) {
        track_state_t st;
        ipc_get_state(&st);
        seek_duration = st.have_track ? st.duration_ms : 0;
        seek_drag = seek_duration > 0;
        seek_start = p;
        last_touch = lv_tick_get();
    }
    if ((c == LV_EVENT_PRESSING || c == LV_EVENT_RELEASED) && seek_drag) {
        int x = p.x - 62;
        if (x < 0)
            x = 0;
        if (x > 236)
            x = 236;
        lv_obj_set_width(fill, x ? x : 1);
        lv_obj_set_x(playhead, 62 + x - 3);
        if (c == LV_EVENT_RELEASED) {
            if (abs(p.y - seek_start.y) < 70)
                ui_seek_to((long)((long long)seek_duration * x / 236));
            seek_drag = 0;
        }
    }
    if (c == LV_EVENT_PRESS_LOST)
        seek_drag = 0;
}
static void gesture_event(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    if (!in)
        return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_event_code_t c = lv_event_get_code(e);
    uint32_t now = lv_tick_get();
    last_touch = now;
    if (c == LV_EVENT_PRESSED) {
        gz_start = p;
        gz_t0 = now;
        if (cfg_get_int("null_controls", 0) == 1 && !ctl_shown) {
            ctl_shown = 1;
            wake_guard = 1;
            lv_obj_move_foreground(gesture);
            return;
        }
    }
    if (c == LV_EVENT_RELEASED && wake_guard) {
        wake_guard = 0;
        return;
    }
    if (c != LV_EVENT_RELEASED || cfg_get_int("null_controls", 0) != 2)
        return;
    int dx = p.x - gz_start.x, dy = p.y - gz_start.y;
    if (abs(dx) >= 40 && abs(dx) > 2 * abs(dy) && now - gz_t0 < 700) {
        gz_last = 0;
        ui_transport_command(dx > 0 ? "0201000C0001" : "0201000C0002");
    } else if (abs(dx) < 18 && abs(dy) < 18 && now - gz_t0 < 400) {
        if (gz_last && now - gz_last < 400) {
            gz_last = 0;
            ui_transport_command("0201000C0000");
        } else
            gz_last = now;
    }
}
static void layout_music(void)
{
    if (!home)
        return;
    /* Split at the artist / album boundary, keeping both halves clear of the 90px
     * menu band. */
    int lift = nav_value * 46 / 1000, drop = nav_value * 51 / 1000;
    lv_obj_set_y(title, 116 - lift);
    lv_obj_set_y(artist, 154 - lift);
    lv_obj_set_y(album, 180 + drop);
    lv_obj_set_y(seek, 197 + drop);
    lv_obj_set_y(playhead, 216 + drop);
    lv_obj_set_y(times, 227 + drop);
    lv_obj_set_y(mode, 227 + drop);
    for (int i = 0; i < 3; i++)
        lv_obj_set_y(buttons[i], 254 + drop);
    lv_obj_set_y(gesture, 248 + drop);
}
static void nav_paint(void)
{
    if (!band)
        return;
    lv_obj_set_x(band, left() ? -360 + nav_value * 360 / 1000 : 360 - nav_value * 360 / 1000);
    layout_music();
    int cx = left() ? 24 : 336, dir = left() ? 1 : -1;
    if (nav_open)
        dir = -dir;
    edge_points[0] = (lv_point_precise_t){cx - dir * 5, 168};
    edge_points[1] = (lv_point_precise_t){cx + dir * 5, 180};
    edge_points[2] = (lv_point_precise_t){cx - dir * 5, 192};
    lv_color_t col =
        nav_open && cfg_get_int("null_drawer_state_colours", 1) ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY);
    for (int i = 0; i < 5; i++) {
        lv_line_set_points(edge_line[i], edge_points, 3);
        lv_obj_set_style_line_color(edge_line[i], col, 0);
    }
    lv_obj_set_x(edge, left() ? 0 : 312);
}
static void nav_anim(void *u, int32_t v)
{
    (void)u;
    nav_value = v;
    nav_paint();
}
static void nav_set(int on)
{
    nav_open = !!on;
    if (menu)
        null_menu_set_active(menu, on);
    last_touch = lv_tick_get();
    lv_anim_delete(nav, nav_anim);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, nav);
    lv_anim_set_exec_cb(&a, nav_anim);
    lv_anim_set_values(&a, nav_value, on ? 1000 : 0);
    lv_anim_set_duration(&a, screen_get_anim() ? 200 : 1);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    nav_paint();
}
static void edge_event(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    if (!in)
        return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) {
        edge_start = p;
        edge_drag = 0;
        last_touch = lv_tick_get();
    } else if (c == LV_EVENT_PRESSING) {
        int dx = (p.x - edge_start.x) * (left() ? 1 : -1);
        if (abs(dx) > 7) {
            edge_drag = 1;
            lv_anim_delete(nav, nav_anim);
            nav_value = nav_open ? 1000 + dx * 4 : dx * 4;
            if (nav_value < 0)
                nav_value = 0;
            if (nav_value > 1000)
                nav_value = 1000;
            nav_paint();
        }
    } else if (c == LV_EVENT_RELEASED) {
        nav_set(edge_drag ? nav_value >= 500 : !nav_open);
    } else if (c == LV_EVENT_PRESS_LOST)
        nav_set(nav_value >= 500);
}
static void menu_selected(int index, void *u)
{
    (void)u;
    nav_set(0);
    disco_catalog_open(index);
}
void null_menu_refresh(void)
{
    if (!band)
        return;
    if (menu) {
        null_menu_destroy(menu);
        menu = NULL;
    }
    int n = disco_catalog_load();
    if (n > 10)
        n = 10;
    static const char *ids[] = {"music",  "library", "settings",  "modes",   "shortcuts", "eq",
                                "search", "albums",  "queue",     "folders", "books",     "artists",
                                "songs",  "myqueue", "weather",   "ma",      "roulette",  "boomshine",
                                "stats",  "apps",    "songsphere"};
    static const char *caps[] = {"MUSIC", "LIBRY",  "STNGS", "MODES", "SHRTS", "EQ",    "SRCH",
                                 "ALBMS", "UP NXT", "FLDRS", "BOOKS", "ARTST", "SONGS", "QUEUE",
                                 "WTHR",  "SNDSPN", "ROULT", "BOOM",  "STATS", "APPS",  "SPHERE"};
    for (int i = 0; i < n; i++) {
        const char *s = disco_catalog_name(i);
        for (int k = 0; k < 21; k++)
            if (!strcmp(disco_catalog_id(i), ids[k])) {
                s = caps[k];
                break;
            }
        theme_upper(names[i], sizeof names[i], s);
        name_ptrs[i] = names[i];
    }
    menu = null_menu_create(band, name_ptrs, n, TC(TEXT_SECONDARY), TC(TEXT_PRIMARY), menu_selected, NULL);
    null_menu_set_active(menu, nav_open);
    lv_obj_update_layout(band);
}
static void font_setup(void)
{
    pixel14 = null_pixel_14;
    pixel14.fallback = ui_font_cjk(14);
}
static void clock_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        int on = lv_obj_has_flag(clock_box, LV_OBJ_FLAG_HIDDEN);
        if (cfg_set_int("null_clock", on) < 0) {
            ui_toast("Couldn't save settings");
            return;
        }
        null_options_changed();
    } else if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED &&
               !lv_obj_has_flag(clock_box, LV_OBJ_FLAG_HIDDEN))
        weather_app_open();
}
void null_home_clock(const char *t) { text_set(music_clock, t ? t : "--:--"); }
void null_home_status(int batt, int charging, int wifi, int bt)
{
    char b[64] = "";
    size_t n = 0;
    if (wifi)
        n += (size_t)snprintf(b + n, sizeof b - n, LV_SYMBOL_WIFI "  ");
    if (bt)
        n += (size_t)snprintf(b + n, sizeof b - n, LV_SYMBOL_BLUETOOTH "  ");
    const char *bs = charging     ? LV_SYMBOL_CHARGE
                     : batt >= 90 ? LV_SYMBOL_BATTERY_FULL
                     : batt >= 65 ? LV_SYMBOL_BATTERY_3
                     : batt >= 40 ? LV_SYMBOL_BATTERY_2
                     : batt >= 15 ? LV_SYMBOL_BATTERY_1
                                  : LV_SYMBOL_BATTERY_EMPTY;
    if (batt >= 0)
        snprintf(b + n, sizeof b - n, "%s", bs);
    text_set(music_status, b);
}
void null_home_create(lv_obj_t *r)
{
    home = r;
    font_setup();
    bg = lv_image_create(r);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    wash = box(r, 0, 0, 360, 360);
    lv_obj_set_style_bg_opa(wash, 153, 0);
    lv_obj_t *clock_hit = box(r, 72, 10, 216, 58);
    lv_obj_set_style_bg_opa(clock_hit, 0, 0);
    lv_obj_add_flag(clock_hit, LV_OBJ_FLAG_CLICKABLE);
    ui_on(clock_hit, clock_event, LV_EVENT_LONG_PRESSED, NULL, "null.clock.toggle", UI_REACHABLE);
    lv_obj_add_event_cb(clock_hit, clock_event, LV_EVENT_SHORT_CLICKED, NULL);
    clock_box = box(r, 72, 10, 216, 58);
    lv_obj_set_style_bg_opa(clock_box, 0, 0);
    music_status = text(clock_box, "", 0, 7, 216, TF(UI_14), TC(TEXT_SECONDARY));
    music_clock = text(clock_box, "--:--", 0, 28, 216, TF(CLOCK), TC(TEXT_PRIMARY));
    lv_obj_set_style_text_font(music_clock, TF(UI_22), 0);
    lv_obj_set_style_text_align(music_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(music_clock, LV_TEXT_ALIGN_CENTER, 0);
    title = text(r, "NOT PLAYING", 36, 116, 288, TF(HEADER), TC(TEXT_PRIMARY));
    artist = text(r, "", 36, 157, 288, &pixel14, TC(TEXT_SECONDARY));
    album = text(r, "", 36, 180, 288, TF(UI_16), TC(TEXT_SECONDARY));
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_long_mode(artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_long_mode(album, LV_LABEL_LONG_DOT);
    lv_obj_t *track = box(r, 36, 64, 272, 132);
    lv_obj_set_style_bg_opa(track, 0, 0);
    lv_obj_add_flag(track, LV_OBJ_FLAG_CLICKABLE);
    ui_on(track, track_event, LV_EVENT_CLICKED, NULL, "null.track.menu", UI_REACHABLE);
    ui_on(track, track_event, LV_EVENT_LONG_PRESSED, NULL, "null.track.immersive", UI_REACHABLE);
    seek = box(r, 62, 234, 236, 44);
    lv_obj_set_style_bg_opa(seek, 0, 0);
    lv_obj_add_flag(seek, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_t *line = box(seek, 0, 21, 236, 3);
    lv_obj_set_style_bg_color(line, TC(TEXT_SECONDARY), 0);
    fill = box(seek, 0, 21, 1, 3);
    lv_obj_set_style_bg_color(fill, TC(TEXT_PRIMARY), 0);
    playhead = box(r, 59, 253, 7, 7);
    lv_obj_set_style_bg_color(playhead, TC(TEXT_PRIMARY), 0);
    ui_on(seek, seek_event, LV_EVENT_ALL, NULL, "np.seek", UI_CORE);
    times = text(r, "00:00 / 00:00", 72, 263, 174, TF(UI_14), TC(TEXT_PRIMARY));
    mode = box(r, 250, 263, 44, 24);
    lv_obj_set_style_bg_opa(mode, 0, 0);
    lv_obj_add_flag(mode, LV_OBJ_FLAG_CLICKABLE);
    text(mode, "", 10, 0, 24, TF(UI_16), TC(TEXT_SECONDARY));
    ui_on(mode, mode_event, LV_EVENT_CLICKED, NULL, "np.mode", UI_CORE);
    gesture = box(r, 76, 282, 208, 62);
    lv_obj_set_style_bg_opa(gesture, 0, 0);
    lv_obj_add_flag(gesture, LV_OBJ_FLAG_CLICKABLE);
    ui_on(gesture, gesture_event, LV_EVENT_ALL, NULL, "null.music.gestures", UI_CORE);
    hints[0] = text(gesture, "SWIPE L    SWIPE R", 0, 3, 208, TF(UI_12), TC(TEXT_HINT));
    hints[1] = text(gesture, "PREV          NEXT", 0, 17, 208, TF(UI_12), TC(TEXT_HINT));
    hints[2] = text(gesture, "DOUBLE TAP: PLAY/PAUSE", 0, 31, 208, TF(UI_12), TC(TEXT_HINT));
    for (int i = 0; i < 3; i++)
        buttons[i] = transport_button(r, 96 + i * 60, 288, 48, i, &icons[i]);
    keep_tree(r);
    null_options_changed();
}
void null_home_update(const char *t, const char *a, int p)
{
    have = t != NULL;
    playing = !!p;
    if (title) {
        char b[192];
        theme_upper(b, sizeof b, t ? t : "NOT PLAYING");
        if (strcmp(last_title, b)) {
            snprintf(last_title, sizeof last_title, "%s", b);
            text_set(title, b);
        }
        theme_upper(b, sizeof b, a ? a : "");
        if (strcmp(last_artist, b)) {
            snprintf(last_artist, sizeof last_artist, "%s", b);
            text_set(artist, b);
        }
    }
    controls_paint();
}
/* The cover decoder owns source pixels. Copy/average once per art publication
 * into this theme's cache. */
void null_art_changed(void)
{
    if (!home)
        return;
    const void *src = ui_current_sharp_img();
    last_sharp = src;
    int kind = cfg_get_int("null_music_background", 0);
    if (src && lv_image_src_get_type(src) == LV_IMAGE_SRC_VARIABLE && kind == 2) {
        const lv_image_dsc_t *d = src;
        int bpp = d->header.cf == LV_COLOR_FORMAT_RGB888                                                 ? 3
                  : d->header.cf == LV_COLOR_FORMAT_XRGB8888 || d->header.cf == LV_COLOR_FORMAT_ARGB8888 ? 4
                                                                                                         : 0;
        if (bpp && d->data && d->header.w >= 20 && d->header.h >= 20) {
            if (!pixel_buf)
                pixel_buf = malloc(360 * 360 * 3);
            if (pixel_buf) {
                int size = d->header.w < d->header.h ? d->header.w : d->header.h,
                    ox = (d->header.w - size) / 2, oy = (d->header.h - size) / 2;
                for (int cy = 0; cy < 20; cy++)
                    for (int cx = 0; cx < 20; cx++) {
                        unsigned sums[3] = {0}, n = 0;
                        for (int y = cy * size / 20; y < (cy + 1) * size / 20; y++)
                            for (int x = cx * size / 20; x < (cx + 1) * size / 20; x++) {
                                const uint8_t *p = d->data + (y + oy) * d->header.stride + (x + ox) * bpp;
                                for (int c = 0; c < 3; c++)
                                    sums[c] += p[c];
                                n++;
                            }
                        for (int y = cy * 18; y < (cy + 1) * 18; y++)
                            for (int x = cx * 18; x < (cx + 1) * 18; x++)
                                for (int c = 0; c < 3; c++)
                                    pixel_buf[(y * 360 + x) * 3 + c] = (uint8_t)(sums[c] / n);
                    }
                pixel_dsc = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                                        .cf = LV_COLOR_FORMAT_RGB888,
                                                        .w = 360,
                                                        .h = 360,
                                                        .stride = 1080},
                                             .data_size = 388800,
                                             .data = pixel_buf};
                lv_image_set_src(bg, NULL);
                lv_image_set_scale(bg, 256);
                lv_image_set_src(bg, &pixel_dsc);
                lv_obj_set_pos(bg, 0, 0);
            }
        }
    } else if (src && kind == 1) {
        lv_image_set_src(bg, NULL);
        lv_image_set_src(bg, src);
        const lv_image_dsc_t *d = src;
        if (lv_image_src_get_type(src) == LV_IMAGE_SRC_VARIABLE) {
            lv_image_set_scale(bg, 360 * 256 / d->header.w);
            lv_obj_center(bg);
        }
    }
    if (kind == 0 || !src)
        lv_obj_add_flag(bg, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(wash, kind == 2 ? 164 : 153, 0);
    if (qart) {
        const void *cover = ui_current_cover_dsc();
        lv_image_set_src(qart, cover);
        if (cover) {
            const lv_image_dsc_t *d = cover;
            lv_image_set_scale(qart, 96 * 256 / d->header.w);
            lv_obj_center(qart);
        } else
            lv_image_set_src(qart, NULL);
    }
}
static void qs_field_event(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot < 0 || slot >= qcount)
        return;
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED)
        lv_obj_set_style_text_color(qcaptions[slot], TC(TEXT_PRIMARY), 0);
    else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST)
        lv_obj_set_style_text_color(qcaptions[slot], TC(TEXT_SECONDARY), 0);
    else if (c == LV_EVENT_LONG_PRESSED)
        quicksettings_null_action(qids[slot], 1);
    else if (c == LV_EVENT_CLICKED && !screen_press_was_long())
        quicksettings_null_action(qids[slot], 0);
}
static void qs_close_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
        screen_back();
}
static void qs_bright_event(lv_event_t *e)
{
    int value = lv_slider_get_value(lv_event_get_target(e));
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
        ui_backlight(value);
    else
        ui_set_brightness(value);
}
static void qs_outside(lv_event_t *e)
{
    if (lv_event_get_target(e) != qroot || lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    screen_back();
}
static void qpos(void *u, int32_t v)
{
    lv_obj_set_y(u, v);
    if (qclose) {
        if (v == 0 && qs_open)
            lv_obj_remove_flag(qclose, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(qclose, LV_OBJ_FLAG_HIDDEN);
    }
}
static void qhidden(lv_anim_t *a)
{
    (void)a;
    if (!qs_open && qroot)
        lv_obj_add_flag(qroot, LV_OBJ_FLAG_HIDDEN);
}
void null_qs_slide(int open)
{
    if (!qpanel)
        return;
    qs_open = !!open;
    lv_anim_delete(qpanel, qpos);
    if (open && lv_obj_get_y(qpanel) == 0)
        lv_obj_set_y(qpanel, -360);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, qpanel);
    lv_anim_set_exec_cb(&a, qpos);
    lv_anim_set_values(&a, lv_obj_get_y(qpanel), open ? 0 : -360);
    lv_anim_set_duration(&a, screen_get_anim() ? 200 : 1);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, qhidden);
    lv_anim_start(&a);
}
static void qdeleted(lv_event_t *e)
{
    (void)e;
    qpanel = qcover = qart = qpp = qprev = qnext = qclose = qbright = NULL;
    memset(qicons, 0, sizeof qicons);
    memset(qfields, 0, sizeof qfields);
    memset(qcaptions, 0, sizeof qcaptions);
}
static void qs_construct(void)
{
    if (qpanel)
        return;
    qpanel = box(qroot, 72, -360, 216, 360);
    lv_obj_add_event_cb(qpanel, qdeleted, LV_EVENT_DELETE, NULL);
    /* An inexpensive, static halo separates the black column from the black Music
     * background. */
    lv_obj_set_style_shadow_color(qpanel, TC(TEXT_SECONDARY), 0);
    lv_obj_set_style_shadow_width(qpanel, 14, 0);
    lv_obj_set_style_shadow_opa(qpanel, 65, 0);
    lv_obj_set_style_border_side(qpanel, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(qpanel, 1, 0);
    lv_obj_set_style_border_color(qpanel, TC(TEXT_SECONDARY), 0);
    lv_obj_set_style_border_opa(qpanel, 45, 0);
    qcover = box(qpanel, 60, 132, 96, 96);
    lv_obj_set_style_shadow_color(qcover, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_shadow_width(qcover, 18, 0);
    lv_obj_set_style_shadow_opa(qcover, playing ? 85 : 0, 0);
    qart = lv_image_create(qcover);
    lv_obj_remove_flag(qart, LV_OBJ_FLAG_CLICKABLE);
    qprev = transport_button(qpanel, 2, 158, 44, 0, &qicons[0]);
    qpp = transport_button(qcover, 24, 24, 48, 1, &qicons[1]);
    lv_obj_set_style_bg_opa(qpp, 160, 0);
    qnext = transport_button(qpanel, 170, 158, 44, 2, &qicons[2]);
    /* Four fields above and four below. Lower field hit boxes end before the
     * close handle. */
    for (int k = 0; k < 8; k++) {
        int col = k % 2, row = (k % 4) / 2, y = k < 4 ? 20 + row * 48 : 226 + row * 44;
        qfields[k] = box(qpanel, 12 + col * 96, y, 96, 44);
        lv_obj_set_style_bg_opa(qfields[k], 0, 0);
        lv_obj_add_flag(qfields[k], LV_OBJ_FLAG_CLICKABLE);
        qcaptions[k] = text(qfields[k], "", 0, 12, 96, &pixel14, TC(TEXT_SECONDARY));
        lv_obj_set_style_text_align(qcaptions[k], LV_TEXT_ALIGN_CENTER, 0);
        ui_on(qfields[k], qs_field_event, LV_EVENT_CLICKED, (void *)(intptr_t)k, "qs.tile", UI_CORE);
        ui_on(qfields[k], qs_field_event, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)k, "qs.tile.hold",
              UI_CORE);
        lv_obj_add_event_cb(qfields[k], qs_field_event, LV_EVENT_PRESSED, (void *)(intptr_t)k);
        lv_obj_add_event_cb(qfields[k], qs_field_event, LV_EVENT_RELEASED, (void *)(intptr_t)k);
        lv_obj_add_event_cb(qfields[k], qs_field_event, LV_EVENT_PRESS_LOST, (void *)(intptr_t)k);
    }
    qclose = box(qpanel, 84, 314, 48, 44);
    lv_obj_set_style_bg_opa(qclose, 0, 0);
    lv_obj_add_flag(qclose, LV_OBJ_FLAG_CLICKABLE);
    static lv_point_precise_t points[3] = {{12, 27}, {24, 17}, {36, 27}};
    int widths[] = {19, 13, 9, 5, 3}, opas[] = {8, 17, 38, 80, 255};
    for (int i = 0; i < 5; i++) {
        lv_obj_t *l = lv_line_create(qclose);
        lv_line_set_points(l, points, 3);
        lv_obj_set_style_line_color(l, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_line_width(l, widths[i], 0);
        lv_obj_set_style_line_opa(l, opas[i], 0);
        lv_obj_set_style_line_rounded(l, 1, 0);
    }
    ui_on(qclose, qs_close_event, LV_EVENT_CLICKED, NULL, "qs.close", UI_CORE);
    lv_obj_add_flag(qclose, LV_OBJ_FLAG_HIDDEN);
    /* Keep brightness reachable without changing the eight-field composition: a
     * slim side slider. */
    qbright = lv_slider_create(qroot);
    lv_obj_set_size(qbright, 6, 80);
    lv_obj_set_pos(qbright, 310, 140);
    lv_obj_set_ext_click_area(qbright, 12);
    lv_slider_set_range(qbright, 4, 40);
    lv_slider_set_value(qbright, ui_get_brightness(), LV_ANIM_OFF);
    lv_obj_set_style_radius(qbright, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(qbright, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(qbright, TC(TEXT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(qbright, 3, LV_PART_KNOB);
    ui_on(qbright, qs_bright_event, LV_EVENT_VALUE_CHANGED, NULL, "qs.brightness", UI_CORE);
    lv_obj_add_event_cb(qbright, qs_bright_event, LV_EVENT_RELEASED, NULL);
    keep_tree(qroot);
}
void null_qs_create(lv_obj_t *r)
{
    qroot = r;
    font_setup();
    lv_obj_set_style_bg_opa(r, 0, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, qs_outside, LV_EVENT_CLICKED, NULL);
    qs_construct();
    null_qs_build();
}
void null_qs_build(void)
{
    if (!qroot)
        return;
    qs_construct();
    int items[QS_MAX_ITEMS];
    int count = quicksettings_items_load(items);
    qcount = 0;
    /* Wi-Fi and Settings are pinned, duplicates are filtered by the loader. Fill
     * unused slots deterministically. */
    qids[qcount++] = 0;
    for (int k = 0; k < count && qcount < 7; k++)
        if (items[k] != 0 && items[k] != 5)
            qids[qcount++] = items[k];
    static const int fallback[] = {1, 4, 6, 3, 7, 11, 2};
    for (int i = 0; i < 7 && qcount < 7; i++) {
        int found = 0;
        for (int j = 0; j < qcount; j++)
            if (qids[j] == fallback[i])
                found = 1;
        if (!found)
            qids[qcount++] = fallback[i];
    }
    qids[qcount++] = 5;
    static const char *shorts[] = {"WIFI",  "BT",   "LIBRY", "SCAN",   "MODES", "STNGS",
                                   "EQ",    "SRCH", "QUEUE", "UP NXT", "SHRTS", "WTHR",
                                   "LYRCS", "INFO", "IMMRS", "FLDRS",  "FAVS"};
    for (int i = 0; i < 8; i++)
        text_set(qcaptions[i], qids[i] >= 0 && qids[i] < 17 ? shorts[qids[i]] : "ITEM");
    if (qpp) {
        icon_replace(qpp, &qicons[1], 13, 13, playing ? NULL_ICON_PAUSE : NULL_ICON_PLAY,
                     playing ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY));
    }
    null_art_changed();
}
static void radio_state(void)
{
    if (!qs_open || !qpanel)
        return;
    for (int i = 0; i < qcount; i++) {
        int on = qids[i] == 0   ? wifi_radio_live()
                 : qids[i] == 1 ? bt_state() == BT_ON
                 : qids[i] == 3 ? scanner_active()
                                : 0;
        if (!lv_obj_has_state(qfields[i], LV_STATE_PRESSED))
            lv_obj_set_style_text_color(qcaptions[i], on ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY), 0);
    }
}
static void time_fmt(char *b, size_t n, long ms)
{
    long s = ms > 0 ? ms / 1000 : 0;
    snprintf(b, n, "%02ld:%02ld", s / 60, s % 60);
}
static void ctl_opa(void *o, int32_t value) { lv_obj_set_style_opa(o, (lv_opa_t)value, 0); }
static void ctl_done(lv_anim_t *a)
{
    if (!ctl_shown)
        lv_obj_add_flag(a->var, LV_OBJ_FLAG_HIDDEN);
}
static void controls_visibility(int cm, int visible)
{
    ctl_shown = visible;
    if (last_ctl_mode == cm && last_ctl_visible == visible)
        return;
    int animate = last_ctl_mode == cm && cm == 1;
    last_ctl_mode = cm;
    last_ctl_visible = visible;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *o = i < 3 ? buttons[i] : mode;
        lv_anim_delete(o, ctl_opa);
        if (cm == 2) {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        if (!animate) {
            if (visible)
                lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(o, visible ? 255 : 0, 0);
        } else {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, o);
            lv_anim_set_exec_cb(&a, ctl_opa);
            lv_anim_set_values(&a, lv_obj_get_style_opa(o, 0), visible ? 255 : 0);
            lv_anim_set_duration(&a, visible ? 180 : 400);
            lv_anim_set_completed_cb(&a, ctl_done);
            lv_anim_start(&a);
        }
        if (visible)
            lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        else
            lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    }
    if (cm == 2 && visible) {
        lv_obj_remove_flag(mode, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mode, LV_OBJ_FLAG_CLICKABLE);
    }
}
static void heartbeat(lv_timer_t *t)
{
    (void)t;
    if (!th_null())
        return;
    int cur = screen_current();
    track_state_t st = {0};
    if (cur == SCR_HOME || cur == SCR_QUICK)
        ipc_get_state(&st);
    if (cur == SCR_HOME && home) {
        char b[192], a[32], z[32];
        if (strcmp(last_album, st.have_track ? st.album : "")) {
            snprintf(last_album, sizeof last_album, "%s", st.have_track ? st.album : "");
            text_set(album, last_album);
        }
        time_fmt(a, sizeof a, st.position_ms);
        time_fmt(z, sizeof z, st.duration_ms);
        snprintf(b, sizeof b, "%s / %s", a, z);
        if (strcmp(b, last_times)) {
            snprintf(last_times, sizeof last_times, "%.63s", b);
            text_set(times, b);
        }
        if (!seek_drag) {
            int v = st.have_track && st.duration_ms > 0
                        ? (int)((long long)st.position_ms * 236 / st.duration_ms)
                        : 0;
            if (v < 0)
                v = 0;
            if (v > 236)
                v = 236;
            if (lv_obj_get_width(fill) != (v ? v : 1))
                lv_obj_set_width(fill, v ? v : 1);
            if (lv_obj_get_x(playhead) != 59 + v)
                lv_obj_set_x(playhead, 59 + v);
        }
        static const char *md[] = {LV_SYMBOL_LOOP, LV_SYMBOL_LOOP, LV_SYMBOL_SHUFFLE, LV_SYMBOL_LOOP, "1"};
        int m = ui_play_mode_now();
        if (m < 0 || m > 4)
            m = 0;
        text_set(lv_obj_get_child(mode, 0), md[m]);
        int cm = cfg_get_int("null_controls", 0),
            shown = cm != 1 || lv_display_get_inactive_time(NULL) < 5000;
        controls_visibility(cm, shown);
        if ((cm == 1 && (!shown || wake_guard)) || cm == 2) {
            lv_obj_remove_flag(gesture, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(gesture);
        } else
            lv_obj_add_flag(gesture, LV_OBJ_FLAG_HIDDEN);
    }
    radio_state();
    if (nav_open) {
        static const int secs[] = {0, 5, 10, 30};
        int a = cfg_get_int("null_autohide", 2);
        if (a < 0 || a > 3)
            a = 2;
        uint32_t idle = lv_display_get_inactive_time(NULL);
        if (idle < 300)
            last_touch = lv_tick_get();
        if (secs[a] && lv_tick_get() - last_touch >= (uint32_t)secs[a] * 1000)
            nav_set(0);
    }
}
int null_owns_point(int x, int y)
{
    if (!th_null())
        return 0;
    if (screen_current() == SCR_QUICK || disco_track_overlay_visible())
        return 1;
    if (nav_value > 0 && y >= 135 && y <= 225)
        return 1;
    if (y >= 152 && y <= 208 && (left() ? x <= 48 : x >= 312))
        return 1;
    if (screen_current() == SCR_HOME) {
        if (y >= 188 && y < 310 && x >= 60 && x <= 302)
            return 1;
        if (y >= 248 && y < 360 && x >= 76 && x <= 284)
            return 1;
        if (y >= 10 && y < 68 && x >= 72 && x <= 288)
            return 1;
        if (y >= 64 && y <= 196 && x >= 36 && x <= 308)
            return 1;
    }
    return 0;
}
void null_screen_leaving(void)
{
    if (!th_null())
        return;
    dhome_overlay_close();
    if (nav)
        nav_set(0);
    seek_drag = 0;
}
void null_screen_entered(int which)
{
    if (!th_null() || !nav)
        return;
    lv_obj_t *root = screen_get_root(which);
    if (root && which != SCR_QUICK && which != SCR_HOME)
        kit_restyle(root);
    int hidden = which == SCR_QUICK || which == SCR_SAVER || which == SCR_NOWPLAYING ||
                 which == SCR_SPHERE; /* custom app interiors stay independent */
    if (menu)
        null_menu_set_active(menu, !hidden && nav_open);
    if (hidden)
        lv_obj_add_flag(nav, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_remove_flag(nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(nav);
        int main = which == SCR_HOME || which == SCR_LIBRARY || which == SCR_SETTINGS || which == SCR_APPS ||
                   which == SCR_APPSHELF;
        lv_obj_set_style_opa(nav, main ? 255 : 145, 0);
    }
}
void null_init(lv_obj_t *parent)
{
    lv_timer_set_period(lv_display_get_refr_timer(lv_display_get_default()), 16);
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in))
        if (lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER)
            lv_timer_set_period(lv_indev_get_read_timer(in), 16);
    nav = box(parent, 0, 0, 360, 360);
    lv_obj_set_style_bg_opa(nav, 0, 0);
    band = box(nav, 360, 135, 360, 90);
    lv_obj_set_style_border_side(band, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(band, 1, 0);
    lv_obj_set_style_border_color(band, TC(BORDER), 0);
    null_menu_refresh();
    edge = box(nav, left() ? 0 : 312, 152, 48, 56);
    lv_obj_set_style_bg_opa(edge, 0, 0);
    lv_obj_add_flag(edge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    ui_on(edge, edge_event, LV_EVENT_ALL, NULL, "null.nav.drawer", UI_CORE);
    int widths[] = {19, 13, 9, 5, 3}, opas[] = {8, 17, 38, 80, 255};
    for (int i = 0; i < 5; i++) {
        edge_line[i] = lv_line_create(nav);
        lv_obj_set_style_line_width(edge_line[i], widths[i], 0);
        lv_obj_set_style_line_opa(edge_line[i], opas[i], 0);
        lv_obj_set_style_line_rounded(edge_line[i], 1, 0);
        lv_obj_remove_flag(edge_line[i], LV_OBJ_FLAG_CLICKABLE);
    }
    nav_paint();
    keep_tree(nav);
    if (!clock_timer)
        clock_timer = lv_timer_create(heartbeat, 100, NULL);
    null_art_changed();
    null_screen_entered(screen_current());
}
void null_options_changed(void)
{
    if (!th_null() || !home)
        return;
    if (clock_box) {
        if (cfg_get_int("null_clock", 1))
            lv_obj_remove_flag(clock_box, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(clock_box, LV_OBJ_FLAG_HIDDEN);
    }
    last_ctl_mode = -1;
    last_ctl_visible = -1;
    wake_guard = 0;
    lv_obj_set_style_text_align(title, alignment(), 0);
    lv_obj_set_style_text_align(artist, alignment(), 0);
    lv_obj_set_style_text_align(album, alignment(), 0);
    lv_obj_set_style_text_align(times, alignment(), 0);
    if (cfg_get_int("null_music_album", 1))
        lv_obj_remove_flag(album, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(album, LV_OBJ_FLAG_HIDDEN);
    int cm = cfg_get_int("null_controls", 0);
    if (cm == 2)
        lv_obj_remove_flag(gesture, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(gesture, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; i++) {
        if (cm == 2)
            lv_obj_add_flag(buttons[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 3; i++) {
        if (cm == 2)
            lv_obj_remove_flag(hints[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(hints[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (nav)
        nav_paint();
    null_art_changed();
}
static lv_obj_t *sclock, *sdate, *sweather, *strack, *sartist;
void null_saver_create(lv_obj_t *r)
{
    sclock = text(r, "--:--", 36, 122, 288, TF(SAVER_CLOCK), TC(TEXT_PRIMARY));
    sdate = text(r, "", 50, 196, 260, TF(UI_14), TC(TEXT_SECONDARY));
    sweather = text(r, "", 44, 75, 272, TF(UI_16), TC(TEXT_SECONDARY));
    strack = text(r, "", 40, 242, 280, TF(HEADER_SM), TC(TEXT_PRIMARY));
    sartist = text(r, "", 44, 275, 272, TF(UI_14), TC(TEXT_SECONDARY));
    lv_label_set_long_mode(strack, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_long_mode(sartist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    keep_tree(r);
}
void null_saver_clock(const char *t, const char *d)
{
    text_set(sclock, t ? t : "--:--");
    text_set(sdate, d ? d : "");
}
void null_saver_weather(const char *w) { text_set(sweather, w ? w : ""); }
void null_saver_track(const char *t, const char *a)
{
    text_set(strack, t ? t : "");
    text_set(sartist, a ? a : "");
}
/* Shared screen grammar: black canvas, square controls, pixel headers, fine
 * pixel reading text. No screen is rebuilt here, so its action registry, list
 * model, menus and keyboard remain intact. */
static void null_style(lv_obj_t *o, kit_role_t role)
{
    if (!th_null())
        return;
    if (role == KIT_SCREEN) {
        if (o != qroot) {
            lv_obj_set_style_bg_color(o, TC(CANVAS), 0);
            lv_obj_set_style_bg_opa(o, 255, 0);
            lv_obj_set_style_bg_image_src(o, NULL, 0);
        }
        return;
    }
    if (role == KIT_ROW || role == KIT_BUTTON || role == KIT_ICON_BUTTON) {
        lv_obj_set_style_radius(o, 0, 0);
        lv_obj_set_style_shadow_width(o, 0, 0);
        lv_obj_set_style_bg_color(o, TC(CANVAS), 0);
        lv_obj_set_style_bg_opa(o, 0, 0);
        lv_obj_set_style_bg_color(o, TC(SURFACE_STRONG), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(o, 70, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(o, role == KIT_ROW ? 1 : 0, 0);
        lv_obj_set_style_border_side(o, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(o, TC(BORDER), 0);
        lv_obj_set_style_text_color(o, TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(o, TC(TEXT_PRIMARY), LV_STATE_PRESSED);
        return;
    }
    if (role == KIT_CARD || role == KIT_POPUP || role == KIT_SCRIM) {
        lv_obj_set_style_radius(o, 0, 0);
        lv_obj_set_style_shadow_width(o, 0, 0);
        lv_obj_set_style_bg_color(o, TC(CANVAS), 0);
        lv_obj_set_style_bg_opa(
            o,
            role == KIT_CARD ? ((lv_obj_get_width(o) >= 340 && lv_obj_get_height(o) >= 300) ? 255 : 0) : 245,
            0);
        if (role == KIT_POPUP) {
            lv_obj_set_style_border_width(o, 1, 0);
            lv_obj_set_style_border_color(o, TC(TEXT_SECONDARY), 0);
        }
        return;
    }
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *s = lv_label_get_text(o);
        int glyph = s && ((unsigned char)s[0] >= 0xE0);
        extern const lv_font_t font_weather16;
        if (lv_obj_get_style_text_font(o, 0)->fallback == &font_weather16)
            glyph = 1;
        if (role == KIT_TITLE) {
            lv_obj_set_style_text_color(o, TC(TEXT_PRIMARY), 0);
            if (!lv_obj_has_flag(o, THEME_TITLE_STEPPED))
                lv_obj_set_style_text_font(o, TF(HEADER), 0);
        } else if (role == KIT_ROW_TEXT || role == KIT_ROW_CHEVRON || role == KIT_BACK) {
            lv_obj_set_style_text_color(o, TC(TEXT_SECONDARY), 0);
        } else if (role == KIT_ROW_SUB)
            lv_obj_set_style_text_color(o, TC(TEXT_PRIMARY), 0);
        if (!glyph && s && s[0] && role != KIT_TITLE) {
            const lv_font_t *cur = lv_obj_get_style_text_font(o, 0);
            int h = lv_font_get_line_height(cur);
            const lv_font_t *f = h >= 28   ? TF(UI_28)
                                 : h >= 24 ? TF(UI_20)
                                 : h >= 21 ? TF(UI_18)
                                 : h >= 19 ? TF(UI_16)
                                 : h >= 16 ? TF(UI_14)
                                           : TF(UI_12);
            lv_obj_set_style_text_font(o, f, 0);
        }

        return;
    }
    if (role == KIT_SLIDER || role == KIT_BAR || role == KIT_TOGGLE) {
        lv_obj_set_style_radius(o, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(o, 0, LV_PART_INDICATOR);
        lv_obj_set_style_radius(o, 0, LV_PART_KNOB);
    }
    if (role == KIT_ARC) {
        lv_obj_set_style_arc_rounded(o, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(o, 0, LV_PART_INDICATOR);
    }
}
static lv_obj_t *null_header(lv_obj_t *r, const char *t, lv_event_cb_t cb)
{
    if (!th_null())
        return theme_kit_default.header(r, t, cb);
    lv_obj_t *l = theme_kit_default.header(r, t, cb);
    char b[192];
    theme_upper(b, sizeof b, t);
    text_set(l, b);
    const lv_font_t *f = TF(HEADER);
    lv_point_t size;
    lv_text_get_size(&size, b, f, 0, 0, LV_COORD_MAX, 0);
    if (size.x > 152) {
        f = &pixel14;
        lv_obj_add_flag(l, THEME_TITLE_STEPPED);
    }
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_t *back = lv_obj_get_child(r, lv_obj_get_index(l) - 1);
    if (back) {
        lv_obj_set_x(back, left() ? 244 : 72);
        lv_obj_t *ic = lv_obj_get_child(back, 0);
        if (ic)
            text_set(ic, left() ? ">" : "<");
    }
    return l;
}
const theme_kit_t theme_kit_null = {
    .id = "null", .header = null_header, .style = null_style, .back_text = "<"};

void null_nav_set_open(int open)
{
    if (nav)
        nav_set(open);
}
