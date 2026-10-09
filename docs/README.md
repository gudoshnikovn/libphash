# libphash documentation

This directory is the source of the documentation site,
<https://gudoshnikovn.github.io/libphash/>, and every page in it also reads on GitHub as
it is. `make site` builds the site into `build/site/`, and `make site-serve`
serves it with live reload; [Documentation site](development.md#documentation-site) in
the development guide says what the build does and the rules that keep it green. The
site's navigation is `nav` in `zensical.toml` at the repository root.

## What is where

- [`index.md`](index.md) — the site's front page.
- [`theory/`](theory/perceptual-hashing.md) — how perceptual hashing works: the steps
  every algorithm shares ([image preparation](theory/preparation.md)), comparing hashes
  and choosing a threshold ([comparing](theory/comparing.md)), the nine algorithms
  measured side by side ([choosing](theory/choosing.md)), and a page per algorithm, from
  [aHash](theory/ahash.md) to [ColorMoments](theory/color-moments.md).
- [`guide/`](guide/quickstart.md) — using the library: installing, loading images,
  configuring a context, handling errors, hashing many files, storing and searching
  hashes, performance, and [how libphash works](guide/how-it-works.md) as a map of the
  rest. [`guide/migration.md`](guide/migration.md) shows `MIGRATION.md` from the
  repository root.
- `api/` — the API reference, one page per topic, generated from the doc comments in
  [`include/libphash.h`](../include/libphash.h) by `make site` (not tracked).
  `make docs` writes the same reference as Doxygen HTML into `build/api-docs/html/`, and
  each CI run publishes that as the `api-reference-html` artifact; `api-index.md` and
  `Doxyfile` here are its main page and configuration.
- `project/` — the changelog and the security policy, shown from the repository root,
  and the [photo corpus](project/corpus.md) the site's charts are drawn over (generated).
- `assets/` — the logo; `assets/generated/` holds the site's figures and measured tables,
  drawn by `tools/site/` (not tracked).

Four documents keep the paths the code and scripts refer to:

- [**Algorithm provenance**](algorithm-provenance.md) — where each algorithm comes from,
  what its source specifies against what this code does, and every known divergence.
- [**References**](references.md) — the bibliography: full citations and links for every
  source the algorithms rest on, with how far each one can be trusted and whether it was
  read directly.
- [**Verification methodology**](methodology.md) — what this project treats as correct:
  the premise, the criterion for a defect, the measurable properties and the test corpus.
- [**Development guide**](development.md) — build systems and options, presets, the CI
  matrix, testing, the source map and the conventions.

[`benchmarks/`](benchmarks/README.md) holds dated measurement records behind the choice of
decoders and build defaults, each with its exact commit, machines and method.

## Quick build reference

```bash
# Using standard Makefile (portable, stb_image only; Clang by default, CC=gcc to override)
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
make test

# Using CMake (recommended -- bundled high-performance decoders)
CC=clang cmake --preset release      # every bundled decoder; CC=gcc works too
cmake --build --preset release -j
ctest --preset release
```

See [`development.md`](development.md) for the full option table, both build systems'
defaults, the CI matrix, and how to run the fuzzer.
