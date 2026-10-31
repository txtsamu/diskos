/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "coverart.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CBMP COVERART_BMP

#pragma GCC push_options
#pragma GCC optimize ("O0")

static float g_lin_lut[256]; static pthread_once_t g_lin_once = PTHREAD_ONCE_INIT;
static void lin_lut_init(void){
    for(int i=0;i<256;i++){ float x=i/255.0f; g_lin_lut[i]= x<=0.04045f ? x/12.92f : powf((x+0.055f)/1.055f, 2.4f); }
}
static float a_clamp01(float x){ return x<0?0:(x>1?1:x); }
static float a_smooth(float a,float b,float x){ if(b<=a) return x>=b?1.0f:0.0f; float t=a_clamp01((x-a)/(b-a)); return t*t*(3.0f-2.0f*t); }
static void rgb2oklab(int R,int G,int B, float *L,float *a,float *bb){
    float r=g_lin_lut[R], g=g_lin_lut[G], bl=g_lin_lut[B];
    float l=0.4122214708f*r+0.5363325363f*g+0.0514459929f*bl;
    float m=0.2119034982f*r+0.6806995451f*g+0.1073969566f*bl;
    float s=0.0883024619f*r+0.2817188376f*g+0.6299787005f*bl;
    float l_=cbrtf(l), m_=cbrtf(m), s_=cbrtf(s);
    *L =0.2104542553f*l_+0.7936177850f*m_-0.0040720468f*s_;
    *a =1.9779984951f*l_-2.4285922050f*m_+0.4505937099f*s_;
    *bb=0.0259040371f*l_+0.7827717662f*m_-0.8086757660f*s_;
}
static int oklab2rgb(float L,float a,float b, int *R,int *G,int *B){
    float l_=L+0.3963377774f*a+0.2158037573f*b;
    float m_=L-0.1055613458f*a-0.0638541728f*b;
    float s_=L-0.0894841775f*a-1.2914855480f*b;
    float l=l_*l_*l_, m=m_*m_*m_, s=s_*s_*s_;
    float lin[3];
    lin[0]= 4.0767416621f*l-3.3077115913f*m+0.2309699292f*s;
    lin[1]=-1.2684380046f*l+2.6097574011f*m-0.3413193965f*s;
    lin[2]=-0.0041960863f*l-0.7034186147f*m+1.7076147010f*s;
    int in = 1; int *o[3]={R,G,B};
    for(int i=0;i<3;i++){
        float x=lin[i];
        if(x<-0.001f||x>1.001f) in=0;
        if(x<0)x=0; if(x>1)x=1;
        float v = x<=0.0031308f ? 12.92f*x : 1.055f*powf(x,1.0f/2.4f)-0.055f;
        *o[i] = (int)(v*255.0f+0.5f);
    }
    return in;
}
static float wcag_lum(int R,int G,int B){ return 0.2126f*g_lin_lut[R]+0.7152f*g_lin_lut[G]+0.0722f*g_lin_lut[B]; }

int coverart_read_bmp(const char *path, uint8_t *buf)
{
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    uint8_t hdr[54];
    if(fread(hdr,1,54,f)!=54){ fclose(f); return -1; }
    uint32_t off = hdr[10]|(hdr[11]<<8)|(hdr[12]<<16)|((uint32_t)hdr[13]<<24);
    int w = hdr[18]|(hdr[19]<<8), h = hdr[22]|(hdr[23]<<8);
    int bpp = hdr[28]|(hdr[29]<<8);
    uint32_t comp = hdr[30]|(hdr[31]<<8)|(hdr[32]<<16)|((uint32_t)hdr[33]<<24);
    if(w!=CBMP || h!=CBMP || bpp!=24 || comp!=0){ fclose(f); return -1; }
    uint8_t row[CBMP*3];
    if(fseek(f, off, SEEK_SET)!=0){ fclose(f); return -1; }
    for(int yy=0; yy<CBMP; yy++){
        if(fread(row,1,CBMP*3,f)!=(size_t)(CBMP*3)){ fclose(f); return -1; }
        uint8_t *d = buf + (CBMP-1-yy)*CBMP*4;
        for(int x=0;x<CBMP;x++){
            d[x*4+0]=row[x*3+0]; d[x*4+1]=row[x*3+1];
            d[x*4+2]=row[x*3+2]; d[x*4+3]=0xFF;
        }
    }
    fclose(f);
    return 0;
}

int coverart_accent_from_bgra(const uint8_t *cbuf, uint32_t *out_rgb)
{
    if(!cbuf) return 0;
    pthread_once(&g_lin_once, lin_lut_init);
    float *pL=malloc(2025*sizeof(float)), *pA=malloc(2025*sizeof(float)),
          *pB=malloc(2025*sizeof(float)), *pW=malloc(2025*sizeof(float));
    if(!pL||!pA||!pB||!pW){ free(pL);free(pA);free(pB);free(pW); return 0; }
    int rc = 0;
    int n=0; float wsum=0,csum=0;
    for(int y=8;y<140;y+=3){
        const uint8_t *rp=cbuf+(size_t)y*CBMP*4;
        for(int x=8;x<140 && n<2025;x+=3){
            const uint8_t *p=rp+x*4; int B=p[0],G=p[1],R=p[2];
            float L,a,b; rgb2oklab(R,G,B,&L,&a,&b);
            float C=sqrtf(a*a+b*b), w=1.0f;
            if(C<0.025f||L<0.08f||L>0.96f) w=0.35f;
            pL[n]=L;pA[n]=a;pB[n]=b;pW[n]=w; wsum+=w; csum+=C*w; n++;
        }
    }
    if(n<32) goto done;
    if(wsum>0 && csum/wsum < 0.025f){ *out_rgb=0xAEB4BE; rc=1; goto done; }

    { const int K=8; float cL[8],cA[8],cB[8],pop[8];
    { int bi=0; float bc=-1; for(int i=0;i<n;i++){ float c=pA[i]*pA[i]+pB[i]*pB[i]; if(c>bc){bc=c;bi=i;} }
      cL[0]=pL[bi];cA[0]=pA[bi];cB[0]=pB[bi]; }
    for(int k=1;k<K;k++){ int bi=0; float bd=-1;
        for(int i=0;i<n;i++){ float md=1e9f;
            for(int j=0;j<k;j++){ float dL=pL[i]-cL[j],da=pA[i]-cA[j],db=pB[i]-cB[j]; float d=1.25f*dL*dL+da*da+db*db; if(d<md)md=d; }
            if(md>bd){bd=md;bi=i;} }
        cL[k]=pL[bi];cA[k]=pA[bi];cB[k]=pB[bi]; }
    for(int it=0;it<10;it++){
        float sL[8]={0},sA[8]={0},sB[8]={0},sw[8]={0};
        for(int i=0;i<n;i++){ int bj=0; float bd=1e9f;
            for(int j=0;j<K;j++){ float dL=pL[i]-cL[j],da=pA[i]-cA[j],db=pB[i]-cB[j]; float d=1.25f*dL*dL+da*da+db*db; if(d<bd){bd=d;bj=j;} }
            float w=pW[i]; sL[bj]+=pL[i]*w; sA[bj]+=pA[i]*w; sB[bj]+=pB[i]*w; sw[bj]+=w; }
        for(int j=0;j<K;j++){ if(sw[j]>0){cL[j]=sL[j]/sw[j];cA[j]=sA[j]/sw[j];cB[j]=sB[j]/sw[j];} pop[j]=sw[j]; }
    }
    float best=-1; int bk=-1;
    for(int j=0;j<K;j++){
        float L=cL[j],a=cA[j],b=cB[j]; float C=sqrtf(a*a+b*b);
        float pf = wsum>0 ? pop[j]/wsum : 0;
        if(pf<0.008f||C<0.035f||L<0.18f||L>0.92f) continue;
        float popS=sqrtf(pf);
        float lS=1.0f-a_clamp01(fabsf(L-0.72f)/0.32f);
        float cS=1.0f-a_clamp01(fabsf(C-0.16f)/0.14f);
        float vivid=a_smooth(0.07f,0.18f,C);
        float darkP=a_smooth(0.18f,0.38f,L);
        float lightP=1.0f-a_smooth(0.88f,0.98f,L);
        float sc=(0.42f*popS+0.28f*cS+0.22f*lS+0.08f*vivid)*darkP*lightP;
        if(pf<0.02f && C<0.12f) sc*=0.5f;
        if(sc>best){best=sc;bk=j;}
    }
    if(bk<0){ *out_rgb=0xAEB4BE; rc=1; goto done; }

    float L=cL[bk],a=cA[bk],b=cB[bk]; float C=sqrtf(a*a+b*b), H=atan2f(b,a);
    if(L<0.58f)L=0.58f; else if(L>0.84f)L=0.84f;
    if(C<0.075f)C=0.075f; else if(C>0.22f)C=0.22f;
    if(C>0.18f&&L>0.72f) C*=0.92f;
    if(C<0.10f) C=C+(0.10f-C)*0.35f;
    a=C*cosf(H); b=C*sinf(H);
    float Ybg=wcag_lum(0x1C,0x1C,0x1E); int R=0,G=0,Bo=0;
    for(int iter=0;iter<48;iter++){
        a=C*cosf(H); b=C*sinf(H);
        int in=oklab2rgb(L,a,b,&R,&G,&Bo);
        if(!in && C>0.04f){ C*=0.94f; continue; }
        float Y=wcag_lum(R,G,Bo); float hi=Y>Ybg?Y:Ybg, lo=Y>Ybg?Ybg:Y;
        if((hi+0.05f)/(lo+0.05f) >= 4.5f || L>=0.88f) break;
        L+=0.015f;
    }
    a=C*cosf(H); b=C*sinf(H); oklab2rgb(L,a,b,&R,&G,&Bo);
    *out_rgb = ((uint32_t)R<<16)|((uint32_t)G<<8)|(uint32_t)Bo;
    rc=1;
    }
done:
    free(pL);free(pA);free(pB);free(pW);
    return rc;
}
#pragma GCC pop_options

int coverart_accent_from_bmp(const char *path, uint32_t *rgb)
{
    uint8_t *buf = malloc((size_t)CBMP * CBMP * 4);
    if(!buf) return 0;
    int rc = coverart_read_bmp(path, buf) == 0 && coverart_accent_from_bgra(buf, rgb);
    free(buf);
    return rc;
}
