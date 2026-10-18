/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The artwork decoder (tools/diskos_artdec.c) linked into mq_ui and run in the art worker thread instead of as a
 * separate diskos-artdec process: same decoding, limits and outputs, no fork. See artdec_inproc() there. */
#define ARTDEC_INPROC 1
#include "tools/diskos_artdec.c"

/* Fixed-output inflate uses no decoder allocation-budget globals: safe alongside
 * artwork decoding. Resource dimensions/size are checked by the caller. */
int ui_resource_inflate(void *out,int bytes,const unsigned char *src,int len){
    return stbi_zlib_decode_buffer((char *)out,bytes,(const char *)src,len)==bytes?0:-1;
}
