import os
import re
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

def analyze_write_amplification(directory):
    """
    遍历指定目录下的日志文件，提取文件名中的参数和文件中最后一次出现的写放大值，
    然后对数据进行聚合和可视化。
    """
    
    # --- 1. 定义正则表达式 (无变化) ---
    filename_pattern = re.compile(r".*_CT(\d+)_level1base(\d+)_.*\.log")
    wa_pattern = re.compile(r"WriteAmplification:\s*([\d.]+)")

    # --- 2. 遍历目录并提取数据 ---
    
    extracted_data = []
    print(f"正在扫描目录: {directory}")

    if not os.path.isdir(directory):
        print(f"错误：目录不存在 -> {directory}")
        return

    for filename in os.listdir(directory):
        if filename.startswith("SATune_PreLoad_0.2B") and filename.endswith(".log"):
            filename_match = filename_pattern.match(filename)
            if not filename_match:
                print(f"  - 警告: 文件名格式不匹配，已跳过 -> {filename}")
                continue

            ct0_val = int(filename_match.group(1))
            l1_val = int(filename_match.group(2))
            full_path = os.path.join(directory, filename)
            
            # ***--- 主要修改点：从查找“最后一行”改为查找“最后匹配行” ---***
            last_matching_line = None # 用于存储最后一次匹配到的行
            try:
                with open(full_path, 'r', encoding='utf-8') as f:
                    # 逐行读取文件
                    for line in f:
                        # 如果当前行包含 "WriteAmplification"，就更新我们的变量
                        if "WriteAmplification" in line:
                            last_matching_line = line
                
                # 在整个文件读取完毕后，处理我们找到的最后匹配行
                if last_matching_line:
                    wa_match = wa_pattern.search(last_matching_line)
                    if wa_match:
                        write_amp = float(wa_match.group(1))
                        extracted_data.append({
                            "CT0": ct0_val,
                            "L1": l1_val,
                            "WriteAmplification": write_amp
                        })
                    else:
                        # 这种情况很少见，即行里有 "WriteAmplification" 但正则匹配失败
                        print(f"  - 警告: 找到包含WriteAmplification的行但正则解析失败 -> {filename}")
                else:
                    # 如果遍历完整个文件都没找到，则发出警告
                    print(f"  - 警告: 文件中未找到任何包含'WriteAmplification'的行，已跳过 -> {filename}")

            except Exception as e:
                print(f"处理文件时发生错误 -> {filename}, 错误: {e}")

    if not extracted_data:
        print("未找到任何有效数据，程序退出。")
        return

    # --- 3. 使用 Pandas 进行数据聚合和排序 (无变化) ---
    df = pd.DataFrame(extracted_data)
    df_sorted = df.sort_values(by=['CT0', 'L1'])
    print("\n--- 数据提取与整理结果 ---")
    for ct0_group, group_data in df_sorted.groupby('CT0'):
        print(f"\nCT0 = {ct0_group}:")
        print(group_data[['L1', 'WriteAmplification']].to_string(index=False))
    print("\n--------------------------\n")

    # --- 4. 使用 Matplotlib 和 Seaborn 进行数据可视化 (无变化) ---
    print("正在生成图表...")
    sns.set_theme(style="whitegrid")
    plt.figure(figsize=(12, 7))
    sns.lineplot(data=df_sorted, x='L1', y='WriteAmplification', hue='CT0', marker='o', palette='viridis', legend='full')
    plt.title('Write Amplification vs. L1 Value (Grouped by CT0)', fontsize=16)
    plt.xlabel('L1 Value', fontsize=12)
    plt.ylabel('Write Amplification', fontsize=12)
    plt.grid(True, which='both', linestyle='--')
    plt.tight_layout() # 建议在 savefig 之前调用

    # --- 这是修改后的保存逻辑 ---

    # 1. 定义您想要的图片文件名
    output_filename = "write_amplification_analysis.png"

    # 2. 获取当前正在运行的这个脚本文件所在的目录
    #    __file__ 是一个特殊的Python变量，代表当前脚本的文件路径
    script_directory = os.path.dirname(os.path.abspath(__file__))

    # 3. 将目录路径和文件名拼接成一个完整的、绝对的保存路径
    full_save_path = os.path.join(script_directory, output_filename)

    # 4. 使用这个完整路径来保存图片
    try:
        plt.savefig(full_save_path, dpi=300)
        print(f"图表已成功保存到: {full_save_path}")
    except Exception as e:
        print(f"保存图片时发生错误: {e}")

    plt.close() # 关闭图形，释放内存

if __name__ == '__main__':
    target_directory = os.path.dirname(os.path.abspath(__file__)) # <--- 请修改为您的实际路径
    analyze_write_amplification(target_directory)