/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SS_DB_H
#define SS_DB_H
/* SongSphere's own small database (its folder on the Disc), and read-only use of the library. */
enum { SS_SONG = 0, SS_ALBUM = 1, SS_ARTIST = 2 };
typedef struct { char name[128], sub[128]; int songs, plays, placed; float x, y; } ss_item_t;

int  ssdb_open(void);                       /* 1 = ready (the library is readable; our file opens or is created) */
void ssdb_close(void);
int  ssdb_items(int kind, ss_item_t **out); /* albums or artists, most played first; caller frees; count (-1 error) */
int  ssdb_get(int kind, const char *key, float *x, float *y);   /* 1 = placed */
int  ssdb_put(int kind, const char *key, float x, float y);     /* 1 = saved */
int  ssdb_del(int kind, const char *key);
int  ssdb_coverage(int *placed, int *albums);
/* the playlist for a spot: up to `want` song IDs (malloc'd), a few per album/artist, near first, recent plays
 * less likely. *radius_out = how far it had to look (0..2). Returns the count, 0 if nothing is placed near enough. */
int  ssdb_pick(float x, float y, int want, int **ids, float *radius_out);
#endif
