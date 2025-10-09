import pandas as pd
import matplotlib.pyplot as plt
import os # 导入os模块来处理文件路径和目录

def analyze_and_plot_combined_ratios(filepath, chunk_sizes, cluster_num, n_rows=None, output_dir='.'):
    """
    分析Put/Get比率，并将不同批次大小的结果合并在一张图中。

    Args:
        filepath (str): 输入文件的路径。
        chunk_sizes (list): 代表批次大小的整数列表。
        cluster_num (int): 当前正在分析的集群编号，用于生成唯一的文件名。
        n_rows (int, optional): 要分析的文件前n行。
        output_dir (str, optional): 保存输出文件的目录。
    """
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"输出目录 '{output_dir}' 不存在，已自动创建。")

    try:
        print(f"正在读取文件 '{filepath}'（已优化，仅加载第2和第6列）...")
        df = pd.read_csv(
            filepath,
            header=None,
            usecols=[1, 5],
            names=['key', 'operation']
        )
    except FileNotFoundError:
        print(f"错误：在当前环境中找不到文件 '{filepath}'。跳过此集群。")
        return

    if n_rows and isinstance(n_rows, int) and n_rows > 0:
        print(f"正在分析文件的前 {n_rows} 行。")
        df = df.head(n_rows)
    else:
        print("正在分析整个文件。")
        n_rows = len(df)

    # --- 创建一张图，为所有批次大小做准备 ---
    plt.style.use('seaborn-v0_8-whitegrid')
    plt.figure(figsize=(15, 8)) # 创建一个更大、更清晰的画布

    # 循环遍历每个批次大小，计算结果并直接绘图
    for size in chunk_sizes:
        print(f"正在处理批次大小: {size}...")
        results = []
        for i in range(0, len(df), size):
            chunk = df.iloc[i:i + size]
            operation_counts = chunk['operation'].value_counts()
            gets = operation_counts.get('get', 0)
            puts = operation_counts.get('put', 0)
            
            # --- 计算 Put / Get 比率 ---
            # 如果 gets 为 0，则比率为 NaN (Not a Number)，在图上不会显示
            put_get_ratio = puts / gets if gets > 0 else np.nan
            
            results.append({
                # X轴将使用 'processed_rows'
                'processed_rows': i + len(chunk),
                'put_get_ratio': put_get_ratio
            })
        
        results_df = pd.DataFrame(results)

        # --- 在同一张图上绘制当前批次大小的线 ---
        plt.plot(
            results_df['processed_rows'], 
            results_df['put_get_ratio'], 
            label=f'批次大小: {size}', 
            linewidth=1.0 # 设置更细的线宽
        )

    # --- 统一配置并保存最终的图表 ---
    title_text = f'集群 {cluster_num} 中，Put/Get 比率随处理进度的变化'
    if n_rows:
        title_text = f'集群 {cluster_num} 的前 {n_rows} 行数据中，' + 'Put/Get 比率随处理进度的变化'

    plt.title(title_text, fontsize=16)
    plt.xlabel('已处理行数', fontsize=12)
    plt.ylabel('Put / Get 比率', fontsize=12)
    plt.legend()
    plt.grid(True)
    # y轴使用对数坐标可能有助于观察，如果比率变化范围很大
    # plt.yscale('log') # 如果需要，可以取消本行注释
    plt.tight_layout()

    # --- 核心修改：在输出文件名中加入集群编号 ---
    plot_filename = f'cluster_{cluster_num}_combined_plot_top_{n_rows}.png'
    output_plot_filepath = os.path.join(output_dir, plot_filename)
    plt.savefig(output_plot_filepath)
    plt.close()
    print(f"\n分析完成！集群 {cluster_num} 的图表已保存至 '{output_plot_filepath}'")

# ===================================================================
# --- 请在这里配置 ---
# ===================================================================

# --- 主循环配置 ---
# 定义一个列表或范围，包含所有需要分析的集群编号
cluster_numbers_to_analyze = [1, 13, 30, 40,49,25,51] 

# --- 其他配置 ---
chunks_to_analyze = [100, 1000, 10000]
num_rows_to_analyze = 500000000 
# 指定一个目录来保存所有输出文件
save_directory = '/home/jeff-wang/LSM-tuning/workload_generation/traces_ops_visualization' 

# ===================================================================
# --- 循环调用分析函数 ---
# ===================================================================
for cluster_num in cluster_numbers_to_analyze:
    print(f"\n{'='*25} 开始分析集群 {cluster_num} {'='*25}")
    
    # 动态生成当前循环需要分析的文件名
    file_to_analyze = f'/mnt/nvm/second_cluster{cluster_num}.sort'
    
    # 调用函数时传入当前集群编号
    analyze_and_plot_combined_ratios(
        file_to_analyze,
        chunks_to_analyze,
        cluster_num=cluster_num, # 新增参数，用于区分输出
        n_rows=num_rows_to_analyze, 
        output_dir=save_directory
    )
    
    print(f"{'='*25} 集群 {cluster_num} 分析完成 {'='*25}\n")