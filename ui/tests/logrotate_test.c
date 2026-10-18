/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "../logrotate.h"
#include <assert.h>
#include <stdlib.h>
int main(void){
    char dir[]="/tmp/disco-log-test-XXXXXX";assert(mkdtemp(dir));
    char path[256],backup[260],stage[264];snprintf(path,sizeof path,"%s/live.log",dir);
    snprintf(backup,sizeof backup,"%s.1",path);snprintf(stage,sizeof stage,"%s.1.tmp",path);
    FILE *writer=fopen(path,"a");assert(writer);
    fputs("0123456789abcdef",writer);fflush(writer);
    assert(diskos_log_rotate(path,8)==1);
    struct stat st;assert(!stat(path,&st)&&st.st_size==0);assert(!stat(backup,&st)&&st.st_size==8);
    FILE*f=fopen(backup,"r");char text[32]={0};assert(fread(text,1,8,f)==8);fclose(f);assert(!strcmp(text,"89abcdef"));
    /* Existing append descriptors must still target the live inode. */
    fputs("new entry",writer);fflush(writer);assert(!stat(path,&st)&&st.st_size==9);
    assert(diskos_log_rotate(path,8)==1);fputs("ok",writer);fflush(writer);
    assert(diskos_log_rotate(path,8)==0);assert(!stat(path,&st)&&st.st_size==2);
    unlink(backup);assert(!mkdir(backup,0700));fputs("more data",writer);fflush(writer);
    assert(diskos_log_rotate(path,8)==-1);assert(!stat(path,&st)&&st.st_size==11);assert(access(stage,F_OK)==-1);
    fclose(writer);rmdir(backup);unlink(path);rmdir(dir);
    puts("PASS: bounded backup, repeated rotation, retained append descriptor, and backup failure preserves live log");
}
