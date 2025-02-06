import numpy as np
import pandas as pd
from scipy import stats
import os

# -------------------------------
# 参数和常量定义
# -------------------------------

# 工作负载操作百分比数据
# v: percentage of zero-result point lookups  
# r: percentage of non-zero-result point lookups  
# q: percentage of range lookups  
# w: percentage of writes
operation_percentages = {
    'v': [25, 97,  1,  1,  1, 49, 49, 49,  1,  1,  1, 33, 33, 33,  1],
    'r': [25,  1, 97,  1,  1, 49,  1,  1, 49, 49,  1, 33, 33,  1, 33],
    'q': [25,  1,  1, 97,  1,  1, 49,  1, 49,  1, 49, 33,  1, 33, 33],
    'w': [25,  1,  1,  1, 97,  1,  1, 49,  1, 49, 49,  1, 33, 33, 33]
}

# 每个工作负载的基准操作总数，用于计算各操作的数量（百分比作用于此值）
total_operations = 10000

# 每个工作负载实际需要的操作总数（独立设置，每个 workload 的总量不同）
workload_operations = [500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500]

# 分布类型设置：可选 'uniform', 'zipf', 'normal'
distribution_type = 'zipf'
zipf_alpha = 1.5       # Zipf 分布参数
normal_mean = 5000     # 正态分布均值（仅对 normal 分布有效）
normal_std = 1000      # 正态分布标准差（仅对 normal 分布有效）

# 操作键值的生成范围
key_range_min = 1
key_range_max = 100000

# -------------------------------
# 辅助函数定义
# -------------------------------

def generate_distribution(count, distribution_type, zipf_alpha, normal_mean, normal_std):
    """
    根据指定分布类型生成一个不超过 count 的整数数据。

    参数：
      - count: 上限基数
      - distribution_type: 'uniform', 'zipf', 'normal' 中的一个
      - zipf_alpha: Zipf 分布参数
      - normal_mean: 正态分布均值
      - normal_std: 正态分布标准差

    返回：
      一个整数，表示按照指定分布生成的数值
    """
    if distribution_type == 'uniform':
        return int(np.random.uniform(0, count))
    elif distribution_type == 'zipf':
        # 使用 scipy.stats.zipf.rvs 生成一个符合 Zipf 分布的数值，
        # 取模 count 以确保不超过上限
        return int(stats.zipf.rvs(zipf_alpha, size=1)[0] % count)
    elif distribution_type == 'normal':
        return int(np.random.normal(normal_mean, normal_std))
    else:
        raise ValueError("Unsupported distribution type")

def adjust_to_workload(operation_count, workload_index):
    """
    根据当前工作负载目标操作总数调整操作数量。

    参数：
      - operation_count: 由分布生成的初始操作数
      - workload_index: 工作负载索引（0 到 14）

    返回：
      调整后的操作数（取整）
    """
    return int(operation_count * (workload_operations[workload_index] / total_operations))

# -------------------------------
# 生成并保存单个工作负载数据
# -------------------------------

def generate_and_save_workload(workload_index, distribution_type, zipf_alpha, normal_mean, normal_std):
    """
    为单个工作负载生成四种操作（v, r, q, w）的 key，并将 (operation, key) 对作为一个整体放入列表中，
    然后对整个列表进行 shuffle，最后写入 CSV 文件。CSV 文件包含两列：Operation 和 Key。

    参数：
      - workload_index: 工作负载编号（从 0 开始）
      - distribution_type: 数据分布类型
      - zipf_alpha, normal_mean, normal_std: 分布参数
    """
    file_directory="/mnt/workloads"
    # 根据百分比计算各操作的初始基数（不是真正的数量，只是用于生成随机数的上限）
    v_base = operation_percentages['v'][workload_index] / 100 * total_operations
    r_base = operation_percentages['r'][workload_index] / 100 * total_operations
    q_base = operation_percentages['q'][workload_index] / 100 * total_operations
    w_base = operation_percentages['w'][workload_index] / 100 * total_operations

    # 分别生成各操作的初步数量
    v_count = generate_distribution(v_base, distribution_type, zipf_alpha, normal_mean, normal_std)
    r_count = generate_distribution(r_base, distribution_type, zipf_alpha, normal_mean, normal_std)
    q_count = generate_distribution(q_base, distribution_type, zipf_alpha, normal_mean, normal_std)
    w_count = generate_distribution(w_base, distribution_type, zipf_alpha, normal_mean, normal_std)

    # 按照当前工作负载目标操作总数进行调整
    v_count = adjust_to_workload(v_count, workload_index)
    r_count = adjust_to_workload(r_count, workload_index)
    q_count = adjust_to_workload(q_count, workload_index)
    w_count = adjust_to_workload(w_count, workload_index)

    # 限制操作数在预设的键值范围内
    v_count = np.clip(v_count, key_range_min, key_range_max)
    r_count = np.clip(r_count, key_range_min, key_range_max)
    q_count = np.clip(q_count, key_range_min, key_range_max)
    w_count = np.clip(w_count, key_range_min, key_range_max)

    # -------------------------------
    # 为每种操作生成 (Operation, Key) 对的列表
    # -------------------------------
    workload_elements = []

    # 为操作 'v' 生成数据
    v_keys = np.random.randint(key_range_min, key_range_max, size=v_count)
    for key in v_keys:
        workload_elements.append(('v', key))
    
    # 为操作 'r' 生成数据
    r_keys = np.random.randint(key_range_min, key_range_max, size=r_count)
    for key in r_keys:
        workload_elements.append(('r', key))
    
    # 为操作 'q' 生成数据
    q_keys = np.random.randint(key_range_min, key_range_max, size=q_count)
    for key in q_keys:
        workload_elements.append(('q', key))
    
    # 为操作 'w' 生成数据
    w_keys = np.random.randint(key_range_min, key_range_max, size=w_count)
    for key in w_keys:
        workload_elements.append(('w', key))
    
    # 现在 workload_elements 中的每个元素都是 (operation, key) 的元组，
    # 我们对整个列表进行 shuffle，保证每个元组作为一个整体被随机打乱
    np.random.shuffle(workload_elements)
    
    # 转换为 DataFrame，包含两列：Operation 和 Key
    workload_df = pd.DataFrame(workload_elements, columns=['Operation', 'Key'])

    # 构造 CSV 文件名，例如：workload_1_distribution(zipf)_percentage(97).csv
    file_name = f"{file_directory}/workload_{workload_index + 1}_distribution({distribution_type}).csv"
    workload_df.to_csv(file_name, index=False)
    print(f"Data for workload {workload_index + 1} written to {file_name}")

# -------------------------------
# 生成所有工作负载
# -------------------------------

def generate_all_workloads(distribution_type, zipf_alpha, normal_mean, normal_std):
    """
    生成所有 15 个工作负载的数据，每个工作负载包含四种操作 (v, r, q, w) 的 (Operation, Key) 对，
    并分别写入 CSV 文件中。
    """
    for i in range(15):
        generate_and_save_workload(i, distribution_type, zipf_alpha, normal_mean, normal_std)

# -------------------------------
# 主程序入口：调用生成函数
# -------------------------------

if __name__ == "__main__":
    generate_all_workloads(distribution_type, zipf_alpha, normal_mean, normal_std)
