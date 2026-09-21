# Contributing to libphash

Thanks for considering a contribution. This document covers the practical parts —
getting a build running, what a pull request needs before it can be reviewed, and the
handful of rules that keep the public API stable across releases. For the reasoning
behind the build system, the test strategy, and the sanitizer/coverage setup, see
[`docs/development.md`](docs/development.md); this file assumes that context and does
not repeat it.

## Getting started

```bash
git clone https://github.com/gudoshnikovn/libphash.git
cd libphash
git submodule update --init --recursive   # only needed for the CMake (vendored) build
```

Two build systems exist and are not interchangeable — see
[`docs/development.md`](docs/development.md#build-systems-and-their-defaults) for the
full comparison:

```bash
# CMake -- vendored SIMD decoders (libjpeg-turbo, libpng/spng, libwebp, zlib-ng)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Makefile -- portable/minimal build, zero external dependencies (stb_image only)
make -j8
make test
```

## Before opening a pull request

Every one of these has to be run and green, on the affected build system(s) at
minimum:

```bash
make -j8 && make test     # portable build, every tests/src/test_*.c binary
make format                # clang-format -i; the diff after this must be empty
make debug && make test    # -fsanitize=address,undefined rebuild, then rerun the suite
```

If your change touches `CMakeLists.txt`, a native decoder backend
(`src/loaders/*.c`), or the batch thread pool (`src/batch.c`), also run the CMake
Release build and `ctest`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

CI runs a wider matrix than any of the above on its own (multiple platforms and
architectures, TSan, a 32-bit leg, a format check, fuzzing) — it is the actual gate; the
commands above are what let you find CI's problems before CI does.

### Fuzzing

The `fuzz_load` harness (`tests/fuzz/fuzz_load.c`) drives `ph_decode_buffer()` under
libFuzzer. It requires Clang (libFuzzer is a compiler-rt feature GCC does not ship):

```bash
cmake -S . -B build -DPHASH_BUILD_FUZZERS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang
cmake --build build --target fuzz_load
./build/fuzz_load -max_total_time=90 -dict=tests/fuzz/magic.dict tests/data
```

Every PR gets a short (90s) fuzzing session against `tests/data` in CI automatically;
you do not need to run a long session locally unless you are chasing something the
short one already flagged.

### Coverage

```bash
make coverage
open docs/coverage/html/index.html   # or just point a browser at the file
```

`make coverage` is a `make clean && make test PHASH_COVERAGE=1` under the hood, so it
only covers the portable (stb_image) build's code paths — the native decoder backends
are excluded. See `docs/development.md`'s "Two coverage targets, and why one isn't
enough" section for `make coverage-cmake`, which covers those instead.

## Commit and PR conventions

- Commit messages: short imperative summary line, in English, with a body when the
  *why* isn't obvious from the diff alone. No fixed prefix format is enforced, but
  `fix:`/`feat:`/`test:`/`docs:`/`ci:`/`refactor:` prefixes are the norm in this
  repository's history — match it.
- A change that affects what a consumer of the library observes (new API, changed
  behavior, a fixed bug, a changed default) needs a line in `CHANGELOG.md` under
  `[Unreleased]`, in the appropriate `Added`/`Changed`/`Fixed`/`Security` section, or —
  for anything that breaks existing callers — under `BREAKING CHANGES`, with either a
  one-line "how to restore the old behaviour" or a link to the relevant section of
  [`MIGRATION.md`](MIGRATION.md). A change with no consumer-visible effect (internal
  refactor, test-only, CI-only, comment/doc fix) does not need one.
- Pull requests target `main`.
- Keep a PR to one logical change. A bug fix does not need to carry an unrelated
  cleanup along with it, even in the same file.
- Use the PR template's checklist; it exists so a reviewer does not have to
  reconstruct which of the gates above you actually ran.

## Public API stability

`include/libphash.h` is the entire public surface; everything under `src/` is internal
and can change shape freely between releases. Two rules apply specifically to that
header, because they affect binary compatibility for anyone linking a prebuilt
`libphash` or generating FFI bindings against the header mechanically:

- **Error codes are append-only.** `ph_error_t`'s values are ABI: FFI bindings map the
  integers to their own exception types. A new code is added at the end of the
  enumeration with the next free negative value; an existing value is never
  renumbered or reused, even after the code it named is removed (see the retired `-2`
  in the header for the pattern to follow, and `tests/src/test_error_diagnostics.c` for
  the test that pins every code to a description and fails if one goes missing).
- **This project follows semantic versioning** for `include/libphash.h`: a
  source-or-binary-incompatible change (a removed/renamed public symbol, a changed
  function signature, a struct layout change, a default that changes existing hash
  output) is a major version bump and belongs in `BREAKING CHANGES` in
  `CHANGELOG.md`, never silently released as a minor or patch. If you are not sure
  whether your change qualifies, open an issue before writing the code.

## Reporting a security issue

Do not open a public issue or pull request for a security vulnerability (e.g. a crash
or memory-safety issue reachable from `ph_decode_buffer()`/`ph_load_from_*()` on
attacker-controlled input). See [`SECURITY.md`](SECURITY.md) for the reporting channel
and disclosure timeline.

## License

By contributing, you agree that your contribution is licensed under this project's
[MIT license](LICENSE).
