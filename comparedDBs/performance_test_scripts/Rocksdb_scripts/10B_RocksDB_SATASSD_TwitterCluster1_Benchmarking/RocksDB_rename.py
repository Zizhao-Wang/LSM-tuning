import re
from pathlib import Path

# 配置：指向包含 .log 文件的目录
log_dir = Path("/home/jeff-wang/LSM-tuning/comparedDBs/performance_test_scripts/Rocksdb_scripts/10B_RocksDB_Twitter_Benchmarking")

# 正则：匹配以 “RocksDB_0.1B” 开头，后面跟 “_” 并以 “.log” 结尾的文件名
pattern = re.compile(r'^(RocksDB_0\.1B)(_.*\.log)$')

for filepath in log_dir.glob("RocksDB_0.1B_val267*.log"):
    name = filepath.name
    m = pattern.match(name)
    if not m:
        # 如果有其他以 RocksDB_0.1B 开头但不符合格式的文件，可在这里处理或跳过
        continue

    # 构造新的文件名：在第一个组（RocksDB_0.1B）后面插入 “_in0.2B”
    new_name = f"{m.group(1)}_in0.2B{m.group(2)}"
    new_path = filepath.with_name(new_name)

    # 重命名
    print(f"Renaming:\n  {filepath.name}\n→ {new_name}\n")
    filepath.rename(new_path)
