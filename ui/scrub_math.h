/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISKOS_SCRUB_MATH_H
#define DISKOS_SCRUB_MATH_H
#include <math.h>
/* Positive angles are clockwise in display coordinates (y points downward). */
static inline float scrub_degrees_at(int x,int y){return atan2f((float)(y-180),(float)(x-180))*57.29578f;}
static inline int scrub_angle_at(int x,int y){int a=(int)lroundf(scrub_degrees_at(x,y)*10);return (a+3600)%3600;}
static inline int scrub_angle_delta(int now,int before){int d=now-before;if(d>1800)d-=3600;if(d< -1800)d+=3600;return d;}
static inline int scrub_arc_fraction(int x,int y,int start,int span){
    int rel=(int)lroundf(scrub_degrees_at(x,y))-start;
    while(rel<0)rel+=360;while(rel>=360)rel-=360;
    if(rel>span)return rel<span+(360-span)/2?1000:0;
    return rel*1000/span;
}
#endif
