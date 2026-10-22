/* Round-glass geometry shared by the list screens.
 * The panel is 360x360 but visible only inside the circle (x-180)^2+(y-180)^2 <= 180^2.
 * Chord width at row y = 2*sqrt(180^2-(y-180)^2): 226 px at y40, 268 at y60/y300,
 * 288 at y72, 275 at y296, 221 at y322. A 268 px row (x46..314) is fully visible only for y 60..300. */
#ifndef UI_ROUND_H
#define UI_ROUND_H

#define UI_SCR_R        180
#define UI_SCR_SIZE     360

/* Shared scrolling-list viewport. Top just under the header; the bottom runs to y340 so the
 * bezel clips the list naturally (no flat cut across the glass at y322). */
#define UI_LIST_X       30
#define UI_LIST_Y       72
#define UI_LIST_W       300
#define UI_LIST_BOTTOM  340
#define UI_LIST_H       (UI_LIST_BOTTOM - UI_LIST_Y)     /* 268 */
/* The last row must scroll up to y<=296 (chord 275 px >= 268 row): pad = 340-296. */
#define UI_LIST_PAD_BOTTOM 44
/* height of a viewport that starts at y0 and ends at UI_LIST_BOTTOM */
#define UI_LIST_H_FROM(y0) (UI_LIST_BOTTOM - (y0))

/* Chord width (px) of the visible circle at screen row y; 0 outside the glass. */
static inline int ui_chord_w(int y){
    int dy = y - UI_SCR_R;
    int v = UI_SCR_R*UI_SCR_R - dy*dy;
    if(v <= 0) return 0;
    int lo = 0, hi = UI_SCR_R;
    while(lo < hi){ int m = (lo + hi + 1) / 2; if(m*m <= v) lo = m; else hi = m - 1; }
    return 2 * lo;
}

/* Widest centred width that stays inside the circle for a rect spanning rows y0..y1
 * (the narrowest chord is at whichever end is farther from the centre). */
static inline int ui_fit_w(int y0, int y1){
    int a = ui_chord_w(y0), b = ui_chord_w(y1);
    return a < b ? a : b;
}

/* Clamp a centred rect (y, w) so its corners lie inside the circle: returns the max w. */
static inline int ui_fit_rect_w(int y, int h, int w){
    int m = ui_fit_w(y, y + h);
    return w < m ? w : m;
}

#endif
