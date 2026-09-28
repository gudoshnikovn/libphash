# 3.21 for CMAKE_C_STANDARD 17/23 recognition (added in 3.21) and for
# CMakePresets.json's schema version used by CMakePresets.json at the repo root.
cmake_minimum_required(VERSION 3.21)
project(libphash VERSION 2.0.0 LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Explicit C standard. Left unset, the dialect is whatever the compiler defaults to
# (gnu17 on modern gcc/clang, gnu23 on GCC 15, gnu11 or lower on old ones). C17 is
# the widest-supported non-legacy standard across the full CI matrix, including
# windows-latest/MSVC, whose C23 support is still immature.
# CMAKE_C_EXTENSIONS is OFF because nothing in this codebase uses GNU dialect
# features (typeof, statement expressions, VLAs) -- plain -std=c17, not gnu17.
#
# That switch costs more than the GNU dialect, though, and the cost is invisible
# on Darwin: a strict dialect makes the compiler define __STRICT_ANSI__, and glibc
# hides *every* non-ISO declaration behind it -- POSIX included. M_PI is defined by
# src/hashes/hashes.h and tests/src/test_macros.h, so no libc has to provide it. A
# translation unit that needs a POSIX or GNU declaration (clock_gettime(), openat(),
# mkstemp(), sched_getaffinity(), ...) requests it with a feature-test macro before its
# first #include. Anything new that reaches for a non-ISO libc name has to do the same;
# scripts/check_strict_iso.sh is what notices when it doesn't.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Only install()/export when built standalone -- a parent project that pulls
# libphash in via add_subdirectory() owns its own install rules.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(PHASH_IS_TOP_LEVEL ON)
else()
    set(PHASH_IS_TOP_LEVEL OFF)
endif()

# The optimisation level belongs to CMAKE_BUILD_TYPE, so a standalone configure that
# names none must not silently produce an unoptimised library. Release when nothing
# was asked for; never under a multi-config generator (the type is picked at build
# time there) and never inside a parent project, whose choice this is. Without it, the
# effective default would be whatever libwebp/zlib-ng write into the cache as a side
# effect of add_subdirectory(), which the minimal (stb-only) build never reaches.
if(PHASH_IS_TOP_LEVEL AND NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo or MinSizeRel" FORCE)
endif()

# BUILD_SHARED_LIBS and BUILD_TESTING are global CMake variables, not options of the
# vendored subprojects -- but those subprojects have to see them OFF while they
# configure. Forcing them into the cache and walking away would silently reconfigure
# any parent project that pulls libphash in via add_subdirectory(), turning off its
# shared build and its ctest. So: force them, then put the parent's values back.
#
# Macros, not functions: cache edits are global, but these also read and write plain
# variables in the caller's scope, which a function() would not see.
macro(phash_push_global_build_flags)
    if(NOT PHASH_IS_TOP_LEVEL)
        foreach(phash_gflag BUILD_SHARED_LIBS BUILD_TESTING)
            set(PHASH_SAVED_${phash_gflag} "${${phash_gflag}}")
            if(DEFINED CACHE{${phash_gflag}})
                set(PHASH_HAD_${phash_gflag} ON)
            else()
                set(PHASH_HAD_${phash_gflag} OFF)
            endif()
        endforeach()
    endif()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
endmacro()

macro(phash_pop_global_build_flags)
    if(NOT PHASH_IS_TOP_LEVEL)
        foreach(phash_gflag BUILD_SHARED_LIBS BUILD_TESTING)
            if(PHASH_HAD_${phash_gflag})
                set(${phash_gflag} "${PHASH_SAVED_${phash_gflag}}" CACHE BOOL "" FORCE)
            else()
                unset(${phash_gflag} CACHE)
            endif()
        endforeach()
    endif()
endmacro()

# Computes a vendored static target's real installed file name, for baking
# into an INSTALL_INTERFACE consumer link path as a plain string.
#
# $<TARGET_FILE_NAME:...> looks like the obvious tool for this, but doesn't
# work inside INSTALL_INTERFACE here: these targets (zlib-ng, png_static,
# spng_static, webpdecoder) are deliberately not part of phash's export set
# (see the comment above target_link_libraries(phash PRIVATE
# $<BUILD_INTERFACE:png_static> ...) below), so by the time a consumer's
# CMake evaluates that generator expression, the target it names doesn't
# exist in that project at all, and the consumer's configure fails with
# "No target png_static" (scripts/smoke_install.sh covers this).
#
# So this reads each of PREFIX/OUTPUT_NAME/SUFFIX back from the target right
# now, while it still exists in this configure, falling back to CMake's
# regular static-library defaults for whichever property the vendor's own
# CMakeLists didn't override -- the same three inputs $<TARGET_FILE_NAME:...>
# itself would combine, just resolved eagerly instead of deferred.
#
# Optional third argument `src_dir`: a vendor can retarget
# CMAKE_STATIC_LIBRARY_PREFIX itself (a plain variable, not a target
# property) from inside its own add_subdirectory() tree -- libwebp does
# exactly this for MSVC ("match the naming convention used by nmake"),
# forcing a literal "lib" prefix even on Windows. That assignment is scoped
# to libwebp's own subdirectory and never reaches this file's scope, so
# get_target_property(PREFIX) sees nothing and CMAKE_STATIC_LIBRARY_PREFIX
# here is back to MSVC's own default ("") the moment add_subdirectory()
# returns. get_directory_property(... DEFINITION ...) against that
# subdirectory is the one way to read the variable's value as it stood
# during that directory's own processing. Without the src_dir fallback,
# libwebp's INSTALL_INTERFACE path gets the wrong file name under MSVC.
function(phash_static_lib_filename out_var target)
    set(_phash_src_dir "")
    if(ARGC GREATER 2)
        set(_phash_src_dir "${ARGV2}")
    endif()

    get_target_property(_phash_prefix ${target} PREFIX)
    if(NOT _phash_prefix AND _phash_src_dir)
        get_directory_property(_phash_prefix DIRECTORY "${_phash_src_dir}"
            DEFINITION CMAKE_STATIC_LIBRARY_PREFIX)
    endif()
    if(NOT _phash_prefix)
        set(_phash_prefix "${CMAKE_STATIC_LIBRARY_PREFIX}")
    endif()
    get_target_property(_phash_name ${target} OUTPUT_NAME)
    if(NOT _phash_name)
        set(_phash_name "${target}")
    endif()
    get_target_property(_phash_suffix ${target} SUFFIX)
    if(NOT _phash_suffix)
        set(_phash_suffix "${CMAKE_STATIC_LIBRARY_SUFFIX}")
    endif()
    set(${out_var} "${_phash_prefix}${_phash_name}${_phash_suffix}" PARENT_SCOPE)
endfunction()

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
option(PHASH_USE_LIBJPEG_TURBO "Use bundled libjpeg-turbo for fast JPEG decoding" ON)
option(PHASH_USE_LIBPNG "Use bundled libpng for fast PNG decoding" ON)
option(PHASH_USE_SPNG "Use bundled spng (single-file, low-level) instead of libpng for PNG decoding" OFF)
option(PHASH_USE_WEBP "Use external libwebp for WebP decoding" ON)
option(PHASH_USE_ZLIB_NG "Use bundled zlib-ng (zlib-compat) instead of system zlib for PNG inflate" ON)
option(PHASH_OPTIMIZE_NATIVE "Optimize for host CPU architecture (-march=native)" OFF)
option(PHASH_BUILD_FUZZERS "Build libFuzzer harnesses (requires Clang)" OFF)
option(PHASH_ENABLE_MOCK_BACKEND "Register the DE AD mock decoder in the loader's backend table (test-only; must never be ON in a shipped build)" OFF)
option(PHASH_STRICT_DEPS "Fail the configure step instead of silently falling back when a bundled decoder dependency (e.g. vendored libjpeg-turbo/zlib-ng) can't be found" OFF)
option(PHASH_ENABLE_THREADS "Let ph_hash_files()/ph_hash_buffers() spawn a pool of OS threads (pthreads/Win32) instead of always running sequentially" ON)
# `make coverage` (Makefile) measures only the stb_image-only path: the native
# decoders in src/loaders/ compile to nothing or are not built at all under that flow,
# so their max_pixels checks, error-callback plumbing and overflow guards are
# invisible to it. This option instruments *this*
# CMake build instead, so a Release-equivalent config with the vendored decoders on
# can be measured; scripts/coverage_cmake.sh drives it (also for the spng variant)
# and merges both into one report.
option(PHASH_COVERAGE "Instrument phash and its tests with --coverage (gcov/lcov); see scripts/coverage_cmake.sh" OFF)

# libpng and spng are two alternative implementations of the *same* PNG backend,
# src/loaders/png_libpng.c and src/loaders/png_spng.c, and the library compiles one of
# them. Both would define ph_decode_png_mem() twice and link two PNG libraries.
# Refuse the configuration instead of picking a winner behind the user's back.
if(PHASH_USE_LIBPNG AND PHASH_USE_SPNG)
    message(FATAL_ERROR
        "PHASH_USE_LIBPNG and PHASH_USE_SPNG are mutually exclusive: they are two "
        "alternative implementations of the single PNG backend, and only one can be "
        "compiled in. PHASH_USE_LIBPNG defaults to ON, so selecting spng means turning "
        "libpng off explicitly:\n"
        "  cmake -S . -B build -DPHASH_USE_SPNG=ON -DPHASH_USE_LIBPNG=OFF")
endif()

# The JPEG option is named after the codec, libjpeg-turbo. CMake silently ignores an
# unknown -D, and PHASH_USE_TURBOJPEG is the name a build script is likely to pass for
# it; ignoring that would quietly build the default backend set instead of the one the
# script asks for, so it is refused with the right name.
if(DEFINED PHASH_USE_TURBOJPEG)
    message(FATAL_ERROR
        "PHASH_USE_TURBOJPEG is not an option of this project; the JPEG backend is "
        "selected with PHASH_USE_LIBJPEG_TURBO. Pass that instead, and remove the old "
        "entry from an existing build directory's cache:\n"
        "  cmake -S . -B build -UPHASH_USE_TURBOJPEG -DPHASH_USE_LIBJPEG_TURBO=${PHASH_USE_TURBOJPEG}")
endif()

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
    # src/batch.c and src/loader.c include <stdatomic.h> unconditionally, and
    # MSVC only ships that header under
    # /std:c11 or later (VS 17.5+). CMAKE_C_STANDARD (set above, project-wide)
    # already emits the /std:c1x flag matching it -- not repeated here as an
    # explicit /std:c11, which would just be a second, possibly conflicting
    # /std: flag on the same command line.
    #
    # /std:c1x alone isn't enough, though: <stdatomic.h>'s vcruntime_c11_stdatomic.h
    # additionally requires /experimental:c11atomics; without it MSVC's own
    # header self-errors with "C atomic support is not enabled" even under
    # /std:c11.
    #
    # No _USE_MATH_DEFINES: M_PI is not ISO C on any platform (glibc hides it too
    # once CMAKE_C_EXTENSIONS OFF turns __STRICT_ANSI__ on), so src/hashes/hashes.h and
    # tests/src/test_macros.h define it themselves.
    add_compile_options(/experimental:c11atomics)

    # /utf-8: the sources are UTF-8 and carry non-ASCII text (em dashes, in comments
    # only). Without the flag MSVC reads them in the machine's ANSI code page. On a
    # single-byte page such as the CI runners' 1252 every byte decodes to something,
    # so nothing is reported. On a
    # double-byte page (932/936/949, CJK locales) the same bytes draw C4819, and a
    # multi-byte lead byte at the end of a // comment can swallow the newline and
    # comment out the next line of code.
    set(PHASH_OWN_COMPILE_OPTIONS /W3 /utf-8)
else()
    # -fPIC stays directory-wide: the vendored decoders are linked into the same
    # library and need it as much as our own objects do.
    add_compile_options(-fPIC)

    set(PHASH_OWN_COMPILE_OPTIONS -Wall -Wextra)

    # Floating-point contraction (fusing a*b+c into one FMA with a single rounding) is
    # off, explicitly. Left to the compiler, it depends on things that have nothing to
    # do with the hash: GCC fuses freely under -std=gnuNN and not at all under a strict
    # -std=cNN, Clang fuses within an expression either way, and only targets with
    # hardware FMA (arm64 always; x86-64 only with -mfma) are affected at all. pHash's
    # median threshold turns that last-bit difference into flipped bits: on Linux arm64
    # GCC, gnu17 against strict c17 alone moves photo.png's pHash by 10 bits. Off is the
    # source-as-written semantics, which x86-64's baseline and MSVC (whose default
    # /fp:precise does not contract) have anyway, so a hash does not depend on the
    # dialect or compiler it was built with.
    check_c_compiler_flag("-ffp-contract=off" PHASH_HAS_FP_CONTRACT_OFF)
    if(PHASH_HAS_FP_CONTRACT_OFF)
        list(APPEND PHASH_OWN_COMPILE_OPTIONS -ffp-contract=off)
    endif()

    # Warnings the library's own sources are clean under, on both GCC and Clang, so any
    # new warning is a real regression. Applied to `phash` only:
    # tests and the fuzzer keep the -Wall -Wextra baseline (the test sources were
    # never written to this set -- e.g. `void test_x()` trips -Wstrict-prototypes).
    # Each flag is probed, since GCC rejects an unknown -W option outright.
    set(PHASH_LIBRARY_WARNING_CANDIDATES
        -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition
        -Wmissing-variable-declarations -Wundef -Wwrite-strings -Wswitch-enum
        -Wvla -Wformat=2 -Wredundant-decls)
    set(PHASH_LIBRARY_WARNINGS "")
    foreach(flag ${PHASH_LIBRARY_WARNING_CANDIDATES})
        string(MAKE_C_IDENTIFIER "PHASH_HAS${flag}" flag_var)
        check_c_compiler_flag("${flag}" ${flag_var})
        if(${flag_var})
            list(APPEND PHASH_LIBRARY_WARNINGS "${flag}")
        endif()
    endforeach()
    
    if(PHASH_OPTIMIZE_NATIVE)
        add_compile_options(-march=native)
    else()
        # Architecture specific optimizations.
        #
        # CMAKE_SYSTEM_PROCESSOR is NOT reliable for telling 32-bit x86 apart from
        # 64-bit here: without a real cross-compiling toolchain file (CMAKE_SYSTEM_NAME
        # set), CMake reports the *host*'s uname -- so build-and-test-32bit's
        # -DCMAKE_C_FLAGS=-m32 -DCMAKE_SYSTEM_PROCESSOR=i686 in ci.yml is silently
        # overridden back to the runner's real "x86_64" during project(), and this
        # branch would match "x86_64|amd64" for that leg too (and hand -mavx2 to
        # src/hashes/common.c on a 32-bit build). CMAKE_SIZEOF_VOID_P, by contrast, is derived from an actual compiled
        # test binary and correctly reflects -m32, so it's what distinguishes the two
        # x86 widths below.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|i[3-6]86|^x86$")
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                add_compile_options(-msse4.2)
                # Dynamically check for AVX2 support and apply ONLY to common.c
                check_c_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
                if(COMPILER_SUPPORTS_AVX2)
                    set_source_files_properties(src/hashes/common.c PROPERTIES COMPILE_OPTIONS "-mavx2")
                endif()
            else()
                # 32-bit x86 has no fixed float ABI of its own -- GCC/Clang
                # default to x87 FPU extended-precision intermediates for scalar
                # double/float math there, not SSE2, unless told otherwise. That default
                # silently changes this library's own floating-point output (e.g.
                # ph_compute_phash()'s rounding noise on solid-colour input,
                # tests/src/test_hash_properties.c's synthetic rotation profile) purely
                # because of how the compiler happens to schedule intermediate
                # precision, with nothing in the library or its test corpus asking for
                # it. Force SSE2-based float math so 32-bit and 64-bit x86 builds agree
                # bit-for-bit wherever they should. Raises the minimum 32-bit CPU to
                # SSE2-capable hardware (~Pentium 4/Athlon 64, 2000-2003) -- the same
                # floor most current Linux distributions already assume for i686 builds.
                add_compile_options(-msse2 -mfpmath=sse)
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            add_compile_options(-march=armv8-a+simd)
        endif()
    endif()
endif()

# --- Library ---
file(GLOB_RECURSE SOURCES "src/*.c")
# The two PNG backends are added by their options below, at most one of them.
list(FILTER SOURCES EXCLUDE REGEX "/src/loaders/png_(libpng|spng)\\.c$")

# The vendored stb_image_resize2 packs filter coefficients with deliberately
# unaligned 64-bit moves (STBIR_MOVE_2 casts float* -> stbir_uint64*) on its own
# internal, 16-byte-aligned coefficient array — nothing we pass in is misaligned,
# and the pattern is unchanged upstream as of the vendored v2.18. Exempt ONLY the
# TU that instantiates stb (it contains no code of ours) from UBSan's alignment
# check, so the `sanitizers` CI job reports only our own findings. A runtime UBSAN_OPTIONS suppression file does not work here: it is
# ignored under -fno-sanitize-recover=all, which that job uses.
check_c_compiler_flag("-fno-sanitize=alignment" PHASH_HAS_FNO_SANITIZE_ALIGNMENT)
if(PHASH_HAS_FNO_SANITIZE_ALIGNMENT)
    set_source_files_properties(src/image/stb_resize_impl.c
        PROPERTIES COMPILE_OPTIONS "-fno-sanitize=alignment")
endif()

if(PHASH_BUILD_SHARED)
    add_library(phash SHARED ${SOURCES})
    target_compile_definitions(phash PRIVATE LIBPHASH_EXPORTS)
else()
    add_library(phash STATIC ${SOURCES})
    # include/libphash.h's PH_API has three Windows cases: building the DLL
    # (LIBPHASH_EXPORTS -> dllexport), consuming it (dllimport), and a static build
    # (PHASH_STATIC_DEFINE -> no-op). Without this define MSVC refuses to compile the
    # static library itself (C2491, "definition of dllimport function not allowed",
    # since every PH_API function's own .c file defines a function its own header
    # declared dllimport).
    #
    # PUBLIC, not PRIVATE: a consumer linking this exported target must see
    # the same PH_API expansion this library itself was compiled with, or its
    # calls resolve to (unnecessary, and on some MSVC versions mismatching)
    # dllimport thunks against a static archive that has none.
    target_compile_definitions(phash PUBLIC PHASH_STATIC_DEFINE)
endif()

target_include_directories(phash PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>
)

# Not on Windows: there is no libm there (or an "m.lib" of any kind) --
# math.h's functions are part of the CRT already. A STATIC build never
# actually invokes a linker to resolve this (ar/lib.exe just bundles object
# files), so only a SHARED Windows build exposes it: LNK1181 "cannot open input
# file 'm.lib'".
if(NOT WIN32)
    target_link_libraries(phash PRIVATE m)
endif()

# Warnings are a property of this project's code, not of the directory: set with
# add_compile_options() they would also reach every vendored decoder added below,
# which would make -Werror impossible (it would fail on their code, not ours).
target_compile_options(phash PRIVATE ${PHASH_OWN_COMPILE_OPTIONS} ${PHASH_LIBRARY_WARNINGS})

# The library's exported surface is include/libphash.h and nothing else. Two mechanisms,
# because the symbols come from two places:
#  - our own objects are compiled hidden, so only what PH_API marks default-visible
#    leaves them (internal ph_* helpers and the stb_image instantiation included);
#  - the vendored decoders arrive as static archives compiled with default visibility,
#    and a shared libphash would re-export all of them -- libpng, libjpeg, libwebp and
#    zlib under their own names, interposing on an application's copy of the same
#    library. The linker's export list keeps only ph_*.
# Hidden visibility applies to the static archive too, so a consumer that links it into
# a shared library of its own does not re-export our internals either. MSVC exports only
# __declspec(dllexport) and needs neither.
set_target_properties(phash PROPERTIES C_VISIBILITY_PRESET hidden)
if(PHASH_BUILD_SHARED AND NOT WIN32)
    include(CheckLinkerFlag)
    if(APPLE)
        target_link_options(phash PRIVATE
            "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/cmake/phash.exported_symbols")
    else()
        check_linker_flag(C "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/phash.map"
            PHASH_HAS_VERSION_SCRIPT)
        if(PHASH_HAS_VERSION_SCRIPT)
            target_link_options(phash PRIVATE
                "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/phash.map")
        endif()
    endif()
endif()

if(PHASH_COVERAGE)
    # -O0: matches Makefile's `coverage` (PHASH_COVERAGE=1 there too, same intent,
    # unrelated implementation) -- line/branch counts are only trustworthy without
    # the reordering/inlining an optimising build type (Release: -O3) applies.
    #
    # -fprofile-update=atomic: this project's tests run gcov-instrumented code from
    # multiple threads (test_batch_stress, test_thread_safety), and a static inline
    # function defined in a header (e.g. ph_safe_image_alloc_size() in safety.h)
    # gets its own counter instance per translation unit -- default (non-atomic)
    # counter increments race there and corrupt the merged .gcda, surfacing as
    # `geninfo: ERROR: Unexpected negative count` when scripts/coverage_cmake.sh
    # runs lcov --capture. This is a known GCC/gcov
    # limitation (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68080), not a libphash
    # bug; the fix is atomic counter updates, not suppressing/ignoring the error,
    # since that would silently under-report coverage on whatever counters actually
    # raced instead of reporting a real (if slightly slower) number.
    target_compile_options(phash PRIVATE --coverage -O0 -g -fprofile-update=atomic)
    target_link_options(phash PRIVATE --coverage)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(phash PRIVATE dl)
endif()

# --- Optional batch-hashing thread pool ---
# PHASH_NEEDS_THREADS_DEP records whether the installed package has to
# find_dependency(Threads) on the consumer's side; it's substituted into
# cmake/phashConfig.cmake.in. Kept in lockstep with the link below so the two
# can't drift apart.
set(PHASH_NEEDS_THREADS_DEP OFF)
if(PHASH_ENABLE_THREADS)
    target_compile_definitions(phash PRIVATE PH_ENABLE_THREADS)
    if(NOT WIN32)
        find_package(Threads REQUIRED)
        target_link_libraries(phash PRIVATE Threads::Threads)
        set(PHASH_NEEDS_THREADS_DEP ON)
    endif()
endif()

# --- Bundled libjpeg-turbo (libjpeg API, jpeg-static) ---
if(PHASH_USE_LIBJPEG_TURBO)
    # The libjpeg API archive (jpeg-static), not the TurboJPEG one (turbojpeg-static):
    # libjpeg-turbo 3.x compiles its own zlib and spng into the TurboJPEG archive, with
    # global symbols, for image-file I/O this library never uses -- and on macOS libpng
    # and spng would then bind to those copies instead of the vendored zlib-ng and spng.
    # jpeg-static is the codec alone.
    #
    # libjpeg-turbo doesn't like add_subdirectory, so we point to the pre-built lib.
    # NO_DEFAULT_PATH: otherwise a system copy (e.g. Homebrew's jpeg-turbo) found on the
    # default search path wins over the vendored static build here, even when both exist.
    #
    # The extra PATHS entries below are multi-config generator output dirs (Visual Studio
    # on Windows): a single-config generator (Makefiles, Ninja) writes the archive
    # straight into .../build, but a multi-config one writes .../build/<config> instead,
    # and never both. The archive is named libjpeg.a on Unix and jpeg-static.lib with MSVC.
    set(_phash_libjpeg_find_library_args
        NAMES jpeg jpeg-static
        PATHS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build
              ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build/Release
              ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build/RelWithDebInfo
              ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build/MinSizeRel
              ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build/Debug
        NO_DEFAULT_PATH)
    find_library(PHASH_LIBJPEG_LIB ${_phash_libjpeg_find_library_args})

    # Not built yet: build it here at configure time, so a plain `cmake -B build` gets
    # the native JPEG backend instead of silently falling back to stb_image (once --
    # cached by PHASH_LIBJPEG_LIB like the pre-built case above).
    if(NOT PHASH_LIBJPEG_LIB AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/CMakeLists.txt)
        message(STATUS "Vendored libjpeg-turbo not built yet -- building it now (one-time; cd vendor/libjpeg-turbo to rebuild manually if this ever needs to change).")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo
                                      -B ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build
                                      -DCMAKE_BUILD_TYPE=Release -DENABLE_SHARED=OFF
                                      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            RESULT_VARIABLE _phash_libjpeg_configure_result)
        if(_phash_libjpeg_configure_result EQUAL 0)
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build
                                          --target jpeg-static --config Release
                RESULT_VARIABLE _phash_libjpeg_build_result)
        else()
            set(_phash_libjpeg_build_result ${_phash_libjpeg_configure_result})
        endif()
        if(NOT _phash_libjpeg_build_result EQUAL 0)
            message(WARNING "Auto-build of vendored libjpeg-turbo failed (see output above). Falling back to stb_image.")
        endif()
        # The cached NOTFOUND from the first find_library() call must be cleared first,
        # or find_library() would just return the stale cached result.
        unset(PHASH_LIBJPEG_LIB CACHE)
        find_library(PHASH_LIBJPEG_LIB ${_phash_libjpeg_find_library_args})
    endif()

    if(PHASH_LIBJPEG_LIB)
        target_compile_definitions(phash PRIVATE PH_USE_LIBJPEG_TURBO)
        # BUILD_INTERFACE: link the build-tree archive directly. INSTALL_INTERFACE: link
        # the copy installed below instead -- the build-tree path won't exist for an
        # installed consumer.
        target_link_libraries(phash PRIVATE $<BUILD_INTERFACE:${PHASH_LIBJPEG_LIB}>)
        target_include_directories(phash PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/src
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libjpeg-turbo/build
        )
        if(PHASH_INSTALL_PRIVATE_DEPS)
            # Installed under a private name: "libjpeg.a" in a shared prefix would shadow
            # (or be shadowed by) the system libjpeg. CMAKE_STATIC_LIBRARY_PREFIX/SUFFIX
            # rather than a hardcoded "lib…a", which is only right for Unix ar archives.
            set(PHASH_LIBJPEG_INSTALLED_NAME
                "${CMAKE_STATIC_LIBRARY_PREFIX}phash_jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
            target_link_libraries(phash PRIVATE
                $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${PHASH_LIBJPEG_INSTALLED_NAME}>)
            install(FILES ${PHASH_LIBJPEG_LIB} DESTINATION ${CMAKE_INSTALL_LIBDIR}
                    RENAME ${PHASH_LIBJPEG_INSTALLED_NAME})
            list(APPEND PHASH_PC_LIBS_PRIVATE "-lphash_jpeg")
        endif()
    else()
        if(PHASH_STRICT_DEPS)
            message(FATAL_ERROR "libjpeg (static) not found in vendor/libjpeg-turbo/build (PHASH_STRICT_DEPS=ON). Build the submodule first: cd vendor/libjpeg-turbo && cmake -B build && cmake --build build --target jpeg-static --config Release")
        else()
            message(WARNING "libjpeg (static) not found in vendor/libjpeg-turbo/build. Falling back to stb_image.")
        endif()
    endif()
endif()

# --- Bundled zlib-ng (zlib-compat inflate/deflate used by libpng/spng) ---
set(PHASH_ZLIB_TARGET z)
# When libphash is pulled in via add_subdirectory(), the parent project owns its own
# build settings and its own zlib. Forcing ours into the cache would silently reconfigure
# the parent: BUILD_SHARED_LIBS and BUILD_TESTING are global CMake variables, and
# ZLIB::ZLIB / ZLIB_INCLUDE_DIR / ZLIB_LIBRARY are how the parent's find_package(ZLIB)
# reports its result. So: if the parent already brought its own zlib, step
# aside and use it; otherwise configure zlib-ng but leave the parent's cache intact.
if(NOT PHASH_IS_TOP_LEVEL AND PHASH_USE_ZLIB_NG)
    # A ZLIB_LIBRARY cache entry that points inside this build tree is libphash's
    # own, not the parent's zlib. Standing down for it would drop the zlib-ng target
    # while that path stays on libpng's link line, so ignore it.
    set(PHASH_PARENT_HAS_ZLIB OFF)
    if(DEFINED CACHE{ZLIB_LIBRARY})
        string(FIND "$CACHE{ZLIB_LIBRARY}" "${CMAKE_CURRENT_BINARY_DIR}/vendor/zlib-ng" PHASH_STALE_ZLIB_PIN)
        if(NOT PHASH_STALE_ZLIB_PIN EQUAL 0)
            set(PHASH_PARENT_HAS_ZLIB ON)
        else()
            unset(ZLIB_LIBRARY CACHE)
            unset(ZLIB_INCLUDE_DIR CACHE)
        endif()
    endif()
    if(TARGET ZLIB::ZLIB OR PHASH_PARENT_HAS_ZLIB)
        message(STATUS
            "phash: parent project already provides zlib; using it instead of the "
            "bundled zlib-ng (set PHASH_USE_ZLIB_NG=OFF to silence this).")
        set(PHASH_USE_ZLIB_NG OFF)
    endif()
endif()

if(PHASH_USE_ZLIB_NG AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/zlib-ng/CMakeLists.txt")
    # zlib-ng's own options: scoped to it, harmless to a parent.
    set(ZLIB_COMPAT ON CACHE BOOL "" FORCE)
    set(ZLIB_ALIASES ON CACHE BOOL "" FORCE)
    set(WITH_GTEST OFF CACHE BOOL "" FORCE)
    set(WITH_FUZZERS OFF CACHE BOOL "" FORCE)
    set(WITH_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(WITH_BENCHMARK_APPS OFF CACHE BOOL "" FORCE)

    # BUILD_SHARED_LIBS and BUILD_TESTING are NOT zlib-ng's own -- they are global.
    # zlib-ng must see them OFF while it configures, so remember what the parent had
    # and put it back immediately afterwards. At top level there is nothing to
    # preserve, so the plain forced value is fine.
    phash_push_global_build_flags()
    add_subdirectory(vendor/zlib-ng EXCLUDE_FROM_ALL)
    phash_pop_global_build_flags()

    # Predefine ZLIB::ZLIB before libpng's find_package(ZLIB) runs: FindZLIB.cmake
    # skips creating its own imported target when one already exists, so libpng
    # links against zlib-ng instead of the system zlib it would otherwise find.
    # Guarded: creating a target that already exists is a hard error, and the parent
    # may well have defined it (that case is handled above by standing down).
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlib-ng)
    endif()
    # find_package(ZLIB) also short-circuits its own find_path()/find_library()
    # searches once these are already set, so pin them to zlib-ng's generated
    # zlib.h. Without this, libpng's pnglibconf.h gets baked with the system
    # zlib's ZLIB_VERNUM while the actual compile picks up zlib-ng's zlib.h
    # through the include path, and the two disagree at compile time.
    #
    # These are deliberately NORMAL (directory-scope) variables, not cache
    # entries. vendor/libpng is configured from a child scope and inherits them,
    # which is all its find_package(ZLIB) needs -- while nothing of ours is left
    # behind in the parent project's cache, and nothing survives into the *next*
    # configure run of the same build tree. As cache entries they would trip the
    # stand-down check above ("DEFINED CACHE{ZLIB_LIBRARY}" => the parent brought
    # its own zlib) on every re-configure: the zlib-ng target would never be
    # created while libpng kept its archive on its link interface --
    #   "No rule to make target 'phash_build/vendor/zlib-ng/libz.a'".
    #
    # ZLIB_LIBRARY holds the zlib-ng TARGET NAME, not a path to libz.a: the
    # archive's file name is a guess (wrong on MSVC, and wrong for any build in
    # which zlib-ng comes out shared), and CMake resolves a target name to the
    # real artifact plus a build-order dependency. FindZLIB never puts this value
    # on a link line here -- it is guarded by "if(NOT ZLIB_LIBRARY)", used only
    # for find_package_handle_standard_args() and for an IMPORTED_LOCATION on a
    # ZLIB::ZLIB it declines to create, because the alias above already exists.
    # libpng links ZLIB::ZLIB (i.e. the target), which is what makes this work.
    set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/vendor/zlib-ng")
    set(ZLIB_LIBRARY zlib-ng)
    target_compile_definitions(phash PRIVATE PH_USE_ZLIB_NG)
    set(PHASH_ZLIB_TARGET zlib-ng)
    if(PHASH_INSTALL_PRIVATE_DEPS)
        install(TARGETS zlib-ng ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        # phash_static_lib_filename(), not a hardcoded "libz.a": see that
        # function's own comment (near the top of this file) for why.
        phash_static_lib_filename(PHASH_ZLIB_LIB_FILENAME zlib-ng)
        set(PHASH_ZLIB_INSTALL_LINK_LIB
            $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${PHASH_ZLIB_LIB_FILENAME}>)
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
    target_sources(phash PRIVATE src/loaders/png_libpng.c)
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
        # "png_static") via OUTPUT_NAME -- read it back rather than assuming,
        # for the pkg-config "-l" flag below (phash_static_lib_filename()
        # covers the actual installed file name, but pkg-config's -lNAME
        # needs the bare name instead).
        get_target_property(PHASH_PNG_LIB_NAME png_static OUTPUT_NAME)
        if(NOT PHASH_PNG_LIB_NAME)
            set(PHASH_PNG_LIB_NAME png_static)
        endif()
        install(TARGETS png_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        phash_static_lib_filename(PHASH_PNG_LIB_FILENAME png_static)
        list(APPEND PHASH_INSTALL_LINK_LIBS
            $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${PHASH_PNG_LIB_FILENAME}>)
        list(APPEND PHASH_PC_LIBS_PRIVATE "-l${PHASH_PNG_LIB_NAME}")
        set(PHASH_ZLIB_NEEDED ON)
    endif()
endif()

# --- Bundled spng ---
if(PHASH_USE_SPNG)
    target_compile_definitions(phash PRIVATE PH_USE_SPNG)
    target_sources(phash PRIVATE src/loaders/png_spng.c)
    add_library(spng_static STATIC vendor/spng/spng/spng.c)
    target_include_directories(spng_static PUBLIC vendor/spng/spng)
    target_link_libraries(phash PRIVATE $<BUILD_INTERFACE:spng_static> $<BUILD_INTERFACE:${PHASH_ZLIB_TARGET}>)
    target_include_directories(phash PRIVATE vendor/spng/spng)
    if(PHASH_INSTALL_PRIVATE_DEPS)
        install(TARGETS spng_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        phash_static_lib_filename(PHASH_SPNG_LIB_FILENAME spng_static)
        list(APPEND PHASH_INSTALL_LINK_LIBS
            $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${PHASH_SPNG_LIB_FILENAME}>)
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
    set(WEBP_LINK_STATIC ON CACHE BOOL "" FORCE)

    # Same global-flag hazard as the zlib-ng block above: libwebp's configure also
    # forces BUILD_SHARED_LIBS.
    phash_push_global_build_flags()
    add_subdirectory(vendor/libwebp EXCLUDE_FROM_ALL)
    phash_pop_global_build_flags()

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
        # libwebp is why phash_static_lib_filename() takes a src_dir: under MSVC it
        # sets CMAKE_STATIC_LIBRARY_PREFIX to "lib" inside its own directory scope
        # (installed file is "libwebpdecoder.lib", not "webpdecoder.lib").
        phash_static_lib_filename(PHASH_WEBP_LIB_FILENAME webpdecoder
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libwebp")
        list(APPEND PHASH_INSTALL_LINK_LIBS
            $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${PHASH_WEBP_LIB_FILENAME}>)
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
    # NOT WIN32: same reason as that target_link_libraries() call -- there is
    # no libm on Windows.
    if(NOT WIN32)
        list(APPEND PHASH_PC_LIBS_PRIVATE "-lm")
    endif()
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

    # A shared build needs a versioned soname: it's the only signal a consumer's
    # linker gets when libphash breaks compatibility across a major version.
    set_target_properties(phash PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/libphash.h)

    install(TARGETS phash
        EXPORT phashTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Installed alongside libphash.h so consumers get a compile-time version
    # macro (#if PH_VERSION_NUMBER >= ...), not just the runtime ph_version().
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/phash_version.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
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

    # Mirrors the PUBLIC target_compile_definitions(phash PHASH_STATIC_DEFINE)
    # above for the pkg-config path (e.g. MinGW consumers, who use pkg-config
    # rather than find_package(phash)) -- a no-op everywhere except Windows,
    # where PH_API needs it to know a static archive, not a DLL, is being linked.
    set(PHASH_PC_CFLAGS_EXTRA "")
    if(NOT PHASH_BUILD_SHARED)
        set(PHASH_PC_CFLAGS_EXTRA "-DPHASH_STATIC_DEFINE")
    endif()

    # Keep libphash.pc relocatable. Substituting CMAKE_INSTALL_FULL_LIBDIR would bake
    # the configure-time absolute path into the file, so moving (or packaging and
    # unpacking elsewhere) the install tree would yield -L/-I pointing at a directory
    # that is not there. Expressing libdir/includedir relative to ${prefix}
    # means pkg-config's own --define-prefix (and the redefinition of prefix that
    # relocatable distributions do) reaches them.
    #
    # GNUInstallDirs permits CMAKE_INSTALL_LIBDIR/INCLUDEDIR to be absolute -- some
    # distributions and toolchain files set them that way. An absolute value cannot
    # be made prefix-relative, so it is emitted verbatim: such an install is not
    # relocatable by construction, and silently prefixing it would produce a path
    # that is simply wrong.
    foreach(_dir LIBDIR INCLUDEDIR)
        if(IS_ABSOLUTE "${CMAKE_INSTALL_${_dir}}")
            set(PHASH_PC_${_dir} "${CMAKE_INSTALL_${_dir}}")
            message(STATUS "phash: CMAKE_INSTALL_${_dir} is absolute "
                           "(${CMAKE_INSTALL_${_dir}}); libphash.pc will not be relocatable")
        else()
            set(PHASH_PC_${_dir} "\${prefix}/${CMAKE_INSTALL_${_dir}}")
        endif()
    endforeach()

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/libphash.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/libphash.pc
        @ONLY
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libphash.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )
endif()

# --- Mock decoder backend (test-only) ---
# Deliberately independent of PHASH_BUILD_TESTS: that option is ON by default, so
# tying the mock to it would put a loader that "decodes" any buffer starting with
# DE AD into a 1x1 image into every default Release build.
if(PHASH_ENABLE_MOCK_BACKEND)
    message(WARNING "PHASH_ENABLE_MOCK_BACKEND=ON: the DE AD mock decoder is compiled "
                    "into libphash. Never ship this build.")
    target_compile_definitions(phash PUBLIC PH_ENABLE_MOCK_BACKEND)
endif()

# --- Fuzzing (libFuzzer, Clang only) ---
# Configured before the tests below on purpose: when fuzzers and tests are enabled
# together, the ctest binaries link a sanitizer-instrumented libphash and therefore
# need the very same sanitizer flags at link time. Without them the link fails on
# undefined __asan_*/__ubsan_* symbols.
if(PHASH_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "PHASH_BUILD_FUZZERS requires Clang (libFuzzer is a Clang/compiler-rt feature).")
    endif()

    # Instrument the library for coverage/sanitizers without pulling in a fuzzer main,
    # so it can still be linked into the plain ctest binaries below.
    set(PHASH_FUZZ_INSTRUMENT_FLAGS -fsanitize=fuzzer-no-link,address,undefined)
    target_compile_options(phash PRIVATE ${PHASH_FUZZ_INSTRUMENT_FLAGS} -g -O1)
    target_link_options(phash PRIVATE ${PHASH_FUZZ_INSTRUMENT_FLAGS})

    add_executable(fuzz_load tests/fuzz/fuzz_load.c)
    target_compile_options(fuzz_load PRIVATE ${PHASH_OWN_COMPILE_OPTIONS})
    target_link_libraries(fuzz_load PRIVATE phash)
    target_compile_options(fuzz_load PRIVATE -fsanitize=fuzzer,address,undefined -g -O1)
    target_link_options(fuzz_load PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

# --- Tests ---
if(PHASH_BUILD_TESTS)
    enable_testing()
    file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/src/test_*.c")
    foreach(test_src ${TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        # Tests reach internal functions (src/*.h) as well as the public API. A shared
        # libphash exports only the public API, so in a shared build the tests link the
        # library's object files and its link dependencies directly instead.
        if(PHASH_BUILD_SHARED)
            add_executable(${test_name} ${test_src} $<TARGET_OBJECTS:phash>)
            target_link_libraries(${test_name} PRIVATE $<GENEX_EVAL:$<TARGET_PROPERTY:phash,LINK_LIBRARIES>>)
            target_include_directories(${test_name} PRIVATE
                $<TARGET_PROPERTY:phash,INCLUDE_DIRECTORIES>)
        else()
            add_executable(${test_name} ${test_src})
            target_link_libraries(${test_name} PRIVATE phash)
        endif()
        target_compile_options(${test_name} PRIVATE ${PHASH_OWN_COMPILE_OPTIONS})
        target_include_directories(${test_name} PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
        )
        target_compile_definitions(${test_name} PRIVATE
            TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data\"
            PH_TESTING
            $<TARGET_PROPERTY:phash,COMPILE_DEFINITIONS>
        )
        if(PHASH_BUILD_FUZZERS)
            # Match the instrumentation of the library the test links against.
            target_compile_options(${test_name} PRIVATE ${PHASH_FUZZ_INSTRUMENT_FLAGS} -g -O1)
            target_link_options(${test_name} PRIVATE ${PHASH_FUZZ_INSTRUMENT_FLAGS})
        endif()
        if(PHASH_COVERAGE)
            # -fprofile-update=atomic: see the matching comment on the `phash`
            # target above -- must match here too, since test_batch_stress.c and
            # test_thread_safety.c are themselves gcov-instrumented and call into
            # the same racing header-inline functions from multiple threads.
            target_compile_options(${test_name} PRIVATE --coverage -O0 -g -fprofile-update=atomic)
            target_link_options(${test_name} PRIVATE --coverage)
        endif()
        add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)
    endforeach()

    # test_abi.c built again under -fshort-enums, which shrinks any public enum without
    # its width spacer. Header-only by design, so it is not linked against phash. MSVC has
    # no such flag (nor clang-cl, which MSVC also covers), and its enums are always int-sized.
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
        add_executable(test_abi_short_enums ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/test_abi.c)
        target_include_directories(test_abi_short_enums PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_BINARY_DIR}/generated
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/src
        )
        target_compile_options(test_abi_short_enums PRIVATE ${PHASH_OWN_COMPILE_OPTIONS} -fshort-enums)
        add_test(NAME test_abi_short_enums COMMAND $<TARGET_FILE:test_abi_short_enums>)
    endif()

    # Add benchmark as a smoke test (verifies loading and hashing works)
    add_test(NAME benchmark_smoke COMMAND $<TARGET_FILE:test_benchmark> hash ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/photo.jpeg 10)
endif()
