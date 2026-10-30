TARGET = mipsel-linux-musl
OUTPUT = /opt/mipsel-n2008-musl-cross
GCC_VER = 11.2.0
MUSL_VER = 1.2.6
BINUTILS_VER = 2.44
GCC_CONFIG += --with-arch=mips32r2 --with-nan=2008 --with-fp-32=64 --with-odd-spreg-32
GCC_CONFIG += --disable-libquadmath --disable-libsanitizer --enable-languages=c
COMMON_CONFIG += MAKEINFO=true
