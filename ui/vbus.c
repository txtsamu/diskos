/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "vbus.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <sys/ioctl.h>

#define I2C_SLAVE_FORCE_ 0x0706
#ifndef VBUS_CHG_BUS
#define VBUS_CHG_BUS "/dev/i2c-2"
#endif
#ifndef VBUS_UDC_STATE_GLOB
#define VBUS_UDC_STATE_GLOB "/sys/class/udc/*/state"
#endif
#define CHG_ADDR 0x1a
#define CHG_REG_STATUS 0x08

int vbus_stat(void){
    int fd = open(VBUS_CHG_BUS, O_RDWR | O_CLOEXEC);
    if(fd < 0) return -1;
    unsigned char reg = CHG_REG_STATUS, val = 0;
    int ok = ioctl(fd, I2C_SLAVE_FORCE_, CHG_ADDR) == 0 && write(fd, &reg, 1) == 1 && read(fd, &val, 1) == 1;
    close(fd);
    return ok ? (val >> 5) & 7 : -1;
}
int vbus_stat_external(int stat){ return stat > VBUS_STAT_NONE && stat < VBUS_STAT_OTG; }
int vbus_present(void){
    int s = vbus_stat();
    return s < 0 ? -1 : s != VBUS_STAT_NONE;
}
int vbus_external_input(void){
    int s = vbus_stat();
    return s < 0 ? -1 : vbus_stat_external(s);
}
int vbus_plain_charger(void){
    glob_t g; int plain = 1;
    if(glob(VBUS_UDC_STATE_GLOB, 0, NULL, &g) != 0) return 1;
    for(size_t i = 0; i < g.gl_pathc; i++){
        FILE *f = fopen(g.gl_pathv[i], "r"); char s[32] = "";
        if(!f) continue;
        if(fgets(s, sizeof s, f) && strncmp(s, "not attached", 12) != 0) plain = 0;
        fclose(f);
    }
    globfree(&g);
    return plain;
}
