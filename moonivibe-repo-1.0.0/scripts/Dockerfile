# Cross-compile toolchain for aarch64 (Ableton Move).
# debian:bookworm-slim matches the Move's glibc closely enough for a module
# built against it to load.
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
      gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu libc6-dev-arm64-cross make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /w
