# ViteMap: Ultra-Fast Bitmap Compression

ViteMap (pronounced like French "vite" [vit], meaning "fast") is a high-performance bitmap compression scheme designed for speed-critical applications, implemented in C. Leveraging AVX-512 SIMD instructions, ViteMap achieves compression and decompression speeds of up to 75 Gbps on a single core - 10x faster than [Snappy](https://github.com/google/snappy), and 20x than [Zstd](https://github.com/facebook/zstd), while offering comparable compression ratios.

ViteMap is inspired by [Daniel Lemire's Roaring Bitmap](https://arxiv.org/abs/1709.07821), but with a laser focus on speed. It uses smaller 256-bit buckets and omits the Run Length Encoding (RLE) scheme to maximize performance. The result is a compression scheme that prioritizes speed above all else, making it perfect for latency-sensitive applications.

## Use Cases

ViteMap is particularly useful in scenarios where rapid compression and decompression of bitmap-like data structures are crucial:

- Database indexing: Efficient bitmap index operations
- High-Frequency Trading: Rapid analysis of stock price indicators
- Network packet analysis: Quick classification of network traffic patterns
- Real-time data streaming: Fast processing of binary indicator streams

## Non-Use Cases

ViteMap is *NOT* designed for:

- General-purpose file compression (e.g., compressing documents or media files)
- Archival storage prioritizing compression ratio over speed (e.g., long-term backup systems)
- Systems without AVX-512 support (e.g., older or low-end processors)
- Any scenario where compression ratio is more important than speed

If your primary goal is to minimize storage space and access speed is not critical, or you are trying to compress data that is not extremely sparse (i.e. bitmaps), other compression algorithms might be better.

## Getting Started

### Prerequisites

- A CPU with AVX-512 support
- GCC or Clang compiler
- Make build system

### Installing Benchmark Dependencies

To run the full set of benchmarks, install Snappy and Zstd:

```bash
sudo apt-get update
sudo apt-get install libsnappy-dev libzstd-dev
```

### Building and Running

```bash
git clone https://github.com/your-username/vitemap.git
cd vitemap
make all
./target/benchmarking
```

## Usage

To use ViteMap in your project, include the `vite.h` header and compile the ViteMap source files with your project. Here's a simple example:

```c
#include "vite.h"
#include <stdint.h>
#include <stdlib.h>

// Compression
Vitemap *vm = vitemap_create(your_data_size);
memcpy(vm->input, your_data, your_data_size);
uint32_t compressed_size = vitemap_compress(vm, your_data_size);

// Decompression
uint32_t decompressed_size, buffer_size;
vitemap_extract_decompressed_sizes(vm->output, &decompressed_size, &buffer_size);
uint8_t *decompressed_data = malloc(buffer_size);
vitemap_decompress(vm->output, compressed_size, decompressed_data);

// Don't forget to clean up
vitemap_delete(vm);
free(decompressed_data);
```

When compiling your project, make sure to include the ViteMap source files and use the appropriate compiler flags for AVX-512 support.

## Benchmarks

Our benchmarks showcase ViteMap's performance compared to other compression algorithms. We used synthetically generated US stock option data as our test dataset (see the `/traces` directory). The benchmarks were run on the following CPU:

```
Architecture:        x86_64
Model name:          11th Gen Intel(R) Core(TM) i7-11800H @ 2.30GHz
CPU(s):              16
Thread(s) per core:  2
Core(s) per socket:  8
Socket(s):           1
CPU max MHz:         4600.0000
CPU min MHz:         800.0000
```

![ViteMap Performance Comparison](resources/benchmarking.png)

## Contributing

We welcome contributions to ViteMap! Please feel free to submit a Pull Request or open an Issue on our GitHub repository.

## Contact

For questions, feedback, or collaboration opportunities, please reach out to:

Alexis Schlomer - aschlome@andrew.cmu.edu

## License

ViteMap is released under the MIT License. See the [LICENSE](LICENSE) file for details.