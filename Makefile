# Clang/LLVM is the default (the libFuzzer build requires it), but `CC=gcc make` or
# any environment CC still overrides it. Plain
# `CC ?= clang` would NOT do that: GNU Make ships a built-in default of
# `CC = cc`, so by the time this line runs CC already looks "defined" and
# `?=` becomes a no-op. `origin` distinguishes that built-in default from an
# actual override and only replaces the former.
ifeq ($(origin CC),default)
CC = clang
endif
GENERATED_DIR = generated
# The same dialect CMakeLists.txt pins (CMAKE_C_STANDARD 17, extensions OFF).
# Left unset, the dialect would follow the compiler's default, which moves (GCC 15
# defaults to gnu23), so the build would change standard under anyone who upgrades.
#
# -ffp-contract=off: the same explicit no-FMA-fusion policy CMakeLists.txt sets, and
# for the same reason -- left to the compiler it follows the dialect (GCC fuses under
# gnuNN, not under cNN) and moves pHash's bits on any target with hardware FMA.
CFLAGS = -std=c17 -ffp-contract=off -I./include -I./src -I./$(GENERATED_DIR) -O3 -Wall -Wextra -fPIC
LDFLAGS = -lm

# Extra flags from the command line, appended after everything this file sets: `make
# CFLAGS=...` would replace CFLAGS wholesale, include paths and all. A 32-bit build on a
# 64-bit host is `make EXTRA_CFLAGS=-m32 EXTRA_LDFLAGS=-m32`.
EXTRA_CFLAGS ?=
EXTRA_LDFLAGS ?=

# Architecture flags follow the TARGET, read from the compiler's predefined macros (with
# EXTRA_CFLAGS, so -m32 counts), not from `uname -m`, which names the host. Why each
# flag, and why arm64 gets none: see the matching block in CMakeLists.txt.
TARGET_MACROS := $(shell $(CC) $(EXTRA_CFLAGS) -dM -E -x c /dev/null 2>/dev/null)
ifneq (,$(findstring __x86_64__,$(TARGET_MACROS)))
    CFLAGS += -msse4.2
else ifneq (,$(findstring __i386__,$(TARGET_MACROS)))
    CFLAGS += -msse2 -mfpmath=sse
endif

# --- WebP Support ---
# To use WebP in the standalone Makefile, ensure libwebp is installed and paths are set.
# For bundled/static build, use CMake instead.
USE_WEBP ?= 0
ifeq ($(USE_WEBP),1)
    CFLAGS += -DPH_USE_WEBP
    LDFLAGS += -lwebp -lwebpdecoder
endif

# --- Batch-hashing thread pool ---
# ON by default, matching the CMake option PHASH_ENABLE_THREADS, so `make test` and
# `make coverage` exercise the threaded batch path too. That makes `-pthread` a
# dependency of the portable build, deliberately. Opt out with
# `make PHASH_ENABLE_THREADS=0`.
PHASH_ENABLE_THREADS ?= 1
ifeq ($(PHASH_ENABLE_THREADS),1)
    CFLAGS += -DPH_ENABLE_THREADS -pthread
    LDFLAGS += -pthread
endif

# OS-specific flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -ldl
endif

# Project structure
LIB_NAME = libphash.a
OBJ_DIR = obj
SRC_DIR = src
HASH_DIR = $(SRC_DIR)/hashes
TEST_DIR = tests/src
INC_DIR = include

# CFLAGS updates
# The mock decoder backend in src/loader.c is opt-in via PHASH_ENABLE_MOCK_BACKEND=1
# and must never be present in a shipped artifact.
CFLAGS += -I./$(TEST_DIR) -DTEST_DATA_DIR=\"$(shell pwd)/tests/data\"

# Opt-in test-only mock decoder backend (see src/loader.c)
PHASH_ENABLE_MOCK_BACKEND ?= 0
ifeq ($(PHASH_ENABLE_MOCK_BACKEND),1)
CFLAGS += -DPH_ENABLE_MOCK_BACKEND
endif

# --- Instrumented build modes ---
# These are switches rather than target-specific variables on purpose: a
# `debug: clean all` prerequisite shape lets `clean` run in parallel with
# compilation under `-jN` and delete objects out from under the compiler. The
# `debug`/`coverage` targets below recurse instead (`$(MAKE) clean`, then
# `$(MAKE) all PHASH_SANITIZE=1`), which is ordered at any -j level, and the flags
# are picked up here instead of being attached to the target.
PHASH_SANITIZE ?= 0
ifeq ($(PHASH_SANITIZE),1)
CFLAGS += -g -O0 -fsanitize=address,undefined
LDFLAGS += -fsanitize=address,undefined
# The vendored stb_image_resize2 packs filter coefficients with deliberately
# unaligned 64-bit moves (STBIR_MOVE_2 casts float* -> stbir_uint64*), which UBSan
# reports as `load/store of misaligned address ... for type 'stbir_uint64'`. The
# misaligned buffer is stb's own internal coefficient array (16-byte aligned at the
# base; the odd stride is stb's), not anything we pass in, and the pattern is
# unchanged upstream as of the vendored v2.18 -- so it cannot be fixed by a bump.
# We exempt ONLY the TU that instantiates stb (src/image/stb_resize_impl.c, which
# contains no code of ours) from the alignment check, so our own findings still fire.
STB_NOSAN_CFLAGS = -fno-sanitize=alignment
endif

PHASH_COVERAGE ?= 0
ifeq ($(PHASH_COVERAGE),1)
# -fprofile-update=atomic: test_batch_stress/test_thread_safety run gcov-instrumented
# code from multiple threads, and a static inline function defined in a header
# (e.g. ph_safe_image_alloc_size() in safety.h) gets its own counter instance per
# translation unit -- default (non-atomic) counter increments race there and corrupt
# the merged .gcda, surfacing as `geninfo: ERROR: Unexpected negative count` (a known
# GCC/gcov limitation, https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68080 -- see the
# matching comment in CMakeLists.txt). Applied here too since this Makefile flow builds the same
# multi-threaded test binaries.
CFLAGS += -g -O0 --coverage -fprofile-update=atomic
LDFLAGS += --coverage
endif

# Command-line additions go last, so they can override anything above.
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)

# Sources and Objects
LOADER_DIR = $(SRC_DIR)/loaders
IMAGE_DIR = $(SRC_DIR)/image
# The libpng backend needs its vendored library, which only the CMake build provides;
# this flow decodes PNG through stb_image.
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(HASH_DIR)/*.c) $(wildcard $(IMAGE_DIR)/*.c) \
       $(filter-out $(LOADER_DIR)/png_libpng.c,$(wildcard $(LOADER_DIR)/*.c))
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Tests
TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=%) test_abi_short_enums

# Default target
all: $(LIB_NAME) $(TEST_BINS)

# Version header (single source of truth: project() VERSION in CMakeLists.txt)
$(GENERATED_DIR)/phash_version.h: CMakeLists.txt include/phash_version.h.in scripts/gen_version.sh
	@./scripts/gen_version.sh CMakeLists.txt include/phash_version.h.in $@

$(OBJS): $(GENERATED_DIR)/phash_version.h

# Diagnostic/Debug build (ASan + UBSan).
# NOTE: `debug` only REBUILDS; it does NOT run the tests -- follow it with `make test`.
# Recursive by design so that `clean` finishes before anything compiles even under -jN
# (see the PHASH_SANITIZE block above).
debug:
	@$(MAKE) clean
	@$(MAKE) all PHASH_SANITIZE=1

# Reformat code. The perimeter and the pinned clang-format major live in the
# script, which the CI format-check job calls too.
format:
	./scripts/format.sh

# Library build, byte-identical from identical sources: GNU and LLVM ar zero the member
# timestamps with D; Apple's ar has no D and reads ZERO_AR_DATE instead (the same rule
# as cmake/deterministic_archives.cmake). Decided by what `ar --version` says, not by
# the OS -- Linux can have LLVM's ar, macOS GNU's.
AR_VERSION := $(shell $(AR) --version 2>/dev/null)
ifneq (,$(filter GNU LLVM,$(AR_VERSION)))
    AR_DETERMINISTIC = $(AR) rcsD
else ifeq ($(UNAME_S),Darwin)
    AR_DETERMINISTIC = ZERO_AR_DATE=1 $(AR) rcs
else
    AR_DETERMINISTIC = $(AR) rcs
endif

$(LIB_NAME): $(OBJS)
	$(AR_DETERMINISTIC) $@ $^

# Object file compilation.
# -fvisibility=hidden: only what include/libphash.h marks PH_API is visible outside the
# archive, as in the CMake build, so a consumer that links libphash.a into a shared
# library of its own does not re-export the internals or the stb_image instantiation.
# The library's objects only: a test binary defines hooks such as
# __asan_default_options() that the sanitizer runtime has to find.
LIB_CFLAGS = -fvisibility=hidden
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LIB_CFLAGS) -c $< -o $@

# stb_image_resize2 implementation TU — see the PHASH_SANITIZE block above.
# Empty outside sanitizer builds, so release builds are unaffected.
STB_NOSAN_CFLAGS ?=
$(OBJ_DIR)/image/stb_resize_impl.o: $(SRC_DIR)/image/stb_resize_impl.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LIB_CFLAGS) $(STB_NOSAN_CFLAGS) -c $< -o $@

# Test compilation
# test_abi.c built again under -fshort-enums, which shrinks any public enum without its
# width spacer. Header-only by design, so it is not linked against the library.
test_abi_short_enums: $(TEST_DIR)/test_abi.c $(GENERATED_DIR)/phash_version.h
	$(CC) $(CFLAGS) -fshort-enums $< -o $@ $(LDFLAGS)

test_%: $(TEST_DIR)/test_%.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< $(LIB_NAME) -o $@ $(LDFLAGS)

# Run all tests. Each test is its own target, so `make test -j8` runs them side by side:
# they share no files (each names its own scratch files) and none depends on timing.
# A test's output goes to <test>.log and is printed when it fails; make starts no new
# test after a failure.
TEST_RUNS = $(TEST_BINS:%=run-%)

test: $(TEST_RUNS)
	@echo "ALL TESTS PASSED"

# Not .PHONY: make skips pattern rules for phony targets. No file named run-* is ever
# made, so the recipe runs every time anyway.
run-%: %
	@if ./$* >$*.log 2>&1; then \
		echo "PASS $*"; \
	else \
		cat $*.log; \
		echo "FAIL $*"; \
		exit 1; \
	fi

# Coverage build. Recursive for the same reason as `debug` above.
# Note this inherits PHASH_ENABLE_THREADS=1, so the threaded batch path in
# src/batch.c is instrumented and executed here.
# Branches as well as lines. lcov 2.x stops on llvm-cov's gcov emulation without the
# suppressions: "mismatch" and "unused" for harmless version and zero-hit warnings,
# "inconsistent" for a line hit with none of its branches evaluated (inside stb_image),
# "unsupported" for branch data it cannot attribute. Each appears twice so that lcov
# reports the suppressed messages instead of only counting them.
LCOV_BRANCH_FLAGS = --rc branch_coverage=1 \
	--ignore-errors mismatch,mismatch,unused,unused,inconsistent,inconsistent,unsupported,unsupported

coverage:
	@$(MAKE) clean
	@$(MAKE) test PHASH_COVERAGE=1
	@echo "Generating coverage reports..."
	@mkdir -p docs/coverage
	@lcov --capture --directory . --output-file docs/coverage/coverage.info $(LCOV_BRANCH_FLAGS)
	@lcov --remove docs/coverage/coverage.info '/usr/*' 'tests/*' 'vendor/*' --output-file docs/coverage/coverage.info $(LCOV_BRANCH_FLAGS)
	@genhtml docs/coverage/coverage.info --output-directory docs/coverage/html $(LCOV_BRANCH_FLAGS)
	@python3 scripts/check_coverage.py --report docs/coverage/coverage.info
	@echo "Coverage report generated at docs/coverage/html/index.html"

# Coverage for the CMake build's native decoders (libjpeg-turbo/libpng/
# libwebp/zlib-ng) -- `coverage` above only ever measures the stb_image-only
# Makefile flow. See scripts/coverage_cmake.sh for what this actually runs.
coverage-cmake:
	@./scripts/coverage_cmake.sh

# The benchmark is a measuring tool, not a test: outside TEST_BINS, built and run here.
bench_hash: $(TEST_DIR)/bench_hash.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< $(LIB_NAME) -o $@ $(LDFLAGS)

benchmark: bench_hash
	./bench_hash hash tests/data/photo.jpeg 100

# --- install / uninstall ---
# `make install PREFIX=/opt/libphash` (default /usr/local; DESTDIR for staging) puts
# libphash.a, the two public headers and libphash.pc under PREFIX/lib and PREFIX/include.
# The .pc comes from the same libphash.pc.in as the CMake build's, in the same
# relocatable form (prefix relative to the .pc file's own directory), with what this
# build links in Libs: there is no shared library for Libs.private to serve.
PREFIX ?= /usr/local
DESTDIR ?=
PH_VERSION = $(shell sed -nE 's/.*project\([^)]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -n1)
PC_LIBS = -lm
ifeq ($(PHASH_ENABLE_THREADS),1)
    PC_LIBS += -lpthread
endif
ifeq ($(UNAME_S),Linux)
    PC_LIBS += -ldl
endif
ifeq ($(USE_WEBP),1)
    PC_LIBS += -lwebp -lwebpdecoder
endif

install: $(LIB_NAME) $(GENERATED_DIR)/phash_version.h
	install -d $(DESTDIR)$(PREFIX)/lib/pkgconfig $(DESTDIR)$(PREFIX)/include
	install -m 644 $(LIB_NAME) $(DESTDIR)$(PREFIX)/lib/$(LIB_NAME)
	install -m 644 $(INC_DIR)/libphash.h $(GENERATED_DIR)/phash_version.h $(DESTDIR)$(PREFIX)/include/
	sed -e 's|@PHASH_PC_PREFIX@|$${pcfiledir}/../..|' \
	    -e 's|@PHASH_PC_LIBDIR@|$${prefix}/lib|' \
	    -e 's|@PHASH_PC_INCLUDEDIR@|$${prefix}/include|' \
	    -e 's|@PROJECT_VERSION@|$(PH_VERSION)|' \
	    -e 's|@PHASH_PC_RPATH@||' \
	    -e 's|@PHASH_PC_LIBS_STR@| $(PC_LIBS)|' \
	    -e 's|@PHASH_PC_LIBS_PRIVATE_STR@||' \
	    -e 's|@PHASH_PC_CFLAGS_EXTRA@|-DPHASH_STATIC_DEFINE|' \
	    libphash.pc.in > $(DESTDIR)$(PREFIX)/lib/pkgconfig/libphash.pc

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(LIB_NAME) $(DESTDIR)$(PREFIX)/lib/pkgconfig/libphash.pc \
	      $(DESTDIR)$(PREFIX)/include/libphash.h $(DESTDIR)$(PREFIX)/include/phash_version.h

# Smoke-test both builds' packaging: CMake's install()/pkg-config/find_package(phash),
# static and shared, and this file's install/uninstall. Each installs into a throwaway
# prefix and builds a consumer against it.
install-test:
	./scripts/smoke_install.sh static
	./scripts/smoke_install.sh shared
	./scripts/smoke_make_install.sh

clean:
	rm -rf $(OBJ_DIR) $(GENERATED_DIR) *.a *.o test_* bench_hash bench_hash.dSYM build .cache docs/coverage
	find . -name "*.gcda" -delete
	find . -name "*.gcno" -delete
	find . -name "*.gcov" -delete
	rm -f tests/output_*.jpeg

# Native linux/arm64 dev container. Functional Linux/GCC sanity check only — NOT a
# substitute for the CI matrix, and NEVER use this image with --platform linux/amd64
# for perf numbers (QEMU emulation invalidates any benchmark).
DOCKER_IMAGE = libphash-dev-arm64

docker-build:
	docker build -t $(DOCKER_IMAGE) .

# \$$(nproc): make passes on $(nproc) literally, so the container's shell counts the
# container's cores; a bare $$(nproc) would be expanded by the host shell first.
docker-test: docker-build
	docker run --rm $(DOCKER_IMAGE) bash -c \
		"echo \"jobs: \$$(nproc)\" && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$$(nproc) && cd build && ctest -j\$$(nproc) --output-on-failure"

docker-shell: docker-build
	docker run --rm -it $(DOCKER_IMAGE) bash

.PHONY: all debug test clean format benchmark coverage coverage-cmake docker-build docker-test docker-shell install uninstall install-test
