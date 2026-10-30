/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef MUSICDB_H
#define MUSICDB_H

#define MDB_MAX_SONGS   1500
#define MDB_MAX_GROUPS  600
#define MDB_STR         160

#define DISKOS_AUDIOBOOKS 1

#define DISKOS_RSV_LISTID -1000
#define XSTR_(x) #x
#define XSTR(x) XSTR_(x)

typedef struct {
    int  id;
    char title[MDB_STR];
    char artist[MDB_STR];
    char artist_group[MDB_STR];
    char album[MDB_STR];
    unsigned album_h;
    char genre[MDB_STR];
    int  dur_ms;
    int  track;
    int  love_id;
    int  code_artist, code_album, code_genre, code_group;
} mdb_song_t;

int mdb_load(void);
int mdb_load_failed(void);

int               mdb_song_count(void);
const mdb_song_t *mdb_song(int i);
int               mdb_song_path(int id, char *out, int cap);
int               mdb_song_id_by_path(const char *path);
int               mdb_song_album(int id, char *out, int cap);
int               mdb_song_meta_by_path(const char *path, char *album, int acap, char *artist, int arcap);
int               mdb_play_pos(int id, int list_type, const char *name);
int               mdb_current_play(mdb_song_t *out, int *pos_ms, int *is_playing);

int  mdb_albums(char names[][MDB_STR], char artists[][MDB_STR], int *counts, int cap);
typedef struct {
    char owner[MDB_STR];
    int  owner_null;
    char album[MDB_STR];
} mdb_album_key_t;
typedef struct {
    int  list_type;
    char name[2*MDB_STR + 40];
    int *ids; int count;
} mdb_plan_t;
int  mdb_album_keys(mdb_album_key_t *keys, int *counts, int *rep_ids, int cap);
int  mdb_album_key_song_ids(const mdb_album_key_t *k, int *ids, int cap);
int  mdb_album_plan(const mdb_album_key_t *k, mdb_plan_t *plan);
int  mdb_artist_plan(const char *key, mdb_plan_t *plan);
#define MDB_SCOPE_ARTIST 1
#define MDB_SCOPE_GENRE  2
int  mdb_scope_albums(int kind, const char *key, char names[][MDB_STR], char artists[][MDB_STR], int *counts, int cap);
int  mdb_scope_album_songs(int kind, const char *key, const char *album, const mdb_song_t **out, int cap);
int  mdb_scope_album_plan(int kind, const char *key, const char *album, mdb_plan_t *plan);
static inline int mdb_id_cmp(const void *a, const void *b){
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
int  mdb_plan_materialize(mdb_plan_t *plan);
int  mdb_plan_pos(const mdb_plan_t *plan, int song_id);
void mdb_plan_free(mdb_plan_t *plan);
int  mdb_subtrack_count(const char *path, int iso_only);
int  mdb_subtrack_plan(const char *const *paths, int npaths, mdb_plan_t *plan);

typedef struct {
    int  base_id;
    int  ord;
    int  dur_ms;
    char title[MDB_STR];
    char artist[MDB_STR];
    char path[256];
} mdb_qrow_t;
#define MDB_UPNEXT_EMPTY    0
#define MDB_UPNEXT_ERROR    (-1)
#define MDB_UPNEXT_NOTFOUND (-2)
#define MDB_UPNEXT_BUSY     (-3)
int mdb_upnext(int shuffle, int cur_pos_id, const char *cur_path, mdb_qrow_t *out, int cap, int *more);
int mdb_queue_row_valid(int base_id, int ord, const char *path);
int mdb_sysconfig_play_mode(void);
int  ui_play_plan(mdb_plan_t *plan, int song_id);
int  mdb_artist_class_type(void);
void mdb_set_artist_mode(int album_artist);
int  mdb_artist_mode(void);
int  mdb_artist_missing(const char *key);
int  mdb_album_rep_ids(int *out, int cap);
int  mdb_album_count(void);
int  mdb_artists(char names[][MDB_STR], int cap);

typedef struct {
    int  id;
    char title[MDB_STR];
    char author[MDB_STR];
    char path[512];
    long position_ms;
    int  completed;
} book_t;
int  mdb_total_song_count(void);
int  mdb_is_book_path(const char *path);
int  mdb_song_genre(const char *path, int track, char *out, int cap);
int  mdb_books(book_t *out, int max);
int  mdb_book_progress(const char *key, char *member_out, int member_cap, long *position_ms, int *completed_out, long *updated_out);
int  mdb_book_save(const char *key, const char *member_path, long position_ms, int completed);
int  mdb_reserved_slot_set(const char *path);
int  mdb_reserved_slot_set_playlist(long pid);
int  mdb_reserved_slot_player_pos(long pid, int row);
int  mdb_migrate_books(void);
int  mdb_artist_count(void);
int  mdb_genre_count(void);
int  mdb_genres(char names[][MDB_STR], int *counts, int cap);

int  mdb_album_songs(const char *album, const mdb_song_t **out, int cap);
int  mdb_album_track_ids(const char *album, int *ids, int cap);
int  mdb_artist_songs(const char *artist, const mdb_song_t **out, int cap);
int  mdb_genre_songs(const char *genre, const mdb_song_t **out, int cap);

int  mdb_search(const char *q, const mdb_song_t **out, int cap);

int  mdb_favorites(mdb_song_t *out, int cap, int stock_order);
int  mdb_favorite_count(void);
int  mdb_love_path(int love_id, char *out, int cap);
void mdb_record_play(const char *path);
int  mdb_stats_count(int by_recent);
int  mdb_mostplayed(mdb_song_t *out, int cap);
int  mdb_recent(mdb_song_t *out, int cap);
int  mdb_unfavorite(int love_id);
int  mdb_playlist_sync_registry(void);
int  mdb_playlist_num(void);
int  mdb_playlists(char names[][MDB_STR], long *ids, int cap);
int  mdb_playlists_counted(char names[][MDB_STR], long *ids, int *counts, int cap);
long mdb_playlist_create(const char *name);
int  mdb_playlist_add_song(long pid, const char *path);
int  mdb_playlist_add_song_ex(long pid, const char *path, int *hard_err);
int  mdb_playlist_remove_at(long pid, int position);
int  mdb_song_subtrack(const char *path, int pos_id, int *track, int *is_cue, int *is_iso);
int  mdb_song_subtrack_verified(const char *path, int pos_id, const char *title, int *track, int *is_cue, int *is_iso);   
int  mdb_playlist_add_subtrack(long pid, const char *path, int track, int *hard_err);
int  mdb_playlist_add_ids(long pid, const int *ids, int n);
int  mdb_song_multi(const char *path);
int  mdb_playlist_has_subtrack(long pid, const char *path, int track);
int  mdb_playlist_add_group(long pid, const char *col, const char *val);
int  mdb_playlist_has_song(long pid, const char *path);
int  mdb_playlist_rename(long pid, const char *name);
int  mdb_playlist_delete(long pid);
long mdb_book_scope_set(const char *path);
int  mdb_playlist_export(long pid, const char *name, char *outname, int cap);
int  mdb_playlist_songs(long pid, mdb_song_t *out, int cap);
int  mdb_playlist_count(long pid);
int  mdb_import_m3u_dir(const char *dir);
int  mdb_import_m3u_sd(const char *root);

int  mdb_split_artists(const char *raw, char toks[][MDB_STR], int cap);

int  mdb_song_accent(const char *path);
void mdb_set_song_accent(const char *path, int rgb, const char *cover_key);
int  mdb_song_accent_fresh(const char *path, const char *cover_key);
int  mdb_prewarm_next(int after_id, int *id, char *path, int cap);

int  mdb_set_peq(int style_preset, double master_gain, const char *params_json);
int  mdb_get_peq(int style_preset, int *master_out, int *gains_out);
int  mdb_peq_is_graphic(int style_preset);

int  mdb_sysconfig_folder_jump(void);
int  mdb_sysconfig_language(void);          
int  mdb_sysconfig_auto_time(void);
int  mdb_sysconfig_set_auto_time(int on);
#endif
