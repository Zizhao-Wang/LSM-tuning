#!/bin/bash

# --- 脚本设置 ---
# set -e: 任何命令执行失败则立即退出脚本
# set -u: 使用未定义的变量则立即退出脚本
# set -o pipefail: 管道中的任何一个命令失败，整个管道的返回值就为非零
set -euo pipefail

# --- 配置区 (请根据你的环境修改) ---

# 1. 测试程序的可执行文件路径
# 通常在 build 目录下面
TESTER_EXE="./build/smoose_tester"

# 2. CSV 数据文件路径
# !!! 警告: 请务必将此路径修改为你的真实 CSV 文件路径 !!!
DATA_FILE="/path/to/your/data.csv"

# 3. 结果日志文件存放目录
# 脚本会自动创建这个目录
LOG_DIR="./experiment_logs"

# --- 启动前的检查 ---
if [ ! -f "$TESTER_EXE" ]; then
    echo "错误: 测试程序不存在于 '$TESTER_EXE'" >&2
    echo "请先运行 ./build.sh 编译程序。" >&2
    exit 1
fi

if [ "$DATA_FILE" == "/path/to/your/data.csv" ] || [ ! -f "$DATA_FILE" ]; then
    echo "错误: 数据文件路径不正确或文件不存在。" >&2
    echo "请在脚本中修改 DATA_FILE 变量为你真实的 CSV 文件路径。" >&2
    exit 1
fi
# 创建日志目录
mkdir -p "$LOG_DIR"


# --- 实验运行函数 ---
# 这个函数负责执行一次单独的测试
# 参数:
# $1: 写入总数 (num)
# $2: Key 大小 (key_size)
# $3: Value 大小 (value_size)
# $4: 每批次条目数 (entries_per_batch)
run_single_test() {
    local num=$1
    local key_size=$2
    local value_size=$3
    local batch_size=$4

    # 根据参数生成一个清晰、描述性的日志文件名
    local log_file="${LOG_DIR}/test_num-${num}_key-${key_size}_val-${value_size}_batch-${batch_size}.log"

    echo "========================================================================"
    echo "=> [STARTING TEST]"
    echo "   Num Keys: ${num}, Key Size: ${key_size}, Value Size: ${value_size}, Batch Size: ${batch_size}"
    echo "   Log file: ${log_file}"
    echo "------------------------------------------------------------------------"

    # 构建并执行命令
    # 使用 `tee` 可以同时在屏幕上显示输出并将其保存到日志文件
    "$TESTER_EXE" \
        --data_file_path="$DATA_FILE" \
        --num="$num" \
        --key_size="$key_size" \
        --value_size="$value_size" \
        --entries_per_batch="$batch_size" \
        | tee "$log_file"

    echo "=> [TEST FINISHED]"
    echo "========================================================================"
    echo "" # 增加一个空行，让输出更清晰
}

# --- 实验执行区 ---
# 在这里定义并运行你想要执行的各项实验。
# 你可以自由地添加、删除或修改下面的调用。

echo ">>> Starting all experiments..."
echo ""

# --- 实验系列 1: 测试不同数量级的数据 ---
echo ">>> Running Experiment Series 1: Varying number of keys..."
run_single_test 500000   20 100 1000 # 50万条
run_single_test 1000000  20 100 1000 # 100万条 (基准)
run_single_test 2000000  20 100 1000 # 200万条

# --- 实验系列 2: 测试不同 value 大小 ---
echo ">>> Running Experiment Series 2: Varying value sizes..."
run_single_test 1000000  20 64   1000 # 小 value
run_single_test 1000000  20 256  1000 # 中等 value
run_single_test 1000000  20 1024 1000 # 大 value

# --- 实验系列 3: 测试不同批处理大小 ---
echo ">>> Running Experiment Series 3: Varying batch sizes..."
run_single_test 1000000  20 100 500  # 小批次
run_single_test 1000000  20 100 2000 # 大批次

echo ">>> All experiments have been completed."
echo ">>> Check the '${LOG_DIR}' directory for results."

