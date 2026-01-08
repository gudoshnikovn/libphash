CC = gcc
CFLAGS = -I./include -O3 -Wall -Wextra -fPIC
LDFLAGS = -lm

UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
    CFLAGS += -msse4.2
endif
ifeq ($(UNAME_M),arm64)
    CFLAGS += -march=armv8-a+simd
endif

LIB_NAME = libphash.a
OBJ_DIR = obj
SRC_DIR = src
HASH_DIR = $(SRC_DIR)/hashes
TEST_DIR = tests
INC_DIR = include

SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(HASH_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=%)

all: $(LIB_NAME) $(TEST_BINS)

format:
	find $(SRC_DIR) $(TEST_DIR) $(INC_DIR) -name "*.c" -o -name "*.h" | xargs clang-format -i

debug: CFLAGS = -I./include -g -O0 -fsanitize=address,undefined -Wall -Wextra -fPIC
debug: clean all

$(LIB_NAME): $(OBJS)
	ar rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test_%: $(TEST_DIR)/test_%.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< $(LIB_NAME) -o $@ $(LDFLAGS)

test: $(TEST_BINS)
	@for test in $(TEST_BINS); do ./$$test || exit 1; done
	@echo "ALL TESTS PASSED"

clean:
	rm -rf $(OBJ_DIR) *.a test_*

.PHONY: all debug test clean format
