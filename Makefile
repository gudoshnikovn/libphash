CC ?= gcc
GENERATED_DIR = generated
CFLAGS = -I./include -I./src -I./$(GENERATED_DIR) -O3 -Wall -Wextra -fPIC
LDFLAGS = -lm

# Architecture-specific optimizations
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
    CFLAGS += -msse4.2
endif
ifeq ($(UNAME_M),arm64)
    CFLAGS += -march=armv8-a+simd
endif

# --- WebP Support ---
# To use WebP in the standalone Makefile, ensure libwebp is installed and paths are set.
# For bundled/static build, use CMake instead.
USE_WEBP ?= 0
ifeq ($(USE_WEBP),1)
    CFLAGS += -DPH_USE_WEBP
    LDFLAGS += -lwebp -lwebpdecoder
endif

# --- Batch-hashing thread pool (task 9) ---
# ON by default, matching the CMake option PHASH_ENABLE_THREADS (also ON).
# R15/L13: the two build systems used to disagree here (Makefile 0, CMake ON).
# The cost of that divergence was not "one build is leaner": it was that the
# threaded batch path never ran in the local Makefile flow at all -- neither in
# `make test` nor in `make coverage` -- which is exactly how the Windows thread-pool
# defect H1 stayed hidden. `-pthread` is now a dependency of the portable build;
# that is a deliberate trade. Opt out with `make PHASH_ENABLE_THREADS=0`.
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
# Note: PH_TESTING is NOT defined for the library build. The mock decoder
# backend in src/loader.c is opt-in via PHASH_ENABLE_MOCK_BACKEND=1 and must
# never be present in a shipped artifact.
CFLAGS += -I./$(TEST_DIR) -DTEST_DATA_DIR=\"$(shell pwd)/tests/data\"

# Opt-in test-only mock decoder backend (see src/loader.c)
PHASH_ENABLE_MOCK_BACKEND ?= 0
ifeq ($(PHASH_ENABLE_MOCK_BACKEND),1)
CFLAGS += -DPH_ENABLE_MOCK_BACKEND
endif

# --- Instrumented build modes ---
# These are switches rather than target-specific variables on purpose (R15/R23):
# the old `debug: clean all` / `coverage: clean test` shape let `clean` run in
# parallel with compilation under `-jN` and deleted objects out from under the
# compiler. The `debug`/`coverage` targets below now recurse (`$(MAKE) clean`,
# then `$(MAKE) all PHASH_SANITIZE=1`), which is ordered at any -j level, and
# the flags are picked up here instead of being attached to the target.
PHASH_SANITIZE ?= 0
ifeq ($(PHASH_SANITIZE),1)
CFLAGS += -g -O0 -fsanitize=address,undefined
LDFLAGS += -fsanitize=address,undefined
# R46: the vendored stb_image_resize2 packs filter coefficients with deliberately
# unaligned 64-bit moves (STBIR_MOVE_2 casts float* -> stbir_uint64*), which UBSan
# reports as `load/store of misaligned address ... for type 'stbir_uint64'`. The
# misaligned buffer is stb's own internal coefficient array (16-byte aligned at the
# base; the odd stride is stb's), not anything we pass in, and the pattern is
# unchanged in current upstream master (v2.18) -- so it cannot be fixed by a bump.
# We exempt ONLY the TU that instantiates stb (src/image/stb_resize_impl.c, which
# contains no code of ours) from the alignment check, so our own findings still fire.
STB_NOSAN_CFLAGS = -fno-sanitize=alignment
endif

PHASH_COVERAGE ?= 0
ifeq ($(PHASH_COVERAGE),1)
CFLAGS += -g -O0 --coverage
LDFLAGS += --coverage
endif

# Sources and Objects
LOADER_DIR = $(SRC_DIR)/loaders
IMAGE_DIR = $(SRC_DIR)/image
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(HASH_DIR)/*.c) $(wildcard $(LOADER_DIR)/*.c) $(wildcard $(IMAGE_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Tests
TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=%)

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

# Reformat code
format:
	find $(SRC_DIR) $(TEST_DIR) $(INC_DIR) -name "*.c" -o -name "*.h" | xargs clang-format -i

# Library build
$(LIB_NAME): $(OBJS)
	ar rcs $@ $^

# Object file compilation
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# R46: stb_image_resize2 implementation TU — see the PHASH_SANITIZE block above.
# Empty outside sanitizer builds, so release builds are unaffected.
STB_NOSAN_CFLAGS ?=
$(OBJ_DIR)/image/stb_resize_impl.o: $(SRC_DIR)/image/stb_resize_impl.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(STB_NOSAN_CFLAGS) -c $< -o $@

# Test compilation
test_%: $(TEST_DIR)/test_%.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< $(LIB_NAME) -o $@ $(LDFLAGS)

# Run all tests
test: $(TEST_BINS)
	@for test in $(TEST_BINS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done
	@echo "ALL TESTS PASSED"

# Coverage build. Recursive for the same reason as `debug` above.
# Note this inherits PHASH_ENABLE_THREADS=1, so the threaded batch path in
# src/batch.c is instrumented and executed here (it was dead before R15).
coverage:
	@$(MAKE) clean
	@$(MAKE) test PHASH_COVERAGE=1
	@echo "Generating coverage reports..."
	@mkdir -p docs/coverage
	@lcov --capture --directory . --output-file docs/coverage/coverage.info --ignore-errors mismatch,mismatch,unused,unused
	@lcov --remove docs/coverage/coverage.info '/usr/*' 'tests/*' 'vendor/*' --output-file docs/coverage/coverage.info --ignore-errors unused,unused
	@genhtml docs/coverage/coverage.info --output-directory docs/coverage/html
	@echo "Coverage report generated at docs/coverage/html/index.html"

# Legacy/Standalone benchmark target (internal use)
benchmark: test_benchmark
	./test_benchmark hash tests/data/photo.jpeg 100

# Smoke-test install()/pkg-config/find_package(phash) packaging (task 4).
# Builds+installs into a throwaway prefix, then builds a consumer against it.
install-test:
	./scripts/smoke_install.sh static
	./scripts/smoke_install.sh shared

clean:
	rm -rf $(OBJ_DIR) $(GENERATED_DIR) *.a test_* benchmark build .cache docs/coverage
	find . -name "*.gcda" -delete
	find . -name "*.gcno" -delete
	find . -name "*.gcov" -delete
	rm -f tests/output_*.jpeg

# Native linux/arm64 dev container (task 17, tasks/17_docker_dev_environment.md).
# Functional Linux/GCC sanity check only — NOT a substitute for the CI matrix (task 7),
# and NEVER use this image with --platform linux/amd64 for perf numbers (QEMU-emulated
# x86_64 invalidates the zlib-ng benchmark from task 6).
DOCKER_IMAGE = libphash-dev-arm64

docker-build:
	docker build -t $(DOCKER_IMAGE) .

docker-test: docker-build
	docker run --rm $(DOCKER_IMAGE) bash -c \
		"mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$$(nproc) && ctest --output-on-failure"

docker-shell: docker-build
	docker run --rm -it $(DOCKER_IMAGE) bash

.PHONY: all debug test clean format benchmark coverage docker-build docker-test docker-shell install-test
