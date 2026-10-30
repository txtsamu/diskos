#include <stdio.h>
#include <string.h>
#include "ipc.h"
static int fails;
#define T(f,exp) do{ if(ipc_frame_is_dangerous(f)!=(exp)){ printf("FAIL %s\n",(f)?(f):"NULL"); fails++; } }while(0)
int main(void){
    T("0800000C0000",1); T("0621000C0000",1); T("0724000C0002",1); T("0724000C0001",0);
    T("0722000C0002",1); T("0720000C0001",1); T("0720000C0000",0);
    T("0715000C0032",0); T("0201000C0000",0); T("0806000C0000",0);
    T("0800",1); T("",0); T("08",0); T("080",0); T(NULL,0); T("0724",1);
    T("0A00000C0000",0); T("0a01000C0000",0);
    printf(fails?"FAILED\n":"ok\n"); return fails!=0;
}
