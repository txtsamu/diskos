/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef OPUSTITLE_WORKER_H
#define OPUSTITLE_WORKER_H
#include <stdint.h>
#include "opustitle.h"
typedef struct opus_title_worker opus_title_worker;
typedef struct {
    uint64_t player_generation, song_id, pos_id;
    unsigned song_id_valid, pos_id_valid;
} otw_identity;
enum { OTW_KIND_KNOWN=1, OTW_CUE=2, OTW_SACD=4 };
typedef struct {
    otw_identity identity;
    uint64_t identity_epoch, serial, media_epoch;
    unsigned flags;
    char path[1024];
} otw_key;
enum { OTW_FAILURE_NONE, OTW_FAILURE_TIMEOUT, OTW_FAILURE_WORKER };
typedef struct {
    otw_key key;
    opus_title_result result;
    unsigned failure;
} otw_commit;
opus_title_worker *otw_create(uint64_t (*clock)(void *), void *ctx);
void otw_destroy(opus_title_worker *w);
int otw_submit(opus_title_worker *w, otw_identity identity, const char *path,
               const char *player_title, unsigned flags);
void otw_identity_end(opus_title_worker *w);
void otw_player_generation(opus_title_worker *w, uint64_t generation);
void otw_library_commit(opus_title_worker *w, uint64_t media_epoch);
int otw_poll(opus_title_worker *w, uint64_t now, otw_commit *out);
void otw_reject_commit(opus_title_worker *w);
int otw_title_ready(const opus_title_worker *w);
#endif
