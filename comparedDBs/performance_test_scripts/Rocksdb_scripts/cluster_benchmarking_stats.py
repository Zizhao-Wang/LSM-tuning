import os
import re
import csv

# ====== 配置区：把下面的变量改成你自己的路径或命名 ======
# 日志所在目录（不带末尾斜杠）
LOG_DIR = "/home/jeff-wang/LSM-tuning/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_TwitterCluster30_Benchmarking"

# 输出目录后缀，会在 LOG_DIR 后面拼接上这个后缀
STATS_DIR_SUFFIX = "Stats"

# 匹配日志文件的正则：只处理 RocksDB_<实数>B_in<实数>B… .log
LOG_FILE_PATTERN = re.compile(r'^RocksDB_\d+(\.\d+)?B_in\d+(\.\d+)?B.*\.log$')
# ============================================================

def parse_log_file(filepath):
    """解析单个 log 文件，返回一个参数字典"""
    with open(filepath, 'r') as f:
        content = f.read()

    # 提取平均延迟
    latency_match = re.search(r"clusterQuery\s*:\s*([\d\.]+)\s+micros/op", content)
    average_latency = float(latency_match.group(1)) if latency_match else None

    # 提取 block cache 统计
    miss = int(re.search(r"rocksdb\.block\.cache\.miss COUNT\s*:\s*(\d+)", content).group(1))
    hit  = int(re.search(r"rocksdb\.block\.cache\.hit COUNT\s*:\s*(\d+)", content).group(1))
    add  = int(re.search(r"rocksdb\.block\.cache\.add COUNT\s*:\s*(\d+)", content).group(1))
    miss_rate = miss / (miss + hit) if (miss + hit) > 0 else None

    # 从文件名解析各参数
    filename = os.path.basename(filepath)
    params = {'filename': filename}
    parts = filename.rstrip('.log').split('_')
    for part in parts:
        if re.match(r'^\d+(\.\d+)?B$', part):
            if 'query_data_size' not in params:
                params['query_data_size'] = part
            else:
                params['write_data_size'] = part
        elif part.startswith('in') and re.match(r'^in\d+(\.\d+)?B$', part):
            params['write_data_size'] = part[2:]
        elif part.startswith('val'):
            params['value_size'] = part[3:]
        elif part.startswith('mem'):
            params['mem_size'] = part[3:]
        elif part.startswith('Cluster'):
            params['cluster'] = part.replace('Cluster', '')
        elif part.startswith('CT'):
            params['CT'] = part[2:]
        elif part.startswith('level') and 'base' in part:
            key, val = part.split('base', 1)
            params[key + 'base'] = val
        elif part.startswith('targetbase'):
            params['targetbase'] = part.replace('targetbase', '')
        elif part.startswith('Blkcache'):
            params['block_cache'] = part.replace('Blkcache', '')
        elif part.startswith('Tabcache'):
            params['table_cache'] = part.replace('Tabcache', '')

    # 添加统计指标
    params.update({
        'average_latency_micros': average_latency,
        'block_cache_miss_rate':  miss_rate,
        'block_cache_miss':       miss,
        'block_cache_hit':        hit,
        'block_cache_add':        add,
    })
    return params

def main(log_dir):
    # 自动在 log_dir 后面加上后缀，作为输出目录
    stats_dir = f"{log_dir}{STATS_DIR_SUFFIX}"
    os.makedirs(stats_dir, exist_ok=True)

    rows = []
    for fname in os.listdir(log_dir):
        if not LOG_FILE_PATTERN.match(fname):
            continue  # 跳过不符合模式的文件
        path = os.path.join(log_dir, fname)
        try:
            rows.append(parse_log_file(path))
        except Exception as e:
            print(f"Warning: 解析文件 {fname} 失败：{e}")

    if not rows:
        print("未找到任何符合模式的 log 文件。")
        return

    # 写入 CSV
    output_csv = os.path.join(stats_dir, "parsed_results.csv")
    fieldnames = list(rows[0].keys())
    with open(output_csv, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"已将结果写入：{output_csv}")

# 脚本入口
if __name__ == "__main__":
    main(LOG_DIR)
