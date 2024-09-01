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

#ifndef VITE_H
#define VITE_H

#include <stdint.h>
#include <stdlib.h>

// Bucket size definitions
#define BUCKET_SIZE 256    // Number of bits in a bucket
#define BUCKET_SIZE_U8 32  // Number of uint8_t  in a bucket (256 / 8)
#define BUCKET_SIZE_U16 16 // Number of uint16_t in a bucket (256 / 16)
#define BUCKET_SIZE_U32 8  // Number of uint32_t in a bucket (256 / 32)
#define BUCKET_SIZE_U64 4  // Number of uint64_t in a bucket (256 / 64)

/**
 * Vitemap: The main structure for compression.
 *
 * This structure manages the compression process, holding both input and output
 * data, as well as auxiliary buffers for optimization purposes.
 */
typedef struct {
  uint8_t *input; // Input bitmap data
  uint32_t
      max_size; // Maximum size of input (rounded up to nearest 32B multiple)
  uint32_t num_buckets; // Number of buckets in the input bitmap

  uint8_t *output;              // Compressed bitmap output
  uint32_t max_compressed_size; // Maximum size of output (worst-case scenario)
  uint32_t output_size; // Actual size of compressed data after compression

  uint8_t *helper_bucket; // Auxiliary buffer for compression optimization
} Vitemap;

/**
 * Creates a new Vitemap structure for usage and compression.
 *
 * @param upper_size Maximum expected size of input bitmap
 * @return Initialized Vitemap structure
 *
 * The function allocates memory for input and output buffers, rounding up
 * the input size to the nearest multiple of 32 bytes.
 */
Vitemap *vitemap_create(uint32_t upper_size);

/**
 * Frees all memory associated with a Vitemap structure
 *
 * @param vm Pointer to the Vitemap structure to be deallocated
 */
void vitemap_delete(Vitemap *vm);

/**
 * Compresses the input bitmap
 *
 * @param vm Pointer to the Vitemap structure
 * @param size Actual size of input data to compress (must not exceed max_size)
 * @return Size of the compressed data
 *
 * This function compresses the input bitmap stored in vm->input and writes
 * the compressed data to vm->output. The actual size of the compressed data
 * is stored in vm->output_size.
 */
uint32_t vitemap_compress(Vitemap *vm, uint32_t size);

/**
 * Extracts the uncompressed data size and buffer size to allocate from the
 * compressed data
 *
 * @param compressed_data Pointer to the compressed data
 * @param[out] data_size Pointer to the extracted data size
 * @param[out] buffer_size Pointer to the extracted buffer size
 *
 * This function extracts the size of the *decompressed* data (and associate
 * buffer size, rounding up to the nearest 32B) from the compressed Vitemap. The
 * size is stored in the first 4 bytes of the data.
 */
void vitemap_extract_decompressed_sizes(uint8_t *compressed_data,
                                        uint32_t *data_size,
                                        uint32_t *buffer_size);

/**
 * Decompresses the input bitmap
 *
 * @param compressed_data Pointer to the compressed data
 * @param size Size of the compressed data
 * @param decompressed_data Pointer to the decompressed data
 *
 * This function decompresses the input bitmap stored in compressed_data and
 * writes the decompressed data to decompressed_data.
 *
 * The size of the decompressed data buffer is assumed to be large enough, it
 * can be extracted using the function `vitemap_extract_decompressed_size`.
 */
void vitemap_decompress(uint8_t *compressed_data, uint32_t size,
                        uint8_t *decompressed_data);

#endif // VITE_H