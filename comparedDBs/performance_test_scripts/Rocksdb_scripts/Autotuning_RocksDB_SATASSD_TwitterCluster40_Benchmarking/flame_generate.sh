#!/bin/bash

# 火焰图生成脚本 - 配置版本
# 在这里修改你的路径配置

# ==============================================
# 配置区域 - 修改这些路径
# ==============================================

SCRIPT_DIR="$(realpath "$(dirname "$0")")"
# perf.data 文件的路径
PERF_DATA_FILE="${SCRIPT_DIR}/perf_rocksdb.data.old"
# FlameGraph 工具的目录路径
FLAMEGRAPH_DIR="/home/jeff-wang/FlameGraph"
# 输出目录（火焰图和报告会保存到这里）
OUTPUT_DIR="${SCRIPT_DIR}/flame_results"
# 输出火焰图的文件名（不包含.svg扩展名）
OUTPUT_NAME="rocksdb_flamegraph"

# ==============================================
# 脚本开始 - 下面不需要修改
# ==============================================

echo "=== Flame Graph Generator ==="
echo "Configuration:"
echo "  Perf data file: $PERF_DATA_FILE"
echo "  FlameGraph dir: $FLAMEGRAPH_DIR"
echo "  Output dir:     $OUTPUT_DIR"
echo "  Output name:    $OUTPUT_NAME"
echo ""

# 检查并创建输出目录
if [ ! -d "$OUTPUT_DIR" ]; then
    echo "Creating output directory: $OUTPUT_DIR"
    mkdir -p "$OUTPUT_DIR"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to create output directory $OUTPUT_DIR"
        exit 1
    fi
fi

# 检查perf数据文件是否存在
if [ ! -f "$PERF_DATA_FILE" ]; then
    echo "Error: Perf data file '$PERF_DATA_FILE' not found!"
    echo "Please update PERF_DATA_FILE path in this script"
    exit 1
fi

# 检查FlameGraph目录是否存在
if [ ! -d "$FLAMEGRAPH_DIR" ]; then
    echo "Error: FlameGraph directory '$FLAMEGRAPH_DIR' not found!"
    echo "Please clone it from: https://github.com/brendangregg/FlameGraph"
    echo "Or update FLAMEGRAPH_DIR path in this script"
    exit 1
fi

# 检查FlameGraph脚本是否存在
STACKCOLLAPSE="$FLAMEGRAPH_DIR/stackcollapse-perf.pl"
FLAMEGRAPH="$FLAMEGRAPH_DIR/flamegraph.pl"

if [ ! -f "$STACKCOLLAPSE" ]; then
    echo "Error: stackcollapse-perf.pl not found in $FLAMEGRAPH_DIR"
    exit 1
fi

if [ ! -f "$FLAMEGRAPH" ]; then
    echo "Error: flamegraph.pl not found in $FLAMEGRAPH_DIR"
    exit 1
fi

# 生成输出文件名（完整路径）
FLAME_SVG="$OUTPUT_DIR/${OUTPUT_NAME}.svg"
PERF_REPORT="$OUTPUT_DIR/${OUTPUT_NAME}_report.txt"

# 检查perf数据文件大小
PERF_SIZE=$(stat -f%z "$PERF_DATA_FILE" 2>/dev/null || stat -c%s "$PERF_DATA_FILE" 2>/dev/null)
echo "Perf data size: $(echo $PERF_SIZE | numfmt --to=iec 2>/dev/null || echo $PERF_SIZE bytes)"
echo ""

# 切换到输出目录进行处理
cd "$OUTPUT_DIR"

# 生成火焰图
echo "Generating flame graph..."
echo "Step 1/3: Running perf script..."
perf script -i "$PERF_DATA_FILE" > temp_perf_script.out 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Error: Failed to run perf script on $PERF_DATA_FILE"
    echo "Make sure the perf.data file is valid and readable"
    exit 1
fi

echo "Step 2/3: Collapsing stacks..."
"$STACKCOLLAPSE" temp_perf_script.out > temp_collapsed.out
if [ $? -ne 0 ]; then
    echo "Error: Failed to collapse stacks"
    rm -f temp_perf_script.out
    exit 1
fi

echo "Step 3/3: Generating SVG..."
"$FLAMEGRAPH" temp_collapsed.out > "$FLAME_SVG"
if [ $? -ne 0 ]; then
    echo "Error: Failed to generate flamegraph"
    rm -f temp_perf_script.out temp_collapsed.out
    exit 1
fi

# 清理临时文件
rm -f temp_perf_script.out temp_collapsed.out

echo ""
echo "=== Generation Complete ==="
echo "Flame graph saved as: $FLAME_SVG"
echo "File size: $(ls -lh "$FLAME_SVG" | awk '{print $5}')"

# 自动生成perf report
echo ""
echo "Generating perf report..."
perf report -i "$PERF_DATA_FILE" --stdio > "$PERF_REPORT" 2>/dev/null
if [ $? -eq 0 ]; then
    echo "Perf report saved as: $PERF_REPORT"
else
    echo "Warning: Failed to generate perf report"
fi

echo ""
echo "=== Files Generated ==="
echo "Flame graph: $FLAME_SVG"
if [ -f "$PERF_REPORT" ]; then
    echo "Perf report: $PERF_REPORT"
fi
echo ""
echo "Open $FLAME_SVG in a web browser to view the flame graph!"

# 显示一些有用的统计信息
if [ -f "$PERF_REPORT" ]; then
    echo ""
    echo "=== Quick Stats ==="
    echo "Top 5 functions by overhead:"
    head -20 "$PERF_REPORT" | grep -E "^ *[0-9]+\.[0-9]+%" | head -5
fi