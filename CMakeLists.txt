cmake_minimum_required(VERSION 3.10)
project(libphash VERSION 1.9.0 LANGUAGES C)

# --- Options ---
option(PHASH_BUILD_TESTS "Build tests" ON)
option(PHASH_BUILD_SHARED "Build shared library" OFF)
option(PHASH_USE_TURBOJPEG "Use bundled libjpeg-turbo for fast JPEG decoding" ON)
option(PHASH_USE_LIBPNG "Use bundled libpng for fast PNG decoding" ON)
option(PHASH_USE_WEBP "Use external libwebp for WebP decoding" ON)

# --- Compiler Flags ---
if(MSVC)
    add_compile_options(/W3 /O2)
else()
    add_compile_options(-Wall -Wextra -O3 -fPIC)
    # Architecture specific optimizations
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        add_compile_options(-msse4.2)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        add_compile_options(-march=armv8-a+simd)
    endif()
endif()

# --- Library ---
file(GLOB_RECURSE SOURCES "src/*.c")

if(PHASH_BUILD_SHARED)
    add_library(phash SHARED ${SOURCES})
    target_compile_definitions(phash PRIVATE LIBPHASH_EXPORTS)
else()
    add_library(phash STATIC ${SOURCES})
endif()

target_include_directories(phash PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(phash PRIVATE m)

if(UNIX AND NOT APPLE)
    target_link_libraries(phash PRIVATE dl)
endif()

# --- Bundled libjpeg-turbo (TurboJPEG API) ---
if(PHASH_USE_TURBOJPEG)
    target_compile_definitions(phash PRIVATE PH_USE_TURBOJPEG)
    # libjpeg-turbo doesn't like add_subdirectory, so we point to pre-built lib
    find_library(TURBOJPEG_LIB NAMES turbojpeg turbojpeg-static
                 PATHS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build)
    if(TURBOJPEG_LIB)
        target_link_libraries(phash PRIVATE ${TURBOJPEG_LIB})
    else()
        message(WARNING "libturbojpeg.a not found in vendor/libjpeg-turbo/build. Please build it first.")
    endif()
    target_include_directories(phash PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build
    )
endif()

# --- Bundled libpng ---
if(PHASH_USE_LIBPNG)
    target_compile_definitions(phash PRIVATE PH_USE_LIBPNG)
    set(PNG_SHARED OFF CACHE BOOL "" FORCE)
    set(PNG_STATIC ON CACHE BOOL "" FORCE)
    set(PNG_TESTS OFF CACHE BOOL "" FORCE)
    set(PNG_HARDWARE_OPTIMIZATIONS ON CACHE BOOL "" FORCE)
    # libpng works mostly OK with add_subdirectory
    add_subdirectory(vendor/libpng EXCLUDE_FROM_ALL)
    target_link_libraries(phash PRIVATE png_static z)
    target_include_directories(phash PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libpng
        ${CMAKE_CURRENT_BINARY_DIR}/vendor/libpng
    )
endif()

# --- Bundled spng ---
if(PHASH_USE_SPNG)
    target_compile_definitions(phash PRIVATE PH_USE_SPNG)
    add_library(spng_static STATIC vendor/spng/spng/spng.c)
    target_include_directories(spng_static PUBLIC vendor/spng/spng)
    target_link_libraries(phash PRIVATE spng_static z)
    target_include_directories(phash PRIVATE vendor/spng/spng)
endif()

# --- Bundled libwebp ---
if(PHASH_USE_WEBP)
    target_compile_definitions(phash PRIVATE PH_USE_WEBP)
    
    # Configure libwebp build (minimal decoder only)
    set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(WEBP_LINK_STATIC ON CACHE BOOL "" FORCE)

    add_subdirectory(vendor/libwebp EXCLUDE_FROM_ALL)
    
    # We only need the decoder part for phash
    target_link_libraries(phash PRIVATE webpdecoder)
    target_include_directories(phash PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libwebp/src
    )
endif()

# --- Tests ---
if(PHASH_BUILD_TESTS)
    enable_testing()
    file(GLOB TEST_SOURCES "tests/test_*.c")
    foreach(test_src ${TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE phash)
        target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()

    # Add benchmark as a smoke test (verifies loading and hashing works)
    add_test(NAME benchmark_smoke COMMAND test_benchmark hash ${CMAKE_CURRENT_SOURCE_DIR}/tests/photo.jpeg 10)
endif()
