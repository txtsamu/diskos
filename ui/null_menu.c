/* Standalone LVGL 9 continuous menu prototype. No player or screen-stack dependencies. */
#include "null_menu.h"
#include <math.h>
#include <stdlib.h>
#include "theme.h"
#define CELL 132
#define VIEW 264
#define RAIL_X 60
#define RAIL_W 228
#define THUMB_W 30
struct null_menu {
    lv_obj_t *root, *viewport, *input, *rail, *thumb, **labels;
    lv_timer_t *timer;
    int count, down, drag, handle, pressed, coasting, settling, autoscroll;
    float pos, start_pos, last_pos, velocity, from, target;
    lv_point_t origin, last;
    uint32_t began, last_tick, last_sample, settle_start;
    lv_color_t idle, on;
    null_menu_action_t action;
    void *user;
    void (*shortcut)(int);
    uint32_t handle_tap;
    int hold_fired;
};
static float maxpos(null_menu_t *m) { return ((m->count - 1) / 2) * 2 * CELL; }
static float clamp(float p, float max) { return p < 0 ? 0 : p > max ? max : p; }
static lv_obj_t *plain(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
}
static void paint(null_menu_t *m)
{
    for (int i = 0; i < m->count; i++) {
        lv_obj_set_x(m->labels[i], (int)roundf(i * CELL - m->pos));
        lv_obj_set_style_text_color(
            m->labels[i], m->down && !m->drag && !m->handle && m->pressed == i ? m->on : m->idle, 0);
    }
    float max = maxpos(m);
    lv_obj_set_x(m->thumb, (int)roundf(max > 0 ? (RAIL_W - THUMB_W) * m->pos / max : 0));
    lv_obj_set_style_bg_color(m->thumb, m->down ? m->on : m->idle, 0);
}
static void snap(null_menu_t *m)
{
    m->coasting = 0;
    m->settling = 1;
    m->from = m->pos;
    m->target = clamp(roundf(m->pos / (2 * CELL)) * (2 * CELL), maxpos(m));
    m->settle_start = lv_tick_get();
}
static void tick(lv_timer_t *t)
{
    null_menu_t *m = lv_timer_get_user_data(t);
    uint32_t now = lv_tick_get();
    float dt = (now - m->last_tick) / 1000.f;
    if (!lv_obj_is_visible(m->root)) {
        m->last_tick = now;
        m->down = m->drag = m->coasting = 0;
        return;
    }
    if (dt > .05f)
        dt = .05f;
    m->last_tick = now;
    if (m->down && m->handle && now - m->began >= 350) {
        int dir = m->last.x <= RAIL_X + 26 ? -1 : m->last.x >= RAIL_X + RAIL_W - 26 ? 1 : 0;
        if (dir) {
            m->drag = 1;
            m->autoscroll = 1;
            m->pos = clamp(m->pos + dir * CELL * 3.f * dt, maxpos(m));
        }
    }
    if(m->down && m->handle && !m->drag && !m->hold_fired && m->shortcut &&
       m->last.x>RAIL_X+26 && m->last.x<RAIL_X+RAIL_W-26 && now-m->began>=600){
        m->hold_fired=1;m->down=0;m->handle_tap=0;m->shortcut(1);return;
    }
    if (m->coasting) {
        m->pos = clamp(m->pos + m->velocity * dt, maxpos(m));
        m->velocity *= expf(-dt * 13.f);
        if (fabsf(m->velocity) < 70 || m->pos <= 0 || m->pos >= maxpos(m))
            snap(m);
    }
    if (m->settling) {
        float t = (now - m->settle_start) / 140.f;
        if (t >= 1) {
            m->pos = m->target;
            m->settling = 0;
        } else {
            float e = 1 - powf(1 - t, 3);
            m->pos = m->from + (m->target - m->from) * e;
        }
    }
    paint(m);
}
static void pointer(lv_event_t *e)
{
    null_menu_t *m = lv_event_get_user_data(e);
    lv_indev_t *in = lv_indev_active();
    if (!in)
        return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_event_code_t c = lv_event_get_code(e);
    uint32_t now = lv_tick_get();
    if (c == LV_EVENT_PRESSED) {
        m->down = 1;m->hold_fired=0;
        m->drag = 0;
        m->autoscroll = 0;
        m->coasting = m->settling = 0;
        m->velocity = 0;
        m->origin = m->last = p;
        m->start_pos = m->last_pos = m->pos;
        m->began = m->last_sample = now;
        m->handle = p.y >= 194;
        m->pressed = m->handle ? -1 : (int)((p.x - 42 + m->pos) / CELL);
        paint(m);
    } else if (c == LV_EVENT_PRESSING && m->down) {
        int dx = p.x - m->origin.x, dy = p.y - m->origin.y;
        float dt = (now - m->last_sample) / 1000.f;
        if (abs(dx) > 7 || abs(dy) > 7)
            m->drag = 1;
        if (m->drag && !m->autoscroll) {
            float next = m->start_pos + (m->handle ? dx * maxpos(m) / (RAIL_W - THUMB_W) : -dx);
            next = clamp(next, maxpos(m));
            if (dt > .001f)
                m->velocity = .55f * m->velocity + .45f * (next - m->last_pos) / dt;
            m->pos = next;
            m->last_pos = next;
        }
        m->last = p;
        m->last_sample = now;
        paint(m);
    } else if ((c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) && m->down) {
        m->down = 0;
        if (c == LV_EVENT_PRESS_LOST) {
            m->velocity = 0;
            snap(m);
        } else if(!m->drag && m->handle && !m->hold_fired && m->shortcut){
            if(m->handle_tap && now-m->handle_tap<=300){m->handle_tap=0;m->shortcut(0);return;}
            m->handle_tap=now;
        } else if (!m->drag && !m->handle && m->pressed >= 0 && m->pressed < m->count) {
            int selected = m->pressed;
            m->pressed = -1;
            paint(m);
            if (m->action)
                m->action(selected, m->user);
            return;
        } else if (m->drag && !m->handle && fabsf(m->velocity) > 120) {
            m->coasting = 1;
            if (m->velocity > 1800)
                m->velocity = 1800;
            if (m->velocity < -1800)
                m->velocity = -1800;
        } else
            snap(m);
        m->pressed = -1;
        paint(m);
    }
}
null_menu_t *null_menu_create(lv_obj_t *p, const char *const *labels, int n, lv_color_t idle, lv_color_t on,
                              null_menu_action_t action, void *user)
{
    if (n < 1)
        return NULL;
    null_menu_t *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->labels = calloc(n, sizeof *m->labels);
    if (!m->labels) {
        free(m);
        return NULL;
    }
    m->count = n;
    m->idle = idle;
    m->on = on;
    m->action = action;
    m->user = user;
    m->pressed = -1;
    m->last_tick = lv_tick_get();
    m->root = plain(p, 0, 0, 360, 90);
    m->viewport = plain(m->root, 42, 19, VIEW, 30);
    for (int i = 0; i < n; i++) {
        lv_obj_t *l = lv_label_create(m->viewport);
        m->labels[i] = l;
        lv_label_set_text(l, labels[i]);
        lv_obj_set_size(l, 126, 25);
        lv_obj_set_style_text_font(l, TF(HEADER), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    }
    m->rail = plain(m->root, RAIL_X, 70, RAIL_W, 2);
    lv_obj_set_style_bg_color(m->rail, idle, 0);
    lv_obj_set_style_bg_opa(m->rail, 60, 0);
    /* Thumb is a sibling so its 6px height is not clipped by the 2px rail. */
    lv_obj_t *track = plain(m->root, RAIL_X, 68, RAIL_W, 6);
    m->thumb = plain(track, 0, 0, THUMB_W, 6);
    lv_obj_set_style_bg_opa(m->thumb, 255, 0);
    m->input = plain(m->root, 42, 8, VIEW, 80);
    lv_obj_add_flag(m->input, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(m->input, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(m->input, pointer, LV_EVENT_ALL, m);
    m->timer = lv_timer_create(tick, 16, m);
    paint(m);
    return m;
}
void null_menu_destroy(null_menu_t *m)
{
    if (!m)
        return;
    lv_timer_delete(m->timer);
    lv_obj_delete(m->root);
    free(m->labels);
    free(m);
}
float null_menu_position(const null_menu_t *m) { return m ? m->pos : 0; }
int null_menu_dragging(const null_menu_t *m) { return m && m->drag; }

void null_menu_set_shortcut(null_menu_t *m,void (*callback)(int)){if(m)m->shortcut=callback;}
void null_menu_set_active(null_menu_t *m, int on)
{
    if (!m)
        return;
    if (on) {
        m->last_tick = lv_tick_get();
        lv_timer_resume(m->timer);
    } else {
        m->down = m->drag = m->coasting = m->settling = 0;
        m->handle_tap=0;
        m->pressed = -1;
        lv_timer_pause(m->timer);
        paint(m);
    }
}
