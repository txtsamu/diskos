#ifndef UI_ROUND_H
#define UI_ROUND_H

#define UI_SCR_R        180
#define UI_SCR_SIZE     360

#define UI_LIST_X       30
#define UI_LIST_Y       72
#define UI_LIST_W       300
#define UI_LIST_BOTTOM  340
#define UI_LIST_H       (UI_LIST_BOTTOM - UI_LIST_Y)
#define UI_LIST_PAD_BOTTOM 44
#define UI_LIST_H_FROM(y0) (UI_LIST_BOTTOM - (y0))

static inline int ui_chord_w(int y){
    int dy = y - UI_SCR_R;
    int v = UI_SCR_R*UI_SCR_R - dy*dy;
    if(v <= 0) return 0;
    int lo = 0, hi = UI_SCR_R;
    while(lo < hi){ int m = (lo + hi + 1) / 2; if(m*m <= v) lo = m; else hi = m - 1; }
    return 2 * lo;
}

static inline int ui_fit_w(int y0, int y1){
    int a = ui_chord_w(y0), b = ui_chord_w(y1);
    return a < b ? a : b;
}

static inline int ui_fit_rect_w(int y, int h, int w){
    int m = ui_fit_w(y, y + h);
    return w < m ? w : m;
}

#endif
