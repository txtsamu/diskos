/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Optional block of the stock player's FiiO update/version lookups. See phonehome.h.
 *
 * Mechanism: /etc/hosts lives on the read-only squashfs rootfs, so a private copy (stock lines + one line mapping the
 * two FiiO check hosts to loopback) is written under /tmp and bind-mounted over it. The player's wget then gets
 * "connection refused" at once. Nothing on flash changes; the mount is gone after a reboot and is re-made at UI start.
 * Hosts (from the player's util/src/ota_update.c: ota_site in /etc/ota_info, and the Bluetooth OTA server):
 *   discpick.fiio.net          ota_patch_user.json (firmware update list), VER_DRTAILS/log_*.txt (release notes)
 *   fiio-bluetooth.fiio.net    <product>/version.txt (Bluetooth firmware version) */
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

/* Remove every bind mount on /etc/hosts (ours, possibly stacked by a repeated call). Bounded loop. */
static void unbind_hosts(void){
    for(int i = 0; i < 4 && hosts_is_bound(); i++)
        if(umount(HOSTS_PATH) != 0) break;
}

static int build_copy(void){
    FILE *in = fopen(HOSTS_PATH, "r");       /* call only with our mount removed: this is the stock file */
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
    /* Always start from the stock file: drop our mount first, so a re-apply rebuilds from the real /etc/hosts and
     * turning the setting off can never leave a stale copy in place. */
    unbind_hosts();
    if(!want){ unlink(COPY_PATH); return; }
    if(build_copy() != 0){ fprintf(stderr, "phonehome: cannot write %s: %s\n", COPY_PATH, strerror(errno)); return; }
    if(mount(COPY_PATH, HOSTS_PATH, NULL, MS_BIND, NULL) != 0)
        fprintf(stderr, "phonehome: bind mount failed: %s\n", strerror(errno));
}
