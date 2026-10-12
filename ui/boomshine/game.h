/* SPDX-License-Identifier: MIT */
#ifndef BOOMSHINE_GAME_H
#define BOOMSHINE_GAME_H
#include <stdint.h>
#define MAX_DOTS 100
#define LEVELS 20
#define ARENA_X 180.0f
#define ARENA_Y 180.0f
#define ARENA_R 175.0f
#define DOT_R 4.0f
#define BURST_LIFE 1.8f
typedef struct { float x,y,vx,vy,age; int state,color; } Dot;
typedef struct {
    Dot dots[MAX_DOTS];
    int level,count,target,caught,fired,finished,won;
    float seed_x,seed_y,seed_age,burst_scale;
    uint32_t rng;
} Game;
void game_init(Game *g,int level,uint32_t seed);
int game_fire(Game *g,float x,float y);
float game_radius(float age);
float game_blast_radius(const Game *g,float age);
void game_step(Game *g,float dt);
#endif
