#include "null_transport_icons.h"
static const uint16_t masks[4][11] = {{128, 192, 224, 240, 248, 252, 248, 240, 224, 192, 128},
                                      {476, 476, 476, 476, 476, 476, 476, 476, 476, 476, 476},
                                      {1540, 1548, 1564, 1596, 1660, 1788, 1660, 1596, 1564, 1548, 1540},
                                      {259, 387, 451, 483, 499, 507, 499, 483, 451, 387, 259}};
lv_obj_t *null_transport_icon_create(lv_obj_t *p, int x, int y, null_transport_icon_t icon, lv_color_t colour)
{
    if ((unsigned)icon >= 4)
        return NULL;
    lv_obj_t *g = lv_obj_create(p);
    lv_obj_remove_style_all(g);
    lv_obj_set_pos(g, x, y);
    lv_obj_set_size(g, 22, 22);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for (int yy = 0; yy < 11; yy++) {
        int xx = 0;
        while (xx < 11) {
            if (!(masks[icon][yy] & (1 << (10 - xx)))) {
                xx++;
                continue;
            }
            int start = xx;
            while (xx < 11 && (masks[icon][yy] & (1 << (10 - xx))))
                xx++;
            lv_obj_t *r = lv_obj_create(g);
            lv_obj_remove_style_all(r);
            lv_obj_set_pos(r, start * 2, yy * 2);
            lv_obj_set_size(r, (xx - start) * 2, 2);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(r, colour, 0);
            lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
    }
    return g;
}
void null_transport_icon_set_colour(lv_obj_t *g, lv_color_t colour)
{
    if (!g)
        return;
    for (uint32_t i = 0; i < lv_obj_get_child_count(g); i++)
        lv_obj_set_style_bg_color(lv_obj_get_child(g, i), colour, 0);
}
