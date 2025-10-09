#!/usr/bin/env python3
import pandas as pd
import numpy as np
from pathlib import Path

# ============ 配置参数 ============
CLUSTER_NUMBERS = [1, 13, 37, 35, 25, 30, 35, 40, 45, 51]  # 要处理的cluster数字数组
INPUT_PATH_TEMPLATE = "/mnt/nvm/second_cluster{}.sort"         # 输入文件路径模板
N = 500000000                              # 总行数 (1亿)
K = 10000000                               # 每次增加的行数 (1千万)
OUTPUT_FILE = "/home/jeff-wang/LSM-tuning/workload_generation/deduplication_results.txt"          # 输出结果文件
# ================================

def analyze_single_dataset(input_file, dataset_name):
    """分析单个数据集的去重情况"""
    print(f"正在读取文件: {input_file}")
    
    # 检查文件是否存在
    if not Path(input_file).exists():
        print(f"警告: 文件不存在 - {input_file}")
        return False
    
    # 读取文件，只关注第二列作为Key
    try:
        # 读取文件，指定第二列(索引1)为Key列
        df = pd.read_csv(input_file, sep='\s+', header=None, usecols=[1], names=['Key'])
    except:
        try:
            # 如果空格分隔失败，尝试其他分隔符
            df = pd.read_csv(input_file, header=None, usecols=[1], names=['Key'])
        except:
            print(f"错误: 无法读取文件 {input_file}")
            return False
    
    print(f"文件总行数: {len(df)}")
    
    # 检查数据是否足够
    if len(df) < N:
        print(f"警告: 文件只有 {len(df)} 行，少于要求的 {N} 行")
        print(f"将分析前 {len(df)} 行数据")
        N_actual = len(df)
    else:
        N_actual = N
    
    print(f"分析参数: N={N_actual:,}, K={K:,}")
    print(f"将输出 {N_actual//K} 次结果")
    print("=" * 50)
    
    # 用于存储已见过的key和它们的出现次数
    key_counts = {}
    
    # 准备输出内容
    output_lines = []
    output_lines.append(f"{dataset_name}:")
    output_lines.append(f"{'行数':>12} | {'累计去重数':>12} | {'新增去重数':>12} | {'累计单次key':>12} | {'新增单次key':>12}")
    output_lines.append("-" * 75)
    
    # 控制台输出表头
    print(f"{'行数':>12} | {'累计去重数':>12} | {'新增去重数':>12} | {'累计单次key':>12} | {'新增单次key':>12}")
    print("-" * 75)
    
    # 逐步分析每K行的去重情况
    for i in range(1, N_actual//K + 1):
        end_row = i * K
        
        # 获取前end_row行的所有数据
        current_data = df.iloc[0:end_row]['Key']
        
        # 统计前end_row行中每个key的出现次数
        key_counts_current = current_data.value_counts().to_dict()
        
        # 计算前一批次的统计数据（用于计算新增量）
        if i == 1:
            prev_unique_count = 0
            prev_single_count = 0
        else:
            prev_row = (i-1) * K
            prev_data = df.iloc[0:prev_row]['Key']
            prev_key_counts = prev_data.value_counts().to_dict()
            prev_unique_count = len(prev_key_counts)
            prev_single_count = sum(1 for count in prev_key_counts.values() if count == 1)
        
        # 计算当前统计数据
        current_unique_count = len(key_counts_current)
        current_single_count = sum(1 for count in key_counts_current.values() if count == 1)
        
        # 计算新增数据
        new_unique_count = current_unique_count - prev_unique_count
        new_single_count = current_single_count - prev_single_count
        
        # 格式化输出行
        result_line = f"{end_row:>12,} | {current_unique_count:>12,} | {new_unique_count:>12,} | {current_single_count:>12,} | {new_single_count:>12,}"
        
        # 控制台输出
        print(result_line)
        
        # 添加到输出内容
        output_lines.append(result_line)
    
    # 计算最终统计数据 - 基于前N_actual行的完整数据
    final_data = df.iloc[0:N_actual]['Key']
    final_key_counts = final_data.value_counts().to_dict()
    final_unique_count = len(final_key_counts)
    final_single_count = sum(1 for count in final_key_counts.values() if count == 1)
    dedup_ratio = final_unique_count / N_actual * 100
    single_ratio = final_single_count / final_unique_count * 100
    
    # 添加总结信息
    summary_lines = [
        "=" * 75,
        f"最终去重数量: {final_unique_count:,}",
        f"只出现一次的key数量: {final_single_count:,}",
        f"去重比例: {dedup_ratio:.2f}%",
        f"单次key占去重数据比例: {single_ratio:.2f}%",
        ""  # 空行分隔不同数据集
    ]
    
    output_lines.extend(summary_lines)
    
    # 输出到控制台
    print("=" * 75)
    print(f"分析完成! 前 {N_actual:,} 行数据的最终去重数量: {final_unique_count:,}")
    print(f"只出现一次的key数量: {final_single_count:,}")
    print(f"去重比例: {dedup_ratio:.2f}%")
    print(f"单次key占去重数据比例: {single_ratio:.2f}%")
    print()
    
    # 追加写入到文件
    with open(OUTPUT_FILE, 'a', encoding='utf-8') as f:
        for line in output_lines:
            f.write(line + '\n')
    
    print(f"结果已追加保存到: {OUTPUT_FILE}")
    return True

def analyze_deduplication():
    """批量分析多个数据集"""
    # 清空输出文件
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        f.write("批量去重分析结果\n")
        f.write("=" * 60 + "\n\n")
    
    print(f"开始批量分析 {len(CLUSTER_NUMBERS)} 个数据集...")
    print(f"结果将保存到: {OUTPUT_FILE}")
    print("=" * 60)
    
    success_count = 0
    
    # 循环处理每个cluster数据集
    for cluster_num in CLUSTER_NUMBERS:
        input_file = INPUT_PATH_TEMPLATE.format(cluster_num)
        dataset_name = f"cluster{cluster_num}"
        
        print(f"\n处理数据集 {dataset_name} ({success_count + 1}/{len(CLUSTER_NUMBERS)})...")
        
        if analyze_single_dataset(input_file, dataset_name):
            success_count += 1
        
        print("-" * 60)
    
    print(f"\n批量分析完成! 成功处理 {success_count}/{len(CLUSTER_NUMBERS)} 个数据集")
    print(f"所有结果已保存到: {OUTPUT_FILE}")

if __name__ == "__main__":
    analyze_deduplication()