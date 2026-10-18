#!/usr/bin/env python3
"""Exercise the production history hook without LVGL or a player process."""
from pathlib import Path
import subprocess
import tempfile

ui = Path(__file__).resolve().parents[1]
src = (ui / 'ui.c').read_text()
hook = src[src.index('void ui_history_tick('):src.index('void ui_update(')]
main = (ui / 'main.c').read_text()
assert main.index('ui_history_tick(&st);') < main.index('if(bl_state != 2 && (st.seq')
program = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {int have_track, state; char path[256];} track_state_t;
static char g_stat_path[256];
static int calls;
static void copy_cstr(char *d,size_t n,const char *s){snprintf(d,n,"%s",s);}
static int mdb_is_book_path(const char *s){return strstr(s,".m4b")!=NULL;}
static void mdb_record_play(const char *s){assert(s[0]);calls++;}
''' + hook + r'''
int main(void){
 track_state_t s={.have_track=1,.state=1,.path="A.flac"};
 ui_history_tick(&s); assert(calls==0);
 s.state=2;ui_history_tick(&s);ui_history_tick(&s);assert(calls==1);
 s.state=1;strcpy(s.path,"B.flac");ui_history_tick(&s);assert(calls==1);
 s.state=2;ui_history_tick(&s);assert(calls==2);
 strcpy(s.path,"book.m4b");ui_history_tick(&s);assert(calls==2);
 strcpy(s.path,"B.flac");ui_history_tick(&s);assert(calls==3);
 s.have_track=0;ui_history_tick(&s);s.have_track=1;ui_history_tick(&s);assert(calls==4);
 ui_history_tick(NULL);ui_history_tick(&s);assert(calls==5);
 s.path[0]=0;ui_history_tick(&s);assert(calls==5);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    source=Path(d)/'history.c';binary=Path(d)/'history'
    source.write_text(program)
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: production History hook, paused skips, deduplication, books, reset and screen-off placement')
