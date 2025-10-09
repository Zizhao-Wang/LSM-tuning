import pandas as pd
import numpy as np
import json
from collections import Counter
from scipy import optimize
import warnings
warnings.filterwarnings('ignore')

def zipf_function(rank, s, a):
    """
    Zipf分布函数: f(rank) = a / (rank^s)
    参数:
        rank: 排名
        s: Zipf指数 (通常在0.5-2之间)
        a: 缩放因子
    """
    return a / (rank ** s)

def fit_zipf_parameters(frequencies):
    """
    拟合Zipf分布参数
    参数:
        frequencies: 频次列表 (已排序，从高到低)
    返回:
        dict: 包含拟合参数和拟合质量的字典
    """
    # 创建排名数组 (从1开始)
    ranks = np.arange(1, len(frequencies) + 1)
    
    # 过滤掉频次为0的数据点
    valid_mask = frequencies > 0
    valid_ranks = ranks[valid_mask]
    valid_frequencies = frequencies[valid_mask]
    
    if len(valid_frequencies) < 10:  # 数据点太少，无法可靠拟合
        return {
            'zipf_exponent': None,
            'scaling_factor': None,
            'r_squared': None,
            'fit_quality': 'insufficient_data',
            'error_message': 'Too few data points for reliable fitting'
        }
    
    try:
        # 定义拟合函数
        def objective(params):
            s, a = params
            predicted = zipf_function(valid_ranks, s, a)
            return np.sum((valid_frequencies - predicted) ** 2)
        
        # 初始猜测参数
        initial_s = 1.0  # 典型的Zipf指数
        initial_a = valid_frequencies[0]  # 用最高频次作为缩放因子初值
        
        # 参数边界
        bounds = [(0.1, 3.0), (1, max(valid_frequencies) * 2)]
        
        # 执行优化拟合
        result = optimize.minimize(objective, [initial_s, initial_a], 
                                 method='L-BFGS-B', bounds=bounds)
        
        if result.success:
            s_fitted, a_fitted = result.x
            
            # 计算R²
            predicted_frequencies = zipf_function(valid_ranks, s_fitted, a_fitted)
            ss_res = np.sum((valid_frequencies - predicted_frequencies) ** 2)
            ss_tot = np.sum((valid_frequencies - np.mean(valid_frequencies)) ** 2)
            r_squared = 1 - (ss_res / ss_tot) if ss_tot > 0 else 0
            
            # 评估拟合质量
            if r_squared > 0.8:
                fit_quality = 'excellent'
            elif r_squared > 0.6:
                fit_quality = 'good'
            elif r_squared > 0.4:
                fit_quality = 'moderate'
            else:
                fit_quality = 'poor'
            
            return {
                'zipf_exponent': round(s_fitted, 4),
                'scaling_factor': round(a_fitted, 4),
                'r_squared': round(r_squared, 4),
                'fit_quality': fit_quality,
                'valid_data_points': len(valid_frequencies),
                'total_data_points': len(frequencies)
            }
        else:
            return {
                'zipf_exponent': None,
                'scaling_factor': None,
                'r_squared': None,
                'fit_quality': 'fit_failed',
                'error_message': f'Optimization failed: {result.message}'
            }
            
    except Exception as e:
        return {
            'zipf_exponent': None,
            'scaling_factor': None,
            'r_squared': None,
            'fit_quality': 'error',
            'error_message': str(e)
        }

def analyze_key_frequency(filename, n_rows=None):
    """
    分析文件中key的频次分布
    """
    try:
        print(f"开始读取文件: {filename}")
        
        # 读取CSV文件
        if n_rows:
            df = pd.read_csv(filename, nrows=n_rows)
        else:
            df = pd.read_csv(filename)
        
        if df.empty:
            print("文件为空")
            return None
            
        # 假设key列名为'key'，如果不是请修改
        if 'key' not in df.columns:
            # 尝试常见的列名
            possible_key_columns = ['Key', 'KEY', 'k', 'K']
            key_column = None
            for col in possible_key_columns:
                if col in df.columns:
                    key_column = col
                    break
            
            if key_column is None:
                print(f"未找到key列，可用列: {list(df.columns)}")
                return None
        else:
            key_column = 'key'
        
        print(f"使用列: {key_column}")
        
        # 统计每个key的频次
        key_counts = Counter(df[key_column])
        
        # 获取基本统计信息
        frequencies = list(key_counts.values())
        
        # 按频次排序 (从高到低)
        sorted_frequencies = sorted(frequencies, reverse=True)
        
        # 进行Zipf参数拟合
        print("开始Zipf参数拟合...")
        zipf_results = fit_zipf_parameters(np.array(sorted_frequencies))
        
        return {
            'total_rows': len(df),
            'unique_keys': len(key_counts),
            'mean_frequency': np.mean(frequencies),
            'variance_frequency': np.var(frequencies),
            'std_frequency': np.std(frequencies),
            'max_frequency': max(frequencies),
            'min_frequency': min(frequencies),
            'zipf_analysis': zipf_results
        }
        
    except FileNotFoundError:
        print(f"文件不存在: {filename}")
        return None
    except Exception as e:
        print(f"处理文件时出错: {e}")
        return None

if __name__ == "__main__":
    # 配置参数
    base_filename = "/mnt/workloads/kvcache_202206/kvcache_traces_{}.csv"  # 文件名模板，{}会被数字替换
    n_rows = 500000000  # 要分析的行数
    output_file = "/home/jeff-wang/LSM-tuning/workload_generation/data_distribution_visualization/key_frequency_results.json"  # 输出结果文件名
    
    # 文件编号列表，根据你的实际文件修改
    file_numbers = [1,2,3,4,5]  # 可以添加或删除需要的文件编号
    
    # 存储所有结果
    all_results = {}
    
    for i in file_numbers:
        filename = base_filename.format(i)
        print(f"正在分析文件: {filename}")
        
        result = analyze_key_frequency(filename, n_rows)
        
        if result:
            # 存储结果到字典中
            all_results[f"cluster_{i}"] = {
                "filename": filename,
                "total_rows": result['total_rows'],
                "unique_keys": result['unique_keys'],
                "mean_frequency": round(result['mean_frequency'], 4),
                "variance_frequency": round(result['variance_frequency'], 4),
                "std_frequency": round(result['std_frequency'], 4),
                "max_frequency": result['max_frequency'],
                "min_frequency": result['min_frequency'],
                "zipf_exponent": result['zipf_analysis']['zipf_exponent'],
                "zipf_scaling_factor": result['zipf_analysis']['scaling_factor'],
                "zipf_r_squared": result['zipf_analysis']['r_squared'],
                "zipf_fit_quality": result['zipf_analysis']['fit_quality'],
                "zipf_details": result['zipf_analysis'],
                "status": "success"
            }
            
            # 打印结果
            print(f"✓ 完成分析:")
            print(f"  平均频次={result['mean_frequency']:.4f}, 方差={result['variance_frequency']:.4f}")
            zipf_info = result['zipf_analysis']
            if zipf_info['zipf_exponent'] is not None:
                print(f"  Zipf指数={zipf_info['zipf_exponent']}, R²={zipf_info['r_squared']}, 拟合质量={zipf_info['fit_quality']}")
            else:
                print(f"  Zipf拟合失败: {zipf_info.get('error_message', 'Unknown error')}")
                
        else:
            # 如果文件不存在或出错，记录错误信息
            all_results[f"cluster_{i}"] = {
                "filename": filename,
                "status": "error",
                "error_message": "文件不存在或读取失败"
            }
            print(f"✗ 分析失败: {filename}")
        
        print("-" * 50)
    
    # 保存为JSON文件
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(all_results, f, indent=2, ensure_ascii=False)
    
    # 同时输出格式化的摘要
    print(f"\n=== 分析摘要 ===")
    print(f"{'Cluster':<12} {'平均频次':<10} {'方差':<12} {'Zipf指数':<10} {'R²':<8} {'拟合质量':<12}")
    print("-" * 80)
    
    for cluster_name, data in all_results.items():
        if data['status'] == 'success':
            zipf_exp = data.get('zipf_exponent', 'N/A')
            r_squared = data.get('zipf_r_squared', 'N/A')
            fit_quality = data.get('zipf_fit_quality', 'N/A')
            
            print(f"{cluster_name:<12} {data['mean_frequency']:<10.4f} "
                  f"{data['variance_frequency']:<12.4f} {zipf_exp:<10} "
                  f"{r_squared:<8} {fit_quality:<12}")
        else:
            print(f"{cluster_name:<12} {'分析失败':<40}")
    
    print(f"\n所有结果已保存到: {output_file}")
    print("结果包含Zipf分布拟合参数，便于进一步的分布特性分析")
    
    # 输出Zipf拟合的解释
    print(f"\n=== Zipf参数解释 ===")
    print("• zipf_exponent (s): Zipf指数，典型值在0.5-2之间")
    print("  - 接近1: 经典Zipf分布")
    print("  - <1: 分布相对均匀")
    print("  - >1: 高度倾斜，少数key占主导")
    print("• scaling_factor (a): 缩放因子，影响频次的绝对大小")
    print("• r_squared: 拟合优度，越接近1拟合越好")
    print("• fit_quality: 拟合质量评估 (excellent/good/moderate/poor)")