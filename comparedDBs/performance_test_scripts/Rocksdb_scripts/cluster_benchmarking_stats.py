import os
import re
import csv

# ====== 配置区 ======
LOG_DIR = "/home/jeff-wang/LSM-tuning/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_TwitterCluster30_Benchmarking"        # 日志目录
STATS_DIR_SUFFIX = "Stats"               # 输出目录后缀
LOG_FILE_PATTERN = re.compile(
    r'^RocksDB_\d+(\.\d+)?B_in\d+(\.\d+)?B.*\.log$'
)
# ==================

def parse_log_file(filepath):
    """解析单个日志，失败则返回 None，成功返回字典"""
    with open(filepath, 'r') as f:
        content = f.read()

    # 1. 平均延迟
    latency_m = re.search(r"clusterQuery\s*:\s*([\d\.]+)\s+micros/op", content)
    # 2. 三项 cache 统计
    miss_m = re.search(r"rocksdb\.block\.cache\.miss COUNT\s*:\s*(\d+)", content)
    hit_m  = re.search(r"rocksdb\.block\.cache\.hit COUNT\s*:\s*(\d+)", content)
    add_m  = re.search(r"rocksdb\.block\.cache\.add COUNT\s*:\s*(\d+)", content)

    # 如果任何一项关键数据匹配失败，跳过此文件
    if not (latency_m and miss_m and hit_m and add_m):
        return None

    # 解析数值
    average_latency = float(latency_m.group(1))
    miss = int(miss_m.group(1))
    hit  = int(hit_m.group(1))
    add  = int(add_m.group(1))
    miss_rate = miss / (miss + hit) if (miss + hit) > 0 else None

    # 从文件名解析其它参数
    filename = os.path.basename(filepath)
    params = {'filename': filename}
    parts = filename.rstrip('.log').split('_')
    for part in parts:
        if re.match(r'^\d+(\.\d+)?B$', part):
            # 第一个匹配到的作为 query_data_size，第二个作为 write_data_size
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
        elif re.match(r'^Blk\d+$', part):
            # 新增：解析 Blk<数字> 为 block size
            params['block_size'] = part[3:]
        elif part.startswith('Blkcache'):
            params['block_cache'] = part.replace('Blkcache', '')
        elif part.startswith('Tabcache'):
            params['table_cache'] = part.replace('Tabcache', '')

    # 添加统计字段
    params.update({
        'average_latency_micros': average_latency,
        'block_cache_miss_rate':  miss_rate,
        'block_cache_miss':       miss,
        'block_cache_hit':        hit,
        'block_cache_add':        add,
    })
    return params

def main(log_dir):
    stats_dir = f"{log_dir}{STATS_DIR_SUFFIX}"
    os.makedirs(stats_dir, exist_ok=True)

    rows = []
    for fname in os.listdir(log_dir):
        if not LOG_FILE_PATTERN.match(fname):
            continue
        fullpath = os.path.join(log_dir, fname)
        result = parse_log_file(fullpath)
        if result is None:
            # 匹配不全，跳过此文件
            continue
        rows.append(result)

    if not rows:
        print("未找到任何符合条件且完整匹配的 log 文件，CSV 未生成。")
        return

    # 写 CSV
    output_csv = os.path.join(stats_dir, "parsed_results.csv")
    fieldnames = list(rows[0].keys())
    with open(output_csv, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"已将结果写入：{output_csv}")

if __name__ == "__main__":
    main(LOG_DIR)