cmake_minimum_required(VERSION 3.10)
project(libphash VERSION 1.11.0 LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Only install()/export when built standalone -- a parent project that pulls
# libphash in via add_subdirectory() owns its own install rules.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(PHASH_IS_TOP_LEVEL ON)
else()
    set(PHASH_IS_TOP_LEVEL OFF)
endif()

# --- Version header (single source of truth: the project() VERSION above) ---
set(PH_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(PH_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(PH_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(PH_VERSION_STRING ${PROJECT_VERSION})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phash_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/phash_version.h
    @ONLY
)

# --- Options ---
option(PHASH_BUILD_TESTS "Build tests" ON)
option(PHASH_BUILD_SHARED "Build shared library" OFF)
option(PHASH_USE_TURBOJPEG "Use bundled libjpeg-turbo for fast JPEG decoding" ON)
option(PHASH_USE_LIBPNG "Use bundled libpng for fast PNG decoding" ON)
option(PHASH_USE_SPNG "Use bundled spng (single-file, low-level) instead of libpng for PNG decoding" OFF)
option(PHASH_USE_WEBP "Use external libwebp for WebP decoding" ON)
option(PHASH_USE_ZLIB_NG "Use bundled zlib-ng (zlib-compat) instead of system zlib for PNG inflate" ON)
option(PHASH_OPTIMIZE_NATIVE "Optimize for host CPU architecture (-march=native)" OFF)
option(PHASH_BUILD_FUZZERS "Build libFuzzer harnesses (requires Clang)" OFF)
option(PHASH_STRICT_DEPS "Fail the configure step instead of silently falling back when a bundled decoder dependency (e.g. vendored TurboJPEG/zlib-ng) can't be found" OFF)
option(PHASH_ENABLE_THREADS "Let ph_hash_files()/ph_hash_buffers() spawn a pool of OS threads (pthreads/Win32) instead of always running sequentially" ON)

if(PHASH_IS_TOP_LEVEL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    # Static-build consumers link libphash.a plus whichever vendored codec
    # archives were baked into it -- accumulated below as each backend is
    # configured, then written into libphash.pc's Libs.private and into
    # phashConfig.cmake's INTERFACE_LINK_LIBRARIES. Order matters for the
    # single-pass GNU ld: codecs before the zlib they depend on.
    set(PHASH_PC_LIBS_PRIVATE "")
    set(PHASH_INSTALL_LINK_LIBS "")
endif()

# Vendored codec archives only need to be installed/relinked for a STATIC
# build -- a SHARED build already bakes them into libphash.so, and relisting
# them in Libs.private/INTERFACE_LINK_LIBRARIES would just be redundant.
if(PHASH_IS_TOP_LEVEL AND NOT PHASH_BUILD_SHARED)
    set(PHASH_INSTALL_PRIVATE_DEPS ON)
else()
    set(PHASH_INSTALL_PRIVATE_DEPS OFF)
endif()

# --- Compiler Flags ---
include(CheckCCompilerFlag)

if(MSVC)
    add_compile_options(/W3 /O2)
else()
    add_compile_options(-Wall -Wextra -O3 -fPIC)
    
    if(PHASH_OPTIMIZE_NATIVE)
        add_compile_options(-march=native)
    else()
        # Architecture specific optimizations
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
            add_compile_options(-msse4.2)
            # Dynamically check for AVX2 support and apply ONLY to common.c
            check_c_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
            if(COMPILER_SUPPORTS_AVX2)
                set_source_files_properties(src/hashes/common.c PROPERTIES COMPILE_OPTIONS "-mavx2")
                set_source_files_properties(src/hashes/color_hsv.c PROPERTIES COMPILE_OPTIONS "-ffast-math")
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            add_compile_options(-march=armv8-a+simd)
        endif()
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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(phash PRIVATE m)

if(UNIX AND NOT APPLE)
    target_link_libraries(phash PRIVATE dl)
endif()

# --- Optional batch-hashing thread pool (task 9) ---
if(PHASH_ENABLE_THREADS)
    target_compile_definitions(phash PRIVATE PH_ENABLE_THREADS)
    if(NOT WIN32)
        find_package(Threads REQUIRED)
        target_link_libraries(phash PRIVATE Threads::Threads)
    endif()
endif()

# --- Bundled libjpeg-turbo (TurboJPEG API) ---
if(PHASH_USE_TURBOJPEG)
    # libjpeg-turbo doesn't like add_subdirectory, so we point to pre-built lib
    # NO_DEFAULT_PATH: otherwise a system copy (e.g. Homebrew's jpeg-turbo)
    # found on the default search path wins over the vendored static build
    # here, even when both exist -- see tasks/PROGRESS.md known bugs (task 7).
    find_library(TURBOJPEG_LIB NAMES turbojpeg turbojpeg-static
                 PATHS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build
                 NO_DEFAULT_PATH)
    if(TURBOJPEG_LIB)
        target_compile_definitions(phash PRIVATE PH_USE_TURBOJPEG)
        # BUILD_INTERFACE: link the build-tree archive directly, as before.
        # INSTALL_INTERFACE: link the copy installed below instead -- the
        # build-tree path above won't exist for an installed consumer.
        target_link_libraries(phash PRIVATE $<BUILD_INTERFACE:${TURBOJPEG_LIB}>)
        target_include_directories(phash PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/src
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build
        )
        if(PHASH_INSTALL_PRIVATE_DEPS)
            target_link_libraries(phash PRIVATE
                $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/libturbojpeg.a>)
            install(FILES ${TURBOJPEG_LIB} DESTINATION ${CMAKE_INSTALL_LIBDIR}
                    RENAME libturbojpeg.a)
            list(APPEND PHASH_PC_LIBS_PRIVATE "-lturbojpeg")
        endif()
    else()
        if(PHASH_STRICT_DEPS)
            message(FATAL_ERROR "libturbojpeg.a not found in vendor/libjpeg-turbo/build (PHASH_STRICT_DEPS=ON). Build the submodule first: cd vendor/libjpeg-turbo && cmake -B build && cmake --build build --target turbojpeg-static")
        else()
            message(WARNING "libturbojpeg.a not found in vendor/libjpeg-turbo/build. Falling back to stb_image.")
        endif()
    endif()
endif()

# --- Bundled zlib-ng (zlib-compat inflate/deflate used by libpng/spng) ---
set(PHASH_ZLIB_TARGET z)
if(PHASH_USE_ZLIB_NG AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/zlib-ng/CMakeLists.txt")
    set(ZLIB_COMPAT ON CACHE BOOL "" FORCE)
    set(ZLIB_ALIASES ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(WITH_GTEST OFF CACHE BOOL "" FORCE)
    set(WITH_FUZZERS OFF CACHE BOOL "" FORCE)
    set(WITH_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(WITH_BENCHMARK_APPS OFF CACHE BOOL "" FORCE)
    add_subdirectory(vendor/zlib-ng EXCLUDE_FROM_ALL)
    # Predefine ZLIB::ZLIB before libpng's find_package(ZLIB) runs: FindZLIB.cmake
    # skips creating its own imported target when one already exists, so libpng
    # links against zlib-ng instead of the system zlib it would otherwise find.
    add_library(ZLIB::ZLIB ALIAS zlib-ng)
    # find_package(ZLIB) also short-circuits its own find_path()/find_library()
    # searches once these are already set, so pin them to zlib-ng's generated
    # zlib.h and static lib. Without this, libpng's pnglibconf.h gets baked with
    # the system zlib's ZLIB_VERNUM while the actual compile picks up zlib-ng's
    # zlib.h through the include path, and the two disagree at compile time.
    set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/vendor/zlib-ng" CACHE PATH "" FORCE)
    set(ZLIB_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/vendor/zlib-ng/libz.a" CACHE FILEPATH "" FORCE)
    target_compile_definitions(phash PRIVATE PH_USE_ZLIB_NG)
    set(PHASH_ZLIB_TARGET zlib-ng)
    if(PHASH_INSTALL_PRIVATE_DEPS)
        install(TARGETS zlib-ng ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        # Appended after the codec blocks below (not here) so it lands last
        # in link order -- libpng/spng need to precede the zlib they use.
        set(PHASH_ZLIB_INSTALL_LINK_LIB
            $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/libz.a>)
    endif()
else()
    if(PHASH_USE_ZLIB_NG)
        if(PHASH_STRICT_DEPS)
            message(FATAL_ERROR "vendor/zlib-ng submodule not found (PHASH_STRICT_DEPS=ON). Run: git submodule update --init --recursive")
        else()
            message(WARNING "vendor/zlib-ng submodule not found. Falling back to system zlib.")
        endif()
    endif()
    if(PHASH_INSTALL_PRIVATE_DEPS)
        # System zlib -- nothing to install, just record the dependency.
        set(PHASH_ZLIB_INSTALL_LINK_LIB $<INSTALL_INTERFACE:z>)
    endif()
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
    # png_static/PHASH_ZLIB_TARGET aren't in phash's export set, so they can
    # only be linked for the local build; installed consumers get the copies
    # installed below via PHASH_INSTALL_LINK_LIBS instead.
    target_link_libraries(phash PRIVATE $<BUILD_INTERFACE:png_static> $<BUILD_INTERFACE:${PHASH_ZLIB_TARGET}>)
    target_include_directories(phash PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libpng
        ${CMAKE_CURRENT_BINARY_DIR}/vendor/libpng
    )
    if(PHASH_INSTALL_PRIVATE_DEPS)
        # libpng renames its static target's output (e.g. "png16", not
        # "png_static") via OUTPUT_NAME -- read it back rather than assuming.
        get_target_property(PHASH_PNG_LIB_NAME png_static OUTPUT_NAME)
        if(NOT PHASH_PNG_LIB_NAME)
            set(PHASH_PNG_LIB_NAME png_static)
        endif()
        install(TARGETS png_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        list(APPEND PHASH_INSTALL_LINK_LIBS
            $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/lib${PHASH_PNG_LIB_NAME}.a>)
        list(APPEND PHASH_PC_LIBS_PRIVATE "-l${PHASH_PNG_LIB_NAME}")
        set(PHASH_ZLIB_NEEDED ON)
    endif()
endif()

# --- Bundled spng ---
if(PHASH_USE_SPNG)
    target_compile_definitions(phash PRIVATE PH_USE_SPNG)
    add_library(spng_static STATIC vendor/spng/spng/spng.c)
    target_include_directories(spng_static PUBLIC vendor/spng/spng)
    target_link_libraries(phash PRIVATE $<BUILD_INTERFACE:spng_static> $<BUILD_INTERFACE:${PHASH_ZLIB_TARGET}>)
    target_include_directories(phash PRIVATE vendor/spng/spng)
    if(PHASH_INSTALL_PRIVATE_DEPS)
        install(TARGETS spng_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        list(APPEND PHASH_INSTALL_LINK_LIBS
            $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/libspng_static.a>)
        list(APPEND PHASH_PC_LIBS_PRIVATE "-lspng_static")
        set(PHASH_ZLIB_NEEDED ON)
    endif()
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
    target_link_libraries(phash PRIVATE $<BUILD_INTERFACE:webpdecoder>)
    target_include_directories(phash PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libwebp/src
    )
    if(PHASH_INSTALL_PRIVATE_DEPS)
        # libwebp tags this target with its own public headers -- we only want
        # its archive installed (phash's own libphash.h is the public surface).
        set_target_properties(webpdecoder PROPERTIES PUBLIC_HEADER "")
        install(TARGETS webpdecoder ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        list(APPEND PHASH_INSTALL_LINK_LIBS
            $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/libwebpdecoder.a>)
        list(APPEND PHASH_PC_LIBS_PRIVATE "-lwebpdecoder")
    endif()
endif()

if(PHASH_INSTALL_PRIVATE_DEPS AND PHASH_ZLIB_NEEDED)
    list(APPEND PHASH_INSTALL_LINK_LIBS ${PHASH_ZLIB_INSTALL_LINK_LIB})
    list(APPEND PHASH_PC_LIBS_PRIVATE "-lz")
endif()

# --- install() / find_package(phash) / pkg-config ---
if(PHASH_IS_TOP_LEVEL)
    # -lm/-ldl for libphash.pc's Libs.private: not added to
    # PHASH_INSTALL_LINK_LIBS, since `target_link_libraries(phash PRIVATE m)`
    # above already puts them on phash's exported INTERFACE_LINK_LIBRARIES.
    list(APPEND PHASH_PC_LIBS_PRIVATE "-lm")
    if(UNIX AND NOT APPLE)
        list(APPEND PHASH_PC_LIBS_PRIVATE "-ldl")
    endif()
    if(PHASH_ENABLE_THREADS AND NOT WIN32)
        list(APPEND PHASH_PC_LIBS_PRIVATE "-lpthread")
    endif()

    # For a STATIC build this exposes the vendored codec archives (installed
    # above) to consumers of the exported phash::phash target; for a SHARED
    # build they're already baked into libphash.so and this is a no-op since
    # PHASH_INSTALL_LINK_LIBS entries are BUILD-time inert (INSTALL_INTERFACE-only).
    if(NOT PHASH_BUILD_SHARED)
        target_link_libraries(phash PRIVATE ${PHASH_INSTALL_LINK_LIBS})
    endif()

    set_target_properties(phash PROPERTIES PUBLIC_HEADER
        ${CMAKE_CURRENT_SOURCE_DIR}/include/libphash.h)

    install(TARGETS phash
        EXPORT phashTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(EXPORT phashTargets
        FILE phashTargets.cmake
        NAMESPACE phash::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phash
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/phashConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/phashConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/phashConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phash
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/phashConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/phashConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phash
    )

    string(REPLACE ";" " " PHASH_PC_LIBS_PRIVATE_STR "${PHASH_PC_LIBS_PRIVATE}")
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/libphash.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/libphash.pc
        @ONLY
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libphash.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )
endif()

# --- Tests ---
if(PHASH_BUILD_TESTS)
    target_compile_definitions(phash PRIVATE PH_TESTING)
    enable_testing()
    file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/src/test_*.c")
    foreach(test_src ${TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE phash)
        target_include_directories(${test_name} PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
        )
        target_compile_definitions(${test_name} PRIVATE 
            TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data\"
            PH_TESTING
        )
        add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)
    endforeach()

    # Add benchmark as a smoke test (verifies loading and hashing works)
    add_test(NAME benchmark_smoke COMMAND $<TARGET_FILE:test_benchmark> hash ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/photo.jpeg 10)
endif()

# --- Fuzzing (libFuzzer, Clang only) ---
if(PHASH_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "PHASH_BUILD_FUZZERS requires Clang (libFuzzer is a Clang/compiler-rt feature).")
    endif()

    # Instrument the library for coverage/sanitizers without pulling in a fuzzer main,
    # so it can still be linked into the plain ctest binaries above.
    target_compile_options(phash PRIVATE -fsanitize=fuzzer-no-link,address,undefined -g -O1)

    add_executable(fuzz_load tests/fuzz/fuzz_load.c)
    target_link_libraries(fuzz_load PRIVATE phash)
    target_compile_options(fuzz_load PRIVATE -fsanitize=fuzzer,address,undefined -g -O1)
    target_link_options(fuzz_load PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
