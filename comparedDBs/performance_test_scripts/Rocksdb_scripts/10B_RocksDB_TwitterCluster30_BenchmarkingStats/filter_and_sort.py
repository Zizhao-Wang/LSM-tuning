import os
import csv
from pathlib import Path

# ====== 配置区 ======
# 获取脚本所在目录
WORK_DIR = Path(__file__).resolve().parent

# 假定 CSV 文件位于脚本同级目录，文件名为 parsed_results.csv
STATS_CSV = WORK_DIR / "parsed_results.csv"

# 过滤条件：字典形式，键为参数名，值为要匹配的值或列表
# 如果值是字符串，则按等值过滤；如果是列表，则按成员过滤
FILTER_PARAMS = {
    "table_cache": "300",
    "block_cache": "512",
    # 多值过滤示例：只保留这些 block_size
    "block_size": ["1", "4", "8", "10", "16", "32"],
}

# 排序字段：按照该字段升序排列
SORT_KEY = "block_size"

# 要输出的指标列（不再输出 filename）
METRICS = ["average_latency_micros", "block_cache_miss_rate"]
# ============================================================

def load_rows(csv_path):
    """从 CSV 读取所有行，返回字典列表并将数值字段转换为 float。"""
    rows = []
    with open(csv_path, newline='') as f:
        reader = csv.DictReader(f)
        for r in reader:
            # 转换数值列
            for num_col in [
                "average_latency_micros",
                "block_cache_miss_rate",
                "block_cache_miss",
                "block_cache_hit",
                "block_cache_add"
            ]:
                v = r.get(num_col)
                if v not in (None, ""):
                    try:
                        r[num_col] = float(v)
                    except ValueError:
                        r[num_col] = None
            rows.append(r)
    return rows


def filter_and_sort(rows, filters, sort_key):
    """先过滤再排序。返回符合条件并按 sort_key 升序排列的行列表。"""
    filtered = []
    for r in rows:
        ok = True
        for k, v in filters.items():
            val = r.get(k)
            # 值为列表时，检查成员关系；否则按等值
            if isinstance(v, (list, tuple)):
                if val not in v:
                    ok = False
                    break
            else:
                if val != v:
                    ok = False
                    break
        if ok:
            filtered.append(r)

    # 数值排序：尝试将 sort_key 转为 float，否则排在末尾
    def sort_func(x):
        v = x.get(sort_key)
        try:
            return float(v)
        except (TypeError, ValueError):
            return float('inf')

    return sorted(filtered, key=sort_func)


def main():
    # 检查 CSV 是否存在
    if not STATS_CSV.is_file():
        print(f"Error: 在脚本目录未找到 CSV 文件：{STATS_CSV.name}")
        return

    rows = load_rows(STATS_CSV)
    results = filter_and_sort(rows, FILTER_PARAMS, SORT_KEY)

    if not results:
        print("没有找到满足过滤条件的记录。")
        return

    # 输出带行号的数据：先打印表头
    header = ["idx"] + METRICS
    print("\t".join(header))

    # 打印每一行，带上行号
    for idx, r in enumerate(results):
        values = [
            f"{r.get(col):.6f}" if isinstance(r.get(col), float) else str(r.get(col))
            for col in METRICS
        ]
        print(f"{idx}\t" + "\t".join(values))

if __name__ == "__main__":
    main()
