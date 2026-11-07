# libphash API reference {#mainpage}

Every public function, type and constant of `libphash.h`, grouped by topic. The usual
path through the library:

1. Create a context — @ref context.
2. Load an image into it, from a file, a buffer or raw pixels — @ref loading.
3. Compute hashes: @ref hash64 (aHash, dHash, pHash, wHash) or @ref digests (mHash,
   BMH, Radial, ColorHash, ColorMoments); any of them by value through
   @ref algorithms.
4. Compare two hashes with the function their kind calls for — @ref compare.
5. Store them as text — @ref text.

Many images at once go through @ref batch; tuning is in @ref params; every failure is
explained in @ref errors; the version and the decoders a build carries are in @ref build.

The guides — which algorithm to choose, how batches and threads behave, how the library
is built and verified — are the Markdown pages in the repository's `docs/` directory.

A declaration added after 2.0.0 shows the release that added it ("since 2.1.0"); one
without is part of 2.0.0. A change of its contract in a later minor release ("Changed in
…") and its deprecation ("Deprecated in …") follow its description. [symbols-in-versions.txt](../symbols-in-versions.txt) lists
every public symbol with the release that added it, as plain text a binding can read; the
library installs it under `share/libphash/`.
