import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import os

# ========== 配置区域 ==========
# 1. 设置固定的f值
f = 2.0  # 在这里设置你想要的f值

# 2. 选择分析模式
analysis_mode = "fixed_both"  # 选项如下:
# "comprehensive" - 全面分析，展示完整的参数空间，不需要修改其他参数
# "fixed_R0"      - 固定R₀值，分析R₁的影响，需要设置下面的fixed_R0_value
# "fixed_R1"      - 固定R₁值，分析R₀的影响，需要设置下面的fixed_R1_value
# "fixed_both"    - 固定R₀和R₁，分析x*随f的变化，需要设置fixed_R0_value和fixed_R1_value
# "custom"        - 自定义模式(预留)

# 3. 固定模式的参数设置
# ⚠️ 只有当analysis_mode="fixed_R0"时才需要修改这个值
fixed_R0_value = 0.8  # 将R₀固定为此值，然后分析x*随R₁变化的关系

# ⚠️ 只有当analysis_mode="fixed_R1"时才需要修改这个值  
fixed_R1_value = 0.9  # 将R₁固定为此值，然后分析x*随R₀变化的关系

# ⚠️ 当analysis_mode="fixed_both"时需要修改这两个值
# fixed_R0_value = 0.8  # R₀固定值
# fixed_R1_value = 0.3  # R₁固定值，然后分析x*随f变化的关系

# 4. f值范围设置（仅在fixed_both模式下使用）
f_range = (0.1, 10.0)   # f的扫描范围，避开f=1（因为ln(1)=0）

# 4. 参数扫描范围设置
R0_range = (0.01, 1.0)  # R₀的扫描范围 (最小值, 最大值)
R1_range = (0.01, 1.0)  # R₁的扫描范围 (最小值, 最大值)
resolution = 100        # 扫描点数，越大越精细但计算越慢

# 5. 图片保存设置
save_dpi = 300          # 图片分辨率
save_format = 'png'     # 图片格式，可选: 'png', 'pdf', 'svg', 'jpg'

# ========== 使用说明 ==========
"""
使用示例：

1. 想看完整的参数关系图：
   analysis_mode = "comprehensive"
   (其他参数保持默认即可)

2. 想研究当R₀=0.9时，R₁如何影响x*：
   analysis_mode = "fixed_R0"
   fixed_R0_value = 0.9

3. 想研究当R₁=0.2时，R₀如何影响x*：
   analysis_mode = "fixed_R1"
   fixed_R1_value = 0.2

4. 想研究当R₀=0.8且R₁=0.3时，f如何影响x*：
   analysis_mode = "fixed_both"
   fixed_R0_value = 0.8
   fixed_R1_value = 0.3

5. 想只看某个范围的参数：
   R0_range = (0.1, 0.8)  # 只看R₀从0.1到0.8
   R1_range = (0.2, 1.0)  # 只看R₁从0.2到1.0
"""

# ========== 函数定义 ==========
def optimal_point(R0, R1, f_fixed, alpha=1):
    """
    计算最优点 x*
    
    参数:
    R0: 分子中的R值 (0到1)
    R1: 分母中的R值 (0到1) 
    f_fixed: 固定的f值
    alpha: 指数参数，默认为1
    """
    if R1 == 0 or f_fixed <= 0 or f_fixed == 1:
        return np.nan
    
    ratio = (R0**alpha) / (R1**alpha)
    coefficient = 2 * f_fixed / np.log(f_fixed)
    
    return ratio * coefficient

def create_comprehensive_analysis():
    """创建全面的分析图表"""
    fig = plt.figure(figsize=(20, 15))
    
    R0_values = np.linspace(R0_range[0], R0_range[1], resolution)
    R1_values = np.linspace(R1_range[0], R1_range[1], resolution)
    
    # 1. 固定R1，展示x*随R0的变化（多条线）
    plt.subplot(3, 4, 1)
    R1_test_values = [0.1, 0.3, 0.5, 0.7, 0.9]
    colors = ['red', 'blue', 'green', 'orange', 'purple']
    
    for R1_val, color in zip(R1_test_values, colors):
        x_values = [optimal_point(R0, R1_val, f) for R0 in R0_values]
        plt.plot(R0_values, x_values, color=color, linewidth=2, label=f'R₁={R1_val}')
    
    plt.xlabel('R₀')
    plt.ylabel('x*')
    plt.title(f'x* vs R₀ for different R₁ (f={f})')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    # 2. 固定R0，展示x*随R1的变化（多条线）
    plt.subplot(3, 4, 2)
    R0_test_values = [0.1, 0.3, 0.5, 0.7, 0.9]
    
    for R0_val, color in zip(R0_test_values, colors):
        x_values = [optimal_point(R0_val, R1, f) for R1 in R1_values]
        plt.plot(R1_values, x_values, color=color, linewidth=2, label=f'R₀={R0_val}')
    
    plt.xlabel('R₁')
    plt.ylabel('x*')
    plt.title(f'x* vs R₁ for different R₀ (f={f})')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    # 3. 比值R0/R1的影响
    plt.subplot(3, 4, 3)
    ratio_values = np.linspace(0.1, 10, 100)
    coefficient = 2 * f / np.log(f)
    x_values = ratio_values * coefficient
    
    plt.plot(ratio_values, x_values, 'b-', linewidth=2)
    plt.xlabel('R₀/R₁')
    plt.ylabel('x*')
    plt.title(f'x* vs R₀/R₁ ratio (f={f})')
    plt.grid(True, alpha=0.3)
    
    # 4. 对数坐标系下的关系
    plt.subplot(3, 4, 4)
    plt.loglog(ratio_values, x_values, 'r-', linewidth=2)
    plt.xlabel('log(R₀/R₁)')
    plt.ylabel('log(x*)')
    plt.title(f'Log-log plot (f={f})')
    plt.grid(True, alpha=0.3)
    
    # 5. 3D表面图
    ax = fig.add_subplot(3, 4, 5, projection='3d')
    R0_mesh, R1_mesh = np.meshgrid(R0_values[::5], R1_values[::5])  # 降低分辨率以提高性能
    X_mesh = np.zeros_like(R0_mesh)
    
    for i in range(R0_mesh.shape[1]):
        for j in range(R0_mesh.shape[0]):
            X_mesh[j, i] = optimal_point(R0_mesh[j, i], R1_mesh[j, i], f)
    
    surf = ax.plot_surface(R0_mesh, R1_mesh, X_mesh, cmap='viridis', alpha=0.8)
    ax.set_xlabel('R₀')
    ax.set_ylabel('R₁')
    ax.set_zlabel('x*')
    ax.set_title(f'3D Surface (f={f})')
    
    # 6. 热力图
    plt.subplot(3, 4, 6)
    R0_mesh_full, R1_mesh_full = np.meshgrid(R0_values, R1_values)
    X_mesh_full = np.zeros_like(R0_mesh_full)
    
    for i in range(len(R0_values)):
        for j in range(len(R1_values)):
            X_mesh_full[j, i] = optimal_point(R0_mesh_full[j, i], R1_mesh_full[j, i], f)
    
    im = plt.imshow(X_mesh_full, extent=[R0_range[0], R0_range[1], R1_range[0], R1_range[1]], 
                    aspect='auto', origin='lower', cmap='viridis')
    plt.colorbar(im, label='x*')
    plt.xlabel('R₀')
    plt.ylabel('R₁')
    plt.title(f'Heatmap (f={f})')
    
    # 7. 等高线图
    plt.subplot(3, 4, 7)
    contour_levels = np.logspace(np.log10(np.nanmin(X_mesh_full)), np.log10(np.nanmax(X_mesh_full)), 15)
    contour = plt.contour(R0_mesh_full, R1_mesh_full, X_mesh_full, levels=contour_levels, colors='black', alpha=0.5)
    plt.clabel(contour, inline=True, fontsize=8, fmt='%.2f')
    contourf = plt.contourf(R0_mesh_full, R1_mesh_full, X_mesh_full, levels=contour_levels, cmap='viridis', alpha=0.8)
    plt.colorbar(contourf, label='x*')
    plt.xlabel('R₀')
    plt.ylabel('R₁')
    plt.title(f'Contour Plot (f={f})')
    
    # 8. 梯度分析
    plt.subplot(3, 4, 8)
    gradient_R0 = np.gradient(X_mesh_full, axis=1)
    gradient_R1 = np.gradient(X_mesh_full, axis=0)
    gradient_magnitude = np.sqrt(gradient_R0**2 + gradient_R1**2)
    
    im = plt.imshow(gradient_magnitude, extent=[R0_range[0], R0_range[1], R1_range[0], R1_range[1]], 
                    aspect='auto', origin='lower', cmap='plasma')
    plt.colorbar(im, label='Gradient Magnitude')
    plt.xlabel('R₀')
    plt.ylabel('R₁')
    plt.title(f'Gradient Analysis (f={f})')
    
    # 9-12. 特定切片分析
    slice_values = [0.25, 0.5, 0.75, 1.0]
    for idx, slice_val in enumerate(slice_values):
        plt.subplot(3, 4, 9 + idx)
        if idx < 2:  # R1固定
            x_values = [optimal_point(R0, slice_val, f) for R0 in R0_values]
            plt.plot(R0_values, x_values, 'b-', linewidth=2)
            plt.xlabel('R₀')
            plt.ylabel('x*')
            plt.title(f'R₁={slice_val}')
        else:  # R0固定
            x_values = [optimal_point(slice_val, R1, f) for R1 in R1_values]
            plt.plot(R1_values, x_values, 'r-', linewidth=2)
            plt.xlabel('R₁')
            plt.ylabel('x*')
            plt.title(f'R₀={slice_val}')
        plt.grid(True, alpha=0.3)
    
    return fig

def create_fixed_both_analysis(R0_fixed, R1_fixed):
    """创建固定R0和R1，分析x*随f变化的图表"""
    fig = plt.figure(figsize=(15, 10))
    
    # f值范围，避开f=1
    f_values = np.concatenate([
        np.linspace(f_range[0], 0.99, 50),
        np.linspace(1.01, f_range[1], 50)
    ])
    
    # 计算x*值
    x_values = [optimal_point(R0_fixed, R1_fixed, f_val) for f_val in f_values]
    
    # 计算系数 2f/ln(f)
    coefficients = [2 * f_val / np.log(f_val) for f_val in f_values]
    
    # 1. x* vs f
    plt.subplot(2, 3, 1)
    plt.plot(f_values, x_values, 'b-', linewidth=2)
    plt.axvline(x=f, color='red', linestyle='--', alpha=0.7, label=f'Current f={f}')
    plt.xlabel('f')
    plt.ylabel('x*')
    plt.title(f'x* vs f (R₀={R0_fixed}, R₁={R1_fixed})')
    plt.grid(True, alpha=0.3)
    plt.legend()
    
    # 2. 系数 2f/ln(f) vs f
    plt.subplot(2, 3, 2)
    plt.plot(f_values, coefficients, 'g-', linewidth=2)
    plt.axvline(x=f, color='red', linestyle='--', alpha=0.7, label=f'Current f={f}')
    plt.xlabel('f')
    plt.ylabel('2f/ln(f)')
    plt.title('Coefficient vs f')
    plt.grid(True, alpha=0.3)
    plt.legend()
    
    # 3. 对数坐标下的关系
    plt.subplot(2, 3, 3)
    plt.loglog(f_values, x_values, 'r-', linewidth=2)
    plt.axvline(x=f, color='red', linestyle='--', alpha=0.7, label=f'Current f={f}')
    plt.xlabel('log(f)')
    plt.ylabel('log(x*)')
    plt.title('Log-log plot')
    plt.grid(True, alpha=0.3)
    plt.legend()
    
    # 4. 导数分析
    plt.subplot(2, 3, 4)
    # 计算数值导数
    df = f_values[1] - f_values[0]
    dx_df = np.gradient(x_values, df)
    plt.plot(f_values, dx_df, 'm-', linewidth=2)
    plt.axvline(x=f, color='red', linestyle='--', alpha=0.7, label=f'Current f={f}')
    plt.xlabel('f')
    plt.ylabel('dx*/df')
    plt.title('Derivative of x* with respect to f')
    plt.grid(True, alpha=0.3)
    plt.legend()
    
    # 5. 不同R0/R1比值的对比
    plt.subplot(2, 3, 5)
    ratios = [0.5, 1.0, 2.0, 4.0]
    colors = ['red', 'blue', 'green', 'orange']
    
    for ratio, color in zip(ratios, colors):
        R0_test = 0.5 * ratio
        R1_test = 0.5
        if R0_test <= 1.0:
            x_test = [optimal_point(R0_test, R1_test, f_val) for f_val in f_values]
            plt.plot(f_values, x_test, color=color, linewidth=2, label=f'R₀/R₁={ratio}')
    
    plt.axvline(x=f, color='red', linestyle='--', alpha=0.7)
    plt.xlabel('f')
    plt.ylabel('x*')
    plt.title('x* vs f for different R₀/R₁ ratios')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    # 6. 统计信息和特殊点
    plt.subplot(2, 3, 6)
    plt.axis('off')
    
    # 找到特殊点
    current_x = optimal_point(R0_fixed, R1_fixed, f)
    current_coeff = 2 * f / np.log(f)
    ratio = R0_fixed / R1_fixed
    
    # 寻找极值点（系数的）
    min_idx = np.argmin(coefficients)
    min_f = f_values[min_idx]
    min_coeff = coefficients[min_idx]
    
    stats_text = f"""
    固定R₀和R₁分析f的影响
    
    固定参数:
    R₀ = {R0_fixed}
    R₁ = {R1_fixed}
    R₀/R₁ = {ratio:.3f}
    
    当前f = {f}:
    系数 = {current_coeff:.4f}
    x* = {current_x:.4f}
    
    系数最小值:
    f = {min_f:.3f}
    系数 = {min_coeff:.4f}
    
    函数特性:
    • f → 0⁺: x* → 0
    • f → 1⁻: x* → -∞
    • f → 1⁺: x* → +∞  
    • f → +∞: x* → +∞
    
    公式: x* = {ratio:.3f} × (2f/ln(f))
    """
    
    plt.text(0.05, 0.95, stats_text, transform=plt.gca().transAxes, 
             fontsize=10, verticalalignment='top', fontfamily='monospace')
    
    return fig

def create_fixed_analysis(fix_param, fix_value):
    """创建固定参数的分析图表"""
    fig = plt.figure(figsize=(15, 10))
    
    if fix_param == "R0":
        var_values = np.linspace(R1_range[0], R1_range[1], resolution)
        x_values = [optimal_point(fix_value, R1, f) for R1 in var_values]
        
        plt.subplot(2, 2, 1)
        plt.plot(var_values, x_values, 'b-', linewidth=2)
        plt.xlabel('R₁')
        plt.ylabel('x*')
        plt.title(f'x* vs R₁ (R₀={fix_value}, f={f})')
        plt.grid(True, alpha=0.3)
        
        # 不同R0值的对比
        plt.subplot(2, 2, 2)
        R0_compare = [fix_value * 0.5, fix_value, fix_value * 1.5]
        colors = ['red', 'blue', 'green']
        
        for R0_val, color in zip(R0_compare, colors):
            if R0_val <= 1.0:
                x_vals = [optimal_point(R0_val, R1, f) for R1 in var_values]
                plt.plot(var_values, x_vals, color=color, linewidth=2, label=f'R₀={R0_val:.2f}')
        
        plt.xlabel('R₁')
        plt.ylabel('x*')
        plt.title(f'Comparison around R₀={fix_value}')
        plt.legend()
        plt.grid(True, alpha=0.3)
        
    elif fix_param == "R1":
        var_values = np.linspace(R0_range[0], R0_range[1], resolution)
        x_values = [optimal_point(R0, fix_value, f) for R0 in var_values]
        
        plt.subplot(2, 2, 1)
        plt.plot(var_values, x_values, 'r-', linewidth=2)
        plt.xlabel('R₀')
        plt.ylabel('x*')
        plt.title(f'x* vs R₀ (R₁={fix_value}, f={f})')
        plt.grid(True, alpha=0.3)
        
        # 不同R1值的对比
        plt.subplot(2, 2, 2)
        R1_compare = [fix_value * 0.5, fix_value, fix_value * 1.5]
        colors = ['red', 'blue', 'green']
        
        for R1_val, color in zip(R1_compare, colors):
            if R1_val <= 1.0:
                x_vals = [optimal_point(R0, R1_val, f) for R0 in var_values]
                plt.plot(var_values, x_vals, color=color, linewidth=2, label=f'R₁={R1_val:.2f}')
        
        plt.xlabel('R₀')
        plt.ylabel('x*')
        plt.title(f'Comparison around R₁={fix_value}')
        plt.legend()
        plt.grid(True, alpha=0.3)
    
    # 共同的子图
    plt.subplot(2, 2, 3)
    # 系数随f变化
    f_range = np.linspace(0.1, 10, 100)
    coefficients = [2 * f_val / np.log(f_val) for f_val in f_range]
    plt.plot(f_range, coefficients, 'g-', linewidth=2)
    plt.axvline(x=f, color='red', linestyle='--', label=f'Current f={f}')
    plt.xlabel('f')
    plt.ylabel('Coefficient (2f/ln(f))')
    plt.title('Coefficient vs f')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    plt.subplot(2, 2, 4)
    # 统计信息
    plt.axis('off')
    coefficient = 2 * f / np.log(f)
    stats_text = f"""
    固定参数分析
    
    f = {f}
    系数 = 2f/ln(f) = {coefficient:.4f}
    
    {fix_param} = {fix_value}
    
    函数形式:
    x* = (R₀/R₁) × {coefficient:.4f}
    
    特点:
    • x* 与 R₀ 成正比
    • x* 与 R₁ 成反比
    • 当 R₀ = R₁ 时，x* = {coefficient:.4f}
    """
    plt.text(0.1, 0.9, stats_text, transform=plt.gca().transAxes, 
             fontsize=12, verticalalignment='top', fontfamily='monospace')
    
    return fig

# ========== 主程序 ==========
def main():
    print(f"分析模式: {analysis_mode}")
    print(f"使用固定的f值: {f}")
    print(f"计算系数 2f/ln(f) = {2*f/np.log(f):.4f}")
    print("-" * 50)
    
    # 根据模式选择创建不同的图表
    if analysis_mode == "comprehensive":
        fig = create_comprehensive_analysis()
        filename = f'comprehensive_analysis_f_{f}'
    elif analysis_mode == "fixed_R0":
        fig = create_fixed_analysis("R0", fixed_R0_value)
        filename = f'fixed_R0_{fixed_R0_value}_f_{f}'
    elif analysis_mode == "fixed_R1":
        fig = create_fixed_analysis("R1", fixed_R1_value)
        filename = f'fixed_R1_{fixed_R1_value}_f_{f}'
    elif analysis_mode == "fixed_both":
        fig = create_fixed_both_analysis(fixed_R0_value, fixed_R1_value)
        filename = f'fixed_both_R0_{fixed_R0_value}_R1_{fixed_R1_value}'
    else:
        print("未知的分析模式，使用comprehensive模式")
        fig = create_comprehensive_analysis()
        filename = f'comprehensive_analysis_f_{f}'
    
    plt.tight_layout()
    
    # 保存图片
    script_dir = os.path.dirname(os.path.abspath(__file__))
    output_file = os.path.join(script_dir, f'{filename}.{save_format}')
    plt.savefig(output_file, dpi=save_dpi, bbox_inches='tight')
    print(f"图片已保存到: {output_file}")
    
    # 打印数值例子
    print("\n具体的计算例子:")
    print("-" * 40)
    test_cases = [
        (0.8, 0.3), (0.5, 0.5), (0.9, 0.1), (0.2, 0.8), (1.0, 0.1), (0.1, 1.0)
    ]
    
    for R0, R1 in test_cases:
        x_star = optimal_point(R0, R1, f)
        ratio = R0 / R1
        print(f"R₀={R0}, R₁={R1}, R₀/R₁={ratio:.3f} => x* = {x_star:.4f}")

if __name__ == "__main__":
    main()