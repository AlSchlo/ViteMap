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

// Align to 32-byte boundary for efficient AVX2 loads.
__attribute__((aligned(32))) static uint64_t bit_lookup[BUCKET_SIZE][4] = {{0}};
__attribute__((constructor)) static void init_bit_lookup(void) {
  for (size_t i = 0; i < BUCKET_SIZE; i++) {
    size_t chunk = i / 64;
    size_t bit = i % 64;
    bit_lookup[i][chunk] = 1ULL << bit;
  }
}
__attribute__((aligned(32))) static uint8_t indices[BUCKET_SIZE] = {0};
__attribute__((constructor)) static void init_indices(void) {
  for (size_t i = 0; i < BUCKET_SIZE; i++) {
    indices[i] = i;
  }
}

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
                                    uint8_t *restrict dst) {
  uint8_t *restrict moving_indices = indices;
  for (size_t i = 0; i < BUCKET_SIZE_U64; i++) {
    __m512i indices_vector = _mm512_loadu_si512(moving_indices);
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
    moving_indices += 64;
  }
}

// Performs highly optimized selective bit expansion and scattering.
// Uses provided indices to selectively expand and scatter bits into a
// 256-bit (4x64-bit) bucket using AVX2 SIMD instructions.
// Uses a 65KB lookup table for fast bit expansion.
static void expand_and_scatter_256(uint8_t *restrict src, size_t bucket_size,
                                   uint8_t *restrict dst) {
  __m256i result = _mm256_setzero_si256();
  size_t i;

  // Manually unrolling gives a substantial improvement benefit.
  for (i = 0; i + 4 <= bucket_size; i += 4) {
    __m256i lookup1 = _mm256_loadu_si256((__m256i *)bit_lookup[src[i]]);
    __m256i lookup2 = _mm256_loadu_si256((__m256i *)bit_lookup[src[i + 1]]);
    __m256i lookup3 = _mm256_loadu_si256((__m256i *)bit_lookup[src[i + 2]]);
    __m256i lookup4 = _mm256_loadu_si256((__m256i *)bit_lookup[src[i + 3]]);

    result = _mm256_or_si256(result, lookup1);
    result = _mm256_or_si256(result, lookup2);
    result = _mm256_or_si256(result, lookup3);
    result = _mm256_or_si256(result, lookup4);
  }

  for (; i < bucket_size; i++) {
    __m256i lookup = _mm256_loadu_si256((__m256i *)bit_lookup[src[i]]);
    result = _mm256_or_si256(result, lookup);
  }

  _mm256_storeu_si256((__m256i *)dst, result);
}

// Inverts all 256 bits in src and stores inverse into dst.
static void invert_256(uint8_t *restrict src, uint8_t *restrict dst) {
  __m256i src_vec = _mm256_loadu_si256((__m256i *)src);
  __m256i all_ones = _mm256_set1_epi8((char)0xFFU);
  __m256i inverted = _mm256_xor_si256(src_vec, all_ones);
  _mm256_storeu_si256((__m256i *)dst, inverted);
}

Vitemap *vitemap_create(uint32_t size) {
  uint32_t full_buckets = size / BUCKET_SIZE_U8;
  uint32_t remaining_bytes = size % BUCKET_SIZE_U8;
  uint32_t num_buckets = full_buckets + (remaining_bytes > 0 ? 1 : 0);

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
  vm->helper_bucket = calloc(BUCKET_SIZE_U8, sizeof(uint8_t));
  if (vm->input == NULL || vm->output == NULL || vm->helper_bucket == NULL) {
    vitemap_delete(vm);
    return NULL;
  }

  return vm;
}

void vitemap_delete(Vitemap *vm) {
  free(vm->input);
  vm->input = NULL;
  free(vm->output);
  vm->output = NULL;
  free(vm->helper_bucket);
  vm->helper_bucket = NULL;
  free(vm);
}

uint32_t vitemap_compress(Vitemap *vm, uint32_t size) {
  uint32_t result_size = 0;
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

      extract_and_compact_256((uint64_t *)(input), output);
      output += count;

      result_size += (1 + count);
    } else if (BUCKET_SIZE - count < BUCKET_SIZE_U8) {
      *output = (BUCKET_SIZE - count) | 0b01000000;
      output += 1;

      invert_256(input, vm->helper_bucket);
      extract_and_compact_256((uint64_t *)(vm->helper_bucket), output);
      output += (BUCKET_SIZE - count);

      result_size += (1 + BUCKET_SIZE - count);
    } else {
      *output = BUCKET_SIZE_U8 | 0b10000000;
      output += 1;

      memcpy(output, input, BUCKET_SIZE_U8);
      output += BUCKET_SIZE_U8;

      result_size += (1 + BUCKET_SIZE_U8);
    }

    input += BUCKET_SIZE_U8;
  }

  return result_size;
}

void vitemap_extract_decompressed_sizes(uint8_t *compressed_data,
                                        uint32_t *data_size,
                                        uint32_t *buffer_size) {
  *data_size = *(uint32_t *)compressed_data;
  uint32_t full_buckets = *data_size / BUCKET_SIZE_U8;
  uint32_t remaining_bytes = *data_size % BUCKET_SIZE_U8;
  *buffer_size =
      (full_buckets + (remaining_bytes > 0 ? 1 : 0)) * BUCKET_SIZE_U8;
}

void vitemap_decompress(uint8_t *compressed_data, uint32_t size,
                        uint8_t *decompressed_data) {
  const uint8_t *end = compressed_data + size;
  compressed_data += 4;

  while (compressed_data < end) {
    uint8_t bucket_size = *compressed_data & 0x3F;
    uint8_t category = *compressed_data >> 6;
    compressed_data += 1;

    switch (category) {
    case 0:
      expand_and_scatter_256(compressed_data, bucket_size, decompressed_data);
      break;
    case 1:
      expand_and_scatter_256(compressed_data, bucket_size, decompressed_data);
      // Unrestricted version of invert_256.
      __m256i src_vec = _mm256_loadu_si256((__m256i *)decompressed_data);
      __m256i all_ones = _mm256_set1_epi8((char)0xFFU);
      __m256i inverted = _mm256_xor_si256(src_vec, all_ones);
      _mm256_storeu_si256((__m256i *)decompressed_data, inverted);
      break;
    case 2:
      memcpy(decompressed_data, compressed_data, BUCKET_SIZE_U8);
      break;
    }

    compressed_data += bucket_size;
    decompressed_data += BUCKET_SIZE_U8;
  }
}