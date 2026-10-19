#!/usr/bin/env python3
"""Compile the production Bluetooth resume guard, bulk Favourites action and favourite DB functions; fake only the
device/UI boundaries. Contributed with the 2.5.6 review and adapted to the shipped behaviour: after a codec switch the
guard only shows a hint; after a re-route it plays once, then hints; it never records a codec failure.
"""
from pathlib import Path
import subprocess
import tempfile
import sys
ui = Path(__file__).resolve().parents[1]
baseline = False

def fn(src, signature):
    start = src.index(signature)
    opening = src.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[start:end]

main = (ui/'main.c').read_text()
guard = main[main.index('static uint32_t g_rg_until'):main.index('static char g_play_initpath')]
g = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ipc.h"
static uint32_t now=100;static unsigned gen=1;
static int g_playing=1,g_play_pending,sends,failed,notices;
static char route[20]="AA:BB:CC:DD:EE:FF";
static track_state_t current={.have_track=1,.path="/music/a.flac",.pos_id=1,.path_seq=1,.position_ms=10000,.duration_ms=100000};
static uint32_t lv_tick_get(void){return now;}
static uint32_t lv_tick_elaps(uint32_t t){return now-t;}
unsigned ipc_generation(void){return gen;}
static const char *ui_route_mac(void){return route;}
void ipc_get_state(track_state_t *s){*s=current;}
int ipc_send_cmd(const char *s){(void)s;sends++;return 0;}
void bt_codec_note_failed(const char *s){(void)s;failed++;}
static int ui_bt_codec_frame(const char *m,char *o,int cap){(void)m;snprintf(o,cap,"SBC");return 0;}
static void ui_toast(const char *s){(void)s;notices++;}
''' + guard + r'''
static void stall(void){now+=100;resume_guard_tick(0);now+=3600;resume_guard_tick(0);}
int main(void){
 ui_resume_guard_arm(2,route);stall();stall();
 assert(sends==0 && failed==0 && notices==1);          /* codec switch: a hint only, once */
 notices=0;now=10000;ui_resume_guard_arm(1,route);stall();assert(sends==1 && !notices);   /* route: play once */
 stall();assert(sends==1 && notices==1 && failed==0);  /* still stopped: a hint, never a second press */
 sends=0;
 notices=0;now=20000;ui_resume_guard_arm(1,route);route[0]=0;stall();assert(!notices && !sends);
 strcpy(route,"AA:BB:CC:DD:EE:FF");now=40000;ui_resume_guard_arm(2,route);gen++;stall();assert(!notices);
 now=60000;ui_resume_guard_arm(2,route);current.pos_id++;stall();assert(!notices);
 now=80000;ui_resume_guard_arm(2,route);current.path_seq++;stall();assert(!notices);
 now=100000;ui_resume_guard_arm(2,route);strcpy(current.path,"/music/b.flac");stall();assert(!notices);
 now=120000;ui_resume_guard_arm(2,route);current.position_ms=current.duration_ms;stall();assert(!notices);
 current.position_ms=10000;now=140000;ui_resume_guard_arm(2,route);ui_resume_guard_cancel();stall();assert(!notices);
 now=160000;ui_resume_guard_arm(2,route);now+=11000;resume_guard_tick(0);assert(!notices);
 now=180000;ui_resume_guard_arm(2,route);resume_guard_tick(0);now+=2000;resume_guard_tick(1);now+=2000;resume_guard_tick(0);assert(!notices);
 puts("PASS BT: codec switch only hints; route plays once then hints; never a codec failure; stale route/generation/track, EOF, cancel, expiry and transient stall suppressed");
}
'''
db = (ui/'musicdb.c').read_text()
f = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"
static sqlite3 *handle;static int admitted=1,leases,exits;static char toast[100];
static sqlite3 *db(void){return handle;}
static sqlite3 *db_read(void){return handle;}
static int sd_io_begin(void){if(!admitted)return 0;leases++;return 1;}
static void sd_io_end(void){assert(leases>0);leases--;}
static const char *colt(sqlite3_stmt *s,int n){const char *p=(const char*)sqlite3_column_text(s,n);return p?p:"";}
static void ui_toast(const char *s){snprintf(toast,sizeof toast,"%s",s);}
typedef struct mp_host {uint8_t *sel;int cap;int (*collect)(const uint8_t*,int,int**);} mp_host_t;
static void mp_exit(mp_host_t *h){(void)h;exits++;}
static int chosen=1;
static int collect(const uint8_t *s,int cap,int **out){(void)s;(void)cap;*out=malloc(chosen*sizeof **out);for(int i=0;i<chosen;i++)(*out)[i]=i+1;return chosen;}
'''
for sig in ('int mdb_song_path(', 'static int mdb_is_favorite_path_leased(', 'int mdb_is_favorite_path(', 'static int mdb_set_favorite_path_leased(', 'int mdb_set_favorite_path('):
    f += '\n'+fn(db, sig)
mp = (ui/'multipick.c').read_text()
f += '\n'+fn(mp,'static int songs_of(')+'\n'+fn(mp,'static void act_favourite(')
f += r'''
static void sql(const char *s){assert(sqlite3_exec(handle,s,0,0,0)==SQLITE_OK);}
static int rows(void){sqlite3_stmt *s;assert(sqlite3_prepare_v2(handle,"SELECT COUNT(*) FROM MY_LOVE",-1,&s,0)==SQLITE_OK);assert(sqlite3_step(s)==SQLITE_ROW);int n=sqlite3_column_int(s,0);sqlite3_finalize(s);return n;}
int main(void){
 assert(sqlite3_open(":memory:",&handle)==SQLITE_OK);
 sql("CREATE TABLE SONG(ID INTEGER PRIMARY KEY,PATH TEXT,TITLE TEXT);CREATE TABLE MY_LOVE(ID INTEGER PRIMARY KEY,PATH TEXT,TITLE TEXT);INSERT INTO SONG VALUES(1,'/a.flac','A'),(2,'/b.flac','B');");
 mp_host_t h={.collect=collect};
 sql("PRAGMA query_only=ON;");assert(mdb_set_favorite_path("/a.flac",1)==0);act_favourite(&h);assert(rows()==0);
 assert(!exits && !strcmp(toast,"Couldn't add favourites"));
 sql("PRAGMA query_only=OFF;");act_favourite(&h);assert(exits==1 && rows()==1);
 act_favourite(&h);assert(exits==2 && rows()==1); /* retry is idempotent */
 sql("CREATE TRIGGER fail_b BEFORE INSERT ON MY_LOVE WHEN NEW.PATH='/b.flac' BEGIN SELECT RAISE(FAIL,'test denied'); END;");
 chosen=2;act_favourite(&h);assert(exits==2 && rows()==1 && !strcmp(toast,"1 saved, 1 failed"));
 admitted=0;act_favourite(&h);assert(exits==2 && !strcmp(toast,"Couldn't add favourites"));assert(!leases);
 sqlite3_close(handle);puts("PASS favourites: real SQLite success, read-only denial, partial trigger failure, retry and storage refusal");
}
'''
with tempfile.TemporaryDirectory(prefix='disco-bt-fav-') as tmp:
    root=Path(tmp)
    for name,source in [('guard',g),('favourites',f)]:
        path=root/(name+'.c');path.write_text(source)
        command=['cc','-O0','-std=gnu11','-Wall','-Wextra','-Wno-unused-function','-DBASELINE='+str(int(baseline)),'-I'+str(ui),str(path)]
        if name=='favourites':command += [str(ui/'sqlite3.c'),'-pthread','-ldl','-lm']
        binary=root/name
        subprocess.run(command+['-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=10)
