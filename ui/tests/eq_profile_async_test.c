/* Exercise the actual worker and SQLite reader, including exclusive lock/stale requests. */
#include "eqprofile.h"
#include "sqlite3.h"
#include "jsmn.h"
#include "art_pixels.h"
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
int sd_io_begin(void){return 1;}
void sd_io_end(void){}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static eq_profile_t await(unsigned t){
    eq_profile_t p;
    for(int i=0;i<200;i++){if(eq_profile_take(t,&p))return p;usleep(10000);}
    assert(!"profile timed out");return p;
}
int main(void){
    unlink(DB_PATH);sqlite3 *db;assert(sqlite3_open(DB_PATH,&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"CREATE TABLE PEQ(STYLE_PRESET INTEGER,MASTER_GAIN REAL,PARAMS_JSON TEXT);"
        "INSERT INTO PEQ VALUES(11,1.25,'[]'),(12,2.5,'[]');BEGIN EXCLUSIVE",0,0,0)==SQLITE_OK);
    double a=seconds();unsigned old=eq_profile_request(11),last=eq_profile_request(12);
    assert(seconds()-a<0.05);eq_profile_t p=await(last);
    assert(p.preset==12 && p.read<0);assert(!eq_profile_take(old,&p));
    assert(sqlite3_exec(db,"ROLLBACK",0,0,0)==SQLITE_OK);
    last=eq_profile_request(11);p=await(last);assert(p.preset==11 && p.read==1 && p.master==1.25 && !p.graphic);
    last=eq_profile_request(12);p=await(last);assert(p.preset==12 && p.read==1 && p.master==2.5);
    sqlite3_stmt *st;assert(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM sqlite_master WHERE type='table'",-1,&st,0)==SQLITE_OK);
    assert(sqlite3_step(st)==SQLITE_ROW && sqlite3_column_int(st,0)==1);sqlite3_finalize(st);
    FILE *f=fopen(ART_MEMINFO,"w");assert(f);fputs("MemAvailable: 2048 kB\n",f);fclose(f);assert(!art_pixels_memory_ok(1500000));
    f=fopen(ART_MEMINFO,"w");assert(f);fputs("MemAvailable: 65536 kB\n",f);fclose(f);assert(art_pixels_memory_ok(1500000));
    f=fopen(ART_MEMINFO,"w");assert(f);fputs("MemFree: 65536 kB\n",f);fclose(f);assert(!art_pixels_memory_ok(1500000));
    sqlite3_close(db);
    puts("PASS: EQ enqueue <50ms with exclusive DB lock; latest request wins; isolated read-only profiles; low-RAM preparation fallback");
}
