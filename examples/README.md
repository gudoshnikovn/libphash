# Examples

Each example is one C file that builds against an installed libphash with
`cc <file>.c $(pkg-config --cflags --libs libphash)`. `scripts/build_examples.sh` builds and
runs every one of them in CI against a fresh install, so they compile against the current
header.

| Example | What it shows |
|---|---|
| [`basic_hash.c`](basic_hash.c) | Load one image, compute a 64-bit pHash, print it as hex. |
| [`compare_two_images.c`](compare_two_images.c) | Hash two images and compare them: Hamming distance and similarity. |
| [`load_sources.c`](load_sources.c) | One picture loaded from pixels in memory (with a row stride), from an encoded buffer and from a file, and the same hash from each. |
| [`configure.c`](configure.c) | One function that configures every context alike, a refused value that changes nothing, the grayscale weights read back, and when a setting takes effect: at the next hash or at the next load. |
| [`hash_distance.c`](hash_distance.c) | aHash, dHash, pHash and wHash of two images from one `ph_compute_multi()` call each, and the bits each pair differs in. |
| [`digest_and_metrics.c`](digest_and_metrics.c) | The digest algorithms (mHash, BMH, Radial, ColorHash, ColorMoments), the comparison each digest's kind calls for, what a mismatched comparison returns, and a digest stored as text and read back. |
| [`batch_hash.c`](batch_hash.c) | Many files at once on a worker pool: a configuration template, a progress callback, several hashes per file, and a status per file. |
| [`find_duplicates.c`](find_duplicates.c) | Many files hashed with pHash, each hash printed as it would be stored, with what decides its value, and every pair within a threshold, closest first. |
| [`error_handling.c`](error_handling.c) | What each error code means and what a caller does about it, on real inputs: a missing path, a non-image, a damaged file, an image over the size limit, a format the build cannot decode. |
| [`cmake_consumer/`](cmake_consumer/) | A CMake project using an installed libphash through `find_package(phash 2)` and `phash::phash`. `scripts/smoke_install.sh` builds it in CI. |

[`../docs/guide/batch.md`](../docs/guide/batch.md) covers the batch API in depth,
[`../docs/theory/choosing.md`](../docs/theory/choosing.md) which algorithm to pick, and the API
reference (`make docs`) every function.
