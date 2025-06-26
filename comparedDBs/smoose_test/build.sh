#!/bin/bash

# --- 脚本设置 ---
# set -e: 任何命令执行失败则立即退出脚本
# set -u: 使用未定义的变量则立即退出脚本
# set -o pipefail: 管道中的任何一个命令失败，整个管道的返回值就为非零
set -euo pipefail

# --- 脚本配置 ---
# 获取脚本所在的目录，也就是我们的项目根目录
PROJECT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# 定义 build 目录的路径
BUILD_DIR="${PROJECT_DIR}/build"

# --- 脚本执行 ---

# 1. 清理旧的构建文件
echo "--- [Step 1] Cleaning up previous build files ---"
if [ -d "${BUILD_DIR}" ]; then
    echo "Removing old build directory: ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi
echo "Cleanup complete."
echo ""

# 2. 打印信息，并重新创建 build 目录
echo "--- [Step 2] Creating a fresh build directory ---"
mkdir -p "${BUILD_DIR}"
echo "Build directory is at: ${BUILD_DIR}"
echo ""

# 3. 进入 build 目录并运行 CMake
echo "--- [Step 3] Running CMake to generate build files ---"
cd "${BUILD_DIR}"
# ".." 指向上一级目录，CMake 会在那里寻找 CMakeLists.txt
cmake ..
echo ""

# 4. 运行 make 来编译项目
echo "--- [Step 4] Running make to compile the project ---"
# -j$(nproc) 会使用所有可用的CPU核心来并行编译，大大加快速度
make -j$(nproc)
echo ""

# 5. 完成提示
echo "--- Build successful! ---"
echo "Executable is located at: ${BUILD_DIR}/smoose_tester"
echo ""
echo "To run the tester, execute the following command:"
echo "cd ${BUILD_DIR} && ./smoose_tester"
