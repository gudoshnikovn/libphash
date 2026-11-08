---
title: 'API and ABI compatibility'
description: 'What a patch, minor and major release of libphash may change in the public header and the shared library, what counts as a break, and how CI checks it.'
---

# API and ABI compatibility

A program compiled against one 2.x release of libphash keeps working, without being
recompiled, with the shared library of any later 2.x release. A program that compiles
against one 2.x header compiles against every later one. The shared library's soname
carries only the major version (`libphash.so.2`, `libphash.2.dylib`), and nothing a
compiled caller depends on changes within it.

The public API is `libphash.h` and the `phash_version.h` it includes. Everything else in
the source tree is internal.

## What each release may change

| Release | May change |
|---|---|
| Patch (2.0.1 → 2.0.2) | Nothing in the public header except comments. Fixes behind the API. |
| Minor (2.0 → 2.1) | Additions only: new functions, types and macros; enumerators after the existing ones; fields at the end of an extensible struct; a reserved field taken for a new one. Deprecations. |
| Major (2 → 3) | Anything, listed in `MIGRATION.md`. Deprecated API is removed only here. |

Each declaration added after 2.0.0 names the release that added it (`@since` in the
[API reference](../api/index.md)), and [`symbols-in-versions.txt`](https://github.com/gudoshnikovn/libphash/blob/main/docs/symbols-in-versions.txt)
lists every public symbol with the release that added, deprecated or removed it.

## What counts as a break

Within a major version, none of these happen:

- a public function is removed, or its parameters or return type change;
- a public struct changes size or a field moves or changes type, apart from the two
  allowed cases below;
- an enumerator changes value, or an enum changes width (every public enum is 32-bit);
- an error code is reused for another meaning;
- a contract becomes stricter: an argument a function accepts becomes an error, or
  a result a caller may rely on stops being returned.

The last one no tool can find: it is reviewed by hand, and an allowed change of a
function's contract in a minor release is recorded in its doc comment ("Changed in").

## Extensible structs and reserved fields

A struct whose first field is `struct_size`, such as `ph_batch_options_t`, is
*extensible*: a minor release may append fields at its end. The caller sets
`struct_size` to the size it was compiled with (its `*_init()` function does that). A
later release accepts the `struct_size` of every earlier one, reads only that many
bytes, and uses the defaults for the fields an older caller does not know.

A struct without `struct_size` keeps its size. If it has a `reserved` field, a minor
release may give part of it a meaning, as long as zero keeps meaning what an older
caller, which zeroes it, expects.

## Deprecation

A deprecated function stays in every release of its major version and still works.
Its doc comment says which release deprecated it and what to use instead
("Deprecated in"), and it is declared with `PH_DEPRECATED`, so a call to it draws a
compiler warning.

## Hash values

The values a hash returns are not part of the API or the ABI, and this policy does not
cover them. A release that changes them says so in the changelog.

## How CI checks it

The `abi-compat` job compares the working tree with the latest release tag:

- `scripts/check_abi_layout.py` compiles the release's structs and enums against both
  headers and compares the size and offset of every field and the value of every
  enumerator, on Linux, macOS and Windows (MSVC);
- on Linux, `scripts/check_abidiff.sh` builds the release's shared library and the
  current one and compares them with `abidiff` (libabigail): a removed function or a
  changed signature, struct or enum fails, an added function passes.

`make docs` checks the header against `symbols-in-versions.txt` and the latest release
tag's header (`scripts/check_symbol_versions.py`).
