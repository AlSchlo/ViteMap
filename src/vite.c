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
#include <immintrin.h>
#include <stdio.h>
#include <string.h>

// Fast popcount for 256 bits.
// Calling this function in the critical section increases incoding time by
// ~25%. When maintaining the bitmap, it makes sense to dynamically keep track
// of the bucket sizes, so this could be a potential optimization.
static size_t popcount_256(uint8_t *restrict ptr) {
  __m256i vec = _mm256_loadu_si256((__m256i *)ptr);
  __m256i popcnt = _mm256_popcnt_epi64(vec);
  __m128i sum = _mm_add_epi64(_mm256_castsi256_si128(popcnt),
                              _mm256_extracti128_si256(popcnt, 1));
  sum = _mm_add_epi64(sum, _mm_shuffle_epi32(sum, _MM_SHUFFLE(1, 0, 3, 2)));
  return _mm_cvtsi128_si64(sum);
}

// Performs highly optimized selective bit extraction and compaction.
// Uses precomputed indices to selectively extract and compact bits from a
// 256-bit (4x64-bit) bucket using AVX-512 SIMD instructions.
static void extract_and_compact_256(uint64_t *restrict src,
                                    uint8_t *restrict dst,
                                    uint8_t *restrict indices) {
  for (size_t i = 0; i < BUCKET_SIZE_U64; i++) {
    __m512i indices_vector = _mm512_loadu_si512(indices);
    __m512i compact = _mm512_maskz_compress_epi8(*src, indices_vector);
    _mm512_storeu_epi8(
        dst,
        compact); // This instruction will systematically
                  // overflow to the next bucket. However, since each bucket is
                  // processed sequentially, this is not a problem. Furthermore,
                  // we allocate one extra bucket at the end of the output
                  // buffer to avoid any tail buffer overflows.

    dst += _mm_popcnt_u64(*src);
    src += 1;
    indices += 64;
  }
}

// Inverts all 256 bits in src and stores inverse into dst.
static void invert_256(uint8_t *restrict src, uint8_t *restrict dst) {
  __m256i src_vec = _mm256_loadu_si256((__m256i *)src);
  __m256i all_ones = _mm256_set1_epi8((char)0xFFU);
  __m256i inverted = _mm256_xor_si256(src_vec, all_ones);
  _mm256_store_si256((__m256i *)dst, inverted);
}

// Useful debugging function.
static void print_binaries(uint8_t *arr, size_t len) {
  for (size_t i = 0; i < len; i++) {
    for (int j = 7; j >= 0; j--) {
      printf("%d", !!((arr[i] >> j) & 1));
    }
  }
  printf("\n");
}

Vitemap *vitemap_create(size_t size) {
  size_t full_buckets = size / BUCKET_SIZE_U8;
  size_t remaining_bytes = size % BUCKET_SIZE_U8;
  size_t num_buckets = full_buckets + (remaining_bytes > 0 ? 1 : 0);

  Vitemap *vm = calloc(1, sizeof(Vitemap));
  if (vm == NULL) {
    return NULL;
  }

  vm->max_size = num_buckets * BUCKET_SIZE_U8;
  vm->num_buckets = num_buckets;

  vm->max_compressed_size =
      4 + vm->max_size +
      num_buckets *
          BUCKET_SIZE_U8; // Orig size + offsets + worst-case encoded size.

  vm->input = calloc(vm->max_size, sizeof(uint8_t));
  vm->output = calloc(vm->max_compressed_size + BUCKET_SIZE_U8,
                      sizeof(uint8_t)); // See `extract_and_compact_256` for
                                        // extra bucket explanation.
  vm->indices = calloc(BUCKET_SIZE, sizeof(uint8_t));
  vm->helper_bucket = calloc(BUCKET_SIZE_U8, sizeof(uint8_t));
  if (vm->input == NULL || vm->output == NULL || vm->indices == NULL ||
      vm->helper_bucket == NULL) {
    vitemap_delete(vm);
    return NULL;
  }

  for (size_t i = 0; i < BUCKET_SIZE; i++) {
    vm->indices[i] = i;
  }

  return vm;
}

void vitemap_delete(Vitemap *vm) {
  free(vm->input);
  vm->input = NULL;
  free(vm->output);
  vm->output = NULL;
  free(vm->indices);
  vm->indices = NULL;
  free(vm->helper_bucket);
  vm->helper_bucket = NULL;
  free(vm);
}

size_t vitemap_compress(Vitemap *vm, size_t size) {
  size_t result_size = 0;
  uint8_t *input = vm->input;
  uint8_t *output = vm->output;

  *(uint32_t *)output = size;
  output += 4;
  result_size += 4;

  for (size_t bucket = 0; bucket < vm->num_buckets; bucket++) {
    size_t count = popcount_256(input);
    if (count < BUCKET_SIZE_U8) {
      *output = count;
      output += 1;

      extract_and_compact_256((uint64_t *)(input), output, vm->indices);
      output += count;

      result_size += (1 + count);
    } else if (BUCKET_SIZE - count < BUCKET_SIZE_U8) {
      *output = (BUCKET_SIZE - count) | 0b10000000;
      output += 1;

      invert_256(input, vm->helper_bucket);
      extract_and_compact_256((uint64_t *)(vm->helper_bucket), output,
                              vm->indices);
      output += (BUCKET_SIZE - count);

      result_size += (1 + BUCKET_SIZE - count);
    } else {
      *output = BUCKET_SIZE_U8 | 0b01100000;
      output += 1;

      memcpy(output, input, BUCKET_SIZE_U8);
      output += BUCKET_SIZE_U8;

      result_size += (1 + BUCKET_SIZE_U8);
    }

    input += BUCKET_SIZE_U8;
  }

  return result_size;
}
