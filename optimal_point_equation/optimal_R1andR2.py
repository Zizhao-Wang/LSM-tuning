import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D

# 设置固定的f值（你可以在这里修改）
f = 10.0  # 在这里设置你想要的f值

print(f"使用固定的f值: {f}")
print(f"计算系数 2f/ln(f) = {2*f/np.log(f):.4f}")
print("-" * 50)

# 定义函数 x* = (R0^α / R1^α) * (2*f / ln(f))
def optimal_point(R0, R1, f_fixed, alpha=1):
    """
    计算最优点 x*
    
    参数:
    R0: 分子中的R值 (0到1)
    R1: 分母中的R值 (0到1) 
    f_fixed: 固定的f值
    alpha: 指数参数，默认为1
    """
    # 避免除零情况
    if R1 == 0 or f_fixed <= 0 or f_fixed == 1:
        return np.nan
    
    ratio = (R0**alpha) / (R1**alpha)
    coefficient = 2 * f_fixed / np.log(f_fixed)
    
    return ratio * coefficient

# 设置R0和R1的范围
R0_values = np.linspace(0.01, 1, 100)   # R0的范围，避开0
R1_values = np.linspace(0.01, 1, 100)   # R1的范围，避开0

# 创建图形
fig = plt.figure(figsize=(15, 10))

# 1. 固定R1，展示x*随R0的变化
plt.subplot(2, 3, 1)
R1_fixed = 0.3
x_values = [optimal_point(R0, R1_fixed, f) for R0 in R0_values]
plt.plot(R0_values, x_values, 'b-', linewidth=2)
plt.xlabel('R₀')
plt.ylabel('x*')
plt.title(f'x* vs R₀ (R₁={R1_fixed}, f={f})')
plt.grid(True, alpha=0.3)

# 2. 固定R0，展示x*随R1的变化
plt.subplot(2, 3, 2)
R0_fixed = 0.8
x_values = [optimal_point(R0_fixed, R1, f) for R1 in R1_values]
plt.plot(R1_values, x_values, 'r-', linewidth=2)
plt.xlabel('R₁')
plt.ylabel('x*')
plt.title(f'x* vs R₁ (R₀={R0_fixed}, f={f})')
plt.grid(True, alpha=0.3)

# 3. 不同R1值下，x*随R0的变化
plt.subplot(2, 3, 3)
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

# 4. 3D图：x*作为R0和R1的函数
ax = fig.add_subplot(2, 3, 4, projection='3d')
R0_mesh, R1_mesh = np.meshgrid(R0_values, R1_values)
X_mesh = np.zeros_like(R0_mesh)

for i in range(len(R0_values)):
    for j in range(len(R1_values)):
        X_mesh[j, i] = optimal_point(R0_mesh[j, i], R1_mesh[j, i], f)

surf = ax.plot_surface(R0_mesh, R1_mesh, X_mesh, cmap='viridis', alpha=0.8)
ax.set_xlabel('R₀')
ax.set_ylabel('R₁')
ax.set_zlabel('x*')
ax.set_title(f'x* surface (f={f})')

# 5. 热力图：x*作为R0和R1的函数
plt.subplot(2, 3, 5)
im = plt.imshow(X_mesh, extent=[R0_values[0], R0_values[-1], R1_values[0], R1_values[-1]], 
                aspect='auto', origin='lower', cmap='viridis')
plt.colorbar(im, label='x*')
plt.xlabel('R₀')
plt.ylabel('R₁')
plt.title(f'x* heatmap (f={f})')

# 6. 等高线图
plt.subplot(2, 3, 6)
contour = plt.contour(R0_mesh, R1_mesh, X_mesh, levels=15, colors='black', alpha=0.5)
plt.clabel(contour, inline=True, fontsize=8)
contourf = plt.contourf(R0_mesh, R1_mesh, X_mesh, levels=15, cmap='viridis', alpha=0.8)
plt.colorbar(contourf, label='x*')
plt.xlabel('R₀')
plt.ylabel('R₁')
plt.title(f'x* contour plot (f={f})')

plt.tight_layout()

# 保存图片到脚本所在目录
import os
script_dir = os.path.dirname(os.path.abspath(__file__))
output_file = os.path.join(script_dir, f'optimal_point_analysis_f_{f}.png')
plt.savefig(output_file, dpi=300, bbox_inches='tight')
print(f"图片已保存到: {output_file}")

# 打印一些具体的数值例子
print("\n具体的计算例子:")
print("-" * 40)
test_cases = [
    (0.8, 0.3),
    (0.5, 0.5),
    (0.9, 0.1),
    (0.2, 0.8),
    (1.0, 0.1),
    (0.1, 1.0)
]

for R0, R1 in test_cases:
    x_star = optimal_point(R0, R1, f)
    ratio = R0 / R1
    print(f"R₀={R0}, R₁={R1}, R₀/R₁={ratio:.3f} => x* = {x_star:.4f}")

print(f"\n函数形式: x* = (R₀/R₁) × {2*f/np.log(f):.4f}")
print("关键观察:")
print("- x* 与 R₀ 成正比")
print("- x* 与 R₁ 成反比") 
print("- 当 R₀ = R₁ 时，x* = 常数")
print(f"- 该常数值为: {2*f/np.log(f):.4f}")