# libphash Technical Documentation

Welcome to the internal technical documentation for `libphash`. This directory contains detailed information about the library's design, development process, and algorithmic implementations.

## Navigation

- [**Architecture Overview**](architecture.md)
  System design, module breakdown, and core data structures.
- [**Development Guide**](development.md)
  Build instructions, testing strategies, coding standards, and naming conventions.
- [**Algorithms Depth**](algorithms.md)
  What each of the nine hashes computes, how to tune it, and what it is good for.
- [**Algorithm Provenance**](algorithm-provenance.md)
  Where each algorithm comes from, what its source specifies against what this code
  does, every known divergence, and the methodology this project verifies against.
- [**References**](references.md)
  The bibliography: full citations and links for every source the algorithms rest on,
  with how far each one can be trusted and whether it was read directly.

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
