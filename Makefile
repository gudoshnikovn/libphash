CC = gcc
# Added optimization flags and math support
CFLAGS = -I./include -O3 -Wall -Wextra -fPIC
LDFLAGS = -lm

LIB_NAME = libphash.a
OBJ_DIR = obj
SRC_DIR = src
HASH_DIR = $(SRC_DIR)/hashes
TEST_DIR = tests

# Library source files (including new algorithms)
SRCS = $(SRC_DIR)/core.c \
       $(SRC_DIR)/image.c \
       $(HASH_DIR)/ahash.c \
       $(HASH_DIR)/dhash.c \
       $(HASH_DIR)/phash.c \
       $(HASH_DIR)/bmh.c \
       $(HASH_DIR)/color_hash.c \
       $(HASH_DIR)/common.c

# Map .c paths to .o paths
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Automatically find all test files
TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=%)

.PHONY: all clean test

all: $(LIB_NAME) $(TEST_BINS)

# Build the static library
$(LIB_NAME): $(OBJS)
	@echo "Archiving $(LIB_NAME)..."
	@ar rcs $@ $^

# Compile object files and recreate directory structure
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Build test executables
# Order is critical: test source -> static library -> linker flags
test_%: $(TEST_DIR)/test_%.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< $(LIB_NAME) -o $@ $(LDFLAGS)

# Execute all tests sequentially
test: $(TEST_BINS)
	@echo "Starting test execution..."
	@for test in $(TEST_BINS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done
	@echo "------------------------------"
	@echo "ALL TESTS PASSED SUCCESSFULLY"

# Remove build artifacts
clean:
	rm -rf $(OBJ_DIR) *.a test_*
	@echo "Cleaned up build artifacts."
