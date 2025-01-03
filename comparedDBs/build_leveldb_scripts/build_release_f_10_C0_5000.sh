#!/bin/bash

# 构建脚本：build_release_f_10_C0_5000.sh
# 用途：为 LevelDB 生成 C0=5000 的构建配置

# 检查并删除现有的构建目录
BUILD_DIR="../leveldb/build_release_f_10_C0_5000"

if [ -d "$BUILD_DIR" ]; then
    echo "Removing existing directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# 创建新的构建目录
echo "Creating directory: $BUILD_DIR"
mkdir -p "$BUILD_DIR"

# 进入构建目录，配置并编译
echo "Configuring and building for C0=5000"
cd "$BUILD_DIR" || { echo "Failed to enter directory: $BUILD_DIR"; exit 1; }

cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j32

# 返回原始目录
cd ../../
