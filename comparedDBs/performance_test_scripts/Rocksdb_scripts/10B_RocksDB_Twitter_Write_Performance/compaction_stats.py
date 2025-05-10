import os
import re
import pandas as pd
from pathlib import Path

# 设置日志目录路径
log_dir = Path("/home/jeff-wang/LSM-tuning/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_Twitter_Write_Performance")

# 自动生成保存统计文件的路径（与 log_dir 同级）
output_dir = log_dir.parent / (log_dir.name.replace("Performance", "CompactionStats"))
output_dir.mkdir(parents=True, exist_ok=True)

# 正则表达式匹配 compaction 数据
compaction_pattern = re.compile(
    r"Cumulative compaction:\s+([\d.]+) GB write, ([\d.]+) MB/s write, ([\d.]+) GB read, ([\d.]+) MB/s read, ([\d.]+) seconds"
)

# 正则表达式解析文件名参数
filename_pattern = re.compile(
    r"RocksDB_.*_Cluster(?P<cluster>\d+)"
    r"_CT0(?P<ct0>\d+)"
    r"_level1base(?P<l1base>\d+)"
    r"_targetbase(?P<targetbase>\d+)"
)

records = []

# 遍历所有 .log 文件
for file in log_dir.glob("*.log"):
    with open(file, "r") as f:
        lines = f.readlines()

    last_match = None
    for line in lines:
        m = compaction_pattern.search(line)
        if m:
            last_match = m  # 只保留最后一次匹配

    if not last_match:
        continue

    # 提取 compaction stats
    write_gb, write_speed, read_gb, read_speed, seconds = map(float, last_match.groups())

    # 从文件名中提取新参数
    name_match = filename_pattern.search(file.name)
    if not name_match:
        continue

    cluster    = int(name_match.group("cluster"))
    ct0        = int(name_match.group("ct0"))
    l1base     = int(name_match.group("l1base"))
    targetbase = int(name_match.group("targetbase"))

    records.append({
        "filename":       file.name,
        "cluster":        cluster,
        "ct0":            ct0,
        "l1base":         l1base,
        "targetbase":     targetbase,
        "write_gb":       write_gb,
        "write_speed_MBps": write_speed,
        "read_gb":        read_gb,
        "read_speed_MBps":  read_speed,
        "duration_sec":   seconds
    })

# 写入 CSV
output_csv = output_dir / "rocksdb_compaction_summary.csv"
df = pd.DataFrame(records)
df.to_csv(output_csv, index=False)

print(f"Saved compaction stats to: {output_csv}")