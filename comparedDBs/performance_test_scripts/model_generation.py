import math

# ==========================================
# 1. 【新方案】有界二次铰链模型 (Bounded Quadratic Hinge)
#    优点：解耦 (Decoupled)、有界 (Safe)、平滑 (Convex)
# ==========================================
def model_new_hinge(read_ratio, c0=4, gap_max=20, r0=0.5):
    """
    :param r0: 读优先阈值 (默认0.5)。超过它，Gap归零。
    """
    # 核心逻辑：只在 r < r0 时计算 Gap
    if read_ratio >= r0:
        gap = 0
    else:
        # 归一化距离：(r0 - r) / r0
        # 使用平方 (Quadratic) 让曲线在接近 r0 时更平滑
        distance = (r0 - read_ratio) / r0
        gap = math.floor(gap_max * (distance ** 2))
        
    return int(c0 + gap)


# ==========================================
# 2. 【旧方案】幂律衰减模型 (Power Law Decay)
#    特点：全区间衰减，依靠 k 控制敏感度
# ==========================================
def model_old_power(read_ratio, c0=4, gap_max=20, k=3):
    """
    :param k: 敏感度系数 (Sensitivity Factor)
    """
    # 核心逻辑：(1 - R)^k
    gap = math.floor(gap_max * ((1 - read_ratio) ** k))
    return int(c0 + gap)


# ==========================================
# 3. 【ChatGPT方案】乘法倒数模型 (Multiplicative Inverse Hinge)
#    对应图片中的公式: C0 * (1 + ((1-2r)+ / r)^2)
#    缺点：C0 增大时 Gap 会失控；R趋近0时无穷大
# ==========================================
def model_chatgpt(read_ratio, c0=4):
    # 防止除以0
    if read_ratio <= 0.001:
        return 9999  # 代表无穷大 (Infinity)
    
    # 核心逻辑：乘法耦合
    # 只有当 read_ratio < 0.5 时，分子才大于0
    numerator = max(0, 1 - 2 * read_ratio)
    
    # 倒数平方项
    term = (numerator / read_ratio) ** 2
    
    slow = c0 * (1 + term)
    return int(slow)


# ==========================================
# 4. 打印对比表格 (模拟 0% 到 100% 读比例)
# ==========================================
def print_comparison():
    print(f"{'Read%':<8} | {'New(Hinge)':<12} | {'Old(Power)':<12} | {'ChatGPT':<12}")
    print("-" * 50)
    
    # 从 0.0 到 1.0，步长 0.1
    for r in range(0, 11):
        ratio = r / 10.0
        
        # 计算三个模型的结果
        res_new = model_new_hinge(ratio, c0=4, gap_max=20, r0=0.5)
        res_old = model_old_power(ratio, c0=4, gap_max=20, k=3)
        res_gpt = model_chatgpt(ratio, c0=4)
        
        # 格式化输出
        gpt_str = "INF" if res_gpt > 1000 else str(res_gpt)
        print(f"{ratio:<8} | {res_new:<12} | {res_old:<12} | {gpt_str:<12}")

if __name__ == "__main__":
    print("假设参数: C0=4, GapMax=20 (用于我的模型)")
    print_comparison()