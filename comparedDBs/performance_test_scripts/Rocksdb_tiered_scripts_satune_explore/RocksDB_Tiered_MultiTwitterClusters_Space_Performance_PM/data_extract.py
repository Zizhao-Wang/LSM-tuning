import re
import os
import csv

def extract_sum_sizes(filepath):
    """从单个log文件中提取所有 Sum 行的 Size，统一转换为 bytes。"""
    with open(filepath, 'r') as f:
        content = f.read()

    pattern = r'^\s*Sum\s+\d+/\d+\s+([\d.]+)\s+(KB|MB|GB|TB)'
    matches = re.findall(pattern, content, re.MULTILINE)

    sizes_bytes = []
    for val_str, unit in matches:
        val = float(val_str)
        if unit == 'KB':
            val *= 1024
        elif unit == 'MB':
            val *= 1024 ** 2
        elif unit == 'GB':
            val *= 1024 ** 3
        elif unit == 'TB':
            val *= 1024 ** 4
        sizes_bytes.append(int(val))

    return sizes_bytes


def main():
    # 自动识别脚本所在目录
    script_dir = os.path.dirname(os.path.abspath(__file__))

    # 找到该目录下所有 .log 文件
    log_files = sorted([
        f for f in os.listdir(script_dir)
        if f.endswith('.log')
    ])

    if not log_files:
        print(f"[WARNING] 目录 {script_dir} 下没有找到 .log 文件")
        return

    for log_name in log_files:
        log_path = os.path.join(script_dir, log_name)
        sizes = extract_sum_sizes(log_path)

        if not sizes:
            print(f"[WARNING] {log_name}: 没有提取到 Sum 行数据，跳过")
            continue

        # 输出 CSV 文件，两列: index, size_bytes
        out_name = os.path.splitext(log_name)[0] + '_space.csv'
        out_path = os.path.join(script_dir, out_name)

        with open(out_path, 'w', newline='') as csvfile:
            writer = csv.writer(csvfile)
            writer.writerow(['index', 'size_bytes'])
            for i, s in enumerate(sizes, start=1):
                writer.writerow([i, s])

        min_gb = min(sizes) / (1024**3)
        max_gb = max(sizes) / (1024**3)
        avg_gb = sum(sizes) / len(sizes) / (1024**3)
        print(f"[OK] {log_name} -> {out_name}  ({len(sizes)} 条记录, "
              f"min={min_gb:.4f} GB, max={max_gb:.4f} GB, avg={avg_gb:.4f} GB)")


if __name__ == '__main__':
    main()