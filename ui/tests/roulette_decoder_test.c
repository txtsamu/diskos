/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the real built-in decoder's failure cleanup, cancellation and shared lock. */
#define ARTDEC_INPROC 1
#include "../tools/diskos_artdec.c"
#include <assert.h>
static const char *small,*large,*directory;
static void output_path(char *out,size_t cap,const char *name){snprintf(out,cap,"%s/%s",directory,name);unlink(out);}
static void *worker(void *arg){
    int roulette=(int)(intptr_t)arg;char out[1024];output_path(out,sizeof out,roulette?"roulette.raw":"saver.raw");
    for(int n=0;n<12;n++){
        unlink(out);
        int rc=roulette?artdec_roulette(small,out,4u<<20,NULL):artdec_inproc("saver",small,out);assert(rc==0);
        struct stat st;assert(!stat(out,&st)&&st.st_size==(roulette?224*224*4:360*360*4));
        FILE *f=fopen(out,"rb");assert(f);unsigned char pixel[4];assert(fread(pixel,1,4,f)==4);fclose(f);
        assert(pixel[0]==12&&pixel[1]==57&&pixel[2]==209&&pixel[3]==255);
    }
    return NULL;
}
int main(int argc,char **argv){
    assert(argc==4);small=argv[1];large=argv[2];directory=argv[3];char out[1024];output_path(out,sizeof out,"fail.raw");
    assert(artdec_roulette(large,out,4u<<20,NULL)==ST_LIMIT);
    assert(!g_live&&!g_blocks&&g_fd==-1&&access(out,F_OK));assert(g_peak<=(4u<<20));
    atomic_int cancel=1;assert(artdec_roulette(small,out,4u<<20,&cancel)==ST_LIMIT);assert(access(out,F_OK));
    atomic_store(&cancel,0);assert(artdec_roulette(small,out,4u<<20,&cancel)==0);assert(!g_live&&!g_blocks&&g_fd==-1);
    pthread_t a,b;assert(!pthread_create(&a,NULL,worker,(void*)1));assert(!pthread_create(&b,NULL,worker,NULL));
    pthread_join(a,NULL);pthread_join(b,NULL);assert(!g_live&&!g_blocks&&g_fd==-1);
    puts("PASS: capped decode rejects over-budget input, frees allocations on failure, honours cancellation and serialises concurrent artwork clients");
}
