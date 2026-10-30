/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <time.h>
#include "jsmn.h"

static mqd_t g_rx=(mqd_t)-1, g_tx=(mqd_t)-1;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static track_state_t g_state;

static pthread_mutex_t g_recov_mu = PTHREAD_MUTEX_INITIALIZER;
static dev_t g_rx_dev = 0;  static ino_t g_rx_ino = 0;
static int g_rx_ready    = 0;
static int g_rx_reopen   = 0;
static int g_reconnected = 0;
static unsigned g_rx_frames = 0;
static unsigned g_generation = 0;
static int g_player_mode = -1;
static dev_t g_tx_dev = 0;  static ino_t g_tx_ino = 0;
static int g_tx_stale    = 0;
static int g_thread_started = 0;

static int mq_identity(mqd_t q, dev_t *dev, ino_t *ino){
    struct stat s;
    if(q==(mqd_t)-1 || fstat((int)q, &s) != 0) return -1;
    *dev = s.st_dev; *ino = s.st_ino; return 0;
}

static char g_stock_title[IPC_TITLE_CAPACITY];
static int g_title_frozen, g_title_stock;
static uint64_t g_title_revision;
static int raw_id(const char *js, const jsmntok_t *t, uint64_t *out){
    uint64_t n=0;
    if(t->type!=JSMN_PRIMITIVE || t->end==t->start) return 0;
    for(int i=t->start;i<t->end;i++){
        unsigned d=(unsigned)(js[i]-'0');
        if(d>9 || n>(UINT64_MAX-d)/10) return 0;
        n=n*10+d;
    }
    *out=n; return 1;
}
static int kind_bool(const char *js, const jsmntok_t *t, int *out){
    int n=t->end-t->start;
    if(t->type!=JSMN_PRIMITIVE) return 0;
    if(n==4 && !memcmp(js+t->start,"true",4)){ *out=1; return 1; }
    if(n==5 && !memcmp(js+t->start,"false",5)){ *out=0; return 1; }
    return 0;
}
static int title_pending(const char *path, const char *title){
    const char *base=strrchr(path,'/'); base=base ? base+1 : path;
    const char *ext=strrchr(base,'.');
    return path[0]=='/' && ext && (!strcasecmp(ext,".opus") ||
        !strcasecmp(ext,".oga") || !strcasecmp(ext,".ogg")) &&
        (!title[0] || !strcmp(base,title));
}
static int tok_eq(const char*js, jsmntok_t*t, const char*s){
    int n=t->end-t->start;
    return t->type==JSMN_STRING && (int)strlen(s)==n && strncmp(js+t->start,s,n)==0;
}
static int find_val(const char*js, jsmntok_t*tk, int ntok, const char*key){
    for(int i=0;i+1<ntok;i++) if(tk[i].size!=0 && tok_eq(js,&tk[i],key)) return i+1;
    return -1;
}
static void copy_tok(const char*js, jsmntok_t*t, char*dst, int dstsz){
    int n=t->end-t->start; if(n>=dstsz) n=dstsz-1;
    memcpy(dst, js+t->start, n); dst[n]=0;
}
static long tok_long(const char*js, jsmntok_t*t){
    char b[32]; copy_tok(js,t,b,sizeof b); return strtol(b,0,10);
}
static int tok_bool(const char*js, jsmntok_t*t){
    char b[8]; copy_tok(js,t,b,sizeof b); return (b[0]=='t'||b[0]=='1'||b[0]=='T');
}
static int unescape(const char*s,int n,char*dst,int dstsz){
    int o=0, i=0;
    for(;i<n && o<dstsz-4;i++){
        if(s[i]!=2 && s[i]==92 && i+1<n){
            char c=s[++i];
            if(c==110) dst[o++]=10; else if(c==116) dst[o++]=9; else if(c==114) dst[o++]=13;
            else if(c==117 && i+4<n){
                char h[5]={s[i+1],s[i+2],s[i+3],s[i+4],0}; i+=4;
                unsigned cp=strtoul(h,0,16);
                if(cp<0x80) dst[o++]=cp;
                else if(cp<0x800){ dst[o++]=0xC0|(cp>>6); dst[o++]=0x80|(cp&0x3F); }
                else { dst[o++]=0xE0|(cp>>12); dst[o++]=0x80|((cp>>6)&0x3F); dst[o++]=0x80|(cp&0x3F); }
            } else dst[o++]=c;
        } else dst[o++]=s[i];
    }
    dst[o]=0;
    return (i<n) ? -1 : o;
}

static void clear_track(void){
    if(g_state.player_confirmed) g_state.playback_token++;
    g_state.player_confirmed=0; g_state.provisional=0; g_state.title_ready=0;
    g_state.song_id_valid=g_state.pos_id_valid=0;
    g_state.is_cue_valid=g_state.is_sacd_valid=0;
    g_state.song_id_raw=g_state.pos_id_raw=0;
    g_title_frozen=g_title_stock=0; g_stock_title[0]=0;
    g_state.have_track=0;
    g_state.title[0]=0; g_state.artist[0]=0; g_state.album[0]=0; g_state.path[0]=0;
    g_state.duration_ms=0; g_state.position_ms=0; g_state.pos_id=0;
    g_state.sample_rate=0; g_state.is_dsd=0;
}

static int stock_a2_space(char c){ return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
static int stock_a2_match(const char *src, int len, int at, const char *pattern){
    for(int i=0;pattern[i];i++){
        if(at>=len || src[at++]!=pattern[i]) return -1;
        if(pattern[i]==',') while(at<len && stock_a2_space(src[at])) at++;
    }
    return at;
}
static int stock_a2_scalar(const char *src, int n, int boolean){
    if(boolean) return (n==4 && !memcmp(src,"true",4)) || (n==5 && !memcmp(src,"false",5));
    int i=(n>0 && src[0]=='-') ? 1 : 0;
    if(i==n || (src[i]=='0' && n-i>1)) return 0;
    for(;i<n;i++) if(src[i]<'0' || src[i]>'9') return 0;
    return 1;
}
static int stock_a2_suffix(const char *src, int len, int at){
    static const char *keys[]={"love","playerflag","state","playing_num","work_mode"};
    for(int k=0;k<5;k++){
        char head[32]; snprintf(head,sizeof head,",\"%s\":",keys[k]);
        at=stock_a2_match(src,len,at,head);
        if(at<0) return 0;
        int start=at;
        if(k>=3){
            if(at>=len || src[at++]!='"') return 0;
            while(at<len && src[at]!='"'){
                if((unsigned char)src[at]<32 || src[at]=='\\') return 0;
                at++;
            }
            if(at>=len) return 0;
            at++;
        } else {
            while(at<len && src[at]!=',' && src[at]!='}') at++;
            if(!stock_a2_scalar(src+start,at-start,k==0)) return 0;
        }
    }
    return at<len && src[at]=='}' && at+1==len;
}
static int stock_a2_append(char *out, int *used, int cap, const char *s, int n){
    if(n<0 || n>=cap-*used) return 0;
    memcpy(out+*used,s,n); *used+=n; out[*used]=0; return 1;
}
static int stock_a2_depth(const char *s, int n){
    int i=0;
    while(i<n && s[i]=='\\') i++;
    if(i==0) return 0;
    if(i>=n || s[i]!='/') return -1;
    return i==1 ? 1 : i==3 ? 2 : -1;
}
static int stock_a2_string(const char *s, int n, int depth, int path,
                           char *out, int *used, int cap){
    int unit=1<<depth, bytes=0;
    for(int i=0;i<n;){
        unsigned char c=(unsigned char)s[i++];
        int slashes=0;
        if(c=='\\'){
            slashes=1;
            while(i<n && s[i]=='\\'){ slashes++; i++; }
            int tail=slashes%unit;
            if(depth && i<n && (s[i]=='/' || s[i]=='"') && tail==unit-1){
                slashes/=unit; c=(unsigned char)s[i++];
            } else {
                if(depth && tail && path) return 0;
                if(!tail) slashes/=unit;
                c=0;
            }
        }
        bytes+=slashes+(c!=0);
        if(path==1 && bytes>(int)sizeof g_state.path-4) return 0;
        while(slashes--){
            if(!stock_a2_append(out,used,cap,"\\\\\\\\",4)) return 0;
        }
        if(c=='"'){
            if(!stock_a2_append(out,used,cap,"\\\\\\\"",4)) return 0;
        } else if(c && c<32){
            char esc[8]; int en=snprintf(esc,sizeof esc,"\\\\u%04x",c);
            if(!stock_a2_append(out,used,cap,esc,en)) return 0;
        } else if(c && !stock_a2_append(out,used,cap,(const char *)&c,1)) return 0;
    }
    return 1;
}
static int stock_a2_normalize(const char *src, int len, char *out, int cap){
    static const char *keys[]={"id","pos_id","song_name","song_duration_time",
        "song_file_path","song_sample_rate","song_encoding_rate","song_bit_rate",
        "song_artist_name","song_album_name","song_style_name","song_track",
        "song_channel","is_sacd","is_cue","is_dsd","is_m3u","m3u_file_path"};
    static const unsigned char types[]={0,0,1,0,1,0,0,0,1,1,1,0,0,2,2,2,2,1};
    const char *prefix="{\"song\":\"{";
    int used=0, at=(int)strlen(prefix), starts[18], ends[18], count=0, depth=0;
    if(len<at || memcmp(src,prefix,at)) return 0;
    for(int k=0;k<18;k++){
        char head[64], boundary[64];
        snprintf(head,sizeof head,"%s\\\"%s\\\":%s",k ? "," : "",keys[k],types[k]==1 ? "\\\"" : "");
        int next=stock_a2_match(src,len,at,head);
        if(next<0) return 0;
        at=next;
        int last=(k==17 || (k==15 &&
            (stock_a2_match(src,len,at,"true}\"")>=0 ||
             stock_a2_match(src,len,at,"false}\"")>=0)));
        int bn;
        if(last) bn=snprintf(boundary,sizeof boundary,"%s}\"",types[k]==1 ? "\\\"" : "");
        else bn=snprintf(boundary,sizeof boundary,"%s,\\\"%s\\\":",types[k]==1 ? "\\\"" : "",keys[k+1]);
        int end=-1;
        for(int i=at;i+bn<=len;i++){
            if(stock_a2_match(src,len,i,boundary)>=0){
                if(end>=0) return 0;
                end=i;
            }
        }
        if(end<0) return 0;
        if(types[k]==1){
            starts[count]=at; ends[count++]=end;
            if(k==4){
                depth=stock_a2_depth(src+at,end-at);
                if(depth<0) return 0;
            }
            at=end+2;
        } else {
            int n=end-at;
            if(!stock_a2_scalar(src+at,n,types[k]==2)) return 0;
            at=end;
        }
        if(last) break;
    }
    if(stock_a2_match(src,len,at,"}\"")<0 || !stock_a2_suffix(src,len,at+2)) return 0;
    int copied=0;
    for(int k=0;k<count;k++){
        if(!stock_a2_append(out,&used,cap,src+copied,starts[k]-copied) ||
           !stock_a2_string(src+starts[k],ends[k]-starts[k],k==1 ? depth : 0,k==1,
                            out,&used,cap)) return 0;
        copied=ends[k];
    }
    if(!stock_a2_append(out,&used,cap,src+copied,len-copied)) return 0;
    return used;
}

static void parse_a2(const char*payload,int len){
    jsmn_parser p; jsmntok_t tk[64];
    static char repaired[8192], song[8192];
    jsmntok_t st[96]; int nt, n2=0, v;
    const char *stock_head="{\"song\":\"{\\\"id\\\":";
    int head_len=(int)strlen(stock_head);
    if(len>=head_len && !memcmp(payload,stock_head,head_len)){
        int normalized=stock_a2_normalize(payload,len,repaired,sizeof repaired);
        if(!normalized) return;
        payload=repaired; len=normalized;
    }
    jsmn_init(&p);
    nt=jsmn_parse(&p,payload,len,tk,64);
    v=nt>0 ? find_val(payload,tk,nt,"song") : -1;
    if(v>=0 && tk[v].type==JSMN_STRING && (tk[v].end-tk[v].start)>2){
        int decoded=unescape(payload+tk[v].start,tk[v].end-tk[v].start,song,sizeof song);
        jsmn_parser p2; jsmn_init(&p2);
        n2=decoded>=0 ? jsmn_parse(&p2,song,decoded,st,96) : -1;
    }
    if(nt<1 || n2<0) return;
    if(n2>0){
        char title[sizeof g_state.title]; int sv=find_val(song,st,n2,"song_name");
        if(sv>=0 && unescape(song+st[sv].start,st[sv].end-st[sv].start,title,sizeof title)<0) return;
    }
    pthread_mutex_lock(&g_mu);
    char oldpath[256]; snprintf(oldpath, sizeof oldpath, "%s", g_state.path);
    v=find_val(payload,tk,nt,"state");        if(v>=0) g_state.state=(int)tok_long(payload,&tk[v]);
    v=find_val(payload,tk,nt,"playing_num");  if(v>=0) copy_tok(payload,&tk[v],g_state.playing_num,sizeof g_state.playing_num);
    v=find_val(payload,tk,nt,"work_mode");    if(v>=0) copy_tok(payload,&tk[v],g_state.work_mode,sizeof g_state.work_mode);
    v=find_val(payload,tk,nt,"love");         if(v>=0) g_state.is_favorite=tok_bool(payload,&tk[v]);
    v=find_val(payload,tk,nt,"song");
    if(v>=0 && tk[v].type==JSMN_STRING && (tk[v].end-tk[v].start)>2){
        if(n2>0){
            char t_title[sizeof g_state.title]={0}, t_artist[sizeof g_state.artist]={0};
            char t_album[sizeof g_state.album]={0}, t_path[sizeof g_state.path]={0};
            long t_dur=-1; int t_sr=-1, t_dsd=-1, have_path=0, sv;
            uint64_t song_id=0, pos_id=0; int sid_ok=0, pid_ok=0, cue=0, sacd=0, cue_ok=0, sacd_ok=0;
            sv=find_val(song,st,n2,"id"); if(sv>=0) sid_ok=raw_id(song,&st[sv],&song_id);
            sv=find_val(song,st,n2,"pos_id"); if(sv>=0) pid_ok=raw_id(song,&st[sv],&pos_id);
            sv=find_val(song,st,n2,"is_cue"); if(sv>=0) cue_ok=kind_bool(song,&st[sv],&cue);
            sv=find_val(song,st,n2,"is_sacd"); if(sv>=0) sacd_ok=kind_bool(song,&st[sv],&sacd);
            sv=find_val(song,st,n2,"song_name");          if(sv>=0) unescape(song+st[sv].start, st[sv].end-st[sv].start, t_title, sizeof t_title);
            sv=find_val(song,st,n2,"song_artist_name");   if(sv>=0) unescape(song+st[sv].start, st[sv].end-st[sv].start, t_artist, sizeof t_artist);
            sv=find_val(song,st,n2,"song_album_name");    if(sv>=0) unescape(song+st[sv].start, st[sv].end-st[sv].start, t_album,  sizeof t_album);
            sv=find_val(song,st,n2,"song_file_path");
            if(sv>=0 && unescape(song+st[sv].start, st[sv].end-st[sv].start, t_path, sizeof t_path) >= 0 && t_path[0]) have_path=1;
            sv=find_val(song,st,n2,"song_duration_time"); if(sv>=0) t_dur=tok_long(song,&st[sv]);
            sv=find_val(song,st,n2,"song_sample_rate");   if(sv>=0) t_sr=(int)tok_long(song,&st[sv]);
            sv=find_val(song,st,n2,"is_dsd");             if(sv>=0) t_dsd=tok_bool(song,&st[sv]);
            if(have_path){
                int changed=!g_state.player_confirmed || strcmp(g_state.path,t_path) ||
                    g_state.song_id_valid!=(unsigned)sid_ok || g_state.pos_id_valid!=(unsigned)pid_ok ||
                    g_state.song_id_raw!=song_id || g_state.pos_id_raw!=pos_id;
                if(changed){
                    g_state.playback_token++; g_title_frozen=g_title_stock=0;
                    g_state.position_ms=0; g_state.pos_seq=0;
                }
                g_state.player_confirmed=1; g_state.provisional=0;
                g_state.song_id_raw=song_id; g_state.pos_id_raw=pos_id;
                g_state.song_id_valid=sid_ok; g_state.pos_id_valid=pid_ok;
                if(strcmp(g_stock_title,t_title) || g_state.is_cue!=cue || g_state.is_sacd!=sacd ||
                   g_state.is_cue_valid!=(unsigned)cue_ok || g_state.is_sacd_valid!=(unsigned)sacd_ok)
                    g_title_revision++;
                g_state.is_cue=cue; g_state.is_sacd=sacd;
                g_state.is_cue_valid=cue_ok; g_state.is_sacd_valid=sacd_ok;
                strcpy(g_stock_title,t_title);
                snprintf(g_state.path, sizeof g_state.path, "%s", t_path);
                if(!g_title_frozen){
                    strcpy(g_state.title,t_title);
                    if(!title_pending(t_path,t_title) || (cue_ok && cue) || (sacd_ok && sacd)) g_title_stock=1;
                    g_state.title_ready=g_title_stock;
                }
                snprintf(g_state.artist, sizeof g_state.artist, "%s", t_artist);
                snprintf(g_state.album,  sizeof g_state.album,  "%s", t_album);
                if(t_dur>=0) g_state.duration_ms=t_dur;
                if(t_sr>=0)  g_state.sample_rate=t_sr;
                if(t_dsd>=0) g_state.is_dsd=t_dsd;
                g_state.pos_id = (pid_ok && pos_id>0 && pos_id<=0x7FFFFFFF) ? (int)pos_id : 0;
                g_state.have_track=1;
            } else {
                clear_track();
            }
        }
    } else if(v>=0 && tk[v].type==JSMN_STRING){
        clear_track();
    }
    g_state.seq++;
    if(strcmp(oldpath, g_state.path) != 0) g_state.path_seq = g_state.seq;
    pthread_mutex_unlock(&g_mu);
}

static int all_hex(const char*s,int n){
    if(n<=0) return 0;
    for(int i=0;i<n;i++){ char c=s[i];
        if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) return 0; }
    return 1;
}

static unsigned g_scr_seq, g_aa03_seq; static int g_scr_on = -1;
static void screen_new_generation(void){
    g_generation++; g_scr_on = -1; g_scr_seq = g_aa03_seq = 0;
}
static void parse_frame(const char*buf,int n){
    if(n<8) return;
    if(buf[0]=='a'&&buf[1]=='a'&&buf[2]=='2'&&buf[3]=='3' && n>=12){
        char h[5]={buf[8],buf[9],buf[10],buf[11],0};
        if(!all_hex(h,4)) return;
        pthread_mutex_lock(&g_recov_mu); g_scr_on=(int)strtol(h,0,16)!=0; g_scr_seq++; pthread_mutex_unlock(&g_recov_mu);
        return;
    }
    if(buf[0]=='a'&&buf[1]=='a'&&buf[2]=='0'&&buf[3]=='3'){
        pthread_mutex_lock(&g_recov_mu); g_aa03_seq++; pthread_mutex_unlock(&g_recov_mu);
        return;
    }
    if(buf[0]==97 && buf[1]==49){
        if(n<8) return;
        char lh[5]={buf[4],buf[5],buf[6],buf[7],0};
        if(!all_hex(lh,4)) return;
        int flen=(int)strtol(lh,0,16);
        if(flen<9 || n<flen) return;
        int pn=flen-8; if(pn>15) return;
        char h[16]={0}; memcpy(h,buf+8,pn);
        if(!all_hex(h,pn)) return;
        errno=0; long ms=strtol(h,0,16);
        if(errno==ERANGE || ms<0) return;
        pthread_mutex_lock(&g_mu); g_state.position_ms=ms; g_state.seq++; g_state.pos_seq=g_state.seq; pthread_mutex_unlock(&g_mu);
    } else if(buf[0]==97 && buf[1]==50){
        parse_a2(buf+8, n-8);
    } else if(buf[0]=='a'&&buf[1]=='7'&&buf[2]=='1'&&buf[3]=='4' && n>=12){
        char h[3]={buf[10],buf[11],0};
        if(!all_hex(h,2)) return;
        int v=(int)strtol(h,0,16);
        pthread_mutex_lock(&g_mu); g_state.volume=v; g_state.volume_seq++; pthread_mutex_unlock(&g_mu);
    } else if(buf[0]=='a'&&buf[1]=='6'&&buf[2]=='0'&&buf[3]=='7' && n>=12){
        char h[5]={buf[8],buf[9],buf[10],buf[11],0};
        if(!all_hex(h,4)) return;
        int mode=(int)strtol(h,0,16);
        pthread_mutex_lock(&g_recov_mu); g_player_mode=mode; pthread_mutex_unlock(&g_recov_mu);
    }
}

static char  *g_rxbuf = NULL;
static size_t g_rxbufsz = 0;

static int rx_do_reopen(void){
    pthread_mutex_lock(&g_recov_mu); g_rx_reopen = 0; pthread_mutex_unlock(&g_recov_mu);
    mqd_t nw = mq_open("/ui", O_RDONLY);
    if(nw==(mqd_t)-1){
        pthread_mutex_lock(&g_recov_mu); g_rx_reopen = 1; pthread_mutex_unlock(&g_recov_mu); return -1;
    }
    struct mq_attr at;
    if(mq_getattr(nw,&at)==0 && at.mq_msgsize>0 && (size_t)at.mq_msgsize > g_rxbufsz){
        char *nb = malloc((size_t)at.mq_msgsize);
        if(nb){ free(g_rxbuf); g_rxbuf = nb; g_rxbufsz = (size_t)at.mq_msgsize; }
        else {
            mq_close(nw);
            pthread_mutex_lock(&g_recov_mu); g_rx_reopen = 1; pthread_mutex_unlock(&g_recov_mu);
            return -1;
        }
    }
    mqd_t old = g_rx;
    g_rx = nw;
    dev_t d=0; ino_t io=0; mq_identity(nw, &d, &io);
    pthread_mutex_lock(&g_recov_mu);
    g_rx_dev = d; g_rx_ino = io; g_rx_ready = 1; g_reconnected = 1; g_player_mode = -1; screen_new_generation();
    unsigned generation=g_generation;
    pthread_mutex_unlock(&g_recov_mu);
    pthread_mutex_lock(&g_mu);
    int had_path=g_state.path[0]!=0;
    clear_track(); g_state.playback_token++; g_state.player_generation=generation; g_state.seq++;
    if(had_path) g_state.path_seq=g_state.seq;
    pthread_mutex_unlock(&g_mu);
    if(old!=(mqd_t)-1 && old!=nw) mq_close(old);
    fprintf(stderr,"ipc: /ui reattached after player restart\n");
    return 0;
}

static void *ipc_thread(void*arg){
    (void)arg;
    for(;;){
        pthread_mutex_lock(&g_recov_mu); int want = g_rx_reopen; pthread_mutex_unlock(&g_recov_mu);
        if(want) rx_do_reopen();
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 1;
        ssize_t n = mq_timedreceive(g_rx, g_rxbuf, g_rxbufsz, NULL, &ts);
        if(n>=0){ pthread_mutex_lock(&g_recov_mu); g_rx_frames++; pthread_mutex_unlock(&g_recov_mu); parse_frame(g_rxbuf,(int)n); }
        else if(errno==ETIMEDOUT) continue;
        else if(errno==EINTR) continue;
        else if(errno==EBADF){ if(rx_do_reopen()!=0) usleep(50000); }
        else usleep(50000);
    }
    return 0;
}

int ipc_start(void){
    if(g_thread_started) return 0;
    g_rx=mq_open("/ui", O_RDONLY);
    if(g_rx==(mqd_t)-1){
        struct mq_attr ca; ca.mq_flags=0; ca.mq_maxmsg=32; ca.mq_msgsize=8192; ca.mq_curmsgs=0;
        g_rx=mq_open("/ui", O_RDONLY|O_CREAT, 0666, &ca);
        if(g_rx==(mqd_t)-1){ ca.mq_maxmsg=10; g_rx=mq_open("/ui", O_RDONLY|O_CREAT, 0666, &ca); }
    }
    if(g_rx==(mqd_t)-1){ fprintf(stderr,"mq_open /ui fail: %s\n",strerror(errno)); return -1; }
    struct mq_attr at;
    g_rxbufsz = (mq_getattr(g_rx,&at)==0 && at.mq_msgsize>0) ? (size_t)at.mq_msgsize : 8200;
    if(g_rxbufsz < 8200) g_rxbufsz = 8200;
    g_rxbuf = malloc(g_rxbufsz);
    if(!g_rxbuf){
        fprintf(stderr,"ipc rxbuf alloc %zu failed\n",g_rxbufsz);
        mq_close(g_rx); g_rx=(mqd_t)-1;
        return -1;
    }
    g_tx=mq_open("/player", O_WRONLY|O_NONBLOCK);
    if(g_tx==(mqd_t)-1) fprintf(stderr,"mq_open /player fail: %s\n",strerror(errno));
    else mq_identity(g_tx, &g_tx_dev, &g_tx_ino);
    mq_identity(g_rx, &g_rx_dev, &g_rx_ino);
    memset(&g_state,0,sizeof g_state);
    pthread_t t;
    if(pthread_create(&t,0,ipc_thread,0)!=0){
        fprintf(stderr,"ipc_thread create failed: %s\n",strerror(errno));
        mq_close(g_rx); g_rx=(mqd_t)-1;
        if(g_tx!=(mqd_t)-1){ mq_close(g_tx); g_tx=(mqd_t)-1; }
        free(g_rxbuf); g_rxbuf=NULL;
        return -1;
    }
    pthread_detach(t);
    g_thread_started = 1;
    pthread_mutex_lock(&g_recov_mu); g_rx_ready = 1; pthread_mutex_unlock(&g_recov_mu);
    return 0;
}
void ipc_health_check(void){
    mqd_t p = mq_open("/ui", O_RDONLY);
    if(p!=(mqd_t)-1){
        struct stat s;
        if(fstat((int)p,&s)==0){
            pthread_mutex_lock(&g_recov_mu);
            if(g_rx_ready && (g_rx_dev||g_rx_ino) && (s.st_dev!=g_rx_dev || s.st_ino!=g_rx_ino)) g_rx_reopen = 1;
            pthread_mutex_unlock(&g_recov_mu);
        }
        mq_close(p);
    }
    mqd_t q = mq_open("/player", O_WRONLY|O_NONBLOCK);
    if(q!=(mqd_t)-1){
        struct stat s;
        if(g_tx!=(mqd_t)-1 && (g_tx_dev||g_tx_ino) && fstat((int)q,&s)==0 &&
           (s.st_dev!=g_tx_dev || s.st_ino!=g_tx_ino))
            g_tx_stale = 1;
        mq_close(q);
    }
}
int ipc_take_reconnected(void){
    pthread_mutex_lock(&g_recov_mu); int r=g_reconnected; g_reconnected=0; pthread_mutex_unlock(&g_recov_mu);
    return r;
}
unsigned ipc_rx_frames(void){
    pthread_mutex_lock(&g_recov_mu); unsigned r=g_rx_frames; pthread_mutex_unlock(&g_recov_mu);
    return r;
}
int ipc_player_mode(void){
    pthread_mutex_lock(&g_recov_mu); int r=g_player_mode; pthread_mutex_unlock(&g_recov_mu);
    return r;
}
void ipc_screen_snapshot(int *on, unsigned *scr_seq, unsigned *poweroff_seq, unsigned *generation){
    pthread_mutex_lock(&g_recov_mu);
    if(on) *on=g_scr_on; if(scr_seq) *scr_seq=g_scr_seq; if(poweroff_seq) *poweroff_seq=g_aa03_seq;
    if(generation) *generation=g_generation;
    pthread_mutex_unlock(&g_recov_mu);
}
unsigned ipc_generation(void){
    pthread_mutex_lock(&g_recov_mu); unsigned r=g_generation; pthread_mutex_unlock(&g_recov_mu);
    return r;
}
void ipc_get_state(track_state_t*out){
    pthread_mutex_lock(&g_mu); *out=g_state; pthread_mutex_unlock(&g_mu);
}
void ipc_get_title_event(ipc_title_event *out){
    pthread_mutex_lock(&g_mu);
    out->state=g_state; strcpy(out->player_title,g_stock_title); out->evidence_revision=g_title_revision;
    pthread_mutex_unlock(&g_mu);
}
int ipc_publish_title(ipc_title_event *event, const char *title, int freeze){
    pthread_mutex_lock(&g_mu);
    int accepted=g_state.player_confirmed && g_state.playback_token==event->state.playback_token &&
        g_title_revision==event->evidence_revision;
    if(accepted){
        snprintf(g_state.title,sizeof g_state.title,"%s",freeze ? title : g_stock_title);
        g_title_frozen=freeze; g_title_stock=!freeze; g_state.title_ready=1; g_state.seq++;
    } else {
        event->state=g_state; strcpy(event->player_title,g_stock_title);
        event->evidence_revision=g_title_revision;
    }
    pthread_mutex_unlock(&g_mu);
    return accepted;
}
void ipc_seed_state(const track_state_t *s){
    if(!s) return;
    pthread_mutex_lock(&g_mu);
    if(!g_state.have_track){
        snprintf(g_state.title,  sizeof g_state.title,  "%s", s->title);
        snprintf(g_state.artist, sizeof g_state.artist, "%s", s->artist);
        snprintf(g_state.album,  sizeof g_state.album,  "%s", s->album);
        snprintf(g_state.path,   sizeof g_state.path,   "%s", s->path);
        g_state.duration_ms = s->duration_ms;
        g_state.position_ms = s->position_ms;
        g_state.state       = s->state;
        g_state.is_favorite = s->is_favorite;
        g_state.have_track  = 1;
        g_state.provisional = 1; g_state.player_confirmed = 0; g_state.title_ready = 0;
        g_state.seq++;
    }
    pthread_mutex_unlock(&g_mu);
}
static volatile int g_send_err = 0;

static int tx_open(void){
    if(g_tx_stale && g_tx!=(mqd_t)-1){ mq_close(g_tx); g_tx=(mqd_t)-1; }
    g_tx_stale = 0;
    if(g_tx==(mqd_t)-1){
        g_tx=mq_open("/player", O_WRONLY|O_NONBLOCK);
        g_tx_dev = 0; g_tx_ino = 0;
        if(g_tx!=(mqd_t)-1) mq_identity(g_tx, &g_tx_dev, &g_tx_ino);
    }
    return g_tx!=(mqd_t)-1;
}
static int tx_try(const char *frame, int *err){
    if(!tx_open()){ *err = ENOENT; return -1; }
    if(mq_send(g_tx, frame, strlen(frame), 0) == 0) return 0;
    *err = errno;
    if(*err == EAGAIN) return 1;
    mq_close(g_tx); g_tx=(mqd_t)-1;
    return -1;
}
static long tx_depth(void){
    struct mq_attr a;
    if(!tx_open() || mq_getattr(g_tx, &a) != 0) return -1;
    return a.mq_curmsgs;
}

#define TX_MAX 48
#define TX_HOLD_MS 6000
#define TX_HOLD_BOOT_MS 30000
static int tx_not_yet(int err){ return err == ENOENT; }
static uint32_t tx_hold(void){ return ipc_rx_frames() ? TX_HOLD_MS : TX_HOLD_BOOT_MS; }
#define TX_REOPEN_MS 2000
static int tx_stall_reopen(uint32_t now, uint32_t stall0){ return (uint32_t)(now - stall0) >= TX_REOPEN_MS; }
typedef struct { char f[448]; uint32_t t0, s0; int quiet; } txe_t;
static txe_t g_txq[TX_MAX];
static int g_txh, g_txn;
static pthread_mutex_t g_txmu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t tx_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); }
static void tx_fail(const txe_t *e, int err){
    if(!e->quiet){ g_send_err = 1; fprintf(stderr,"ipc_send_cmd '%s' failed: %s\n", e->f, strerror(err)); }
}
static void tx_drain_locked(void){
    while(g_txn > 0){
        txe_t *e = &g_txq[g_txh]; int err = 0;
        int r = tx_try(e->f, &err);
        if(r == 1 && tx_stall_reopen(tx_ms(), e->s0)){
            e->s0 = tx_ms();
            if(g_tx != (mqd_t)-1){ mq_close(g_tx); g_tx = (mqd_t)-1; }
            fprintf(stderr,"ipc: /player full for %ums, reopening\n", (unsigned)(e->s0 - e->t0));
            r = tx_try(e->f, &err);
        }
        if(r == 1 || (r < 0 && tx_not_yet(err))){
            if(tx_ms() - e->t0 < tx_hold()) return;
            r = -1; if(!err) err = EAGAIN;
        }
        if(r < 0) tx_fail(e, err);
        g_txh = (g_txh + 1) % TX_MAX; g_txn--;
    }
}
int ipc_frame_is_dangerous(const char *frame){
    if(!frame) return 0;
    size_t n = strlen(frame);
    if(n < 4) return 0;
    char op[5];
    for(int i = 0; i < 4; i++){ char c = frame[i]; op[i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c; }
    op[4] = 0;
    if(!strcmp(op, "0800") || !strcmp(op, "0621")) return 1;
    int isv = !strcmp(op, "0724") || !strcmp(op, "0722") || !strcmp(op, "0720");
    if(!isv) return 0;
    if(n < 12) return 1;
    char *end; char v[5] = { frame[8], frame[9], frame[10], frame[11], 0 };
    unsigned long v1 = strtoul(v, &end, 16);
    if(*end) return 1;
    if(!strcmp(op, "0720")) return v1 != 0;
    return v1 == 2;
}
static int ipc_send_raw(const char*frame, int quiet);
static int ipc_send_internal(const char*frame, int quiet){
    if(ipc_frame_is_dangerous(frame)){ fprintf(stderr, "ipc: refused dangerous frame %.4s\n", frame); return -1; }
    return ipc_send_raw(frame, quiet);
}
static int ipc_send_raw(const char*frame, int quiet){
    if(strlen(frame) >= sizeof g_txq[0].f){ if(!quiet) g_send_err = 1; return -1; }
    pthread_mutex_lock(&g_txmu);
    tx_drain_locked();
    int rc = 0;
    if(g_txn == 0){
        int err = 0, r = tx_try(frame, &err);
        for(int a = 0; r == 1 && a < 3; a++){ usleep(2000); r = tx_try(frame, &err); }
        if(r == 0){ pthread_mutex_unlock(&g_txmu); return 0; }
        if(r < 0 && !tx_not_yet(err)){ txe_t e; snprintf(e.f, sizeof e.f, "%s", frame); e.quiet = quiet; tx_fail(&e, err);
                   pthread_mutex_unlock(&g_txmu); return -1; }
    }
    if(g_txn >= TX_MAX){ txe_t e; snprintf(e.f, sizeof e.f, "%s", frame); e.quiet = quiet; tx_fail(&e, EAGAIN); rc = -1; }
    else {
        txe_t *e = &g_txq[(g_txh + g_txn) % TX_MAX];
        snprintf(e->f, sizeof e->f, "%s", frame); e->t0 = e->s0 = tx_ms(); e->quiet = quiet; g_txn++;
        fprintf(stderr,"ipc: player busy, '%.12s' waits (%d in line)\n", frame, g_txn);
    }
    pthread_mutex_unlock(&g_txmu);
    return rc;
}
void ipc_tx_pump(void){
    if(!g_txn) return;
    pthread_mutex_lock(&g_txmu); tx_drain_locked(); pthread_mutex_unlock(&g_txmu);
}
int ipc_tx_waiting(void){ return g_txn; }
static int g_quiet_n, g_quiet_fails;
void ipc_quiet_begin(void){ g_quiet_n++; }
int  ipc_quiet_end(void){ if(g_quiet_n > 0) g_quiet_n--; int f = g_quiet_fails; if(!g_quiet_n) g_quiet_fails = 0; return f; }
static unsigned g_transport_seq;
unsigned ipc_transport_seq(void){ return g_transport_seq; }
int ipc_send_cmd(const char*frame){
    if(!strncmp(frame,"0100",4) || !strncmp(frame,"0103",4) || !strncmp(frame,"0201",4))
        g_transport_seq++;
    if(g_quiet_n > 0){ int r = ipc_send_internal(frame, 1); if(r < 0) g_quiet_fails++; return r; }
    return ipc_send_internal(frame, 0);
}
int ipc_send_cmd_unsafe(const char*frame){ return ipc_send_raw(frame, 0); }
int ipc_send_quiet(const char*frame){ return ipc_send_internal(frame, 1); }
int ipc_send_probe(const char*frame){
    long d = tx_depth();
    if(g_txn > 0 || d >= 2) return 0;
    return ipc_send_internal(frame, 1);
}
int ipc_is_ready(void){
    pthread_mutex_lock(&g_recov_mu); int r=g_rx_ready; pthread_mutex_unlock(&g_recov_mu); return r;
}
int ipc_take_send_error(void){ int e=g_send_err; g_send_err=0; return e; }

static pthread_mutex_t g_library_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_library_epoch;
void ipc_note_library_commit(void){
    pthread_mutex_lock(&g_library_mu); g_library_epoch++; pthread_mutex_unlock(&g_library_mu);
}
uint64_t ipc_library_epoch(void){
    pthread_mutex_lock(&g_library_mu); uint64_t epoch=g_library_epoch; pthread_mutex_unlock(&g_library_mu);
    return epoch;
}
