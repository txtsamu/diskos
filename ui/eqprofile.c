/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "eqprofile.h"
#include "musicdb.h"
#include <pthread.h>
#include <string.h>
static pthread_mutex_t mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static unsigned wanted, completed;
static int target, pending, started;
static eq_profile_t result;
static void *worker(void *unused){
    (void)unused;
    for(;;){
        pthread_mutex_lock(&mu);
        while(!pending)pthread_cond_wait(&cv,&mu);
        unsigned ticket=wanted;int preset=target;pending=0;
        pthread_mutex_unlock(&mu);
        eq_profile_t p;memset(&p,0,sizeof p);p.preset=preset;
        p.read=mdb_get_peq_readonly(preset,&p.master,p.gains,p.hz,&p.graphic,p.q);
        pthread_mutex_lock(&mu);
        if(ticket==wanted){result=p;completed=ticket;}
        pthread_mutex_unlock(&mu);
    }
    return NULL;
}
unsigned eq_profile_request(int preset){
    pthread_mutex_lock(&mu);
    if(++wanted==0)wanted=1;
    target=preset;pending=1;
    if(!started){
        pthread_attr_t attr;pthread_t t;
        pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,256*1024);
        int rc=pthread_create(&t,&attr,worker,NULL);pthread_attr_destroy(&attr);
        if(rc){pending=0;pthread_mutex_unlock(&mu);return 0;}
        pthread_detach(t);started=1;
    }
    unsigned ticket=wanted;pthread_cond_signal(&cv);pthread_mutex_unlock(&mu);return ticket;
}
int eq_profile_take(unsigned ticket,eq_profile_t *out){
    pthread_mutex_lock(&mu);
    int ready=ticket && ticket==completed && ticket==wanted;
    if(ready)*out=result;
    pthread_mutex_unlock(&mu);return ready;
}
