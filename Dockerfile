# Native linux/arm64 dev environment for libphash (task 17, tasks/17_docker_dev_environment.md).
#
# Built WITHOUT --platform so it runs natively on Apple Silicon (no QEMU emulation).
# This gives a real Linux/GCC/glibc environment for functional testing — it is NOT a
# substitute for the Linux/x86_64 CI matrix (task 7) and must never be used to measure
# performance (e.g. the zlib-ng benchmark from task 6): --platform linux/amd64 on this
# image would run under QEMU emulation, whose overhead dwarfs any real perf difference.
#
# debian:bookworm-slim (not ubuntu:24.04) — same glibc family as the CI's ubuntu-latest
# runners, so it still catches glibc/GCC-specific bugs, but without Ubuntu's much larger
# default package set. Kept deliberately minimal: build-essential (gcc + make) for the
# normal build, clang for the `-fsanitize=fuzzer`/ASAN checks from task 2 that Apple
# Clang can't do locally. No lcov/clang-format/nasm/system-decoder-dev-packages — none
# of those are needed for the vendored CMake build this image exists to run.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    cmake \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Submodules must already be checked out on the host (`git submodule update --init
# --recursive`, per CLAUDE.md) — .dockerignore excludes .git, so this image has no git
# metadata to init them itself. COPY just picks up whatever vendor/ already contains.
COPY . /workspace

CMD ["/bin/bash"]
