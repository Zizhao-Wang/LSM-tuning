from pathlib import Path
import pandas as pd

# ========= 1. 路径 =========
script_dir = Path(__file__).resolve().parent
input_csv = script_dir / "RocksDBDynamic_0.1B_val1796_mem64MB_Cluster35_CT04_L1512_Blkcache8_Tabcache0_f10_metrics.csv"
output_csv = script_dir / "output_with_deltas35.csv"

input_csv = script_dir / "RocksDB_0.1B_val4266_mem64MB_Cluster13_CT04_L1512_Blkcache8_Tabcache500_f10_metrics.csv"
output_csv = script_dir / "output_with_deltas13.csv"

input_csv = script_dir / "RocksDBDynamic_0.01B_val4266_mem64MB_Cluster13_CT016_L1512_Blkcache8_Tabcache500_f10_metrics.csv"
output_csv = script_dir / "output_with_deltas13_4.csv"


df = pd.read_csv(input_csv)

# ========= 2. 你自己设定每个区间的 ops =========
INTERVAL_OPS = 1000000  # 这里你自己改

# ========= 3. 原始增量 =========
# cumulative_write_gb 实际上是 cumulative_compacted_gb
df["delta_compacted_gb"] = df["cumulative_write_gb"].diff()
df.loc[df.index[0], "delta_compacted_gb"] = df.loc[df.index[0], "cumulative_write_gb"]

df["delta_l1_compaction_sec"] = df["l1_compaction_sec"].diff()
df.loc[df.index[0], "delta_l1_compaction_sec"] = df.loc[df.index[0], "l1_compaction_sec"]

df["delta_l0_compaction_sec"] = df["l0_flush_sec"].diff()
df.loc[df.index[0], "delta_l0_compaction_sec"] = df.loc[df.index[0], "l0_flush_sec"]

# 区间时间：当前 elapsed - 上一个 elapsed
df["interval_elapsed_sec"] = df["elapsed_sec"].diff()
df.loc[df.index[0], "interval_elapsed_sec"] = df.loc[df.index[0], "elapsed_sec"]

# 区间时间：当前 elapsed - 上一个 elapsed
df["delta_compaction_sec"] = df["sum_comp_sec"].diff()
df.loc[df.index[0], "delta_compaction_sec"] = df.loc[df.index[0], "sum_comp_sec"]


# 区间 throughput
df["interval_ops"] = INTERVAL_OPS
df["interval_throughput_ops_sec"] = df["interval_ops"] / df["interval_elapsed_sec"]

# ========= 4. 保留两位小数 =========
df = df.round(2)

# ========= 5. 保存 =========
df.to_csv(output_csv, index=False)

print(df.head(10).to_string(index=False))
print(f"\nSaved to: {output_csv}")