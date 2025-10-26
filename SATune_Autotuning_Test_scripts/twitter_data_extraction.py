import os
import re
import csv

# ==== 配置 ====
LOG_DIR = r"/home/jeff-wang/LSM-tuning/SATune_Autotuning_Test_scripts/SATune_SATASSD_TwitterCluster12_Benchmarking_Performance_switch10"  
OUTPUT_FILE = os.path.join(LOG_DIR, "combined_summary.csv")

# ==== 正则 ====
# 文件名提取
re_ct = re.compile(r"CT0(\d+)")
re_l = re.compile(r"L1(\d+)")

# Performance Profile 匹配
re_perf_start = re.compile(r"--- Performance Profile ---")
re_level0 = re.compile(
    r"=== LEVEL 0 OPERATIONS ===.*?Bloom Filter Check:\s*(\d+).*?Index Block Search:\s*(\d+)",
    re.S,
)
re_other_levels = re.compile(
    r"=== OTHER LEVELS OPERATIONS ===.*?Bloom Filter Check:\s*(\d+).*?Index Block Search:\s*(\d+)",
    re.S,
)

# WRITE 匹配
re_write_block = re.compile(
    r"WRITE: Microseconds per op:\s*?\nCount:\s*\d+\s+Average:\s*([\d\.]+)",
    re.M,
)


def extract_last_performance_section(text):
    """提取最后一个 '--- Performance Profile ---' 段"""
    matches = list(re_perf_start.finditer(text))
    if not matches:
        return None
    last_match = matches[-1]
    return text[last_match.start():]


def extract_ops_sum(section_text):
    """从 Performance Section 提取四个数字并求和"""
    m0 = re_level0.search(section_text)
    m1 = re_other_levels.search(section_text)

    if not (m0 and m1):
        return None

    try:
        level0_sum = int(m0.group(1)) + int(m0.group(2))
        other_sum = int(m1.group(1)) + int(m1.group(2))
        total = level0_sum + other_sum
        return total
    except ValueError:
        return None


def extract_average(text):
    """提取 WRITE 段的 Average 值"""
    matches = re_write_block.findall(text)
    if not matches:
        return None
    return float(matches[-1])


def main():
    results = []  # [(CT, L, Sum(ops), Average)]

    for filename in os.listdir(LOG_DIR):
        if not (filename.startswith("SATune") and filename.endswith(".log")):
            continue

        filepath = os.path.join(LOG_DIR, filename)

        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()

        # 提取 Performance Profile
        section = extract_last_performance_section(content)
        total_ops = extract_ops_sum(section) if section else None

        # 提取 WRITE 平均值
        avg = extract_average(content)

        # 提取 CT/L1
        ct_match = re_ct.search(filename)
        l_match = re_l.search(filename)

        if not (ct_match and l_match):
            print(f"[跳过] {filename}: 文件名未匹配到 CT/L1")
            continue

        ct = ct_match.group(1)
        l = l_match.group(1)

        if total_ops is None or avg is None:
            print(f"[警告] {filename}: 缺少数据 (ops或avg)")
            continue

        results.append((ct, l, total_ops, avg))

    if not results:
        print("没有提取到任何结果。")
        return

    results.sort(key=lambda x: (int(x[0]), int(x[1])))

    with open(OUTPUT_FILE, "w", newline="", encoding="utf-8") as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(["CT", "L", "Sum(ops)", "Average(us/op)"])
        writer.writerows(results)

    print(f"✅ 结果已保存至: {OUTPUT_FILE}")
    print("共处理文件数:", len(results))


if __name__ == "__main__":
    main()
