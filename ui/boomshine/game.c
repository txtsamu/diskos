/* SPDX-License-Identifier: MIT */
#include "game.h"
#include <math.h>
#include <string.h>
static const int counts[LEVELS]={5,10,15,20,25,30,35,40,45,50,55,60,65,70,75,80,85,90,95,100};
static const int targets[LEVELS]={1,2,3,5,7,10,15,21,27,33,44,55,60,65,70,75,80,85,91,96};
static float random01(Game *g){
    uint32_t x=g->rng; x^=x<<13; x^=x>>17; x^=x<<5; g->rng=x;
    return (x>>8)*(1.0f/16777216.0f);
}
void game_init(Game *g,int level,uint32_t seed){
    memset(g,0,sizeof *g);
    if(level<0) level=0;
    if(level>=LEVELS) level=LEVELS-1;
    g->level=level; g->count=counts[level]; g->target=targets[level];
    /* Preserve levels 1-12; later rounds compensate for the denser field. */
    g->burst_scale=level<12?1.0f:sqrtf(60.0f/g->count)*(1.0f-0.00625f*(level-11));
    g->rng=seed?seed:0x51f15e31u;
    for(int i=0;i<g->count;i++){
        Dot *d=&g->dots[i];
        float a=random01(g)*6.2831853f,r=sqrtf(random01(g))*(ARENA_R-DOT_R-1);
        d->x=ARENA_X+cosf(a)*r; d->y=ARENA_Y+sinf(a)*r;
        a=random01(g)*6.2831853f; float speed=37+random01(g)*24;
        d->vx=cosf(a)*speed; d->vy=sinf(a)*speed; d->color=i%6;
    }
}
float game_radius(float age){
    if(age<0 || age>=BURST_LIFE) return 0;
    if(age<0.35f) return 43.0f*(age/0.35f);
    if(age<1.20f) return 43.0f;
    return 43.0f*((BURST_LIFE-age)/0.60f);
}
float game_blast_radius(const Game *g,float age){return game_radius(age)*g->burst_scale;}
int game_fire(Game *g,float x,float y){
    float dx=x-ARENA_X,dy=y-ARENA_Y;
    if(g->fired || g->finished || dx*dx+dy*dy>ARENA_R*ARENA_R) return 0;
    g->fired=1; g->seed_x=x;g->seed_y=y;g->seed_age=0;
    return 1;
}
void game_step(Game *g,float dt){
    if(g->finished || dt<=0) return;
    if(dt>0.05f) dt=0.05f;
    if(g->fired) g->seed_age+=dt;
    for(int i=0;i<g->count;i++){
        Dot *d=&g->dots[i];
        if(d->state==1){ d->age+=dt; if(d->age>=BURST_LIFE) d->state=2; }
        else if(d->state==0){
            d->x+=d->vx*dt; d->y+=d->vy*dt;
            float dx=d->x-ARENA_X,dy=d->y-ARENA_Y,dist2=dx*dx+dy*dy,lim=ARENA_R-DOT_R;
            if(dist2>lim*lim){
                float dist=sqrtf(dist2);
                float nx=dx/dist,ny=dy/dist,p=d->vx*nx+d->vy*ny;
                if(p>0){d->vx-=2*p*nx;d->vy-=2*p*ny;}
                d->x=ARENA_X+nx*(lim-0.01f);d->y=ARENA_Y+ny*(lim-0.01f);
            }
        }
    }
    if(!g->fired) return;
    /* Compute each active radius once per tick, not once per candidate pair.
     * Newly caught zero-radius dots enter the list on the following tick. */
    struct {float x,y,r2;} blasts[MAX_DOTS+1];int n=0;
    float radius=game_blast_radius(g,g->seed_age);
    int active=g->seed_age<BURST_LIFE;
    if(radius>0){blasts[n].x=g->seed_x;blasts[n].y=g->seed_y;blasts[n++].r2=(radius+DOT_R)*(radius+DOT_R);}
    for(int j=0;j<g->count;j++)if(g->dots[j].state==1){
        Dot *b=&g->dots[j];active=1;radius=game_blast_radius(g,b->age);
        if(radius>0){blasts[n].x=b->x;blasts[n].y=b->y;blasts[n++].r2=(radius+DOT_R)*(radius+DOT_R);}
    }
    for(int i=0;i<g->count;i++){
        Dot *d=&g->dots[i];if(d->state!=0)continue;
        for(int j=0;j<n;j++){
            float dx=d->x-blasts[j].x,dy=d->y-blasts[j].y;
            if(dx*dx+dy*dy<=blasts[j].r2){d->state=1;d->age=0;g->caught++;active=1;break;}
        }
    }
    if(!active){g->finished=1;g->won=g->caught>=g->target;}
}
