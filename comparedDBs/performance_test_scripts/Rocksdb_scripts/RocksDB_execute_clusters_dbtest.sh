#!/usr/bin/env bash
set -euo pipefail

# 如果脚本不在当前目录，修改为它们所在的路径
SCRIPTS=(
  "./RocksDB_traces_preload_dbtest.sh"
  "./RocksDB_traces_write_benchmarking_dbtest.sh"
)

for script in "${SCRIPTS[@]}"; do
  if [[ ! -x "$script" ]]; then
    echo "给 $script 添加可执行权限"
    chmod +x "$script"
  fi
  echo -e "\n===== 开始执行: $script ====="
  bash "$script"
  echo "===== 执行完成: $script ====="
done

echo -e "\n所有脚本均已执行完毕。"
