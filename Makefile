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

# --- Optional batch-hashing thread pool (task 9) ---
# Off by default: keeps the portable/minimal Makefile build free of pthread linkage.
PHASH_ENABLE_THREADS ?= 0
ifeq ($(PHASH_ENABLE_THREADS),1)
    CFLAGS += -DPH_ENABLE_THREADS
    LDFLAGS += -lpthread
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
CFLAGS += -I./$(TEST_DIR) -DPH_TESTING -DTEST_DATA_DIR=\"$(shell pwd)/tests/data\"

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

# Diagnostic/Debug build
debug: CFLAGS += -g -O0 -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean all

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

# Coverage build
coverage: CFLAGS += -g -O0 --coverage -DPH_TESTING
coverage: LDFLAGS += --coverage
coverage: clean test
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
