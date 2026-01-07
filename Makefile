CC = gcc
CFLAGS = -I./include -O3 -Wall -Wextra -fPIC
LDFLAGS = -lm

LIB_NAME = libphash.a
OBJ_DIR = obj
SRC_DIR = src
HASH_DIR = $(SRC_DIR)/hashes
TEST_DIR = tests

# Список исходников библиотеки
SRCS = $(SRC_DIR)/core.c \
       $(SRC_DIR)/image.c \
       $(HASH_DIR)/ahash.c \
       $(HASH_DIR)/dhash.c \
       $(HASH_DIR)/phash.c \
       $(HASH_DIR)/common.c

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Поиск всех файлов тестов, начинающихся на test_
TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=%)

.PHONY: all clean test

all: $(LIB_NAME) $(TEST_BINS)

# Сборка статической библиотеки
$(LIB_NAME): $(OBJS)
	ar rcs $@ $^

# Компиляция объектных файлов
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Сборка исполняемых файлов тестов
# Важно: $(LIB_NAME) должен идти ПЕРЕД $(LDFLAGS)
test_%: $(TEST_DIR)/test_%.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< $(LIB_NAME) -o $@ $(LDFLAGS)

# Запуск всех тестов
test: $(TEST_BINS)
	@echo "Starting tests..."
	@for test in $(TEST_BINS); do \
		echo "------------------------------"; \
		./$$test || exit 1; \
	done
	@echo "------------------------------"
	@echo "ALL TESTS PASSED SUCCESSFULLY"

clean:
	rm -rf $(OBJ_DIR) *.a test_*
