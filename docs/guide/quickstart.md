# Quick start

From nothing to the hash of your first image, and then to comparing two images.

## Install

The shortest route for each kind of build; [Installing](install.md) has every route, the
difference between the Full and Minimal builds, and how to check a download.

=== "Prebuilt archive"

    Each release publishes static and shared archives for linux-x86_64, linux-arm64,
    macos-arm64 and windows-x86_64 on the
    [releases page](https://github.com/gudoshnikovn/libphash/releases). Unpack one and
    point `pkg-config` at it:

    ```sh
    tar -xzf libphash-2.0.1-linux-x86_64.tar.gz
    export PKG_CONFIG_PATH="$PWD/libphash-2.0.1-linux-x86_64/lib/pkgconfig"
    ```

=== "CMake, from source"

    The recommended build: the bundled libjpeg-turbo, libpng, libwebp and zlib-ng
    decoders, fetched as git submodules.

    ```sh
    git clone --recursive https://github.com/gudoshnikovn/libphash.git
    cd libphash
    cmake --preset release
    cmake --build --preset release
    cmake --install build/release --prefix "$HOME/.local"
    ```

=== "Makefile, from source"

    The portable build: no dependencies, every format decoded by the bundled
    `stb_image`.

    ```sh
    git clone https://github.com/gudoshnikovn/libphash.git
    cd libphash
    make -j8
    make install PREFIX="$HOME/.local"
    ```

## Hash one image

```c title="examples/basic_hash.c"
--8<-- "examples/basic_hash.c"
```

```sh
cc basic_hash.c -o basic_hash $(pkg-config --cflags --libs libphash)
./basic_hash photo.jpg
```

## Compare two images

```c title="examples/compare_two_images.c"
--8<-- "examples/compare_two_images.c"
```

Two copies of one picture land a few bits apart, and two unrelated pictures about 32 of
the 64. Where to draw the line between the two depends on the algorithm and on the
collection; [Comparing hashes](../theory/comparing.md#choosing-a-threshold) measures it.

## Next

- [Which algorithm to use](../theory/choosing.md), the nine measured side by side.
- [How far apart two copies may be](../theory/comparing.md#choosing-a-threshold), and
  which function compares which hash.
- [Hashing many files](batch.md) at once, across threads.
- The functions these programs call, in the [API reference](../api/index.md):
  [`ph_create()`](../api/context.md#ph_create),
  [`ph_load_from_file()`](../api/loading.md#ph_load_from_file),
  [`ph_compute_phash()`](../api/hash64.md#ph_compute_phash),
  [`ph_hamming_distance()`](../api/compare.md#ph_hamming_distance),
  [`ph_get_error_string()`](../api/errors.md#ph_get_error_string) and
  [`ph_free()`](../api/context.md#ph_free).
- More programs — digests and metrics, error handling, batches — are in
  [`examples/`](https://github.com/gudoshnikovn/libphash/tree/main/examples); CI builds
  and runs every one of them.
