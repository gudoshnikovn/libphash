# Compiler and common flags
CC ?= gcc
CFLAGS = -I./include -O3 -Wall -Wextra -fPIC
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
TEST_DIR = tests
INC_DIR = include

# Sources and Objects
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(HASH_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Tests
TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=%)

# Default target
all: $(LIB_NAME) $(TEST_BINS)

# Diagnostic/Debug build
debug: CFLAGS = -I./include -g -O0 -fsanitize=address,undefined -Wall -Wextra -fPIC
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

# Legacy/Standalone benchmark target (internal use)
benchmark: test_benchmark
	./test_benchmark hash tests/photo.jpeg 100

clean:
	rm -rf $(OBJ_DIR) *.a test_* benchmark

.PHONY: all debug test clean format benchmark
