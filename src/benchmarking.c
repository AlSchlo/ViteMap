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

#include "snappy-c.h"
#include "vite.h"
#include "zstd.h"
#include <dirent.h>
#include <immintrin.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define NUM_ITERATIONS 100
#define CONFIDENCE_LEVEL 0.95

// Statistics of benchmarking.
typedef struct {
  long length;
  long time;
} BenchmarkResult;

typedef struct {
  long length;
  double avg_time;
  double ci_margin;
} AggregatedResult;

// Helper function to calculate time difference in nanoseconds
long calculate_time_diff(struct timespec start, struct timespec end) {
  long seconds = end.tv_sec - start.tv_sec;
  long nanoseconds = end.tv_nsec - start.tv_nsec;

  if (nanoseconds < 0) {
    seconds--;
    nanoseconds += 1000000000;
  }

  return seconds * 1000000000 + nanoseconds;
}

// Returns the statistics of compressing with Snappy.
BenchmarkResult benchmark_snappy(uint8_t *bitmap, size_t size) {
  struct timespec start, end;

  size_t output_length = snappy_max_compressed_length(size);
  char *output = malloc(output_length);

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  snappy_compress((const char *)bitmap, size, output, &output_length);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);

  free(output);

  long time_diff = calculate_time_diff(start, end);

  BenchmarkResult results = {output_length, time_diff};
  return results;
}

// Returns the statistics of compressing with Zstd.
BenchmarkResult benchmark_zstd(uint8_t *bitmap, size_t size) {
  struct timespec start, end;

  size_t output_length = ZSTD_compressBound(size);
  void *output = malloc(output_length);

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  output_length = ZSTD_compress(output, output_length, bitmap, size, 1);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);

  free(output);

  long time_diff = calculate_time_diff(start, end);

  BenchmarkResult results = {output_length, time_diff};
  return results;
}

// Returns the statistics of compressing with our Vitemap encoding scheme.
BenchmarkResult benchmark_vitemap(uint8_t *bitmap, size_t size) {
  struct timespec start, end;

  Vitemap *vm = vitemap_create(size);
  memcpy(vm->input, bitmap, size);

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  size_t output_length = vitemap_compress(vm, size);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);

  vitemap_delete(vm);

  long time_diff = calculate_time_diff(start, end);

  BenchmarkResult results = {output_length, time_diff};
  return results;
}

void calculate_stats(long times[], int n, double *mean, double *std_dev) {
  *mean = 0;
  for (int i = 0; i < n; i++) {
    *mean += times[i];
  }
  *mean /= n;

  *std_dev = 0;
  for (int i = 0; i < n; i++) {
    *std_dev += pow(times[i] - *mean, 2);
  }
  *std_dev = sqrt(*std_dev / (n - 1)); // Use n-1 for sample standard deviation
}

AggregatedResult remove_outliers_and_calculate_stats(long *times, int n) {
  double mean, std_dev;
  calculate_stats(times, n, &mean, &std_dev);

  double margin = 1.96 * (std_dev / sqrt(n));

  AggregatedResult result;
  result.avg_time = mean;
  result.ci_margin = margin;

  return result;
}

AggregatedResult
aggregate_benchmark(uint8_t *bitmap, size_t size,
                    BenchmarkResult (*benchmark_func)(uint8_t *, size_t)) {
  long lengths[NUM_ITERATIONS];
  long times[NUM_ITERATIONS];

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    BenchmarkResult result = benchmark_func(bitmap, size);
    lengths[i] = result.length;
    times[i] = result.time;
  }

  AggregatedResult agg_result =
      remove_outliers_and_calculate_stats(times, NUM_ITERATIONS);
  agg_result.length = lengths[0];

  return agg_result;
}

void process_file(const char *filename) {
  FILE *file = fopen(filename, "rb");
  if (file == NULL) {
    perror("Error opening file");
    return;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  if (file_size < 0) {
    fclose(file);
    perror("Error getting file size");
    return;
  }
  fseek(file, 0, SEEK_SET);

  uint8_t *bitmap = (uint8_t *)malloc(file_size);
  if (bitmap == NULL) {
    fclose(file);
    perror("Error allocating memory");
    return;
  }

  size_t bytes_read = fread(bitmap, 1, file_size, file);
  if (bytes_read != (size_t)file_size) {
    free(bitmap);
    fclose(file);
    perror("Error reading file");
    return;
  }
  fclose(file);

  // Run aggregated benchmarks
  AggregatedResult snappy =
      aggregate_benchmark(bitmap, file_size, benchmark_snappy);
  AggregatedResult zstd =
      aggregate_benchmark(bitmap, file_size, benchmark_zstd);
  AggregatedResult vitemap =
      aggregate_benchmark(bitmap, file_size, benchmark_vitemap);

  // Print results for this file
  printf("File: %s\n", filename);
  printf("initial, %ld\n", file_size);
  printf("snappy, %ld (%f), %.2f ± %.2f\n", snappy.length,
         (double)snappy.length / (double)file_size, snappy.avg_time,
         snappy.ci_margin);
  printf("zstd, %ld (%f), %.2f ± %.2f\n", zstd.length,
         (double)zstd.length / (double)file_size, zstd.avg_time,
         zstd.ci_margin);
  printf("vitemap, %ld (%f), %.2f ± %.2f\n", vitemap.length,
         (double)vitemap.length / (double)file_size, vitemap.avg_time,
         vitemap.ci_margin);
  printf("\n");

  free(bitmap);
}

// Runs benchmarks for all files within the ./traces directory.
// We evaluate the compression ratio and time for three different compression
// algorithms:
// - Snappy (general purpose compression algorithm developed by Google)
// - Zstd (general purpose compression algorithm developed by Facebook)
// - Vitemap (our custom *bitmap* encoding scheme, based on Lemir's Roaring
// bitmaps) We run all benchmarks multiple times and aggregate the results: we
// compute a 95% confidence interval for the time.
int main() {
  const char *traces_dir = "traces";
  DIR *dir;
  struct dirent *ent;
  struct stat st;
  char filepath[1024];

  dir = opendir(traces_dir);
  if (dir == NULL) {
    perror("Error opening traces directory");
    return 1;
  }

  while ((ent = readdir(dir)) != NULL) {
    snprintf(filepath, sizeof(filepath), "%s/%s", traces_dir, ent->d_name);
    if (stat(filepath, &st) == -1) {
      perror("Error getting file status");
      continue;
    }

    if (S_ISREG(st.st_mode)) {
      process_file(filepath);
    }
  }
  closedir(dir);

  return 0;
}