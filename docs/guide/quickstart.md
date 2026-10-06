# Quick start

From nothing to the hash of your first image, and then to comparing two images.

## Install

=== "Prebuilt archive"

    Each release publishes static and shared archives for linux-x86_64, linux-arm64,
    macos-arm64 and windows-x86_64 on the
    [releases page](https://github.com/gudoshnikovn/libphash/releases). Unpack one and
    point `pkg-config` at it:

    ```sh
    tar -xzf libphash-2.0.0-linux-x86_64.tar.gz
    export PKG_CONFIG_PATH="$PWD/libphash-2.0.0-linux-x86_64/lib/pkgconfig"
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

## Next

- [Which algorithm to use](../algorithms.md#comparison-summary), and what each one computes.
- [Hashing many files](../batch.md) at once, across threads.
- More programs — digests and metrics, error handling, batches — are in
  [`examples/`](https://github.com/gudoshnikovn/libphash/tree/main/examples); CI builds
  and runs every one of them.
