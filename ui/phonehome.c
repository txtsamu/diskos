/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "phonehome.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define HOSTS_PATH  "/etc/hosts"
#define COPY_PATH   "/tmp/diskos-hosts"
#define MARK        "# diskOS: block FiiO update checks"
#define BLOCK_LINE  "127.0.0.1\tdiscpick.fiio.net fiio-bluetooth.fiio.net " MARK "\n" \
                    "::1\tdiscpick.fiio.net fiio-bluetooth.fiio.net " MARK "\n"

static int hosts_is_bound(void){
    FILE *f = fopen("/proc/mounts", "r");
    if(!f) return 0;
    char ln[512]; int found = 0;
    while(fgets(ln, sizeof ln, f)){
        char dev[256], mp[256];
        if(sscanf(ln, "%255s %255s", dev, mp) == 2 && !strcmp(mp, HOSTS_PATH)){ found = 1; break; }
    }
    fclose(f);
    return found;
}

static void unbind_hosts(void){
    for(int i = 0; i < 4 && hosts_is_bound(); i++)
        if(umount(HOSTS_PATH) != 0) break;
}

static int build_copy(void){
    FILE *in = fopen(HOSTS_PATH, "r");
    char tmp[] = COPY_PATH ".tmp";
    FILE *out = fopen(tmp, "w");
    if(!out){ if(in) fclose(in); return -1; }
    char ln[1024];
    if(in){
        while(fgets(ln, sizeof ln, in)){
            if(strstr(ln, MARK)) continue;
            fputs(ln, out);
            if(!strchr(ln, '\n')) fputc('\n', out);
        }
        fclose(in);
    } else {
        fputs("127.0.0.1\tlocalhost\n", out);
    }
    fputs(BLOCK_LINE, out);
    int bad = ferror(out);
    if(fclose(out) != 0) bad = 1;
    if(bad || chmod(tmp, 0644) != 0 || rename(tmp, COPY_PATH) != 0){ unlink(tmp); return -1; }
    return 0;
}

void phonehome_apply(void){
    int want = cfg_get_int("block_fiio_checks", 0) ? 1 : 0;
    unbind_hosts();
    if(!want){ unlink(COPY_PATH); return; }
    if(build_copy() != 0){ fprintf(stderr, "phonehome: cannot write %s: %s\n", COPY_PATH, strerror(errno)); return; }
    if(mount(COPY_PATH, HOSTS_PATH, NULL, MS_BIND, NULL) != 0)
        fprintf(stderr, "phonehome: bind mount failed: %s\n", strerror(errno));
}
