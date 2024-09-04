import re
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
import subprocess


def run_make_and_benchmark():
    make_process = subprocess.run(
        ['make', 'all'], capture_output=True, text=True)
    if make_process.returncode != 0:
        print("Make error:")
        print(make_process.stderr)
        return None

    benchmark_process = subprocess.run(
        ['./target/benchmarking'], capture_output=True, text=True)
    if benchmark_process.returncode != 0:
        print("Benchmark execution error:")
        print(benchmark_process.stderr)
        return None

    return benchmark_process.stdout.strip()


def parse_results(content):
    data = []
    file_pattern = r"File: (.*?)\ninitial, (\d+)"
    algorithm_pattern = r"(\w+), (\d+) \(([\d.]+)\), ([\d.]+) ± [\d.]+, ([\d.]+) ± [\d.]+"

    file_matches = list(re.finditer(file_pattern, content))
    print(f"Total number of file matches found: {len(file_matches)}")

    for i, file_match in enumerate(file_matches):
        file_name = file_match.group(1)
        initial_size = int(file_match.group(2))

        if i < len(file_matches) - 1:
            next_match_start = file_matches[i+1].start()
        else:
            next_match_start = len(content)

        file_content = content[file_match.end():next_match_start]

        algorithms = {}
        for alg_match in re.finditer(algorithm_pattern, file_content):
            alg_name = alg_match.group(1)
            compression_ratio = float(alg_match.group(3))
            compression_time = float(alg_match.group(4))
            decompression_time = float(alg_match.group(5))
            algorithms[alg_name] = {
                'compression_ratio': compression_ratio,
                'compression_time': compression_time,
                'decompression_time': decompression_time
            }

        if len(algorithms) != 3:
            print(f"Warning: File {file_name} has {
                  len(algorithms)} algorithms instead of 3")
            print(f"File content: {file_content}")

        data.append({
            'file_name': file_name,
            'initial_size': initial_size,
            'snappy_ratio': algorithms.get('snappy', {}).get('compression_ratio', None),
            'snappy_comp_time': algorithms.get('snappy', {}).get('compression_time', None),
            'snappy_decomp_time': algorithms.get('snappy', {}).get('decompression_time', None),
            'zstd_ratio': algorithms.get('zstd', {}).get('compression_ratio', None),
            'zstd_comp_time': algorithms.get('zstd', {}).get('compression_time', None),
            'zstd_decomp_time': algorithms.get('zstd', {}).get('decompression_time', None),
            'vitemap_ratio': algorithms.get('vitemap', {}).get('compression_ratio', None),
            'vitemap_comp_time': algorithms.get('vitemap', {}).get('compression_time', None),
            'vitemap_decomp_time': algorithms.get('vitemap', {}).get('decompression_time', None)
        })

    df = pd.DataFrame(data)
    print(f"Total number of rows in DataFrame: {len(df)}")
    return df


def create_performance_plot(df):
    sizes = {"snappy": [], "zstd": [], "vitemap": []}
    times = {"snappy": [], "zstd": [], "vitemap": []}

    for _, row in df.iterrows():
        initial_size = row['initial_size']
        for alg in ['snappy', 'zstd', 'vitemap']:
            ratio = row[f'{alg}_ratio']
            comp_time = row[f'{alg}_comp_time']
            sizes[alg].append(ratio)
            gbps = (initial_size * 8) / (comp_time)
            times[alg].append(gbps)

    fig, (ax1, ax2) = plt.subplots(nrows=1, ncols=2, figsize=(17, 7))

    labels = ['Snappy', 'Zstd', 'Vitemap']
    colors = ['#98ABC3', '#DD5656', '#91B382']

    bplot1 = ax1.boxplot([sizes['snappy'], sizes['zstd'], sizes['vitemap']],
                         patch_artist=True, widths=0.6)

    for patch, color in zip(bplot1['boxes'], colors):
        patch.set_facecolor(color)

    ax1.set_title('Compression Ratio', fontsize=16)
    ax1.set_ylabel('Ratio', fontsize=14)
    ax1.set_xticklabels(labels, fontsize=12)
    ax1.grid(True, linestyle='--', alpha=0.7)

    bplot2 = ax2.boxplot([times['snappy'], times['zstd'], times['vitemap']],
                         patch_artist=True, widths=0.6)

    for patch, color in zip(ax2.patches, colors):
        patch.set_facecolor(color)

    ax2.set_yscale('log')
    ax2.set_title('Compression Speed', fontsize=16)
    ax2.set_ylabel('Speed [Gbps]', fontsize=14)
    ax2.set_xticklabels(labels, fontsize=12)
    ax2.grid(True, linestyle='--', alpha=0.7)

    for bplot in [bplot1, bplot2]:
        for median in bplot['medians']:
            median.set_color('#333333')
            median.set_linewidth(1)

    def gbps_formatter(x, p):
        if x >= 1:
            return f'{x:.0f}'

    ax2.yaxis.set_major_formatter(FuncFormatter(gbps_formatter))

    plt.tight_layout()
    fig.savefig('performance.png', dpi=600)
    plt.close(fig)


if __name__ == '__main__':
    benchmark_output = run_make_and_benchmark()
    if benchmark_output:
        df = parse_results(benchmark_output)
        create_performance_plot(df)
    else:
        print("Failed to run benchmark")
