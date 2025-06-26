import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# ==============================================================================
# --- 配置区: 请在这里修改 ---
# ==============================================================================

# 自动获取当前脚本所在的目录，作为所有相对路径的前缀
SCRIPT_DIRECTORY = os.path.dirname(os.path.realpath(__file__))

# --- 1. 文件选择配置 ---
# 在这个列表中手动指定您想要绘制的CSV文件的【相对路径】。
FILES_TO_PLOT = [
    # 示例: 假设您之前生成了 L0->L1 和 L1->L2 的数据
    "Compaction_0.2B_key44_val155_Cluster40_mem64M_L1100_CT04_F_Switch3_2F30_compaction_summary_L1_to_L2.csv",
    "compaction_summary_L1_to_L2.csv",
]

# --- 2. Y轴配置 ---
# 这里我们已经根据您的要求，配置为显示 "read_files"。
# 您也可以随时参考下面的注释示例，修改它来分析其他数据。
Y_AXIS_CONFIG = {
    "column_1": "read_files",
    "operation": None,
    "column_2": None,
    "label": "Number of Read Files (read_files)"
}

# # 其他分析示例:
# # a) 分析写入文件的数量
# Y_AXIS_CONFIG = {
#     "column_1": "write_files",
#     "operation": None,
#     "column_2": None,
#     "label": "Number of Written Files (write_files)"
# }
# # b) 分析读写字节比率 (空间放大率的一种体现)
# Y_AXIS_CONFIG = {
#     "column_1": "write_bytes",
#     "operation": "/",
#     "column_2": "read_bytes",
#     "label": "Write/Read Bytes Ratio"
# }

# ==============================================================================
# --- 核心绘图代码: 通常无需修改 ---
# ==============================================================================

def create_plot_from_files(files_to_plot, y_config, base_dir):
    """根据手动指定的相对路径文件列表和配置，生成图表。"""
    
    print("开始根据手动指定的文件列表生成图表...")
    print(f"脚本根目录: {base_dir}")
    print(f"Y轴显示内容: {y_config['label']}")
    
    fig, ax = plt.subplots(figsize=(14, 8))

    for relative_path in files_to_plot:
        absolute_path = os.path.join(base_dir, relative_path)
        
        if not os.path.exists(absolute_path):
            print(f"- 警告: 文件 '{absolute_path}' 不存在，已跳过。")
            continue

        try:
            df = pd.read_csv(absolute_path)
            if df.empty:
                print(f"- 警告: 文件 '{os.path.basename(absolute_path)}' 为空，已跳过。")
                continue

            # X轴: 组的索引 (0, 1, 2, ...)
            x_values = df.index
            # Y轴: 根据配置计算
            y_values = df[y_config["column_1"]] # 因为operation为None,直接取值

            # 在图表上绘制一条线
            base_filename = os.path.basename(absolute_path)
            line_label = os.path.splitext(base_filename)[0]
            ax.plot(x_values, y_values, marker='o', linestyle='-', label=line_label)

        except KeyError as e:
            print(f"- 警告: 文件 '{os.path.basename(absolute_path)}' 缺少必要的列 {e}，已跳过。")
        except Exception as e:
            print(f"- 错误: 处理文件 '{os.path.basename(absolute_path)}' 时发生未知错误: {e}")

    # 美化图表
    ax.set_title("Compaction Analysis", fontsize=16)
    ax.set_xlabel("Group Index (组号)", fontsize=12)
    ax.set_ylabel(y_config["label"], fontsize=12)
    ax.grid(True, linestyle='--', alpha=0.6)
    
    if not ax.get_lines():
        print("\n没有成功绘制任何数据，无法生成图表。")
        return

    ax.legend()
    plt.tight_layout()

    # 保存和显示图表
    output_filename = "compaction_plot.png"
    full_output_path = os.path.join(base_dir, output_filename)
    plt.savefig(full_output_path, dpi=150)
    print(f"\n图表已成功保存为 '{full_output_path}'")
    plt.show()

if __name__ == "__main__":
    create_plot_from_files(FILES_TO_PLOT, Y_AXIS_CONFIG, SCRIPT_DIRECTORY)