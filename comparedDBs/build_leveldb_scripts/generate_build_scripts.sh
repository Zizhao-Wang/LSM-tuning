#!/bin/bash

# 生成多个 LevelDB 构建脚本的主脚本
# 使用方法：
# 1. 将此脚本保存为 generate_build_scripts.sh
# 2. 赋予执行权限：chmod +x generate_build_scripts.sh
# 3. 运行脚本：./generate_build_scripts.sh
# 4. 将构建命令复制到相应的生成的 .sh 文件中

# 定义 C0 的不同值
C0_VALUES=(10 100 500 1000 2000 5000)

# 定义脚本文件前缀和后缀
SCRIPT_PREFIX="build_release_f_10_C0_"
SCRIPT_SUFFIX=".sh"

# 定义脚本存放的目录（可根据需要修改）
SCRIPT_DIR="./build_scripts"

# 创建脚本目录（如果不存在）
mkdir -p "$SCRIPT_DIR"

# 遍历每个 C0 值，生成对应的脚本文件
for C0 in "${C0_VALUES[@]}"; do
    SCRIPT_NAME="${SCRIPT_PREFIX}${C0}${SCRIPT_SUFFIX}"
    SCRIPT_PATH="${SCRIPT_DIR}/${SCRIPT_NAME}"
    
    # 创建脚本文件并写入基本结构
    cat <<EOL > "$SCRIPT_PATH"
#!/bin/bash

# 构建脚本：${SCRIPT_NAME}
# 用途：为 LevelDB 生成 C0=${C0} 的构建配置

# 检查并删除现有的构建目录
BUILD_DIR="../leveldb/${SCRIPT_PREFIX}${C0}"

if [ -d "\$BUILD_DIR" ]; then
    echo "Removing existing directory: \$BUILD_DIR"
    rm -rf "\$BUILD_DIR"
fi

# 创建新的构建目录
echo "Creating directory: \$BUILD_DIR"
mkdir -p "\$BUILD_DIR"

# 进入构建目录，配置并编译
echo "Configuring and building for C0=${C0}"
cd "\$BUILD_DIR" || { echo "Failed to enter directory: \$BUILD_DIR"; exit 1; }

# TODO: 将你的构建命令复制到这里
# 示例：
# cmake -DCMAKE_BUILD_TYPE=Debug -DC0=${C0} ../.. && make -j32

# 返回原始目录
cd ../../
EOL

    # 赋予脚本执行权限
    chmod +x "$SCRIPT_PATH"
    
    echo "生成脚本：$SCRIPT_PATH"
done

echo "所有构建脚本已生成在目录：$SCRIPT_DIR"
