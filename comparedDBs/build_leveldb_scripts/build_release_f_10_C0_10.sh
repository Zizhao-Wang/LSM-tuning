#!/bin/bash

# 构建脚本：build_release_f_10_C0_2000.sh
# 用途：为 LevelDB 生成 C0=10 的构建配置，并记录构建日志

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 定义构建目录的绝对路径
F=4
BUILD_DIR="${SCRIPT_DIR}/../leveldb/build_release_f_${F}_C0_10"

# 检查并删除现有的构建目录
if [ -d "$BUILD_DIR" ]; then
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Removing existing directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    if [ $? -ne 0 ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Failed to remove directory: $BUILD_DIR"
        exit 1
    fi
fi

# 创建新的构建目录
echo "[$(date '+%Y-%m-%d %H:%M:%S')] Creating directory: $BUILD_DIR"
mkdir -p "$BUILD_DIR"
if [ $? -ne 0 ]; then
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Failed to create directory: $BUILD_DIR"
    exit 1
fi

# 定义日志文件路径
LOG_FILE="${BUILD_DIR}/build_info.txt"

# 初始化日志文件
echo "Build Script: build_release_f_10_C0_2000.sh" > "$LOG_FILE"
echo "Build Configuration: C0=10, CMAKE_BUILD_TYPE=Debug" >> "$LOG_FILE"
echo "Start Time: $(date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE"
echo "-----------------------------------------" >> "$LOG_FILE"

# 进入构建目录，配置并编译
echo "[$(date '+%Y-%m-%d %H:%M:%S')] Configuring and building for C0=10"
cd "$BUILD_DIR" || { echo "[$(date '+%Y-%m-%d %H:%M:%S')] Failed to enter directory: $BUILD_DIR" | tee -a "$LOG_FILE"; exit 1; }

# 执行构建命令，并将输出追加到日志文件
cmake -DCMAKE_BUILD_TYPE=Debug .. >> "$LOG_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] CMake configuration failed." | tee -a "$LOG_FILE"
    echo "End Time: $(date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE"
    echo "Build Status: Failed" >> "$LOG_FILE"
    echo "-----------------------------------------" >> "$LOG_FILE"
    exit 1
fi

make -j32 >> "$LOG_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Make build failed." | tee -a "$LOG_FILE"
    echo "End Time: $(date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE"
    echo "Build Status: Failed" >> "$LOG_FILE"
    echo "-----------------------------------------" >> "$LOG_FILE"
    exit 1
fi

echo "[$(date '+%Y-%m-%d %H:%M:%S')] Build succeeded for C0=10" | tee -a "$LOG_FILE"

# 记录完成时间和状态
echo "End Time: $(date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE"
echo "Build Status: Success" >> "$LOG_FILE"
echo "-----------------------------------------" >> "$LOG_FILE"


# 新增功能：将 version_set.cc 的前100行代码追加到 LOG_FILE
VERSION_SET_FILE="${SCRIPT_DIR}/../leveldb/db/version_set.cc"

if [ -f "$VERSION_SET_FILE" ]; then
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Appending first 100 lines of version_set.cc to LOG_FILE" | tee -a "$LOG_FILE"
    echo "----- First 100 lines of version_set.cc -----" | tee -a "$LOG_FILE"
    head -n 100 "$VERSION_SET_FILE" >> "$LOG_FILE" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Failed to read version_set.cc" | tee -a "$LOG_FILE"
    else
        echo "----- End of version_set.cc -----" | tee -a "$LOG_FILE"
    fi
else
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] File version_set.cc does not exist at path: $VERSION_SET_FILE" | tee -a "$LOG_FILE"
fi

# 返回原始目录
cd "${SCRIPT_DIR}/../../"
