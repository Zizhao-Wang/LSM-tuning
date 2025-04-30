import os
import pandas as pd
import matplotlib.pyplot as plt

# === 参数配置 ===
input_csv = "./rocksdb_compaction_summary.csv"

# 固定参数（可多个或为空）
fixed_conditions = {
    "zipf": 1.1,
    "ct0": 8,
}

# 分组变量（决定每条线的条件组合）
dynamic_conditions = [ "l1base"]

# 绘图维度
x_axis = "mem"
y_metric = "write_gb"

# 用户指定的参考线值（红色横线）
reference_line_value = 120.12

# === 加载数据 ===
df = pd.read_csv(input_csv)

# 筛选固定条件
for col, val in fixed_conditions.items():
    df = df[df[col] == val]

# 处理 x 轴格式（如 mem → 数字）
def normalize_mem(mem_str):
    return int(mem_str.replace("MB", ""))

if x_axis == "mem":
    df["mem_num"] = df["mem"].apply(normalize_mem)
    x_axis_actual = "mem_num"
else:
    x_axis_actual = x_axis

# 排序 x 值
unique_x = sorted(df[x_axis_actual].unique())

# === 绘图开始 ===
plt.figure(figsize=(12, 8))

# 分组依据
group_keys = dynamic_conditions if dynamic_conditions else []

for keys, group in df.groupby(group_keys):
    group = group.sort_values(by=x_axis_actual)

    # 构造线标签
    if isinstance(keys, tuple):
        label = ", ".join(f"{k}={v}" for k, v in zip(dynamic_conditions, keys))
    else:
        label = f"{dynamic_conditions[0]}={keys}"

    # 画线
    plt.plot(group[x_axis_actual], group[y_metric], marker='o', label=label)

    # 在线尾部添加注释
    last_x = group[x_axis_actual].values[-1]
    last_y = group[y_metric].values[-1]
    plt.text(last_x + 0.5, last_y, label, fontsize=8, verticalalignment='center')

# 添加参考线
plt.axhline(y=reference_line_value, color='red', linestyle='--', linewidth=2, label=f"Ref={reference_line_value}")

# 设置坐标轴
plt.xticks(unique_x)
plt.xlabel(x_axis)
plt.ylabel(y_metric)

# 图标题、图例
fixed_part = ", ".join(f"{k}={v}" for k, v in fixed_conditions.items()) if fixed_conditions else "NoFixedCondition"
dynamic_part = ", ".join(dynamic_conditions) if dynamic_conditions else "NoDynamicCondition"
plt.title(f"{y_metric} vs {x_axis}\nFixed({fixed_part}), GroupedBy({dynamic_part})")

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
