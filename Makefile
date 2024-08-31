CC = gcc
CFLAGS = -std=c2x -O3 -Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wdisabled-optimization \
         -Wformat=2 -Winit-self -Wlogical-op -Wmissing-include-dirs -Wredundant-decls \
         -Wshadow -Wstrict-overflow=5 -Wundef -Wno-unused -Wno-variadic-macros \
         -Wno-parentheses -fdiagnostics-show-option -Werror -D_POSIX_C_SOURCE=199309L
VITE_FLAGS = -mavx512f -mavx512bw -mavx512vl -mavx512vpopcntdq -mavx512vbmi -mavx512vbmi2 -mavx512bitalg
BENCHMARK_LIBS = -lsnappy -lzstd -lrt
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer -g
LDFLAGS = -lrt -lm

# Directories
SRC_DIR = src
TARGET_DIR = target
OBJ_DIR = $(TARGET_DIR)/obj

# Object files
VITE_OBJ = $(OBJ_DIR)/vite.o
TEST_OBJ = $(OBJ_DIR)/testing.o
BENCHMARK_OBJ = $(OBJ_DIR)/benchmarking.o
CLI_OBJ = $(OBJ_DIR)/cli.o

# Targets
all: $(TARGET_DIR)/testing $(TARGET_DIR)/cli $(TARGET_DIR)/benchmarking

$(OBJ_DIR)/vite.o: $(SRC_DIR)/vite.c $(SRC_DIR)/vite.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(VITE_FLAGS) -c $< -o $@

$(OBJ_DIR)/vite_asan.o: $(SRC_DIR)/vite.c $(SRC_DIR)/vite.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(VITE_FLAGS) $(ASAN_FLAGS) -c $< -o $@

$(OBJ_DIR)/testing.o: $(SRC_DIR)/testing.c $(SRC_DIR)/vite.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -c $< -o $@

$(OBJ_DIR)/benchmarking.o: $(SRC_DIR)/benchmarking.c $(SRC_DIR)/vite.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cli.o: $(SRC_DIR)/cli.c $(SRC_DIR)/vite.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_DIR)/testing: CFLAGS += $(ASAN_FLAGS)
$(TARGET_DIR)/testing: $(OBJ_DIR)/testing.o $(OBJ_DIR)/vite_asan.o | $(TARGET_DIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_DIR)/cli: $(OBJ_DIR)/cli.o $(OBJ_DIR)/vite.o | $(TARGET_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_DIR)/benchmarking: $(OBJ_DIR)/benchmarking.o $(OBJ_DIR)/vite.o | $(TARGET_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(BENCHMARK_LIBS) $(LDFLAGS)

$(OBJ_DIR) $(TARGET_DIR):
	mkdir -p $@

clean:
	rm -rf $(TARGET_DIR)

.PHONY: all clean