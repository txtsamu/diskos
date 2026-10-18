/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISKOS_LOGROTATE_H
#define DISKOS_LOGROTATE_H
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
/* Retain at most the latest limit bytes, then empty the SAME inode: stdout,
 * stderr and already-open appenders keep writing to the live log. On backup
 * failure leave the original untouched. Caller serializes its own writers.
 * Logging is best-effort, never fsync: no extra flash durability barrier. */
static int diskos_log_rotate(const char *path, size_t limit){
    char previous[256],stage[256];
    if(snprintf(previous,sizeof previous,"%s.1",path)>=(int)sizeof previous ||
       snprintf(stage,sizeof stage,"%s.1.tmp",path)>=(int)sizeof stage)return -1;
    int in=open(path,O_RDWR|O_CLOEXEC);if(in<0)return errno==ENOENT?0:-1;
    struct stat st;if(fstat(in,&st)||!S_ISREG(st.st_mode)){close(in);return -1;}
    if(st.st_size<=(off_t)limit){close(in);return 0;}
    int out=open(stage,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
    if(out<0){close(in);return -1;}
    off_t pos=st.st_size-(off_t)limit;size_t left=limit;int ok=1;char buf[4096];
    while(left){
        size_t want=left<sizeof buf?left:sizeof buf;
        ssize_t n=pread(in,buf,want,pos);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){ok=0;break;}
        for(ssize_t written=0;written<n;){
            ssize_t w=write(out,buf+written,(size_t)(n-written));
            if(w<0&&errno==EINTR)continue;
            if(w<=0){ok=0;break;}written+=w;
        }
        if(!ok)break;
        pos+=n;left-=(size_t)n;
    }
    if(close(out))ok=0;
    if(ok&&rename(stage,previous))ok=0;
    if(ok&&ftruncate(in,0))ok=0;
    if(!ok)unlink(stage);
    close(in);return ok?1:-1;
}
#endif
