#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Example script to run dost_tuner.py with specific workload and hardware parameters.
"""

import subprocess
import json

# 路径：假设你的 dost_tuner.py 在当前目录
TUNER_PATH = "/home/jeff-wang/LSM-tuning/comparedDBs/smoose_test/dost_tuner.py"

# ==== 输入参数定义 ====
# 数据集和系统参数
N = 8_000_000_000        # 8B entries (~1TB)
ENTRY_BYTES = 128        # 每个KV的平均大小 (bytes)
BLOCK_BYTES = 4096       # 存储块大小 (bytes)
P = 4096                 # Memtable 容量 (以块数计)
M_BITS = int(10 * N)     # 总的 Bloom bits (10 bits per entry)

# 工作负载混合系数（参考 Dostoevsky Eq.14）
WORKLOAD_MIX = {
    "w": 0.60,  # updates
    "r": 0.20,  # zero-result lookups
    "v": 0.15,  # existing-key lookups
    "q": 0.05   # range lookups
}

# 硬件参数
MU = 4.0       # 顺序/随机读性能比 (SSD)
PHI = 1.0      # 写相对读的惩罚系数
OMEGA = 1.0    # block读取时间常数

# Range 查询参数
S_UNIQUE = 0   # 平均每次range查询覆盖的unique entries

# 约束条件
MAX_AMP = 0.10             # 最大空间放大 (10%)
MAX_T = None               # 可不指定
LIMIT_T_CANDIDATES = 64    # 限制搜索范围

# ==== 调用命令行 ====
cmd = [
    "python3", TUNER_PATH,
    "--N", str(N),
    "--entry-bytes", str(ENTRY_BYTES),
    "--block-bytes", str(BLOCK_BYTES),
    "--P", str(P),
    "--M-bits", str(M_BITS),
    "--mix-w", str(WORKLOAD_MIX["w"]),
    "--mix-r", str(WORKLOAD_MIX["r"]),
    "--mix-v", str(WORKLOAD_MIX["v"]),
    "--mix-q", str(WORKLOAD_MIX["q"]),
    "--mu", str(MU),
    "--phi", str(PHI),
    "--omega", str(OMEGA),
    "--s-unique", str(S_UNIQUE),
    "--max-amp", str(MAX_AMP),
    "--limit-T-candidates", str(LIMIT_T_CANDIDATES)
]

# ==== 执行 ====
print("Running Dostoevsky tuner...\n")
result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

if result.returncode != 0:
    print("❌ Error running tuner script:")
    print(result.stderr)
else:
    print("✅ Tuning completed. Raw output:\n")
    print(result.stdout)
    try:
        config = json.loads(result.stdout)
        print("\nParsed best configuration summary:")
        best = config["best_config"]
        print(f"  T = {best['T']}, K = {best['K']}, Z = {best['Z']}, L = {best['L']}")
        print(f"  Space Amplification = {best['space_amp']:.3f}")
        print(f"  τ (throughput score) = {best['metrics']['tau']:.6f}")
        print(f"  Per-level Bloom FPRs = {best['bloom_fpr_per_level']}")
        print("\nYou can pass `moose_config` directly into Moose/db_bench API.")
    except Exception as e:
        print("⚠️ Could not parse JSON output:", e)
