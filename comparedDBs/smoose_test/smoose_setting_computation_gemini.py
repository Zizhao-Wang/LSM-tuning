import math
import functools
import sys
import json
from typing import List, Tuple, Dict, Any

# 增大Python的递归深度限制，以支持深度动态规划
try:
    # 2000对于大多数由DP引起的深度递归应该是安全的
    sys.setrecursionlimit(2000) 
except (OverflowError, ValueError):
    # 在某些受限环境中，设置可能会失败
    print("警告：无法设置更高的递归深度，将使用默认值。")

@functools.lru_cache(maxsize=None)
def solve_dp_scaled(nd_mult: int, nr_mult: int, n_l_mult: int, R_MAX: int) -> Tuple[float, List[float]]:
    """
    (已更正) [cite_start]实现了Section 3.4中的DP算法 [cite: 359-367]，并增加了 R_MAX 剪枝
    
    该函数求解 S(Nd, Nr) = min_r { sqrt(r) + S(r*Nd, Nr - r*Nd) }
    所有参数都已根据 F (F_mult=1) 进行了缩放。
    
    Args:
        nd_mult (int): 当前 N_d / F
        nr_mult (int): 当前 N_r / F
        n_l_mult (int): 固定的 N_L / F
        R_MAX (int): 最大的允许大小比例 r (用于剪枝)
    
    Returns:
        (最小成本, 最佳大小比例路径)
    """
    
    # 1. DP的终止条件 (Base Case)
    # 当剩余容量不足以创建新层 (r=2) 时
    # [cite: 390-391]
    if nr_mult < 2 * nd_mult:
        # 将所有剩余容量分配给当前层 (L-1)
        last_non_last_cap_mult = nd_mult + nr_mult
        
        if last_non_last_cap_mult <= 0:
            return (float('inf'), [])

        # 计算最后一层的 size ratio, r_L = N_L / N_{L-1}
        r_L = n_l_mult / last_non_last_cap_mult
        
        # 约束条件：r_L 必须在 [2, R_MAX] 范围内
        if not (2 <= r_L <= R_MAX):
            return (float('inf'), [])

        # --- 这是修正的行 ---
        cost = math.sqrt(r_L) # 成本是 sqrt(r_L) [cite: 391]
        # --- 修正结束 ---
        return (cost, [r_L])

    # 2. DP的递归步骤 (Recursive Step)
    min_total_cost = float('inf')
    best_path = []

    r_min = 2 # 最小大小比例
    
    # --- 这是关键的剪枝优化 ---
    # 1. 理论上的 r_max
    r_max_theoretical = int(nr_mult / nd_mult)
    # 2. 应用我们的启发式剪枝 (R_MAX)
    r_max = min(r_max_theoretical, R_MAX)
    # --- 优化结束 ---
    
    if r_max < r_min:
        return (float('inf'), [])

    # [cite_start]遍历所有可能的整数大小比例 'r' [cite: 368]
    for r in range(r_min, r_max + 1):
        
        new_nd_mult = r * nd_mult
        new_nr_mult = nr_mult - new_nd_mult
        
        cost_of_this_step = math.sqrt(r)
        
        # 递归调用DP
        cost_of_future, path_from_future = solve_dp_scaled(new_nd_mult, new_nr_mult, n_l_mult, R_MAX)
        
        if cost_of_future == float('inf'):
            continue 

        total_cost = cost_of_this_step + cost_of_future
        
        if total_cost < min_total_cost:
            min_total_cost = total_cost
            best_path = [r] + path_from_future

    return (min_total_cost, best_path)


def find_best_configuration(N: int, F: int, B: int, M: int, 
                            s_workload: float, u_workload: float, z_workload: float) -> Dict[str, Any]:
    """
    (已更正) 实现了 Section 3.6 中描述的 SMOOSE "Tractable structure adaptation" 
    (可处理结构自适应) [cite_start]算法 [cite: 558-561]。
    
    Args:
        N (int): 数据库中的总条目数
        F (int): 缓冲区大小（以条目为单位）
        B (int): 块大小（以条目为单位）
        M (int): 总 Bloom filter 内存（以比特为单位）
        s_workload (float): 范围查询 (range lookup) 的比例 (0.0 到 1.0)
        u_workload (float): 更新 (update) 的比例 (0.0 到 1.0)
        z_workload (float): 点查询 (point lookup) 的比例 (0.0 到 1.0)

    Returns:
        一个字典，包含找到的最佳配置及其理论成本。
    """
    
    print(f"--- 启动 SMOOSE 优化搜索 ---")
    print(f"输入参数: N={N}, F={F}, B={B}, M={M}")
    print(f"Workload: s={s_workload}, u={u_workload}, z={z_workload}")
    print("-" * 30)

    # --- 新增：剪枝参数 ---
    # 1. 限制DP搜索的最大大小比例。论文中没有明说，但这是在“几秒钟”内跑完的唯一方法
    R_MAX_HEURISTIC = 100 
    # 2. 限制外层 N_L 循环的迭代步数
    OUTER_LOOP_STEPS = 100
    
    print(f"应用剪枝: 最大允许的大小比例 R_MAX = {R_MAX_HEURISTIC}")
    print(f"应用剪枝: N_L 循环的步数 = {OUTER_LOOP_STEPS}")
    
    # --- 缩放/归一化参数 ---
    # [cite_start]我们将所有容量缩放到 F 的倍数，这极大地减少了DP状态空间 [cite: 393]
    if F <= 0:
        print(f"错误: 缓冲区大小 F ({F}) 必须为正数。")
        return {}
        
    N_mult = round(N / F)
    F_mult = 1 # 我们的基本单位是 F
    
    if N_mult <= 0:
        print(f"错误: N ({N}) 或 F ({F}) 必须为正。N/F 比例太小或为0。")
        return {}

    best_overall_config = {
        'min_theoretical_cost': float('inf'),
        'best_N_L_ratio': 0,
        'best_k': 0,
        'best_size_ratios': [],
        'level_capacities': [],
        'run_numbers': [],
        'costs_by_op': {}
    }

    # 估算 p_avg，用于简化的 Z 成本模型
    # [cite_start]p_avg = e^(-BPK * ln(2)^2) [cite: 115, 243]
    if N <= 0:
        print("错误: N 必须为正数才能计算 BPK。")
        return {}
    bpk_avg = M / N
    if bpk_avg <= 0:
        p_avg = 1.0 # 无法使用Bloom filter
    else:
        p_avg = math.exp(-bpk_avg * (math.log(2)**2))
    
    print(f"估算的 p_avg (用于Z成本): {p_avg:.6f}")
    
    # [cite_start]--- 步骤 1: 遍历 N_L (使用缩放后的值) [cite: 564-565] ---
    # 论文建议从 0.5N 迭代到 N
    N_L_start_mult = round(0.5 * N_mult)
    # 至少为非最后级别留出 2*F 的空间
    N_L_end_mult = N_mult - (F_mult * 2) 
    
    if N_L_start_mult >= N_L_end_mult:
        print(f"错误: N/F 比例 ({N_mult}) 太小，无法在 [0.5N, N] 范围内搜索 N_L。")
        return {}

    # (新增) 计算迭代步长
    step_size_mult = max(1, (N_L_end_mult - N_L_start_mult) // OUTER_LOOP_STEPS)

    print(f"将 N_L/F 从 {N_L_start_mult} 迭代到 {N_L_end_mult} (步长 {step_size_mult})...")
    
    for n_l_mult in range(N_L_start_mult, N_L_end_mult + 1, step_size_mult):
        
        nr_initial_mult = N_mult - n_l_mult
        
        if nr_initial_mult < 2 * F_mult:
            continue

        # [cite_start]--- 步骤 2: 为每个 N_L 运行 DP 算法 [cite: 566] ---
        solve_dp_scaled.cache_clear() # 为每个N_L重置DP缓存
        
        (min_A, size_ratios) = solve_dp_scaled(F_mult, nr_initial_mult, n_l_mult, R_MAX_HEURISTIC)
        
        if min_A == float('inf') or not size_ratios:
            continue # 没有为此 N_L 找到有效路径

        # [cite_start]--- 步骤 3: 遍历 k (运行数调节器) [cite: 568] ---
        # [cite_start]"Discussion 1" [cite: 853-855] 建议 k 从 1/sqrt(rmax) 到 sqrt(rmax)
        r_max_found = max(size_ratios)
        if r_max_found <= 0: continue
        
        k_min = 1.0 / math.sqrt(r_max_found)
        k_max = math.sqrt(r_max_found)
        # 我们将 k 空间划分为 10 个步骤进行搜索
        k_step_size = (k_max - k_min) / 10.0
        if k_step_size <= 0: k_step_size = 0.1 # 避免 r_max=1 时出现问题

        for i in range(11):
            k = k_min + i * k_step_size
            
            # [cite_start]--- 计算理论成本 [cite: 554] ---
            # S = k * sum(sqrt(ri)) = k * min_A
            cost_S = k * min_A
            # U = (1 / (k*B)) * sum(sqrt(ri)) = (1 / (k*B)) * min_A
            if B <= 0:
                print("错误: 块大小 B 必须为正数。")
                return {}
            cost_U = (1.0 / (k * B)) * min_A
            # Z = k * sum(pi * sqrt(ri))
            # 我们使用简化的模型 Z = k * p_avg * sum(sqrt(ri))
            cost_Z = k * p_avg * min_A

            # [cite_start]计算最终的“综合可搜索成本” Z_total (Equation 17) [cite: 549-550]
            Z_total = (s_workload * cost_S) + (u_workload * cost_U) + (z_workload * cost_Z)

            # 检查这是否是迄今为止的最佳配置
            if Z_total < best_overall_config['min_theoretical_cost']:
                
                # --- 保存最佳配置的详细信息 ---
                level_caps_out = [F]
                run_nums_out = []
                
                # 从 {r_i} 重建层级容量
                current_cap = F
                for r in size_ratios:
                    current_cap *= r
                    level_caps_out.append(round(current_cap))
                
                # [cite_start]计算每层的运行数 n_i = k * sqrt(r_i) [cite: 399]
                for r in size_ratios:
                    n_i = k * math.sqrt(r)
                    # [cite_start]"run number for each level must be a non-zero integer" [cite: 855]
                    run_nums_out.append(max(1, round(n_i))) 
                
                # 更新最佳配置字典
                best_overall_config['min_theoretical_cost'] = Z_total
                best_overall_config['best_N_L_ratio'] = n_l_mult / N_mult
                best_overall_config['best_k'] = k
                best_overall_config['best_size_ratios'] = size_ratios
                best_overall_config['level_capacities'] = level_caps_out
                best_overall_config['run_numbers'] = run_nums_out
                best_overall_config['costs_by_op'] = {'S': cost_S, 'U': cost_U, 'Z': cost_Z}

    print("--- SMOOSE 优化搜索完成 ---")
    
    if best_overall_config['min_theoretical_cost'] == float('inf'):
        print("警告：未找到有效的LSM-tree配置。请检查参数或尝试增大 R_MAX_HEURISTIC。")
    else:
        print(f"找到最佳理论成本: {best_overall_config['min_theoretical_cost']:.4f}")
        
    return best_overall_config

# --- 主程序入口和示例用法 ---
if __name__ == "__main__":
    
    # --- 1. 输入您的参数 ---
    # 这是您上次运行时使用的参数：
    TOTAL_ENTRIES_N = 104857600
    BUFFER_SIZE_F = 2097
    BLOCK_SIZE_B = 4
    BLOOM_FILTER_BUDGET_M = 1048576000
    
    # --- 2. Workload 配置 ---
    # (Workload J: 33% range, 33% update, 33% point)
    WORKLOAD_S = 0.33  # 范围查询比例
    WORKLOAD_U = 0.33  # 更新比例
    WORKLOAD_Z = 0.33  # 点查询比例

    # 运行 SMOOSE 算法
    best_config = find_best_configuration(
        N=TOTAL_ENTRIES_N,
        F=BUFFER_SIZE_F,
        B=BLOCK_SIZE_B,
        M=BLOOM_FILTER_BUDGET_M,
        s_workload=WORKLOAD_S,
        u_workload=WORKLOAD_U,
        z_workload=WORKLOAD_Z
    )

    # --- 打印输出：最佳配置 ---
    print("\n--- SMOOSE 算法的最佳配置输出 ---")
    if best_config and best_config.get('min_theoretical_cost') != float('inf'):
        # 使用 json 模块进行格式化输出
        print(json.dumps(best_config, indent=2))
    else:
        print("未计算出有效配置。")