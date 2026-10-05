# Local patches to the copied-in stb headers

`vendor/stb_image.h` and `vendor/stb_image_resize2.h` are upstream
[nothings/stb](https://github.com/nothings/stb) files at the commits recorded in
`THIRD-PARTY-NOTICES.md`, with exactly one change each: the patch in this directory.

| File | Upstream | Patch | What it changes |
|---|---|---|---|
| `stb_image.h` | v2.30, `013ac3b` | `stb_image.h.patch` | An allocation failure in the zlib entry points and in the allocating format probes is reported through `stbi_failure_reason()` as `"outofmem"` instead of being lost or overwritten. |
| `stb_image_resize2.h` | v2.18, `904aa67` | `stb_image_resize2.h.patch` | The out-of-memory paths under `STBIR__SEPARATE_ALLOCATIONS` free only what was allocated and leak nothing. |

Every change is also marked in the file itself with
`/* libphash local patch (not upstream): ... */`. The reasoning is in
`docs/development.md` ("Known vendor patch").

`scripts/check_stb_patches.sh` (run in CI) downloads each upstream file at its recorded
commit, checks its hash, applies the patch and requires the result to be the vendored
file byte for byte.

## Updating a header

1. Download the new upstream file into a scratch directory and record its commit.
2. Apply the patch there: `patch -p1 < vendor/patches/<file>.patch`. Resolve any
   rejected hunk by hand, keeping the `libphash local patch` markers.
3. Copy the result over `vendor/<file>` and regenerate the patch from the pristine
   upstream file: `diff -u --label a/<file> --label b/<file> <upstream> vendor/<file> > vendor/patches/<file>.patch`.
4. Record the commit and both hashes in `THIRD-PARTY-NOTICES.md`, run
   `scripts/check_stb_patches.sh`, and run `test_alloc_failure` and
   `test_stb_failure_classification` under `make debug && make test` — the second pins
   the error strings `src/loader.c` matches against.

The vendored files are not run through clang-format: a reformatted file no longer
matches upstream line for line, and the patch would stop applying.
