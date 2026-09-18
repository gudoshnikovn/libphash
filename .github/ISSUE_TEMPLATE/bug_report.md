---
name: Bug report
about: Something behaves incorrectly, crashes, or doesn't match documented behavior
title: ""
labels: bug
---

## What happened

<!-- What you did, what you expected, what actually happened. -->

## Build configuration

<!-- Required: a hash-output or decode discrepancy is frequently backend-specific, and
     "which decoder actually ran" is often the first thing that needs ruling in or out. -->

- Build system: <!-- CMake / Makefile -->
- Decoder backends compiled in: <!-- e.g. TurboJPEG + libpng + WebP; or stb_image only;
     if CMake, the relevant `PHASH_USE_*` values from your `cmake` invocation -->
- Platform: <!-- OS + architecture, e.g. Ubuntu 22.04 x86_64 / macOS 14 arm64 / Windows 11 x64 -->
- Compiler and version: <!-- e.g. clang 17, gcc 13, MSVC 19.38 -->
- libphash version/commit: <!-- `ph_version()` output, or the commit hash if built from source -->

## Minimal repro

<!-- The smallest input and API call sequence that reproduces this. Attach the input
     image if the bug is decode- or hash-output-related and the image can be shared —
     without it, a decoder-specific bug is usually not reproducible by anyone else. -->

## Relevant output

<!-- ph_get_last_error_message() output, a stack trace, ASan/UBSan/TSan output, etc. -->
