#!/usr/bin/env python3
import pandas as pd
import numpy as np
from pathlib import Path

# ============ 配置参数 ============
INPUT_FILE = "/mnt/workloads/zipf1.1_keys10.0B.csv"      # 输入文件名
INITIAL_SAMPLE_COUNT = 1000000000             # 从原始文件中选择的行数
FINAL_SAMPLE_COUNT = 50000000                # 去重后要随机选择的数据个数
RANDOM_SEED = 52                           # 随机种子
# ================================

def generate_random_read_file():
    # 设置随机种子
    np.random.seed(RANDOM_SEED)
    
    print(f"正在读取文件: {INPUT_FILE}")
    
    # 读取CSV文件
    df = pd.read_csv(INPUT_FILE)
    print(f"原始数据总行数: {len(df)}")
    
    # 确保列名为Key
    if 'Key' not in df.columns:
        df.columns = ['Key']
    
    # 第一步：从原始数据中随机选择指定行数
    if len(df) < INITIAL_SAMPLE_COUNT:
        print(f"警告: 原始数据只有 {len(df)} 行，少于要求的 {INITIAL_SAMPLE_COUNT} 行")
        print(f"将使用所有 {len(df)} 行数据")
        df_initial = df
    else:
        print(f"正在从原始数据中随机选择 {INITIAL_SAMPLE_COUNT} 行...")
        df_initial = df.sample(n=INITIAL_SAMPLE_COUNT, random_state=RANDOM_SEED)
    
    print(f"第一步选择后数据行数: {len(df_initial)}")
    
    # 第二步：去重
    print("正在去重...")
    df_unique = df_initial.drop_duplicates(subset=['Key'])
    print(f"去重后数据行数: {len(df_unique)}")
    print(f"重复数据: {len(df_initial) - len(df_unique)} 行")
    
    # 第三步：从去重后的数据中再次随机选择
    if len(df_unique) < FINAL_SAMPLE_COUNT:
        print(f"警告: 去重后只有 {len(df_unique)} 行数据，少于要求的 {FINAL_SAMPLE_COUNT} 行")
        print(f"将使用所有 {len(df_unique)} 行数据")
        df_final = df_unique
    else:
        print(f"正在从去重数据中随机选择 {FINAL_SAMPLE_COUNT} 行...")
        df_final = df_unique.sample(n=FINAL_SAMPLE_COUNT, random_state=RANDOM_SEED)
    
    # 生成输出文件路径（同一目录）
    input_path = Path(INPUT_FILE)
    output_file = input_path.parent / f"{input_path.stem}_random_read{input_path.suffix}"
    
    # 保存到新文件
    print(f"正在保存到: {output_file}")
    df_final.to_csv(output_file, index=False)
    
    print("处理完成!")
    print(f"原始数据: {len(df)} 行")
    print(f"第一步选择: {len(df_initial)} 行")
    print(f"去重后: {len(df_unique)} 行") 
    print(f"最终输出: {len(df_final)} 行")

if __name__ == "__main__":
    generate_random_read_file()