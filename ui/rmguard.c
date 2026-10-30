/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "rmguard.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef RMGUARD_ROOTFS
#define RMGUARD_ROOTFS     "/opt/diskos/bin"
#endif
#ifndef RMGUARD_SYSTEM
#define RMGUARD_SYSTEM     "/bin"
#endif
#ifndef RMGUARD_FALLBACK_PARENT
#define RMGUARD_FALLBACK_PARENT "/usr/data/diskos"
#endif
#ifndef RMGUARD_FALLBACK
#define RMGUARD_FALLBACK   "/usr/data/diskos/bin"
#endif
static const char RMGUARD_SCRIPT[] =
    "#!/bin/sh\n"
    "log_bg() {\n"
    "    { [ -f /usr/data/diskos_rmguard.log ] && [ \"$(/bin/busybox wc -c < /usr/data/diskos_rmguard.log 2>/dev/null)\" -gt 65536 ]; } \\\n"
    "        || echo \"$(/bin/busybox date '+%F %T' 2>/dev/null) $1\" >> /usr/data/diskos_rmguard.log 2>/dev/null\n"
    "}\n"
    "\n"
    "options=1\n"
    "for a in \"$@\"; do\n"
    "    if [ \"$options\" = 1 ]; then\n"
    "        case \"$a\" in --) options=0; continue ;; -*) continue ;; esac\n"
    "    fi\n"
    "    resolved=$(/bin/busybox readlink -f -- \"$a\" 2>/dev/null)\n"
    "    for operand in \"$a\" \"$resolved\"; do\n"
    "    case \"$operand\" in\n"
    "        /tmp/sdcard|/tmp/sdcard/)\n"
    "            /bin/busybox rmdir /tmp/sdcard 2>/dev/null\n"
    "            log_bg \"rmdir-only: rm $*\" &\n"
    "            exit 0 ;;\n"
    "        /|/tmp|/tmp/|/tmp/sdcard/*)\n"
    "            log_bg \"blocked: rm $*\" &\n"
    "            exit 0 ;;\n"
    "    esac\n"
    "    done\n"
    "done\n"
    "exec /bin/busybox rm \"$@\"\n"
    "";
int rmguard_dir_ok(const char *dir){
    char p[256];
    int n = snprintf(p, sizeof p, "%s/rm", dir);
    if(n < 0 || n >= (int)sizeof p) return 0;
    int fd = open(p, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(fd < 0) return 0;
    struct stat sb;
    const size_t len = sizeof RMGUARD_SCRIPT - 1;
    int ok = fstat(fd, &sb) == 0 && S_ISREG(sb.st_mode) &&
             sb.st_size == (off_t)len && access(p, X_OK) == 0;
    size_t pos = 0;
    char buf[512];
    while(ok && pos < len){
        size_t want = len - pos;
        if(want > sizeof buf) want = sizeof buf;
        ssize_t got = read(fd, buf, want);
        if(got < 0 && errno == EINTR) continue;
        if(got <= 0 || memcmp(buf, RMGUARD_SCRIPT + pos, (size_t)got) != 0){ ok = 0; break; }
        pos += (size_t)got;
    }
    close(fd);
    return ok && pos == len;
}
static int rmguard_write_fallback(void){
    mkdir(RMGUARD_FALLBACK_PARENT, 0755); mkdir(RMGUARD_FALLBACK, 0755);
    const size_t len = sizeof RMGUARD_SCRIPT - 1;
    char tmp[256], dst[256];
    int nt = snprintf(tmp, sizeof tmp, "%s/.rm.XXXXXX", RMGUARD_FALLBACK);
    int nd = snprintf(dst, sizeof dst, "%s/rm", RMGUARD_FALLBACK);
    if(nt < 0 || nt >= (int)sizeof tmp || nd < 0 || nd >= (int)sizeof dst) return 0;
    int fd = mkstemp(tmp);
    if(fd < 0) return 0;
    int ok = fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
    size_t pos = 0;
    while(ok && pos < len){
        ssize_t w = write(fd, RMGUARD_SCRIPT + pos, len - pos);
        if(w < 0 && errno == EINTR) continue;
        if(w <= 0){ ok = 0; break; }
        pos += (size_t)w;
    }
    if(ok) ok = fchmod(fd, 0755) == 0 && fsync(fd) == 0;
    if(close(fd) != 0) ok = 0;
    if(!ok || rename(tmp, dst) != 0){ unlink(tmp); return 0; }
    int dfd = open(RMGUARD_FALLBACK, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if(dfd < 0) return 0;
    ok = fsync(dfd) == 0;
    close(dfd);
    return ok && rmguard_dir_ok(RMGUARD_FALLBACK);
}
int rmguard_ensure(void){
    const char *cur = getenv("PATH");
    if(!cur || !*cur) cur = "/bin:/sbin:/usr/bin:/usr/sbin";
    const char *gdir = NULL;
    if(rmguard_dir_ok(RMGUARD_ROOTFS)) gdir = RMGUARD_ROOTFS;
    else if(rmguard_dir_ok(RMGUARD_SYSTEM)) gdir = RMGUARD_SYSTEM;
    else if(rmguard_dir_ok(RMGUARD_FALLBACK) || rmguard_write_fallback()) gdir = RMGUARD_FALLBACK;
    if(!gdir) return 0;
    size_t glen = strlen(gdir);
    if(strncmp(cur, gdir, glen) == 0 && (cur[glen] == ':' || cur[glen] == '\0')) return 1;
    char np[1024]; int n = snprintf(np, sizeof np, "%s:%s", gdir, cur);
    if(n <= 0 || n >= (int)sizeof np) return 0;
    return setenv("PATH", np, 1) == 0;
}

const char *rmguard_script(size_t *len){
    if(len) *len = sizeof RMGUARD_SCRIPT - 1;
    return RMGUARD_SCRIPT;
}
