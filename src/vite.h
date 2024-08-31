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
  uint8_t *input;  // Input bitmap data
  size_t max_size; // Maximum size of input (rounded up to nearest 32B multiple)
  size_t num_buckets; // Number of buckets in the input bitmap

  uint8_t *output;            // Compressed bitmap output
  size_t max_compressed_size; // Maximum size of output (worst-case scenario)
  size_t output_size; // Actual size of compressed data after compression

  uint8_t *indices;       // Auxiliary buffer for compression optimization
  uint8_t *helper_bucket; // Auxiliary buffer for compression optimization
} Vitemap;

/**
 * Creates a new Vitemap structure
 *
 * @param upper_size Maximum expected size of input bitmap
 * @return Initialized Vitemap structure
 *
 * The function allocates memory for input and output buffers, rounding up
 * the input size to the nearest multiple of 32 bytes.
 */
Vitemap *vitemap_create(size_t upper_size);

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
size_t vitemap_compress(Vitemap *vm, size_t size);

#endif // VITE_H