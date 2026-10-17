/* Native check: optimized Squash upscale must match generic interpolation exactly. */
#define main artdec_cli_main
#include "../tools/diskos_artdec.c"
#undef main
#include <assert.h>
#include <time.h>
static unsigned char fixture_rgb[120*120*3];
int main(void){
 unsigned seed=1;
 for(int sample=0;sample<12;sample++){
  for(size_t i=0;i<sizeof fixture_rgb;i++){
   seed=seed*1664525u+1013904223u;
   fixture_rgb[i]=sample==0?0:sample==1?255:sample==2?(unsigned char)(i%256):(unsigned char)(seed>>24);
  }
  uint16_t *reference=resample(fixture_rgb,120,120,360,0),*fast=soft_upscale3(fixture_rgb);
  assert(reference&&fast&&!memcmp(reference,fast,sizeof(uint16_t)*360*360*3));bfree(reference);bfree(fast);
 }
 struct timespec t0,t1,t2;clock_gettime(CLOCK_MONOTONIC,&t0);
 for(int i=0;i<30;i++){uint16_t*p=resample(fixture_rgb,120,120,360,0);assert(p);bfree(p);}
 clock_gettime(CLOCK_MONOTONIC,&t1);
 for(int i=0;i<30;i++){uint16_t*p=soft_upscale3(fixture_rgb);assert(p);bfree(p);}
 clock_gettime(CLOCK_MONOTONIC,&t2);
 double old=(t1.tv_sec-t0.tv_sec)*1000.+(t1.tv_nsec-t0.tv_nsec)/1e6,fast=(t2.tv_sec-t1.tv_sec)*1000.+(t2.tv_nsec-t1.tv_nsec)/1e6;
 printf("PASS: 12 exact pixel comparisons; native 30-run upscale timing generic=%.2f ms optimized=%.2f ms (%.2fx). Not a device benchmark.\n",old,fast,old/fast);
 return 0;
}
