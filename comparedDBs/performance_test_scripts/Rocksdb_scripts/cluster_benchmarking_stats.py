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
    """解析单个日志，失败则返回 None, 成功返回字典"""
    with open(filepath, 'r') as f:
        content = f.read()

    # 匹配平均延迟和 block cache 统计
    latency_m = re.search(r"clusterQuery\s*:\s*([\d\.]+)\s+micros/op", content)
    miss_m = re.search(r"rocksdb\.block\.cache\.miss COUNT\s*:\s*(\d+)", content)
    hit_m  = re.search(r"rocksdb\.block\.cache\.hit COUNT\s*:\s*(\d+)", content)
    add_m  = re.search(r"rocksdb\.block\.cache\.add COUNT\s*:\s*(\d+)", content)
    if not (latency_m and miss_m and hit_m and add_m):
        return None
    average_latency = float(latency_m.group(1))
    miss = int(miss_m.group(1)); hit = int(hit_m.group(1)); add = int(add_m.group(1))
    miss_rate = miss / (miss + hit) if (miss + hit) > 0 else None

    # 匹配 L0/L1/L2andup 命中
    l0 = re.search(r"rocksdb\.l0\.hit COUNT\s*:\s*(\d+)", content)
    l1 = re.search(r"rocksdb\.l1\.hit COUNT\s*:\s*(\d+)", content)
    l2u = re.search(r"rocksdb\.l2andup\.hit COUNT\s*:\s*(\d+)", content)
    l0_hits = int(l0.group(1)) if l0 else None
    l1_hits = int(l1.group(1)) if l1 else None
    l2u_hits = int(l2u.group(1)) if l2u else None

    # 匹配 Bloom filter 统计
    b_useful = re.search(r"rocksdb\.bloom\.filter\.useful COUNT\s*:\s*(\d+)", content)
    b_full_pos = re.search(r"rocksdb\.bloom\.filter\.full\.positive COUNT\s*:\s*(\d+)", content)
    b_full_tp = re.search(r"rocksdb\.bloom\.filter\.full\.true\.positive COUNT\s*:\s*(\d+)", content)
    bloom_useful = int(b_useful.group(1)) if b_useful else None
    bloom_full_pos = int(b_full_pos.group(1)) if b_full_pos else None
    bloom_full_tp = int(b_full_tp.group(1)) if b_full_tp else None
    bloom_false_pos_count = None
    bloom_false_pos_rate = None
    if bloom_full_pos is not None and bloom_full_tp is not None and bloom_full_pos > 0:
        bloom_false_pos_count = bloom_full_pos - bloom_full_tp
        bloom_false_pos_rate = bloom_false_pos_count / bloom_full_pos

    # 匹配 read.block.get.micros SUM
    read_get = re.search(
        r"rocksdb\.read\.block\.get\.micros[\s\S]*?SUM\s*:\s*([\d\.]+)", content
    )
    read_get_sum = float(read_get.group(1)) if read_get else None
    # 计算因 Bloom 假阳性导致的额外 IO 时间 (秒)
    false_io_time_sec = None
    if read_get_sum is not None and bloom_false_pos_rate is not None:
        false_io_time_sec = (read_get_sum * bloom_false_pos_rate) / 1e6

    # 解析文件名参数
    filename = os.path.basename(filepath)
    params = {'filename': filename}
    parts = filename.rstrip('.log').split('_')
    for part in parts:
        if re.match(r'^\d+(\.\d+)?B$', part):
            params.setdefault('query_data_size', part)
            if 'query_data_size' in params and params['query_data_size'] != part:
                params['write_data_size'] = part
        elif part.startswith('in') and re.match(r'^in\d+(\.\d+)?B$', part):
            params['write_data_size'] = part[2:]
        elif part.startswith('val'):
            params['value_size'] = part[3:]
        elif part.startswith('mem'):
            params['mem_size'] = part[3:]
        elif part.startswith('Cluster'):
            params['cluster'] = part.replace('Cluster','')
        elif part.startswith('CT'):
            params['CT'] = part[2:]
        elif part.startswith('level') and 'base' in part:
            k,v = part.split('base',1); params[k+'base']=v
        elif part.startswith('targetbase'):
            params['targetbase']=part.replace('targetbase','')
        elif re.match(r'^Blk\d+$',part):
            params['block_size']=part[3:]
        elif part.startswith('Blkcache'):
            params['block_cache']=part.replace('Blkcache','')
        elif part.startswith('Tabcache'):
            params['table_cache']=part.replace('Tabcache','')

    # 聚合结果
    params.update({
        'average_latency_micros': average_latency,
        'block_cache_miss_rate': miss_rate,
        'block_cache_miss': miss,
        'block_cache_hit': hit,
        'block_cache_add': add,
        'l0_hit_count': l0_hits,
        'l1_hit_count': l1_hits,
        'l2andup_hit_count': l2u_hits,
        'bloom_filter_useful': bloom_useful,
        'bloom_full_positive': bloom_full_pos,
        'bloom_full_true_positive': bloom_full_tp,
        'bloom_false_positive_count': bloom_false_pos_count,
        'bloom_false_positive_rate': bloom_false_pos_rate,
        'read_block_get_sum_micros': read_get_sum,
        'false_positive_io_time_sec': false_io_time_sec,
    })
    return params


def main(log_dir):
    stats_dir = f"{log_dir}{STATS_DIR_SUFFIX}"
    os.makedirs(stats_dir, exist_ok=True)
    rows = []
    for fname in os.listdir(log_dir):
        if not LOG_FILE_PATTERN.match(fname): continue
        res = parse_log_file(os.path.join(log_dir, fname))
        if res: rows.append(res)
    if not rows:
        print("未找到符合条件的日志，未生成 CSV。")
        return
    out_csv = os.path.join(stats_dir,'parsed_results.csv')
    with open(out_csv,'w',newline='') as cf:
        writer=csv.DictWriter(cf,fieldnames=list(rows[0].keys()))
        writer.writeheader(); writer.writerows(rows)
    print(f"结果已写入 {out_csv}")

if __name__=='__main__':
    main(LOG_DIR)
