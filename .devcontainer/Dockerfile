# Custom build image for re-twist-tt2.
# Base: debian bookworm-slim. Pre-installs g++ 12, cmake, make, gdb.
# Used by `podman run --rm -v $PWD:/src:rw tt2-builder:bookworm ...`.
FROM docker.io/library/debian:bookworm-slim

RUN apt-get update -qq \
 && apt-get install -y -qq --no-install-recommends \
        g++ make cmake gdb ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
