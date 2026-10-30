FROM debian:12-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates git wget xz-utils bzip2 patch rsync file \
    && rm -rf /var/lib/apt/lists/*

ARG MCM_URL=https://github.com/richfelker/musl-cross-make.git
ARG MCM_COMMIT=227df8b99103f9c59f6570babf892978e293082f
COPY toolchain/mcm-config.mak /tmp/mcm-config.mak
RUN git clone "$MCM_URL" /tmp/mcm \
    && cd /tmp/mcm && git checkout --detach "$MCM_COMMIT" \
    && test "$(git rev-parse HEAD)" = "$MCM_COMMIT" \
    && cp /tmp/mcm-config.mak config.mak \
    && make -j"$(nproc)" && make install \
    && cd / && rm -rf /tmp/mcm /tmp/mcm-config.mak
ENV CROSS=/opt/mipsel-n2008-musl-cross/bin/mipsel-linux-musl-

WORKDIR /src
CMD ["sh", "-c", "test -f Makefile || { echo 'mount the ui/ source at /src (see the Dockerfile header)'; exit 1; }; make CROSS=$CROSS && ls -l mq_ui"]
