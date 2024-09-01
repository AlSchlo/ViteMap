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
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_NUM_TESTS 100

typedef bool (*test_function)(void);
typedef struct {
  const char *name;
  test_function func;
} test_case;

test_case test_cases[MAX_NUM_TESTS];
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
  assert(test_count < MAX_NUM_TESTS);
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
  Vitemap *vm = vitemap_create(BUCKET_SIZE_U8);
  for (size_t i = 0; i < BUCKET_SIZE_U8; i++) {
    vm->input[i] = 0b10000000;
  }

  vitemap_compress(vm, BUCKET_SIZE_U8);

  if (*(uint32_t *)vm->output != BUCKET_SIZE_U8) {
    printf("Orig size was %d, expected %d.\n", *(uint32_t *)vm->output,
           BUCKET_SIZE_U8);
    vitemap_delete(vm);
    return false;
  }

  if (vm->output[4] != (32 | 0b10000000)) {
    printf("Metadata was %d, expected %d.\n", vm->output[0], (32 | 0b10000000));
    vitemap_delete(vm);
    return false;
  }

  if (memcmp(vm->output + 5, vm->input, BUCKET_SIZE_U8) != 0) {
    printf("Bitmap encoding is not identical (bucket 0).\n");
    printf("\033[1mExpected:\033[0m\n");
    print_binaries(vm->input, BUCKET_SIZE_U8);
    printf("\033[1mGot:\033[0m\n");
    print_binaries(vm->output + 5, BUCKET_SIZE_U8);
    vitemap_delete(vm);
    return false;
  }

  vitemap_delete(vm);
  return true;
}

static bool test_multiple_bitmap_buckets() {
  uint32_t num_buckets = 100;
  uint32_t size = num_buckets * BUCKET_SIZE_U8;

  Vitemap *vm = vitemap_create(size);
  for (size_t i = 0; i < size; i++) {
    vm->input[i] = 0b10101010;
  }

  vitemap_compress(vm, size);

  uint8_t *src = vm->input;
  uint8_t *dst = vm->output;

  if (*(uint32_t *)dst != size) {
    printf("Orig size was %d, expected %d.\n", *(uint32_t *)dst, size);
    vitemap_delete(vm);
    return false;
  }
  dst += 4;

  for (size_t i = 0; i < num_buckets; i++) {
    if (*dst != (32 | 0b10000000)) {
      printf("Metadata was %d, expected %d (bucket %zu).\n", *dst,
             (32 | 0b10000000), i);
      vitemap_delete(vm);
      return false;
    }
    dst++;

    if (memcmp(dst, src, BUCKET_SIZE_U8) != 0) {
      printf("Bitmap encoding is not identical (bucket %zu).\n", i);
      printf("\033[1mExpected:\033[0m\n");
      print_binaries(src, BUCKET_SIZE_U8);
      printf("\033[1mGot:\033[0m\n");
      print_binaries(dst, BUCKET_SIZE_U8);
      vitemap_delete(vm);
      return false;
    }

    dst += BUCKET_SIZE_U8;
    src += BUCKET_SIZE_U8;
  }

  vitemap_delete(vm);
  return true;
}

typedef struct {
  uint8_t input[BUCKET_SIZE_U8];
  size_t bits_set;
  uint8_t expected_output[BUCKET_SIZE_U8];
  const char *description;
} ArrayBucketExample;

static bool test_single_array_bucket(ArrayBucketExample *example, bool invert) {
  printf("\033[1m %sBucket: \033[0m", invert ? "¬ " : "  ");

  Vitemap *vm = vitemap_create(BUCKET_SIZE_U8);
  for (size_t i = 0; i < BUCKET_SIZE_U8; i++) {
    vm->input[i] = invert ? ~example->input[i] : example->input[i];
  }

  uint32_t size = vitemap_compress(vm, BUCKET_SIZE_U8);

  if (*(uint32_t *)vm->output != BUCKET_SIZE_U8) {
    printf("Orig size was %d, expected %d.\n", *(uint32_t *)vm->output,
           BUCKET_SIZE_U8);
    vitemap_delete(vm);
    return false;
  }

  uint8_t expected_metadata =
      invert ? (example->bits_set | 0b01000000) : example->bits_set;
  if (vm->output[4] != expected_metadata) {
    printf("Metadata was %d, expected %d.\n", vm->output[4], expected_metadata);
    vitemap_delete(vm);
    return false;
  }

  uint8_t *dst = vm->output + 5;
  if (memcmp(dst, example->expected_output, example->bits_set) != 0) {
    printf("Array encoding is not identical.\n");
    printf("\033[1mExpected:\033[0m\n[");
    for (size_t i = 0; i < example->bits_set; i++) {
      printf("%d%s", example->expected_output[i],
             i < example->bits_set - 1 ? " " : "]\n");
    }
    printf("\033[1mGot:\033[0m\n[");
    for (size_t i = 0; i < example->bits_set; i++) {
      printf("%d%s", dst[i], i < example->bits_set - 1 ? " " : "]\n");
    }
    vitemap_delete(vm);
    return false;
  }

  vitemap_delete(vm);
  printf("\033[1;32m✓\033[0m\n");

  return true;
}

static ArrayBucketExample array_bucket_configs[] = {
    {.input = {},
     .bits_set = 0,
     .expected_output = {},
     .description = "A `sparse` bucket should use array encoding "
                    "(all empty)."},
    {.input = {0b10101010, 0, 0b00010000, 0b00000100, 0, [31] = 0b00000001},
     .bits_set = 7,
     .expected_output = {1, 3, 5, 7, 20, 26, 248},
     .description = "A `sparse` bucket should use array encoding "
                    "(bits at both parts)."},
    {.input = {0b10000000, 0, 0, 0, [31] = 0b00000001},
     .bits_set = 2,
     .expected_output = {7, 248},
     .description = "A `sparse` bucket should use array encoding "
                    "(bits at the beginning)."},
    {.input = {[30] = 0b00000001, [31] = 0b10000000},
     .bits_set = 2,
     .expected_output = {240, 255},
     .description = "A `sparse` bucket should use array encoding "
                    "(bits at the end)."},
    {.input = {0b11111111, 0b11111111, 0b11111111, 0b00011000, 0b00001001,
               0b00000100, 0b00100010},
     .bits_set = 31,
     .expected_output = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                         11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                         22, 23, 27, 28, 32, 35, 42, 49, 53},
     .description = "A `sparse` bitmap of 1 bucket should use array encoding "
                    "(almost 1/8 bits set)."},
    {.input = {[15] = 0b00010000},
     .bits_set = 1,
     .expected_output = {124},
     .description = "A `sparse` bucket should use array encoding "
                    "(single bit set)."},
};

static bool test_array_bucket_1() {
  return test_single_array_bucket(&array_bucket_configs[0], false) &&
         test_single_array_bucket(&array_bucket_configs[0], true);
}
static bool test_array_bucket_2() {
  return test_single_array_bucket(&array_bucket_configs[1], false) &&
         test_single_array_bucket(&array_bucket_configs[1], true);
}
static bool test_array_bucket_3() {
  return test_single_array_bucket(&array_bucket_configs[2], false) &&
         test_single_array_bucket(&array_bucket_configs[2], true);
}
static bool test_array_bucket_4() {
  return test_single_array_bucket(&array_bucket_configs[3], false) &&
         test_single_array_bucket(&array_bucket_configs[3], true);
}
static bool test_array_bucket_5() {
  return test_single_array_bucket(&array_bucket_configs[4], false) &&
         test_single_array_bucket(&array_bucket_configs[4], true);
}
static bool test_array_bucket_6() {
  return test_single_array_bucket(&array_bucket_configs[5], false) &&
         test_single_array_bucket(&array_bucket_configs[5], true);
}
static bool test_array_bucket_7() {
  return test_single_array_bucket(&array_bucket_configs[6], false) &&
         test_single_array_bucket(&array_bucket_configs[6], true);
}

static test_function array_bucket_tests[] = {
    test_array_bucket_1, test_array_bucket_2, test_array_bucket_3,
    test_array_bucket_4, test_array_bucket_5, test_array_bucket_6};

static bool test_round_up_input_size() {
  Vitemap *vm = vitemap_create(1);
  if (vm->max_size != BUCKET_SIZE_U8) {
    printf("Max size was %d, expected %d.\n", vm->max_size, 32);
    vitemap_delete(vm);
    return false;
  }
  if (vm->num_buckets != 1) {
    printf("Num buckets was %d, expected %d.\n", vm->num_buckets, 1);
    vitemap_delete(vm);
    return false;
  }

  vitemap_delete(vm);

  vm = vitemap_create(100);
  if (vm->max_size != BUCKET_SIZE_U8 * 4) {
    printf("Max size was %d, expected %d.\n", vm->max_size, 32);
    vitemap_delete(vm);
    return false;
  }
  if (vm->num_buckets != 4) {
    printf("Num buckets was %d, expected %d.\n", vm->num_buckets, 1);
    vitemap_delete(vm);
    return false;
  }

  vitemap_delete(vm);

  return true;
}

// Add tests here and execute them.
int main() {
  add_test("A `random` bucket should use bitmap encoding.",
           test_single_bitmap_bucket);
  add_test("A `random` bitmap of 100 buckets should use bitmap encoding.",
           test_multiple_bitmap_buckets);

  for (size_t i = 0;
       i < sizeof(array_bucket_configs) / sizeof(array_bucket_configs[0]);
       i++) {
    add_test(array_bucket_configs[i].description, array_bucket_tests[i]);
  }

  add_test("Input size should round to upper 32B.", test_round_up_input_size);

  run_tests();

  return 0;
}