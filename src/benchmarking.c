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

double total_initial_size = 0;
double total_snappy_size = 0;
double total_zstd_size = 0;
double total_vitemap_size = 0;
double total_snappy_comp_time = 0;
double total_zstd_comp_time = 0;
double total_vitemap_comp_time = 0;
double total_snappy_decomp_time = 0;
double total_zstd_decomp_time = 0;
double total_vitemap_decomp_time = 0;
int total_files = 0;

// Statistics of benchmarking.
typedef struct {
  long length;
  long comp_time;
  long decomp_time;
  int verified;
} BenchmarkResult;

typedef struct {
  long length;
  double avg_comp_time;
  double ci_comp_margin;
  double avg_decomp_time;
  double ci_decomp_margin;
  int verified;
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

// Returns the statistics of compressing and decompressing with Snappy.
BenchmarkResult benchmark_snappy(uint8_t *bitmap, size_t size) {
  struct timespec start, end;
  BenchmarkResult results = {0};

  size_t output_length = snappy_max_compressed_length(size);
  char *output = malloc(output_length);

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  snappy_compress((const char *)bitmap, size, output, &output_length);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  results.comp_time = calculate_time_diff(start, end);

  uint8_t *decompressed = malloc(size);
  size_t decompressed_length = size;

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  snappy_uncompress(output, output_length, (char *)decompressed,
                    &decompressed_length);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  results.decomp_time = calculate_time_diff(start, end);

  results.verified = (memcmp(bitmap, decompressed, size) == 0);
  results.length = output_length;

  free(output);
  free(decompressed);

  return results;
}

// Returns the statistics of compressing and decompressing with Zstd.
BenchmarkResult benchmark_zstd(uint8_t *bitmap, size_t size) {
  struct timespec start, end;
  BenchmarkResult results = {0};

  size_t output_length = ZSTD_compressBound(size);
  void *output = malloc(output_length);

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  output_length = ZSTD_compress(output, output_length, bitmap, size, 1);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  results.comp_time = calculate_time_diff(start, end);

  uint8_t *decompressed = malloc(size);

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  ZSTD_decompress(decompressed, size, output, output_length);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  results.decomp_time = calculate_time_diff(start, end);

  results.verified = (memcmp(bitmap, decompressed, size) == 0);
  results.length = output_length;

  free(output);
  free(decompressed);

  return results;
}

// Returns the statistics of compressing and decompressing with our Vitemap
// encoding scheme.
BenchmarkResult benchmark_vitemap(uint8_t *bitmap, size_t size) {
  struct timespec start, end;
  BenchmarkResult results = {0};

  Vitemap *vm = vitemap_create(size);
  memcpy(vm->input, bitmap, size);

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  size_t output_length = vitemap_compress(vm, size);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  results.comp_time = calculate_time_diff(start, end);

  uint32_t data_size;
  uint32_t buffer_size;
  vitemap_extract_decompressed_sizes(vm->output, &data_size, &buffer_size);
  uint8_t *decompressed = malloc(buffer_size);

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  vitemap_decompress(vm->output, output_length, decompressed);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  results.decomp_time = calculate_time_diff(start, end);

  results.verified =
      (memcmp(bitmap, decompressed, size) == 0) && data_size == size;
  results.length = output_length;

  vitemap_delete(vm);
  free(decompressed);

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

AggregatedResult aggregate_results(long *comp_times, long *decomp_times, int n,
                                   int verified) {
  double comp_mean, comp_std_dev, decomp_mean, decomp_std_dev;
  calculate_stats(comp_times, n, &comp_mean, &comp_std_dev);
  calculate_stats(decomp_times, n, &decomp_mean, &decomp_std_dev);

  double comp_margin = 1.96 * (comp_std_dev / sqrt(n));
  double decomp_margin = 1.96 * (decomp_std_dev / sqrt(n));

  AggregatedResult result = {.avg_comp_time = comp_mean,
                             .ci_comp_margin = comp_margin,
                             .avg_decomp_time = decomp_mean,
                             .ci_decomp_margin = decomp_margin,
                             .verified = verified};

  return result;
}

AggregatedResult
aggregate_benchmark(uint8_t *bitmap, size_t size,
                    BenchmarkResult (*benchmark_func)(uint8_t *, size_t)) {
  long lengths[NUM_ITERATIONS];
  long comp_times[NUM_ITERATIONS];
  long decomp_times[NUM_ITERATIONS];
  int verified = 1;

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    BenchmarkResult result = benchmark_func(bitmap, size);
    lengths[i] = result.length;
    comp_times[i] = result.comp_time;
    decomp_times[i] = result.decomp_time;
    verified &= result.verified;
  }

  AggregatedResult agg_result =
      aggregate_results(comp_times, decomp_times, NUM_ITERATIONS, verified);
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
  printf("snappy, %ld (%f), %.2f ± %.2f, %.2f ± %.2f, %s\n", snappy.length,
         (double)snappy.length / (double)file_size, snappy.avg_comp_time,
         snappy.ci_comp_margin, snappy.avg_decomp_time, snappy.ci_decomp_margin,
         snappy.verified ? "✓" : "✗");
  printf("zstd, %ld (%f), %.2f ± %.2f, %.2f ± %.2f, %s\n", zstd.length,
         (double)zstd.length / (double)file_size, zstd.avg_comp_time,
         zstd.ci_comp_margin, zstd.avg_decomp_time, zstd.ci_decomp_margin,
         zstd.verified ? "✓" : "✗");
  printf("vitemap, %ld (%f), %.2f ± %.2f, %.2f ± %.2f, %s\n", vitemap.length,
         (double)vitemap.length / (double)file_size, vitemap.avg_comp_time,
         vitemap.ci_comp_margin, vitemap.avg_decomp_time,
         vitemap.ci_decomp_margin, vitemap.verified ? "✓" : "✗");
  printf("\n");

  free(bitmap);

  total_files++;
  total_initial_size += file_size;
  total_snappy_size += snappy.length;
  total_zstd_size += zstd.length;
  total_vitemap_size += vitemap.length;
  total_snappy_comp_time += snappy.avg_comp_time;
  total_zstd_comp_time += zstd.avg_comp_time;
  total_vitemap_comp_time += vitemap.avg_comp_time;
  total_snappy_decomp_time += snappy.avg_decomp_time;
  total_zstd_decomp_time += zstd.avg_decomp_time;
  total_vitemap_decomp_time += vitemap.avg_decomp_time;
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

  printf("Aggregate Statistics:\n");
  printf("Average Initial Size: %.2f bytes\n",
         total_initial_size / total_files);
  printf("Snappy - Average Size: %.2f bytes (%.4f), Average Comp Time: %.2f "
         "ns, Average Decomp Time: %.2f ns\n",
         total_snappy_size / total_files,
         (total_snappy_size / total_files) / (total_initial_size / total_files),
         total_snappy_comp_time / total_files,
         total_snappy_decomp_time / total_files);
  printf("Zstd - Average Size: %.2f bytes (%.4f), Average Comp Time: %.2f ns, "
         "Average Decomp Time: %.2f ns\n",
         total_zstd_size / total_files,
         (total_zstd_size / total_files) / (total_initial_size / total_files),
         total_zstd_comp_time / total_files,
         total_zstd_decomp_time / total_files);
  printf("Vitemap - Average Size: %.2f bytes (%.4f), Average Comp Time: %.2f "
         "ns, Average Decomp Time: %.2f ns\n",
         total_vitemap_size / total_files,
         (total_vitemap_size / total_files) /
             (total_initial_size / total_files),
         total_vitemap_comp_time / total_files,
         total_vitemap_decomp_time / total_files);

  return 0;
}