import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# ==============================================================================
# --- 配置区: 请在这里修改你想绘制的图表 ---
# ==============================================================================
SCRIPT_DIRECTORY = os.path.dirname(os.path.realpath(__file__))

# 存放 .csv 文件的目录
FILES_TO_PLOT = [
    # 示例:
    "SATune_PreLoad_2B_val155_mem64M_Cluster40_CT04_L1100_targetbase64_F5_Switch3_2F30_L1-6_on_Disk2.csv",
    "SATune_PreLoad_0.2B_val155_mem64MB_Cluster40_CT04_level1base100_targetbase64_Block4_Blkcache128_Tabcache1000_Switch3_2F50.csv"
]
# --- Y轴配置 ---
# 修改下面的字典来定义Y轴的内容。
#
#   - column_1: 第一个变量 (必须)。
#   - operation:  可以是 '+', '-', '*', '/' 或 None。
#                 如果为 None，则只绘制 column_1 的数据。
#   - column_2: 第二个变量 (进行运算时需要)。
#   - label:      Y轴在图表上显示的标签文字。
#
# --- 预设示例 (使用时请取消注释其中一个) ---

# 示例1: 只显示 'num_l0_files'
Y_AXIS_CONFIG = {
    "column_1": "num_l0_files",
    "operation": "+",
    "column_2": "num_l1_files_in",
    "label": "Number of L0L1 Files"
}

# # 示例2: 只显示 'output_input_ratio'
# Y_AXIS_CONFIG = {
#     "column_1": "output_input_ratio",
#     "operation": None,
#     "column_2": None,
#     "label": "Output/Input Ratio"
# }

# # 示例3: 计算并显示 'total_keys_in' 与 'unique_keys_in' 的比值
# Y_AXIS_CONFIG = {
#     "column_1": "total_keys_in",
#     "operation": "/",
#     "column_2": "unique_keys_in",
#     "label": "Ratio: Total Keys In / Unique Keys In"
# }

# # 示例4: 计算 'total_keys_in' 与 'total_keys_out' 的差值 (丢弃的key数量)
# Y_AXIS_CONFIG = {
#     "column_1": "total_keys_in",
#     "operation": "-",
#     "column_2": "total_keys_out",
#     "label": "Number of Discarded Keys (In - Out)"
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
        # --- 核心修改: 拼接成完整路径 ---
        absolute_path = os.path.join(base_dir, relative_path)
        
        if not os.path.exists(absolute_path):
            print(f"- 警告: 文件 '{absolute_path}' 不存在，已跳过。")
            continue

        try:
            df = pd.read_csv(absolute_path)

            if df.empty:
                print(f"- 警告: 文件 '{os.path.basename(absolute_path)}' 为空，已跳过。")
                continue

            x_values = df.index
            col1_data = df[y_config["column_1"]]
            
            if y_config["operation"] and y_config["column_2"]:
                col2_data = df[y_config["column_2"]]
                op = y_config["operation"]
                if op == '+': y_values = col1_data + col2_data
                elif op == '-': y_values = col1_data - col2_data
                elif op == '*': y_values = col1_data * col2_data
                elif op == '/': y_values = (col1_data / col2_data).replace([np.inf, -np.inf], 0)
                else:
                    print(f"- 警告: 不支持的操作 '{op}'，已跳过。")
                    continue
            else:
                y_values = col1_data

            base_filename = os.path.basename(absolute_path)
            line_label = os.path.splitext(base_filename)[0]
            ax.plot(x_values, y_values, marker='o', linestyle='-', label=line_label)

        except KeyError as e:
            print(f"- 警告: 文件 '{os.path.basename(absolute_path)}' 缺少必要的列 {e}，已跳过。")
        except Exception as e:
            print(f"- 错误: 处理文件 '{os.path.basename(absolute_path)}' 时发生未知错误: {e}")

    ax.set_title("L0 Compaction Analysis (Manual Selection)", fontsize=16)
    ax.set_xlabel("Group Index (组号)", fontsize=12)
    ax.set_ylabel(y_config["label"], fontsize=12)
    ax.grid(True, linestyle='--', alpha=0.6)
    
    if not ax.get_lines():
        print("\n没有成功绘制任何数据，无法生成图表。")
        return

    if len(ax.get_lines()) > 10:
        ax.legend(fontsize='small', bbox_to_anchor=(1.04, 1), loc="upper left")
        plt.tight_layout(rect=[0, 0, 0.85, 1])
    else:
        ax.legend()
        plt.tight_layout()

    output_filename = "compaction_analysis_plot_manual.png"
    # 将基础目录和文件名拼接成完整的输出路径
    full_output_path = os.path.join(base_dir, output_filename)

    plt.savefig(full_output_path, dpi=150)
    print(f"\n图表已成功保存为 '{output_filename}'")
    plt.show()


if __name__ == "__main__":
    create_plot_from_files(FILES_TO_PLOT, Y_AXIS_CONFIG, SCRIPT_DIRECTORY)