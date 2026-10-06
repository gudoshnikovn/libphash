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
# CMake -- vendored SIMD decoders (libjpeg-turbo, libpng, libwebp, zlib-ng)
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
make -j8 && make test -j8      # portable build, every tests/src/test_*.c binary
make format                    # clang-format 23 -i; the diff after this must be empty
make debug && make test -j8    # -fsanitize=address,undefined rebuild, then rerun the suite
```

`make test -j8` runs the tests side by side, each test's output in `test_<name>.log`,
printed when it fails; without `-j` they run one at a time.

`make format` requires clang-format **23** and refuses to run with any other major
version, because a different major formats the same code differently. Install the
pinned version with `pip install clang-format==23.1.1`.

The tree was reformatted in one commit; to see who wrote a line rather than who
reindented it, run `git config blame.ignoreRevsFile .git-blame-ignore-revs` once.

If your change touches `CMakeLists.txt`, a native decoder backend
(`src/loaders/*.c`), or the batch thread pool (`src/batch.c`), also run the CMake
Release build and `ctest`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build -j8 --output-on-failure
```

CI runs a wider matrix than any of the above on its own (multiple platforms and
architectures, TSan, a 32-bit leg, a format check, fuzzing) — it is the actual gate; the
commands above are what let you find CI's problems before CI does.

### If `test_golden_hashes` fails on your machine

`test_golden_hashes` compares every hash with a committed value, exactly. If it fails on
a platform or compiler the CI matrix does not cover, and you did not touch an algorithm,
that is a finding about the library, not a mistake on your side: open an issue with the
test's output and `ph_get_build_info()`'s line (`./build/test_build_info` prints it).
Please do not send a regenerated golden file — the values are the same on every platform
by design, so a different one points at a portability bug to fix, and the procedure is in
`docs/development.md`, "Golden hashes".

### Fuzzing

The `fuzz_load` harness (`tests/fuzz/fuzz_load.c`) feeds arbitrary bytes to
`ph_load_from_memory()` and hashes whatever decodes, under libFuzzer. It requires Clang
(libFuzzer is a compiler-rt feature GCC does not ship):

```bash
cmake -S . -B build-fuzz -DPHASH_BUILD_FUZZERS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang
cmake --build build-fuzz --target fuzz_load -j
mkdir -p tests/fuzz/corpus
./build-fuzz/fuzz_load -max_total_time=90 -max_len=65536 -dict=tests/fuzz/magic.dict \
    tests/fuzz/corpus tests/fuzz/seeds
```

Every PR gets a short (90s) fuzzing session in CI automatically, the same command;
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

- Documentation, doc comments and commit messages use American spelling (color,
  behavior, normalize, gray); `scripts/check_spelling.py` in the format check flags the
  British forms. Text quoted from a source keeps the source's spelling.
- Commit messages: short imperative summary line, in English, with a body when the
  *why* isn't obvious from the diff alone. No fixed prefix format is enforced, but
  `fix:`/`feat:`/`test:`/`docs:`/`ci:`/`refactor:` prefixes are the norm in this
  repository's history — match it.
- A change that affects what a consumer of the library observes (new API, changed
  behavior, a fixed bug, a changed default) needs a line in `CHANGELOG.md` under
  `[Unreleased]`, in the appropriate `Added`/`Changed`/`Fixed`/`Security` section, or —
  for anything that breaks existing callers — under `BREAKING CHANGES`, with either a
  one-line "how to restore the old behavior" or a link to the relevant section of
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
- **Public enums stay 32 bits wide.** Every public enum ends in a `*_FORCE_INT32_`
  enumerator that is not a real value; it keeps the enum int-sized under
  `-fshort-enums` (the default ABI on ARM EABI), where the compiler would otherwise
  pick the smallest type that fits. A new public enum gets one too, and new
  enumerators go before it. `tests/src/test_abi.c` checks the width, and is built a
  second time with `-fshort-enums` to prove the spacer works.
- **What the ABI covers.** The shared library's soname carries only the major version
  (`libphash.so.2`), so everything a compiled consumer depends on must stay put for
  the whole major: exported function signatures, the size and field offsets of every
  public struct, the width and values of every public enum, and array capacities that
  shape a struct (`PH_DIGEST_MAX_BYTES`, `PH_BATCH_HASHES_CAPACITY`).
  `tests/src/test_abi.c` pins the struct layouts; a change that makes it fail is a
  major version bump. The ABI is fixed for the whole 2.x series.
- **This project follows semantic versioning** for `include/libphash.h`: a
  source-or-binary-incompatible change (a removed/renamed public symbol, a changed
  function signature, a struct layout change, a default that changes existing hash
  output) is a major version bump and belongs in `BREAKING CHANGES` in
  `CHANGELOG.md`, never silently released as a minor or patch. If you are not sure
  whether your change qualifies, open an issue before writing the code.

## Reporting a security issue

Do not open a public issue or pull request for a security vulnerability (e.g. a crash
or memory-safety issue reachable from `ph_load_from_file()`/`ph_load_from_memory()` on
attacker-controlled input). See [`SECURITY.md`](SECURITY.md) for the reporting channel
and disclosure timeline.

## License

By contributing, you agree that your contribution is licensed under this project's
[MIT license](LICENSE).
