# Native linux/arm64 dev environment for libphash.
#
# Built WITHOUT --platform so it runs natively on Apple Silicon (no QEMU emulation).
# This gives a real Linux/GCC/glibc environment for functional testing — it is NOT a
# substitute for the Linux/x86_64 CI matrix and must never be used to measure
# performance (e.g. benchmarks): --platform linux/amd64 on this
# image would run under QEMU emulation, whose overhead dwarfs any real perf difference.
#
# debian:bookworm-slim (not ubuntu:24.04) — same glibc family as the CI's ubuntu-latest
# runners, so it still catches glibc/GCC-specific bugs, but without Ubuntu's much larger
# default package set. Kept deliberately minimal: build-essential (gcc + make) for the
# normal build, clang for the `-fsanitize=fuzzer`/ASan builds that Apple Clang can't do
# locally. No lcov/clang-format/nasm/system-decoder-dev-packages — none
# of those are needed for the vendored CMake build this image exists to run.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    cmake \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN useradd --create-home --shell /bin/bash dev

WORKDIR /workspace

# Submodules must already be checked out on the host (`git submodule update --init
# --recursive`, see README.md) — .dockerignore excludes .git, so this image has no git
# metadata to init them itself. COPY just picks up whatever vendor/ already contains.
COPY --chown=dev:dev . /workspace

USER dev

CMD ["/bin/bash"]
