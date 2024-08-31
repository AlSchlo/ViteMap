/*
 *  ____   ____.__  __
 *  \   \ /   /|__|/  |_  ____   _____ _____  ______
 *   \   Y   / |  \   __\/ __ \ /     \\__  \ \____ \
 *    \     /  |  ||  | \  ___/|  Y Y  \/ __ \|  |_> >
 *     \___/   |__||__|  \___  >__|_|  (____  /   __/
 *                           \/      \/     \/|__|
 *
 *
 *  Copyright (c) [2024] [Alexis Schlomer]
 *  This project is licensed under the MIT License.
 *  See the LICENSE file for details.
 */

#include "vite.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define NUM_TESTS 100

typedef bool (*test_function)(void);
typedef struct {
  const char *name;
  test_function func;
} test_case;

test_case test_cases[NUM_TESTS];
size_t test_count = 0;

static void print_binaries(uint8_t *arr, size_t len) {
  for (size_t i = 0; i < len; i++) {
    for (int j = 7; j >= 0; j--) {
      printf("%d", !!((arr[i] >> j) & 1));
    }
  }
  printf("\n");
}

static void add_test(const char *name, test_function func) {
  assert(test_count < NUM_TESTS);
  test_cases[test_count].name = name;
  test_cases[test_count].func = func;
  test_count++;
}

static void run_tests() {
  const char *delimiter =
      "\033[1m======================================\033[0m";

  printf("%s\n", delimiter);
  printf("\033[1m             Running Tests\033[0m\n");
  printf("%s\n\n", delimiter);

  printf("Total tests: \033[1m%zu\033[0m\n\n", test_count);

  for (size_t i = 0; i < test_count; i++) {
    printf("\033[1mTest %zu:\033[0m %s\n", i + 1, test_cases[i].name);
    bool success = test_cases[i].func();
    if (!success) {
      printf("\033[1;31mFAILED\033[0m\n\n");
      printf("%s\n", delimiter);
      printf("\033[1;31mTest suite failed!\033[0m\n");
      printf("%s\n", delimiter);
      return;
    }
    printf("\033[1;32mPASSED\033[0m\n\n");
  }

  printf("%s\n", delimiter);
  printf("\033[1;32mAll tests passed successfully!\033[0m\n");
  printf("%s\n", delimiter);
}

static bool test_single_bitmap_bucket() {
  Vitemap vm = vitemap_create(BUCKET_SIZE_U8);
  for (size_t i = 0; i < BUCKET_SIZE_U8; i++) {
    vm.input[i] = 0b10101010;
  }

  vitemap_compress(&vm, BUCKET_SIZE_U8);

  if (*(uint32_t *)vm.output != BUCKET_SIZE_U8) {
    printf("Orig size was %d, expected %d.\n", *(uint32_t *)vm.output,
           BUCKET_SIZE_U8);
    vitemap_delete(&vm);
    return false;
  }

  if (vm.output[4] != 32) {
    printf("Offset was %d, expected %d.\n", vm.output[0], 32);
    vitemap_delete(&vm);
    return false;
  }

  if (memcmp(vm.output + 5, vm.input, BUCKET_SIZE_U8) != 0) {
    printf("Bitmap encoding is not identical (bucket 0).\n");
    printf("\033[1mExpected:\033[0m\n");
    print_binaries(vm.input, BUCKET_SIZE_U8);
    printf("\033[1mGot:\033[0m\n");
    print_binaries(vm.output + 5, BUCKET_SIZE_U8);
    vitemap_delete(&vm);
    return false;
  }

  vitemap_delete(&vm);
  return true;
}

static bool test_multiple_bitmap_buckets() {
  size_t num_buckets = 100;
  size_t size = num_buckets * BUCKET_SIZE_U8;

  Vitemap vm = vitemap_create(size);
  for (size_t i = 0; i < size; i++) {
    vm.input[i] = 0b10101010;
  }

  vitemap_compress(&vm, size);

  uint8_t *src = vm.input;
  uint8_t *dst = vm.output;

  if (*(uint32_t *)dst != size) {
    printf("Orig size was %d, expected %zu.\n", *(uint32_t *)dst, size);
    vitemap_delete(&vm);
    return false;
  }
  dst += 4;

  for (size_t i = 0; i < num_buckets; i++) {
    if (*dst != 32) {
      printf("Metadata was %d, expected %d (bucket %zu).\n", *dst, 32, i);
      vitemap_delete(&vm);
      return false;
    }
    dst++;

    if (memcmp(dst, src, BUCKET_SIZE_U8) != 0) {
      printf("Bitmap encoding is not identical (bucket %zu).\n", i);
      printf("\033[1mExpected:\033[0m\n");
      print_binaries(src, BUCKET_SIZE_U8);
      printf("\033[1mGot:\033[0m\n");
      print_binaries(dst, BUCKET_SIZE_U8);
      vitemap_delete(&vm);
      return false;
    }

    dst += BUCKET_SIZE_U8;
    src += BUCKET_SIZE_U8;
  }

  vitemap_delete(&vm);
  return true;
}

static bool test_single_array_bucket() {
  Vitemap vm = vitemap_create(BUCKET_SIZE_U8);
  vm.input[0] = 0b10101010;
  vm.input[1] = 0b00000000;
  vm.input[2] = 0b00010000;
  vm.input[3] = 0b00000100;
  vm.input[4] = 0b00000000;
  vm.input[31] = 0b00000001;

  vitemap_compress(&vm, BUCKET_SIZE_U8);

  if (*(uint32_t *)vm.output != BUCKET_SIZE_U8) {
    printf("Orig size was %d, expected %d.\n", *(uint32_t *)vm.output,
           BUCKET_SIZE_U8);
    vitemap_delete(&vm);
    return false;
  }

  if (vm.output[4] != 7) {
    printf("Offset was %d, expected %d.\n", vm.output[0], 7);
    vitemap_delete(&vm);
    return false;
  }

  vitemap_delete(&vm);
  return true;
}

static bool test_round_up_input_size() {
  Vitemap vm = vitemap_create(1);
  if (vm.max_size != BUCKET_SIZE_U8) {
    printf("Max size was %zu, expected %d.\n", vm.max_size, 32);
    vitemap_delete(&vm);
    return false;
  }
  if (vm.num_buckets != 1) {
    printf("Num buckets was %zu, expected %d.\n", vm.num_buckets, 1);
    vitemap_delete(&vm);
    return false;
  }

  vitemap_delete(&vm);

  vm = vitemap_create(100);
  if (vm.max_size != BUCKET_SIZE_U8 * 4) {
    printf("Max size was %zu, expected %d.\n", vm.max_size, 32);
    vitemap_delete(&vm);
    return false;
  }
  if (vm.num_buckets != 4) {
    printf("Num buckets was %zu, expected %d.\n", vm.num_buckets, 1);
    vitemap_delete(&vm);
    return false;
  }

  vitemap_delete(&vm);

  return true;
}

// Add tests here and execute them.
int main() {
  add_test("A `random` bitmap of 1 bucket should use bitmap encoding.",
           test_single_bitmap_bucket);
  add_test("A `random` bitmap of 100 buckets should use bitmap encoding.",
           test_multiple_bitmap_buckets);

  add_test("A `sparse` bitmap of 1 bucket should use array encoding.",
           test_single_array_bucket);

  add_test("Input size should round to upper 32B.", test_round_up_input_size);

  run_tests();

  return 0;
}