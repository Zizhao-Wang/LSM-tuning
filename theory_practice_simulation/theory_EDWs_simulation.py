import numpy as np
import math
from datetime import datetime

# ========== 数据集配置区域 ==========
# 每个数据集的参数 (dataset_name, D, f, Smem, CT0, C1, R0_alpha, R1_alpha, Skv, enable_term2)
datasets = [
    # 数据集名称, D, f, Smem, CT0, C1, R0_alpha, R1_alpha, Skv, 是否启用第二项
    ("Cluster_1", 1.5, 10.0, 1.27, 4, 100, 0.62, 1.0, 347, False),
    ("Cluster_35", 104, 10.0, 24, 4, 100, 1.0, 1.0, 1810, True),
    ("Cluster_51", 6.9, 10.0, 8, 4, 100, 0.73, 0.71, 221, True),  # 第二项为0
    ("Cluster_13", 1035, 10.0, 110, 4, 512, 0.996, 0.999, 4710, True),
    ("Cluster_40", 10, 10.0, 1.5, 4, 100, 0.7, 0.9, 199, False),
]

# ========== 全局设置 ==========
# 对数项分母的乘数 (1000 或 1048576)
log_denominator_multiplier = 1048576  # 可以改为1000

# 输出文件设置
output_file = f"EDWs_calculation_results_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"

# ========== 函数定义 ==========
def calculate_EDWs(C1, D, R0_alpha, CT0, Smem, R1_alpha, f, Skv, enable_term2=True):
    """
    计算EDWs值根据公式(5)
    
    EDWs = (C1 * D) / (2 * R0_alpha * CT0 * Smem) + (f * R0_alpha * D) / (2 * R1_alpha) * log_f((R0_alpha * D * Skv) / (C1 * multiplier))
    
    参数:
    C1, D, R0_alpha, CT0, Smem, R1_alpha, f, Skv, enable_term2
    
    返回:
    EDWs, term1, term2, WAF, EDWs_bytes
    """
    
    # 参数检查
    if R0_alpha <= 0 or R1_alpha <= 0:
        raise ValueError("R0^α 和 R1^α 必须大于0")
    if CT0 <= 0 or Smem <= 0:
        raise ValueError("CT0 和 Smem 必须大于0")
    if f <= 0 or f == 1:
        raise ValueError("f 必须大于0且不等于1")
    if C1 <= 0:
        raise ValueError("C1 必须大于0")
    
    # 计算第一项: (C1 * D) / (2 * R0_alpha * CT0 * Smem)
    term1 = (C1 * D) / (2 * R0_alpha * CT0 * Smem)
    
    # 计算第二项
    if enable_term2:
        log_arg = (R0_alpha * D * Skv) / (C1 * log_denominator_multiplier)
        if log_arg <= 0:
            raise ValueError(f"对数项的参数 (R0^α * D * Skv) / (C1 * {log_denominator_multiplier}) 必须大于0")
        
        # log_f(x) = ln(x) / ln(f)
        log_f_value = math.log(log_arg) / math.log(f)
        term2 = (f * R0_alpha * D) / (2 * R1_alpha) * log_f_value
    else:
        term2 = 0.0
    
    # 总和
    EDWs = term1 + term2
    
    # 计算数据总量（字节）
    EDWs_bytes = EDWs * Skv
    
    # 计算正确的WAF
    WAF = calculate_waf_correct(EDWs, D)
    
    return EDWs, term1, term2, WAF, EDWs_bytes

def convert_gb_to_data_count(D_GB, Skv):
    """
    将GB转换为数据个数
    
    参数:
    D_GB: 数据量(GB)
    Skv: 每个数据项的大小(bytes)
    
    返回:
    数据个数
    """
    bytes_total = D_GB * 1000 * 1000 * 1000  # GB转换为bytes
    data_count = bytes_total / Skv
    return int(data_count)

def format_bytes_to_readable(bytes_value):
    """
    将字节数转换为可读的单位
    
    参数:
    bytes_value: 字节数
    
    返回:
    格式化的字符串
    """
    # 使用二进制单位 (1024)
    if bytes_value >= 1024**3:  # >= 1 GiB
        return f"{bytes_value / (1024**3):.3f} GiB"
    elif bytes_value >= 1024**2:  # >= 1 MiB
        return f"{bytes_value / (1024**2):.3f} MiB"
    elif bytes_value >= 1024:  # >= 1 KiB
        return f"{bytes_value / 1024:.3f} KiB"
    else:
        return f"{bytes_value:.0f} bytes"

def calculate_waf_correct(EDWs, original_data_count):
    """
    计算正确的WAF: (EDWs + 原始数据量) / 原始数据量
    
    参数:
    EDWs: 重写的数据量
    original_data_count: 原始数据个数
    
    返回:
    WAF值
    """
    return (EDWs + original_data_count) / original_data_count

def write_to_file(content):
    """
    将内容写入文件
    """
    with open(output_file, 'a', encoding='utf-8') as f:
        f.write(content)

def print_and_log(content):
    """
    同时打印到控制台和写入文件
    """
    print(content)
    write_to_file(content + '\n')

def print_single_dataset(dataset_name, D_GB, f, Smem, CT0, C1, R0_alpha, R1_alpha, Skv, enable_term2=True):
    """
    打印单个数据集的详细计算过程
    
    参数顺序: dataset_name, D_GB, f, Smem, CT0, C1, R0_alpha, R1_alpha, Skv, enable_term2
    """
    # 将GB转换为数据个数
    D = convert_gb_to_data_count(D_GB, Skv)
    
    print_and_log(f"\n{'='*80}")
    print_and_log(f"数据集: {dataset_name}")
    print_and_log(f"{'='*80}")
    print_and_log("输入参数:")
    print_and_log(f"  D(GB)  = {D_GB}")
    print_and_log(f"  D(个数) = {D:,}")
    print_and_log(f"  f      = {f}")
    print_and_log(f"  Smem   = {Smem}")
    print_and_log(f"  CT0    = {CT0}")
    print_and_log(f"  C1     = {C1}")
    print_and_log(f"  R0^α   = {R0_alpha}")
    print_and_log(f"  R1^α   = {R1_alpha}")
    print_and_log(f"  Skv    = {Skv}")
    print_and_log(f"  第二项 = {'启用' if enable_term2 else '禁用'}")
    print_and_log("")
    
    try:
        EDWs, term1, term2, WAF, EDWs_bytes = calculate_EDWs(C1, D, R0_alpha, CT0, Smem, R1_alpha, f, Skv, enable_term2)
        
        # 格式化数据总量
        EDWs_readable = format_bytes_to_readable(EDWs_bytes)
        original_bytes = D * Skv
        original_readable = format_bytes_to_readable(original_bytes)
        
        print_and_log("计算结果:")
        print_and_log(f"  第一项 = {term1:,.6f}")
        print_and_log(f"  第二项 = {term2:,.6f}")
        print_and_log(f"  EDWs   = {EDWs:,.6f}")
        print_and_log(f"  EDWs数据量 = {EDWs_readable}")
        print_and_log(f"  原始数据量 = {original_readable}")
        print_and_log(f"  WAF    = {WAF:.6f}")
        print_and_log("")
        
        # 显示计算过程
        print_and_log("计算过程:")
        print_and_log(f"  数据转换: {D_GB}GB = {D_GB}×1000³÷{Skv} = {D:,}个数据")
        print_and_log(f"  第一项: ({C1}×{D:,})/(2×{R0_alpha}×{CT0}×{Smem}) = {term1:,.6f}")
        
        if enable_term2:
            log_arg = (R0_alpha * D * Skv) / (C1 * log_denominator_multiplier)
            log_f_value = math.log(log_arg) / math.log(f)
            coefficient = (f * R0_alpha * D) / (2 * R1_alpha)
            
            print_and_log(f"  第二项: ({f}×{R0_alpha}×{D:,})/(2×{R1_alpha}) × log_{f}(({R0_alpha}×{D:,}×{Skv})/({C1}×{log_denominator_multiplier}))")
            print_and_log(f"         = {coefficient:,.6f} × log_{f}({log_arg:.6f})")
            print_and_log(f"         = {coefficient:,.6f} × {log_f_value:.6f}")
            print_and_log(f"         = {term2:,.6f}")
        else:
            print_and_log(f"  第二项: 禁用 = 0.0")
        
        print_and_log("")
        print_and_log(f"  最终结果: EDWs = {term1:,.6f} + {term2:,.6f} = {EDWs:,.6f}")
        print_and_log(f"  数据总量: EDWs × Skv = {EDWs:,.0f} × {Skv} = {EDWs_readable}")
        print_and_log(f"  WAF = (EDWs + 原始数据) / 原始数据 = ({EDWs:,.0f} + {D:,}) / {D:,} = {WAF:.6f}")
        
        return EDWs, WAF, term1, term2, D, EDWs_bytes, original_bytes
        
    except ValueError as e:
        error_msg = f"计算错误: {e}"
        print_and_log(error_msg)
        return None, None, None, None, None, None, None

def batch_calculate():
    """
    批量计算所有数据集
    """
    # 清空输出文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(f"EDWs批量计算结果\n")
        f.write(f"计算时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"对数项分母乘数: {log_denominator_multiplier}\n")
        f.write("=" * 100 + "\n\n")
    
    print_and_log("EDWs 批量计算器")
    print_and_log(f"对数项分母乘数: {log_denominator_multiplier}")
    print_and_log("=" * 80)
    
    results = []
    
    # 批量计算
    for dataset_params in datasets:
        dataset_name = dataset_params[0]
        params = dataset_params[1:]  # 除了名称外的所有参数
        
        result = print_single_dataset(dataset_name, *params)
        EDWs, WAF, term1, term2, D, EDWs_bytes, original_bytes = result
        
        if EDWs is not None:
            results.append({
                'dataset': dataset_name,
                'EDWs': EDWs,
                'WAF': WAF,
                'term1': term1,
                'term2': term2,
                'D': D,
                'EDWs_bytes': EDWs_bytes,
                'original_bytes': original_bytes,
                'params': params
            })
    
    # 汇总表格
    print_and_log("\n" + "=" * 140)
    print_and_log("批量计算汇总表")
    print_and_log("=" * 140)
    print_and_log(f"{'数据集':<15} {'D(GB)':<10} {'D(个数)':<15} {'EDWs':<18} {'EDWs数据量':<15} {'WAF':<12} {'第一项':<18} {'第二项':<18} {'状态':<8}")
    print_and_log("-" * 140)
    
    for result in results:
        dataset = result['dataset']
        EDWs = result['EDWs']
        WAF = result['WAF']
        term1 = result['term1']
        term2 = result['term2']
        D = result['D']
        EDWs_bytes = result['EDWs_bytes']
        D_GB = result['params'][0]  # 第一个参数是D_GB
        enable_term2 = result['params'][-1]  # 最后一个参数是enable_term2
        
        status = "启用" if enable_term2 else "禁用"
        EDWs_readable = format_bytes_to_readable(EDWs_bytes)
        
        print_and_log(f"{dataset:<15} {D_GB:<10.1f} {D:<15,} {EDWs:<18,.0f} {EDWs_readable:<15} {WAF:<12.6f} {term1:<18,.0f} {term2:<18,.0f} {status:<8}")
    
    return results

def compare_with_without_term2():
    """
    比较启用和禁用第二项的结果
    """
    print_and_log("\n" + "=" * 130)
    print_and_log("第二项影响分析")
    print_and_log("=" * 130)
    print_and_log(f"{'数据集':<15} {'D(GB)':<10} {'完整EDWs':<18} {'仅第一项':<18} {'第二项贡献':<18} {'第二项占比':<15} {'增加数据量':<15}")
    print_and_log("-" * 130)
    
    for dataset_params in datasets:
        dataset_name = dataset_params[0]
        D_GB = dataset_params[1]
        params = list(dataset_params[1:])  # 转换为列表以便修改
        
        # 将GB转换为数据个数
        Skv = params[7]  # Skv是第8个参数
        D = convert_gb_to_data_count(D_GB, Skv)
        
        # 计算完整版本
        try:
            params_full = params.copy()
            params_full[-1] = True  # 启用第二项
            # 参数顺序: D_GB, f, Smem, CT0, C1, R0_alpha, R1_alpha, Skv, enable_term2
            D_GB_val, f, Smem, CT0, C1, R0_alpha, R1_alpha, Skv, _ = params_full
            EDWs_full, term1_full, term2_full, WAF_full, EDWs_bytes_full = calculate_EDWs(C1, D, R0_alpha, CT0, Smem, R1_alpha, f, Skv, True)
            
            # 计算仅第一项版本
            EDWs_term1_only, term1_only, term2_only, WAF_term1_only, EDWs_bytes_term1_only = calculate_EDWs(C1, D, R0_alpha, CT0, Smem, R1_alpha, f, Skv, False)
            
            # 计算第二项贡献
            term2_contribution = term2_full
            term2_percentage = (term2_contribution / EDWs_full) * 100 if EDWs_full != 0 else 0
            
            # 计算增加的数据量
            additional_bytes = term2_contribution * Skv
            additional_readable = format_bytes_to_readable(additional_bytes)
            
            print_and_log(f"{dataset_name:<15} {D_GB:<10.1f} {EDWs_full:<18,.0f} {EDWs_term1_only:<18,.0f} {term2_contribution:<18,.0f} {term2_percentage:<15.2f}% {additional_readable:<15}")
            
        except ValueError as e:
            print_and_log(f"{dataset_name:<15} {D_GB:<10.1f} Error: {e}")

if __name__ == "__main__":
    # 执行批量计算
    results = batch_calculate()
    
    # 执行第二项影响分析
    compare_with_without_term2()
    
    print_and_log(f"\n{'='*80}")
    print_and_log("计算完成!")
    print_and_log(f"结果已保存到文件: {output_file}")
    print_and_log("你可以:")
    print_and_log("1. 在datasets列表中添加更多数据集")
    print_and_log("2. 修改log_denominator_multiplier的值")
    print_and_log("3. 设置enable_term2=False来禁用第二项")
    print_and_log("4. D输入格式: GB单位，自动转换为数据个数")
    print_and_log("5. WAF计算: (EDWs + 原始数据) / 原始数据")
    print_and_log("="*80)