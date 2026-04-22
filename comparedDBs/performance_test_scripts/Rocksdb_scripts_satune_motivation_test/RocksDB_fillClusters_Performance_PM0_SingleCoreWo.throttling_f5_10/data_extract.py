"""
模块化 Log 数据提取器
=====================
每个 extractor 是一个函数: (content: str) -> list[(pos, value)]
    - pos:   匹配在文件中的字符偏移（用于多列对齐排序）
    - value: 提取到的数值

要添加新列，只需:
  1. 写一个 extractor 函数
  2. 在 EXTRACTORS 列表中注册 (列名, 函数)
"""

import re
import os
import csv


# ============================================================
#  Extractors  —— 在这里添加新的提取器
# ============================================================

def extract_throughput_v1(content: str):
    """格式: Interval Throughput: 347164.91 ops/sec"""
    pattern = r'Interval Throughput:\s+([\d.]+)\s+ops/sec'
    return [(m.start(), float(m.group(1))) for m in re.finditer(pattern, content)]


def extract_throughput_v2(content: str):
    """格式: (278347.6,252956.3) ops/second  -> 取第二个值（累计吞吐）"""
    pattern = r'\(([\d.]+),([\d.]+)\)\s+ops/second'
    return [(m.start(), float(m.group(2))) for m in re.finditer(pattern, content)]


def extract_sum_size_gb(content: str):
    """提取 Sum 行的 Size（空间占用），统一转换为 GB。
    例如:
    Sum   3904/3   228.67 GB   ...
    -> 228.67
    """
    pattern = r'^\s*Sum\s+\S+\s+([\d.]+)\s+(KB|MB|GB|TB)\b'
    results = []
    for m in re.finditer(pattern, content, re.MULTILINE):
        val = float(m.group(1))
        unit = m.group(2)
        if unit == 'KB':
            val /= 1024 ** 2
        elif unit == 'MB':
            val /= 1024
        elif unit == 'TB':
            val *= 1024
        # GB 不变
        results.append((m.start(), round(val, 4)))
    return results

def extract_cumulative_write_gb(content: str):
    """格式: Cumulative compaction: 190.16 GB write, ...
    提取写入量，统一转换为 GB。"""
    pattern = r'Cumulative compaction:\s+([\d.]+)\s+(KB|MB|GB|TB)\s+write'
    results = []
    for m in re.finditer(pattern, content):
        val = float(m.group(1))
        unit = m.group(2)
        if unit == 'KB':
            val /= 1024 ** 2
        elif unit == 'MB':
            val /= 1024
        elif unit == 'TB':
            val *= 1024
        # GB 不变
        results.append((m.start(), round(val, 4)))
    return results


def extract_l0_comp_sec(content: str):
    """提取 L0 行的 Comp(sec)（flush 时间）
    行格式:  L0  3/0  193.03 MB  0.8  ... Comp(sec) ..."""
    # L0 之后第 14 个字段就是 Comp(sec)
    pattern = r'^\s*L0\s+(?:\S+\s+){13}(\S+)'
    return [(m.start(), float(m.group(1))) for m in re.finditer(pattern, content, re.MULTILINE)]


def extract_l1_comp_sec(content: str):
    """提取 L1 行的 Comp(sec)（L0->L1 compaction 时间）"""
    pattern = r'^\s*L1\s+(?:\S+\s+){13}(\S+)'
    return [(m.start(), float(m.group(1))) for m in re.finditer(pattern, content, re.MULTILINE)]


def extract_sum_comp_sec(content: str):
    """提取 Sum 行的 Comp(sec)（compaction 总时间）"""
    pattern = r'^\s*Sum\s+(?:\S+\s+){13}(\S+)'
    return [(m.start(), float(m.group(1))) for m in re.finditer(pattern, content, re.MULTILINE)]


def extract_ingest_gb(content: str):
    """格式: Cumulative writes: ... ingest: 0.51 GB, ...
    提取 ingest 值，统一转换为 GB。"""
    pattern = r'Cumulative writes:.*?ingest:\s+([\d.]+)\s+(KB|MB|GB|TB)'
    results = []
    for m in re.finditer(pattern, content):
        val = float(m.group(1))
        unit = m.group(2)
        if unit == 'KB':
            val /= 1024 ** 2
        elif unit == 'MB':
            val /= 1024
        elif unit == 'TB':
            val *= 1024
        results.append((m.start(), round(val, 4)))
    return results


def extract_elapsed_v1(content: str):
    """格式: [1149.7s] ... -> 总运行时间（秒）"""
    pattern = r'\[([\d.]+)s\]'
    return [(m.start(), float(m.group(1))) for m in re.finditer(pattern, content)]


def extract_elapsed_v2(content: str):
    """格式: (1.574205,1149.652965) seconds -> 取第二个值（累计运行时间）"""
    pattern = r'\(([\d.]+),([\d.]+)\)\s+seconds'
    return [(m.start(), float(m.group(2))) for m in re.finditer(pattern, content)]


# ============================================================
#  注册表  —— (CSV列名, extractor函数列表)
#  同一列可以有多个 extractor（如两种 throughput 格式），会自动合并
# ============================================================

EXTRACTORS = [
    ("throughput_ops_sec",     [extract_throughput_v1, extract_throughput_v2]),
    ("cumulative_write_gb",    [extract_cumulative_write_gb]),
    ("l0_flush_sec",           [extract_l0_comp_sec]),
    ("l1_compaction_sec",      [extract_l1_comp_sec]),
    ("ingest_gb",              [extract_ingest_gb]),
    ("elapsed_sec",            [extract_elapsed_v1, extract_elapsed_v2]),
    ("sum_comp_sec",           [extract_sum_comp_sec]),
    ("sum_size_gb",            [extract_sum_size_gb]),
    # 示例: 要加新列，照着写就行
    # ("new_metric_name",      [extract_xxx]),
]

# 采样间隔: 每隔多少条记录取一次写入 CSV
# 设为 1 表示全部保留，设为 100 表示每 100 条取第 100 条
SAMPLE_INTERVAL = 10


# ============================================================
#  核心逻辑
# ============================================================

def run_extractors(content: str, extractor_defs):
    """
    对每一列，运行其所有 extractor，按字符偏移合并排序，
    返回 {列名: [value, value, ...]}（按出现顺序）。
    同时返回最大行数 max_rows。
    """
    columns = {}
    max_rows = 0
    for col_name, funcs in extractor_defs:
        all_matches = []
        for func in funcs:
            all_matches.extend(func(content))
        all_matches.sort(key=lambda x: x[0])
        values = [v for _, v in all_matches]
        columns[col_name] = values
        max_rows = max(max_rows, len(values))
    return columns, max_rows


def process_log(log_path, out_path, extractor_defs, sample_interval=1):
    with open(log_path, 'r') as f:
        content = f.read()

    columns, max_rows = run_extractors(content, extractor_defs)

    if max_rows == 0:
        return None

    col_names = [name for name, _ in extractor_defs]

    # 采样: 取第 sample_interval, 2*sample_interval, ... 条（0-indexed）
    step = max(1, sample_interval)
    indices = list(range(step - 1, max_rows, step))

    with open(out_path, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(['index'] + col_names)
        for new_idx, orig_idx in enumerate(indices, start=1):
            row = [new_idx]
            for cn in col_names:
                vals = columns[cn]
                row.append(vals[orig_idx] if orig_idx < len(vals) else '')
            writer.writerow(row)

    # 生成摘要
    summaries = [f"总记录: {max_rows}, 采样间隔: {step}, 输出: {len(indices)} 条"]
    for cn in col_names:
        vals = columns[cn]
        if vals:
            summaries.append(f"  {cn}: min={min(vals):.2f}, max={max(vals):.2f}, "
                             f"avg={sum(vals)/len(vals):.2f}")
    return summaries


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))

    log_files = sorted([
        f for f in os.listdir(script_dir) if f.endswith('.log')
    ])

    if not log_files:
        print(f"[WARNING] 目录 {script_dir} 下没有找到 .log 文件")
        return

    for log_name in log_files:
        log_path = os.path.join(script_dir, log_name)
        out_name = os.path.splitext(log_name)[0] + '_metrics.csv'
        out_path = os.path.join(script_dir, out_name)

        result = process_log(log_path, out_path, EXTRACTORS, SAMPLE_INTERVAL)
        if result is None:
            print(f"[WARNING] {log_name}: 没有提取到任何数据，跳过")
        else:
            print(f"[OK] {log_name} -> {out_name}")
            for s in result:
                print(f"     {s}")


if __name__ == '__main__':
    main()