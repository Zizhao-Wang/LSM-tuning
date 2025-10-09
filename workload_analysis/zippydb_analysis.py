import pandas as pd
import numpy as np
from collections import Counter
from sklearn.linear_model import LinearRegression
import matplotlib.pyplot as plt
from scipy import stats
import os
from datetime import datetime

def analyze_zipf_parameter(csv_file, output_dir="."):
    """
    分析CSV文件中的Zipf分布参数，并输出结果到文件
    """
    # 创建输出目录
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # 创建结果文件
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    results_file = os.path.join(output_dir, f"zipf_analysis_results_{timestamp}.txt")
    detailed_file = os.path.join(output_dir, f"zipf_detailed_data_{timestamp}.csv")
    
    # 读取数据
    df = pd.read_csv(csv_file)
    
    # 开始记录结果
    with open(results_file, 'w', encoding='utf-8') as f:
        f.write("ZippyDB Zipf分布参数分析报告\n")
        f.write("="*50 + "\n")
        f.write(f"分析时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"数据文件: {csv_file}\n")
        f.write(f"总数据量: {len(df)} 条记录\n")
        f.write(f"操作类型分布:\n{df['OpType'].value_counts()}\n\n")
    
    print(f"总数据量: {len(df)} 条记录")
    print(f"操作类型分布:\n{df['OpType'].value_counts()}")
    print(f"结果将保存到: {results_file}")
    
    def calculate_zipf_parameter(keys, operation_name):
        """
        计算给定键列表的Zipf参数
        """
        analysis_text = f"\n=== {operation_name} ===\n"
        print(analysis_text.strip())
        
        # 统计每个key的频率
        key_counts = Counter(keys)
        frequencies = list(key_counts.values())
        
        # 按频率降序排序
        frequencies.sort(reverse=True)
        
        # 创建rank (从1开始)
        ranks = np.arange(1, len(frequencies) + 1)
        
        # 过滤掉频率为0的数据点(虽然不应该有)
        valid_indices = np.array(frequencies) > 0
        frequencies = np.array(frequencies)[valid_indices]
        ranks = ranks[valid_indices]
        
        basic_stats = f"唯一键数量: {len(frequencies)}\n"
        basic_stats += f"最高频率: {max(frequencies)}\n"
        basic_stats += f"最低频率: {min(frequencies)}\n"
        
        print(basic_stats.strip())
        analysis_text += basic_stats
        
        # 转换为对数坐标
        log_ranks = np.log(ranks)
        log_frequencies = np.log(frequencies)
        
        # 线性回归 log(frequency) = -alpha * log(rank) + c
        # 重新整形数据用于sklearn
        X = log_ranks.reshape(-1, 1)
        y = log_frequencies
        
        model = LinearRegression()
        model.fit(X, y)
        
        # Zipf参数是斜率的绝对值
        zipf_alpha = -model.coef_[0]  # 注意负号，因为Zipf分布斜率为负
        intercept = model.intercept_
        r_squared = model.score(X, y)
        
        # 计算Pearson相关系数
        correlation, p_value = stats.pearsonr(log_ranks, log_frequencies)
        
        regression_results = f"线性回归结果:\n"
        regression_results += f"  斜率: {model.coef_[0]:.4f}\n"
        regression_results += f"  截距: {intercept:.4f}\n"
        regression_results += f"  R²: {r_squared:.4f}\n"
        regression_results += f"  Zipf参数 (α): {zipf_alpha:.4f}\n"
        regression_results += f"  Pearson相关系数: {correlation:.4f} (p-value: {p_value:.2e})\n"
        
        print(regression_results.strip())
        analysis_text += regression_results
        
        # 写入结果文件
        with open(results_file, 'a', encoding='utf-8') as f:
            f.write(analysis_text)
        
        # 保存详细数据用于进一步分析
        detail_data = pd.DataFrame({
            'operation': [operation_name] * len(frequencies),
            'rank': ranks,
            'frequency': frequencies,
            'log_rank': log_ranks,
            'log_frequency': log_frequencies
        })
        
        return zipf_alpha, r_squared, model, log_ranks, log_frequencies, analysis_text, detail_data
    
    # 用于收集所有详细数据
    all_detail_data = []
    
    # 分析所有操作
    all_keys = df['Key'].tolist()
    alpha_all, r2_all, model_all, log_ranks_all, log_freq_all, text_all, detail_all = calculate_zipf_parameter(
        all_keys, "所有操作"
    )
    all_detail_data.append(detail_all)
    
    # 分析GET操作
    get_keys = df[df['OpType'] == 'get']['Key'].tolist()
    if get_keys:
        alpha_get, r2_get, model_get, log_ranks_get, log_freq_get, text_get, detail_get = calculate_zipf_parameter(
            get_keys, "GET操作"
        )
        all_detail_data.append(detail_get)
    else:
        print("\n没有找到GET操作数据")
        with open(results_file, 'a', encoding='utf-8') as f:
            f.write("\n没有找到GET操作数据\n")
        alpha_get, r2_get = None, None
    
    # 分析PUT操作
    put_keys = df[df['OpType'] == 'put']['Key'].tolist()
    if put_keys:
        alpha_put, r2_put, model_put, log_ranks_put, log_freq_put, text_put, detail_put = calculate_zipf_parameter(
            put_keys, "PUT操作"
        )
        all_detail_data.append(detail_put)
    else:
        print("\n没有找到PUT操作数据")
        with open(results_file, 'a', encoding='utf-8') as f:
            f.write("\n没有找到PUT操作数据\n")
        alpha_put, r2_put = None, None
    
    # 保存详细数据到CSV
    if all_detail_data:
        combined_detail_data = pd.concat(all_detail_data, ignore_index=True)
        combined_detail_data.to_csv(detailed_file, index=False)
        print(f"详细数据已保存到: {detailed_file}")
    
    # 绘制结果图表并保存
    plt.figure(figsize=(15, 5))
    
    # 绘制所有操作的图
    plt.subplot(1, 3, 1)
    plt.scatter(log_ranks_all, log_freq_all, alpha=0.6, s=20)
    plt.plot(log_ranks_all, model_all.predict(log_ranks_all.reshape(-1, 1)), 'r-', linewidth=2)
    plt.xlabel('log(Rank)')
    plt.ylabel('log(Frequency)')
    plt.title(f'所有操作\nZipf α = {alpha_all:.3f}, R² = {r2_all:.3f}')
    plt.grid(True, alpha=0.3)
    
    # 绘制GET操作的图
    if alpha_get is not None:
        plt.subplot(1, 3, 2)
        plt.scatter(log_ranks_get, log_freq_get, alpha=0.6, s=20, color='green')
        plt.plot(log_ranks_get, model_get.predict(log_ranks_get.reshape(-1, 1)), 'r-', linewidth=2)
        plt.xlabel('log(Rank)')
        plt.ylabel('log(Frequency)')
        plt.title(f'GET操作\nZipf α = {alpha_get:.3f}, R² = {r2_get:.3f}')
        plt.grid(True, alpha=0.3)
    
    # 绘制PUT操作的图
    if alpha_put is not None:
        plt.subplot(1, 3, 3)
        plt.scatter(log_ranks_put, log_freq_put, alpha=0.6, s=20, color='orange')
        plt.plot(log_ranks_put, model_put.predict(log_ranks_put.reshape(-1, 1)), 'r-', linewidth=2)
        plt.xlabel('log(Rank)')
        plt.ylabel('log(Frequency)')
        plt.title(f'PUT操作\nZipf α = {alpha_put:.3f}, R² = {r2_put:.3f}')
        plt.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    # 保存图表
    plot_file = os.path.join(output_dir, f"zipf_analysis_plots_{timestamp}.png")
    plt.savefig(plot_file, dpi=300, bbox_inches='tight')
    print(f"图表已保存到: {plot_file}")
    plt.show()
    
    # 总结并写入文件
    summary = f"\n{'='*50}\n"
    summary += "Zipf参数分析总结:\n"
    summary += f"{'='*50}\n"
    summary += f"所有操作 - Zipf参数: {alpha_all:.4f} (R²: {r2_all:.4f})\n"
    if alpha_get is not None:
        summary += f"GET操作 - Zipf参数: {alpha_get:.4f} (R²: {r2_get:.4f})\n"
    if alpha_put is not None:
        summary += f"PUT操作 - Zipf参数: {alpha_put:.4f} (R²: {r2_put:.4f})\n"
    
    summary += f"\n输出文件:\n"
    summary += f"- 分析报告: {results_file}\n"
    summary += f"- 详细数据: {detailed_file}\n"
    summary += f"- 图表文件: {plot_file}\n"
    
    print(summary)
    
    with open(results_file, 'a', encoding='utf-8') as f:
        f.write(summary)
    
    print(f"\n✅ 所有结果已保存到目录: {os.path.abspath(output_dir)}")
    
    return {
        'all': {'alpha': alpha_all, 'r2': r2_all},
        'get': {'alpha': alpha_get, 'r2': r2_get} if alpha_get is not None else None,
        'put': {'alpha': alpha_put, 'r2': r2_put} if alpha_put is not None else None,
        'files': {
            'results': results_file,
            'details': detailed_file,
            'plot': plot_file
        }
    }

# 使用示例
if __name__ == "__main__":
    # 替换为你的CSV文件路径
    csv_file = "/mnt/workloads/zippyDB_key_file.csv"
    
    try:
        results = analyze_zipf_parameter(csv_file)
        print("\n✅ 分析完成！所有结果文件已生成。")
    except FileNotFoundError:
        print(f"❌ 错误: 找不到文件 {csv_file}")
        print("请确保文件路径正确")
    except Exception as e:
        print(f"❌ 分析过程中出现错误: {e}")
        import traceback
        traceback.print_exc()