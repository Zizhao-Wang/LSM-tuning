import os
import re

# 替换为你的目标目录路径
log_dir = "/home/jeff-wang/LSM-tuning/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_Write_Memtable_Performance"

for filename in os.listdir(log_dir):
    if filename.endswith(".log"):
        # 注意：先匹配 mem 后匹配 zipf
        match = re.match(r"(.+?)_mem(\d+MB)_zipf(1\.\d+)(_.+\.log)", filename)
        if match:
            prefix, mem, zipf, suffix = match.groups()
            # 交换 mem 和 zipf 的顺序
            new_filename = f"{prefix}_zipf{zipf}_mem{mem}{suffix}"
            old_path = os.path.join(log_dir, filename)
            new_path = os.path.join(log_dir, new_filename)
            os.rename(old_path, new_path)
            print(f"Renamed:\n  {filename}\n  --> {new_filename}")

