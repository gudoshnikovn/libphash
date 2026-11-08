---
title: 'Compatibility and versioning'
description: 'What a patch, minor and major release of libphash promise for the API, the ABI, the values of the hashes and the text of a stored digest, what counts as a break, and how CI checks it.'
---

# Compatibility and versioning

A program compiled against one 2.x release of libphash keeps working, without being
recompiled, with the shared library of any later 2.x release. A program that compiles
against one 2.x header compiles against every later one. The shared library's soname
carries only the major version (`libphash.so.2`, `libphash.2.dylib`), and nothing a
compiled caller depends on changes within it.

The public API is `libphash.h` and the `phash_version.h` it includes. Everything else in
the source tree is internal.

## What each release may change

| Release | API and ABI | Hash values | Stored text |
|---|---|---|---|
| Patch (2.0.1 → 2.0.2) | Nothing in the public header except comments. Fixes behind the API. | Never change. | Reads and writes what the release before it did. |
| Minor (2.0 → 2.1) | Additions only: new functions, types and macros; enumerators after the existing ones; fields at the end of an extensible struct; a reserved field taken for a new one. Deprecations. | May change, one algorithm at a time: its [revision](#hash-values) is raised and the change is listed in the changelog. | Reads the text of every earlier release. May write more than the earlier ones read. |
| Major (2 → 3) | Anything, listed in `MIGRATION.md`. Deprecated API is removed only here. | May change; the revisions say which. | May stop reading text from earlier majors. |

Reading is backward compatible; writing is not promised to be readable backwards. A
release reads the text it and the releases before it wrote, and a hash saved by an older
release stays comparable until its algorithm's revision changes. A release does not
promise that an older one reads the text a newer one wrote.

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

The values a hash returns are not part of the API or the ABI, and the checks below do not
cover them. They have a version of their own: every algorithm has a **revision**,
[`ph_algorithm_revision()`](../api/algorithms.md#ph_algorithm_revision), and a change to
the values it computes raises it. The revisions are independent: a change to dHash does
not make pHash hashes stale.

- A patch release never changes a revision.
- A minor or major release that changes an algorithm's values raises its revision and
  lists the change in the changelog under "Hash changes", with the algorithm and the old
  and new revision. Each algorithm page ends with its revision table: revision, the
  release that introduced it, what changed, and whether stored hashes must be recomputed.
- The library computes one revision of each algorithm, the current one, whatever the
  configuration. It does not keep the old ones. A program that cannot recompute its
  stored hashes stays on the earlier release.
- A `ph_digest_t` states its algorithm and revision in its `algorithm` and `revision`
  fields, and the text form `radial.1:coefficients:…` carries them. Comparing two digests
  that state different revisions fails: `PH_ERR_REVISION_MISMATCH` from
  [`ph_radial_similarity()`](../api/compare.md#ph_radial_similarity) and
  [`ph_histogram_intersection()`](../api/compare.md#ph_histogram_intersection), -1 from
  the distance and similarity functions, which have no error channel. A comparison is
  never a plausible wrong number. A digest with `revision` 0, saved by 2.0 or built by
  hand, states nothing and is compared as in 2.0.
- A `uint64_t` hash carries no revision. Store `ph_algorithm_revision()` next to it and
  recompute when it differs ([storing hashes](../guide/storing.md)).
- The golden-hash tests fail on a value that changes without a new revision.

## The text of a digest

[`ph_digest_to_hex()`](../api/text.md#ph_digest_to_hex) writes
`[<algorithm>.<revision>:]<kind>:<hex>`, and
[`ph_digest_from_hex()`](../api/text.md#ph_digest_from_hex) reads it. A release reads
every form an earlier release wrote, so text stored by 2.0 (`<kind>:<hex>`, no prefix)
reads in later releases, as a digest that states no revision. The reverse is not
promised: a release before the one that added a field of the text does not read the
longer text. The text of a digest the library computes fits in 270 bytes in every release.

## How CI checks it

The `abi-compat` job compares the working tree with the latest release tag:

- `scripts/abi/check_abi_layout.py` compiles the release's structs and enums against both
  headers and compares the size and offset of every field and the value of every
  enumerator, on Linux, macOS and Windows (MSVC);
- on Linux, `scripts/abi/check_abidiff.sh` builds the release's shared library and the
  current one and compares them with `abidiff` (libabigail): a removed function or a
  changed signature, struct or enum fails, an added function passes.

`make docs` checks the header against `symbols-in-versions.txt` and the latest release
tag's header (`scripts/docs/check_symbol_versions.py`).
