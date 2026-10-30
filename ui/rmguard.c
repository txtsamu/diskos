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

/* ---- stock-player launcher prep + SD guard ---------------------------------
 * The stock mount routine ignores failed unmounts before recursive cleanup.
 * Rebuilt images guard /bin/rm itself, plus the first PATH entry, including stock
 * fallback/watchdog launches. Hand-deploys can establish a verified NAND fallback.
 * The launcher verifies protection and resets/reads back persisted local mode
 * itself before exec; a failure is published for the UI and never holds the launcher (launcher.c).
 */
/* Where the guard lives, in order of preference. Overridable so a host test can point them at a temp dir. */
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
    "# diskOS SD guard - installed read-only at /opt/diskos/bin/rm and put FIRST in PATH by the boot hook.\n"
    "#\n"
    "# The stock player prepares the card with \"umount /tmp/sdcard\" followed by \"rm -rf /tmp/sdcard\" and never\n"
    "# checks whether the umount succeeded. When the card is busy (still mounted) the delete runs on the\n"
    "# mounted card and empties it. This wrapper refuses any rm whose operand is the card mountpoint or\n"
    "# anything under it; the exact mountpoint gets the stock's legitimate \"remove a stale empty dir\" via a\n"
    "# NON-recursive rmdir (EBUSY while mounted, fine when empty and unmounted). Everything else is passed to\n"
    "# the real rm unchanged. The decision is made before any logging; logging is best-effort and can never\n"
    "# fall through to the real rm. Canonical paths also cover relative paths, repeated slashes,\n"
    "# dot components and symlink aliases. This is a guard for rm, not a block-device write filter.\n"
    "# Logging is evidence, not a prerequisite: it writes to NAND, and the caller here is the stock player\n"
    "# performing its mount cleanup, so a stalled write would stall THAT. Detached and size-capped, it can\n"
    "# never delay the decision above, which has already been made by the time this runs.\n"
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
    /* An executable called rm may be the real destructive command. Verify the complete
     * wrapper, including existing fallback copies; a header alone is not sufficient. */
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
/* Publish the fallback guard on /usr/data (tmp + fsync + rename) and VERIFY it (exact content + exec bit). */
static int rmguard_write_fallback(void){
    mkdir(RMGUARD_FALLBACK_PARENT, 0755); mkdir(RMGUARD_FALLBACK, 0755);
    const size_t len = sizeof RMGUARD_SCRIPT - 1;
    char tmp[256], dst[256];
    int nt = snprintf(tmp, sizeof tmp, "%s/.rm.XXXXXX", RMGUARD_FALLBACK);
    int nd = snprintf(dst, sizeof dst, "%s/rm", RMGUARD_FALLBACK);
    if(nt < 0 || nt >= (int)sizeof tmp || nd < 0 || nd >= (int)sizeof dst) return 0;
    /* UI startup and the player launcher may publish concurrently. Each owns its
     * temporary file, then atomically replaces the same verified destination. */
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
/* Ensure an executable guard exists and PATH starts with its directory. Returns 1 if guarded, 0 if NO
 * guard could be established (caller decides the fail policy). Idempotent; safe to call from the UI and
 * from the launcher. Only mutates PATH of THIS process (and, for the launcher, the exec'd player). */
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
