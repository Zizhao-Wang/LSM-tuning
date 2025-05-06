#!/bin/bash

# 源目录（在机器 172.20.111.180 上）
SOURCE_DIR="/mnt/nvm"

# 目标目录（在机器 172.20.111.239 上）
TARGET_DIR="/mnt/hotdb_test/datain990"

# 机器 172.20.111.239 的 IP 和用户名（假设用户名是 root）
REMOTE_IP="172.20.111.239"
REMOTE_USER="root"

# 定义要迁移的文件列表
files=(
    "cluster12.sort.zst"
    "cluster14.sort.zst"
    "cluster1.sort.zst"
    "cluster22.sort.zst"
    "cluster37_modified.sort"
    "cluster37.sort"
    "cluster37.sort.zst"
    "cluster39.sort.zst"
    "cluster40.sort.zst"
    "cluster40_top10_keys10.0B.csv"
    "cluster40_top1_keys10.0B.csv"
    "cluster45.sort"
    "cluster45.sort.zst"
    "cluster48.sort.zst"
    "cluster49.sort"
    "cluster49.sort.zst"
    "cluster49_top10_keys10.0B.csv"
    "cluster49_top1_keys10.0B.csv"
    "cluster8.sort.zst"
    "second_cluster37.sort"
    "second_cluster39.sort"
    "second_cluster40.sort"
    "second_cluster45.sort"
    "second_cluster49.sort"
)

# 逐个处理每个文件
for file in "${files[@]}"; do
    SOURCE_FILE="$SOURCE_DIR/$file"
    TARGET_FILE="$REMOTE_USER@$REMOTE_IP:$TARGET_DIR/$file"

    # 检查文件是否存在
    if [ -f "$SOURCE_FILE" ]; then
        echo "开始传输文件 $file..."

        # 使用 rsync 传输文件，已经配置了 SSH 密钥认证，无需输入密码
        rsync -avz --remove-source-files --progress "$SOURCE_FILE" "$TARGET_FILE"

        # 检查 rsync 是否成功
        if [ $? -eq 0 ]; then
            echo "文件 $file 已成功传输并删除。"
        else
            echo "文件 $file 传输失败，请检查网络或权限设置。"
        fi
    else
        echo "文件 $file 在源目录中不存在，跳过此文件。"
    fi
done

echo "所有文件传输完成。"
