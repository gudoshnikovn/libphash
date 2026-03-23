# libphash Technical Documentation

Welcome to the internal technical documentation for `libphash`. This directory contains detailed information about the library's design, development process, and algorithmic implementations.

## Navigation

- [**Architecture Overview**](architecture.md)
  System design, module breakdown, and core data structures.
- [**Development Guide**](development.md)
  Build instructions, testing strategies, coding standards, and naming conventions.
- [**Algorithms Depth**](algorithms.md)
  Technical details on Hamming distance, pHash, aHash, dHash, and color-aware hashing.

---

## Quick Build Reference

```bash
# Using standard Makefile
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
make test

# Using CMake (recommended for cross-platform)
mkdir -p build && cd build
cmake ..
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
ctest
```
