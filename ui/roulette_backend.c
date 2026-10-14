/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Album Roulette, built in (2.2.0): the album sampler and cover preparation, from the Album Roulette 1.3.1 app,
 * unchanged in behaviour. A background thread reservoir-samples up to POOL albums (4+ tracks) from the whole library
 * and prepares a round 268 px cover for each: from its SD cache (read-only file mappings, reclaimable), else decoded
 * by diskos-artdec in its own process (only with 16 MiB free, stopped below 8 MiB), else a shared placeholder.
 * Playing the winner is mq_ui's own album plan (roulette_spin.c), so the old player hand-over is gone. */
#include "roulette_backend.h"
#include "sqlite3.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#ifndef DB_PATH
#define DB_PATH "/usr/data/fiio/db/song.db"
#endif
extern char **environ;
album_t albums[POOL];
int album_count;
atomic_int prepared, progress, cancelled;
char load_error[160];
static uint32_t rng;
/* One shared fallback, never 37 separately allocated placeholders. */
static uint32_t fallback_pixels[DISC*DISC];
static pthread_once_t fallback_once=PTHREAD_ONCE_INIT;
static unsigned heap_covers;
#define HEAP_COVER_LIMIT 3
static void init_fallback(void){ placeholder(fallback_pixels,0); }
static void use_fallback(album_t *a){
    release_artwork(a);
    pthread_once(&fallback_once,init_fallback);
    a->pixels=fallback_pixels;
}
static const char *sidecars[]={"cover.jpg","folder.jpg","cover.jpeg","folder.jpeg","cover.png","folder.png","cover.bmp","folder.bmp","Cover.jpg","Folder.jpg","Cover.png","Folder.png"};
#ifndef CACHE_DIR
#define CACHE_DIR "/tmp/sdcard/.diskos/album-roulette"
#endif
#ifndef SD_ROOT
#define SD_ROOT "/tmp/sdcard"
#endif
#ifndef ART_HELPER
#define ART_HELPER "/opt/diskos/bin/diskos-artdec"
#endif
#define CACHE_LIMIT 128
/* Cache only successfully decoded covers, bounded to ~35 MiB; failure is harmless. */
static int cache_available;
static void cache_init(void){
    struct statvfs fs;struct statfs filesystem;
    cache_available=0;
    /* An unmounted SD directory must not turn the persistent cache into a RAM cache. */
    if(statfs(SD_ROOT,&filesystem) || filesystem.f_type==0x01021994 || (unsigned long)filesystem.f_type==0x858458f6u)return;
    if(statvfs(SD_ROOT,&fs) || (uint64_t)fs.f_bavail*fs.f_frsize < 64u*1024u*1024u) return;
    if(mkdir(SD_ROOT "/.diskos",0755) && errno!=EEXIST)return;
    if(mkdir(CACHE_DIR,0755) && errno!=EEXIST)return;
    cache_available=1;
}
static uint64_t hash_bytes(uint64_t h,const void *bytes,size_t n){
    const unsigned char *p=bytes;while(n--){h^=*p++;h*=1099511628211ULL;}return h;
}
static uint64_t hash_file(uint64_t h,const char *path){
    struct stat s;h=hash_bytes(h,path,strlen(path)+1);
    if(stat(path,&s))return h;
    int64_t v[]={s.st_size,s.st_mtime,s.st_mtim.tv_nsec,s.st_ctime,s.st_ctim.tv_nsec};return hash_bytes(h,v,sizeof v);
}
static int cache_key(const album_t *a,char *out,size_t cap){
    struct stat s;if(!cache_available || stat(a->path,&s))return 0;
    uint64_t h=hash_file(1469598103934665603ULL,a->path);
    static const char recipe[]="roulette-1.2.1-268-aa-bilinear";
    h=hash_bytes(h,recipe,sizeof recipe);
    char side[1200];snprintf(side,sizeof side,"%s",a->path);char *slash=strrchr(side,'/');
    if(slash)for(unsigned i=0;i<sizeof sidecars/sizeof *sidecars;i++){
        snprintf(slash+1,sizeof side-(size_t)(slash+1-side),"%s",sidecars[i]);h=hash_file(h,side);
    }
    snprintf(out,cap,CACHE_DIR "/%016llx.argb",(unsigned long long)h);return 1;
}
void release_artwork(album_t *a){
    if(a->pixels && a->pixels!=fallback_pixels){
        if(a->pixels_mapped)munmap(a->pixels,DISC*DISC*4);else {free(a->pixels);if(heap_covers)heap_covers--;}
    }
    a->pixels=NULL;a->pixels_mapped=0;
}
/* Clean, file-backed pages can be reclaimed instead of pinning all 37 covers in heap. */
static int cache_map(const char *path,album_t *a){
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size!=DISC*DISC*4){close(fd);return 0;}
    void *pixels=mmap(NULL,DISC*DISC*4,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
    if(pixels==MAP_FAILED)return 0;
    release_artwork(a);a->pixels=pixels;a->pixels_mapped=1;utime(path,NULL);
    return 1;
}
static void cache_write(const char *path,const uint32_t *pixels){
    char temp[300];snprintf(temp,sizeof temp,"%s.XXXXXX",path);int fd=mkstemp(temp);if(fd<0)return;
    FILE *f=fdopen(fd,"wb");if(!f){close(fd);unlink(temp);return;}
    int ok=fwrite(pixels,4,DISC*DISC,f)==DISC*DISC;if(fclose(f))ok=0;
    if(ok)rename(temp,path);
    unlink(temp);
}
static void cache_trim(void){
    if(!cache_available)return;
    /* Select one oldest file per pass. A launch adds at most 37 files. */
    for(int pass=0;pass<=POOL;pass++){
        DIR *d=opendir(CACHE_DIR);if(!d)return;struct dirent *e;int n=0;time_t oldest=0;char victim[300]="";
        while((e=readdir(d))){
            if(strlen(e->d_name)!=21 || strcmp(e->d_name+16,".argb"))continue;
            char p[300];snprintf(p,sizeof p,CACHE_DIR "/%.21s",e->d_name);struct stat s;
            if(stat(p,&s) || !S_ISREG(s.st_mode))continue;
            n++;
            if(!victim[0] || s.st_mtime<oldest){oldest=s.st_mtime;snprintf(victim,sizeof victim,"%s",p);}
        }
        closedir(d);if(n<=CACHE_LIMIT || !victim[0])return;if(unlink(victim))return;
    }
}
static unsigned char mask[DISC*DISC];
static pthread_once_t mask_once=PTHREAD_ONCE_INIT;
static void init_mask(void){
    for(int y=0;y<DISC;y++)for(int x=0;x<DISC;x++){
        int count=0;for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++){
            int dx=x*8+sx*2+1-DISC*4,dy=y*8+sy*2+1-DISC*4;
            count+=dx*dx+dy*dy<=DISC*DISC*16;
        }
        mask[y*DISC+x]=(unsigned char)((count*255+8)/16);
    }
}
uint32_t random_below(uint32_t n){
    if(!n) return 0;
    if(!rng){ int fd=open("/dev/urandom",O_RDONLY); if(fd>=0){ if(read(fd,&rng,4)!=4) rng=0; close(fd); } if(!rng) rng=(uint32_t)time(NULL)^(uint32_t)getpid()^0x934873u; }
    uint32_t r, threshold = (uint32_t)(-n)%n;
    do { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; r=rng; } while(r<threshold);
    return r%n;
}
void placeholder(uint32_t *p,int seed){
    pthread_once(&mask_once,init_mask);
    for(int y=0;y<DISC;y++) for(int x=0;x<DISC;x++){
        int dx=2*x-DISC+1,dy=2*y-DISC+1,d=dx*dx+dy*dy;
        unsigned c=36+(unsigned)((x+y+seed*7)%28);
        if(d<110*110 && d>100*100) c=110;
        if(d<26*26) c=220;
        if(d<10*10) c=20;
        p[y*DISC+x]=mask[y*DISC+x] ? ((uint32_t)mask[y*DISC+x]<<24)|c*0x010101u:0;
    }
}
/* Per-call private output; decoder owns no shared artwork paths. No shell interpolation. */
static int decode(const char *input,const char *output){
    /* Start with practical headroom; watch pressure while the bounded helper runs. */
    if(!memory_headroom(16u*1024u)){
        runtime_log("Cover decode skipped: less than 16 MiB available RAM.");return 0;
    }
    const char *helper=ART_HELPER;
    char *av[]={(char*)helper,"saver",(char*)input,(char*)output,NULL};
    pid_t child; if(posix_spawn(&child,helper,NULL,NULL,av,environ)) return 0;
    int status=0,done=0,waitable=1;
    for(int i=0;i<160 && !atomic_load(&cancelled);i++){
        pid_t r=waitpid(child,&status,WNOHANG);
        if(r==child){done=1;break;} if(r<0 && errno!=EINTR){waitable=0;runtime_log("Decoder wait failed; its PID will not be signalled.");break;}
        if(!memory_headroom(8u*1024u)){
            runtime_log("Decoder stopped: less than 8 MiB available RAM.");break;
        }
        usleep(25000);
    }
    if(!done && waitable){kill(child,SIGKILL);while(waitpid(child,&status,0)<0 && errno==EINTR){} }
    return done && WIFEXITED(status) && WEXITSTATUS(status)==0;
}
static void artwork(album_t *a,const char *dir,int index){
    char cached[256];int cacheable=cache_key(a,cached,sizeof cached);
    if(cacheable && cache_map(cached,a))return;
    use_fallback(a);
    /* Decode before allocating a destination, keeping decoder headroom intact. */
    if(!memory_headroom(16u*1024u)){
        runtime_log("Using shared placeholder: low decoder headroom.");return;
    }
    if(!cacheable && heap_covers>=HEAP_COVER_LIMIT)return;
    char out[256];snprintf(out,sizeof out,"%s/%d.raw",dir,index);
    int ok=decode(a->path,out);
    char side[1200]; snprintf(side,sizeof side,"%s",a->path); char *slash=strrchr(side,'/');
    if(slash) for(unsigned j=0;!ok && j<sizeof sidecars/sizeof *sidecars && !atomic_load(&cancelled);j++){
        snprintf(slash+1,sizeof side-(size_t)(slash+1-side),"%s",sidecars[j]);
        if(access(side,R_OK)) continue;
        unlink(out);ok=decode(side,out);
    }
    if(ok){
        FILE *f=fopen(out,"rb"); uint32_t *raw=malloc(360*360*4);
        if(f && raw && fread(raw,4,360*360,f)==360*360 && fgetc(f)==EOF){
            uint32_t *destination=calloc(DISC*DISC,4);
            if(!destination){fclose(f);free(raw);unlink(out);return;}
            a->pixels=destination;heap_covers++;
            for(int y=0;y<DISC;y++)for(int x=0;x<DISC;x++){
                if(!mask[y*DISC+x])continue;
                int u=x*359*256/(DISC-1),v=y*359*256/(DISC-1),ix=u>>8,iy=v>>8;
                int nx=ix<359?ix+1:ix,ny=iy<359?iy+1:iy,fx=u&255,fy=v&255;
                uint32_t result=(uint32_t)mask[y*DISC+x]<<24;
                for(int shift=0;shift<24;shift+=8){
                    unsigned a0=(raw[iy*360+ix]>>shift)&255,b0=(raw[iy*360+nx]>>shift)&255;
                    unsigned a1=(raw[ny*360+ix]>>shift)&255,b1=(raw[ny*360+nx]>>shift)&255;
                    unsigned top=a0*(256-fx)+b0*fx,bottom=a1*(256-fx)+b1*fx;
                    result|=((top*(256-fy)+bottom*fy+32768)>>16)<<shift;
                }
                a->pixels[y*DISC+x]=result;
            }
            if(cacheable){cache_write(cached,a->pixels);cache_map(cached,a);}
            /* Cache write/map failure must not accumulate unbounded heap images. */
            if(!a->pixels_mapped && heap_covers>HEAP_COVER_LIMIT)use_fallback(a);
        }
        if(f) fclose(f);
        free(raw);
    }
    unlink(out);
}
static int scan_cancelled(void *unused){(void)unused;return atomic_load(&cancelled);}
void *prepare_albums(void *unused){
    (void)unused;sqlite3 *db=NULL;sqlite3_stmt *st=NULL;int seen=0,rc=SQLITE_ERROR;
    if(sqlite3_open_v2(DB_PATH,&db,SQLITE_OPEN_READONLY,NULL)!=SQLITE_OK) goto fail;
    sqlite3_busy_timeout(db,2000);
    sqlite3_exec(db,"PRAGMA cache_size=-512; PRAGMA mmap_size=0",NULL,NULL,NULL);
    sqlite3_progress_handler(db,1000,scan_cancelled,NULL);
    /* Reservoir sample across ALL album identities: no first-600-albums bias. */
    const char *sql="SELECT COALESCE(ALBUM_ARTIST,ARTIST),ALBUM,MIN(PATH) FROM SONG WHERE ALBUM IS NOT NULL AND ALBUM<>'' AND lower(PATH) NOT LIKE '%.m4b' GROUP BY COALESCE(ALBUM_ARTIST,ARTIST),ALBUM HAVING COUNT(*)>=4";
    if(sqlite3_prepare_v2(db,sql,-1,&st,NULL)!=SQLITE_OK) goto fail;
    while((rc=sqlite3_step(st))==SQLITE_ROW && !atomic_load(&cancelled)){
        const char *owner=(const char*)sqlite3_column_text(st,0),*album=(const char*)sqlite3_column_text(st,1),*path=(const char*)sqlite3_column_text(st,2);
        if(!album || !path || strlen(album)>=MDB_STR || (owner && strlen(owner)>=MDB_STR) || strlen(path)>=sizeof albums[0].path) continue;
        int slot=seen<POOL?seen:(int)random_below((uint32_t)seen+1);seen++;
        if(slot>=POOL)continue;
        album_t *a=&albums[slot];a->key.owner_null=!owner;
        snprintf(a->key.owner,sizeof a->key.owner,"%s",owner?owner:"");
        snprintf(a->key.album,sizeof a->key.album,"%s",album);
        snprintf(a->path,sizeof a->path,"%s",path);
    }
    if(rc!=SQLITE_DONE && !atomic_load(&cancelled)) goto fail;
    sqlite3_finalize(st);sqlite3_close(db);st=NULL;db=NULL;
    if(atomic_load(&cancelled)) return NULL;
    album_count=seen<POOL?seen:POOL;
    if(!album_count){snprintf(load_error,sizeof load_error,"No albums with 4+ tracks.\nCheck your library in Disco.");atomic_store(&prepared,-1);return NULL;}
    for(int i=album_count-1;i>0;i--){int j=(int)random_below(i+1);album_t t=albums[i];albums[i]=albums[j];albums[j]=t;}
    char dir[]="/tmp/album-roulette-XXXXXX";
    if(!mkdtemp(dir)) goto fail;
    cache_init();cache_trim();
    for(int i=0;i<album_count && !atomic_load(&cancelled);i++){
        artwork(&albums[i],dir,i);atomic_store(&progress,i+1);
        if(!albums[i].pixels){snprintf(load_error,sizeof load_error,"Low available memory.\nExit and try again.");rmdir(dir);atomic_store(&prepared,-1);return NULL;}
    }
    rmdir(dir);cache_trim();if(!atomic_load(&cancelled))atomic_store(&prepared,1);return NULL;
fail:
    if(st)sqlite3_finalize(st);
    if(db)sqlite3_close(db);
    snprintf(load_error,sizeof load_error,"Library unavailable.\nExit and finish scanning first.");atomic_store(&prepared,-1);return NULL;
}

/* ---- from the app's runtime.c: the memory check and a small log ---- */
void runtime_log(const char *message){
    fprintf(stderr,"Album Roulette: %s\n",message);
    const char *path="/usr/data/album-roulette.log";
    struct stat st;const char *mode=(!stat(path,&st) && st.st_size>65536)?"w":"a";
    FILE *f=fopen(path,mode);if(!f)return;
    fprintf(f,"%ld %s\n",(long)time(NULL),message);fclose(f);
}
int memory_headroom(uint64_t required_kib){
    FILE *f=fopen("/proc/meminfo","r");if(!f)return 0;
    char line[256];unsigned long long value,available=0,free_kib=0,cached=0,buffers=0,reclaimable=0,shmem=0;int have_available=0,have_free=0;
    while(fgets(line,sizeof line,f)){
        if(sscanf(line,"MemAvailable: %llu kB",&value)==1){available=value;have_available=1;}
        if(sscanf(line,"MemFree: %llu kB",&value)==1){free_kib=value;have_free=1;}
        if(sscanf(line,"Cached: %llu kB",&value)==1)cached=value;
        if(sscanf(line,"Buffers: %llu kB",&value)==1)buffers=value;
        if(sscanf(line,"SReclaimable: %llu kB",&value)==1)reclaimable=value;
        if(sscanf(line,"Shmem: %llu kB",&value)==1)shmem=value;
    }
    fclose(f);
    /* Older kernels: count only half of potentially reclaimable cache, excluding tmpfs. */
    uint64_t cache=cached+buffers+reclaimable;
    uint64_t estimate=free_kib+(cache>shmem?(cache-shmem)/2:0);
    return have_available?available>=required_kib:have_free && estimate>=required_kib;
}
/* Before a new sample: drop the last one's covers (only when its loader has finished) */
void roulette_reset(void){
    for(int i=0;i<POOL;i++)release_artwork(&albums[i]);
    memset(albums,0,sizeof albums);album_count=0;load_error[0]=0;
    atomic_store(&prepared,0);atomic_store(&progress,0);atomic_store(&cancelled,0);
}
