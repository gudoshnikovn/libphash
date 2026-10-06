# A program that uses an installed libphash through CMake. Build:
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<libphash install prefix>
#   cmake --build build
# phash::phash carries the include path and, for a static libphash, every decoder
# library the installed build was configured with.
cmake_minimum_required(VERSION 3.10)
project(phash_consumer C)

find_package(phash 2 REQUIRED CONFIG)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE phash::phash)
