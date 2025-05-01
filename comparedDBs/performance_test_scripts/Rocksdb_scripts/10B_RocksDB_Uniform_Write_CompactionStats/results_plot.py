import os
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# === 参数配置 ===
input_csv = "./rocksdb_compaction_summary.csv"

# 固定条件（可以只固定 zipf，或多个）
fixed_conditions = {
    "l1base": 512,
}

# 动态变量（用于决定每条线）
dynamic_conditions = ["ct0"]

# 横轴、纵轴
x_axis = "zipf"        # 横轴变量
y_metric = "write_gb"  # 纵轴变量

# 添加一条红色参考线
reference_line_value = 120.12

# === 加载数据 ===
df = pd.read_csv(input_csv)

# 筛选固定条件
for col, val in fixed_conditions.items():
    df = df[df[col] == val]

# ==== 自动处理 x 轴 ====
is_float_x = np.issubdtype(df[x_axis].dtype, np.floating)
unique_vals = sorted(df[x_axis].unique())

# 判断是否要映射（连续浮点值，且差值较小）
if is_float_x and max(unique_vals) - min(unique_vals) < 10:
    x_mapping = {val: i for i, val in enumerate(unique_vals)}
    df["x_mapped"] = df[x_axis].map(x_mapping)
    x_axis_actual = "x_mapped"
    use_xticks = True
else:
    x_axis_actual = x_axis
    use_xticks = False

# 获取 x 轴显示顺序
unique_x = sorted(df[x_axis_actual].unique())

# === 开始画图 ===
plt.figure(figsize=(12, 8))

group_keys = dynamic_conditions if dynamic_conditions else []

for keys, group in df.groupby(group_keys):
    group = group.sort_values(by=x_axis_actual)

    # 构造线条 label
    if isinstance(keys, tuple):
        label = ", ".join(f"{k}={v}" for k, v in zip(dynamic_conditions, keys))
    else:
        label = f"{dynamic_conditions[0]}={keys}"

    # 绘图
    plt.plot(group[x_axis_actual], group[y_metric], marker='o', label=label)

    # 线尾加标注
    last_x = group[x_axis_actual].values[-1]
    last_y = group[y_metric].values[-1]
    plt.text(last_x + 0.3, last_y, label, fontsize=8, verticalalignment='center')

# 设置 x 轴刻度标签（如果使用映射）
if use_xticks:
    plt.xticks(ticks=range(len(unique_vals)), labels=[str(v) for v in unique_vals])

plt.xlabel(x_axis)
plt.ylabel(y_metric)

fixed_part = ", ".join(f"{k}={v}" for k, v in fixed_conditions.items()) if fixed_conditions else "NoFixedCondition"
dynamic_part = ", ".join(dynamic_conditions) if dynamic_conditions else "NoDynamicCondition"
plt.title(f"{y_metric} vs {x_axis}\nFixed({fixed_part}), GroupedBy({dynamic_part})")

# 添加参考线
plt.axhline(y=reference_line_value, color='red', linestyle='--', linewidth=2, label=f"Ref={reference_line_value}")

plt.legend(loc="best")
plt.grid(True)
plt.tight_layout()

# === 自动保存图像 ===
current_dir = os.path.dirname(os.path.abspath(__file__))
file_fixed = "_".join(f"{k}{v}" for k, v in fixed_conditions.items()) if fixed_conditions else "NoFixed"
file_dynamic = "_".join(dynamic_conditions) if dynamic_conditions else "NoDynamic"
filename = f"{y_metric}_vs_{x_axis}_groupby_{file_dynamic}_{file_fixed}.png"
output_path = os.path.join(current_dir, filename)

plt.savefig(output_path)
print(f"Plot saved as {output_path}")
