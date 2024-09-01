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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_FILENAME 256

// ANSI color codes
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

void print_usage() {
  printf(ANSI_COLOR_CYAN);
  printf("Usage: ./vitemap [input_file] [output_file] [mode]\n");
  printf("Mode: c for compress, d for decompress\n");
  printf(ANSI_COLOR_RESET);
}

void print_header() {
  printf(ANSI_COLOR_MAGENTA);
  printf("╔════════════════════════════════════════╗\n");
  printf("║             VITEMAP UTILITY            ║\n");
  printf("╚════════════════════════════════════════╝\n");
  printf(ANSI_COLOR_RESET);
}

double get_time_ms(struct timespec start, struct timespec end) {
  return (end.tv_sec - start.tv_sec) * 1000.0 +
         (end.tv_nsec - start.tv_nsec) / 1e6;
}

void print_stats(const char *operation, long input_size, uint32_t output_size,
                 double time_ms) {
  printf(ANSI_COLOR_YELLOW);
  printf("┌─────────────────────────────────────────┐\n");
  printf("│ %-37s   │\n", operation);
  printf("├─────────────────────────────────────────┤\n");
  printf("│ Input size:\t\t%10ld bytes  │\n", input_size);
  printf("│ Output size:\t\t%10u bytes  │\n", output_size);
  printf("│ Ratio:\t\t     %10.2f%%  │\n",
         (operation[0] == 'C') ? (1 - (double)output_size / input_size) * 100
                               : ((double)output_size / input_size - 1) * 100);
  printf("│ Time elapsed:\t\t   %10.2f ms  │\n", time_ms);
  printf("└─────────────────────────────────────────┘\n");
  printf(ANSI_COLOR_RESET);
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    print_usage();
    return 1;
  }

  print_header();

  char *input_file = argv[1];
  char *output_file = argv[2];
  char mode = argv[3][0];

  FILE *in_fp = fopen(input_file, "rb");
  if (!in_fp) {
    perror(ANSI_COLOR_RED "Error opening input file" ANSI_COLOR_RESET);
    return 1;
  }

  fseek(in_fp, 0, SEEK_END);
  long input_size = ftell(in_fp);
  fseek(in_fp, 0, SEEK_SET);

  uint8_t *input_buffer = malloc(input_size);
  if (!input_buffer) {
    perror(ANSI_COLOR_RED "Error allocating memory" ANSI_COLOR_RESET);
    fclose(in_fp);
    return 1;
  }

  fread(input_buffer, 1, input_size, in_fp);
  fclose(in_fp);

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  if (mode == 'c') {
    Vitemap *vm = vitemap_create(input_size);
    if (!vm) {
      perror(ANSI_COLOR_RED "Error creating Vitemap" ANSI_COLOR_RESET);
      free(input_buffer);
      return 1;
    }

    memcpy(vm->input, input_buffer, input_size);
    uint32_t compressed_size = vitemap_compress(vm, input_size);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_ms = get_time_ms(start, end);

    FILE *out_fp = fopen(output_file, "wb");
    if (!out_fp) {
      perror(ANSI_COLOR_RED "Error opening output file" ANSI_COLOR_RESET);
      vitemap_delete(vm);
      free(input_buffer);
      return 1;
    }

    fwrite(vm->output, 1, compressed_size, out_fp);
    fclose(out_fp);

    print_stats("Compression Statistics", input_size, compressed_size, time_ms);

    vitemap_delete(vm);
  } else if (mode == 'd') {
    uint32_t decompressed_size = 0;
    uint32_t buffer_size = 0;
    vitemap_extract_decompressed_sizes(input_buffer, &decompressed_size,
                                       &buffer_size);
    uint8_t *output_buffer = malloc(buffer_size);
    if (!output_buffer) {
      perror(ANSI_COLOR_RED
             "Error allocating memory for decompression" ANSI_COLOR_RESET);
      free(input_buffer);
      return 1;
    }

    vitemap_decompress(input_buffer, input_size, output_buffer);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_ms = get_time_ms(start, end);

    FILE *out_fp = fopen(output_file, "wb");
    if (!out_fp) {
      perror(ANSI_COLOR_RED "Error opening output file" ANSI_COLOR_RESET);
      free(input_buffer);
      free(output_buffer);
      return 1;
    }

    fwrite(output_buffer, 1, decompressed_size, out_fp);
    fclose(out_fp);

    print_stats("Decompression Statistics", input_size, decompressed_size,
                time_ms);

    free(output_buffer);
  } else {
    printf(ANSI_COLOR_RED "Invalid mode. Use 'c' for compress or 'd' for "
                          "decompress.\n" ANSI_COLOR_RESET);
    free(input_buffer);
    return 1;
  }

  free(input_buffer);
  return 0;
}