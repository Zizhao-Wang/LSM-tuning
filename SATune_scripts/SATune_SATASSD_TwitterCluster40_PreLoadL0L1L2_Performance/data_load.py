import os
import re
import csv
import sys

# --- 配置 ---
# 1. 将你的日志文件放在这个文件夹内
LOG_DIRECTORY = "/home/jeff-wang/LSM-tuning/SATune_separate_dirs_scripts/SATune_SATASSD_TwitterCluster40_Load_Separation"
# --- 配置结束 ---

def process_logs_to_individual_csvs(log_dir):
    """
    解析指定目录下的每个日志文件，为每个文件生成一个独立的、
    与之同名的CSV文件来存储提取到的L0压缩统计信息。
    """
    # CSV 文件的表头 (保持不变)
    headers = [
        "total_keys_in",
        "unique_keys_in",
        "total_keys_out",
        "num_l0_files",
        "num_l1_files_in",
        "avg_unique_keys_per_file",
        "output_input_ratio"
    ]

    # 检查日志目录是否存在
    if not os.path.isdir(log_dir):
        print(f"错误: 日志目录 '{log_dir}' 不存在。")
        print("请创建一个名为 'logs' 的文件夹，并将您的日志文件放入其中。")
        sys.exit(1)

    print(f"开始处理 '{log_dir}' 目录中的日志文件...")

    # 正则表达式 (保持不变)
    stat_block_regex = re.compile(
        r"L0 Compaction (?:Tuning )?Stats.*?"  # 匹配标题行, "Tuning"是可选的
        r"Total keys in:\s*(\d+)"
        r".*?"  # 匹配到下一个关键字段前的任何内容
        r"Unique keys in:\s*(\d+)"
        r".*?"
        r"Total keys out:\s*(\d+)"
        r".*?"
        r"Number of L0 files:\s*(\d+)"
        r".*?"
        r"Number of L1 files \(overlap\):\s*(\d+)"
        r".*?"
        r"Average unique keys per file:\s*([\d.]+)"
        r".*?"
        r"Output/Input ratio:\s*([\d.]+)",
        re.DOTALL
    )

    processed_files_count = 0
    # 遍历目录中的所有文件
    for filename in os.listdir(log_dir):
        # 根据您的截图，文件可能是 .log 或 .txt 结尾，我们都处理
        if filename.endswith((".log", ".txt")):
            filepath = os.path.join(log_dir, filename)
            print(f"\n[+] 正在处理文件: {filename}")
            
            try:
                with open(filepath, 'r', encoding='utf-8') as f:
                    content = f.read()
                    
                # 查找所有匹配的数据块
                found_matches = stat_block_regex.findall(content)
                
                # 如果找到了数据，就为这个文件创建一个对应的CSV
                if found_matches:
                    processed_files_count += 1
                    
                    # --- 核心修改：生成新的CSV文件名 ---
                    # 1. 分离文件名和扩展名
                    base_name, _ = os.path.splitext(filename)
                    # 2. 创建新的 .csv 文件名
                    output_csv_filename = f"{base_name}.csv"
                    # 3. 完整的输出路径 (保存在logs文件夹内)
                    output_csv_path = os.path.join(log_dir, output_csv_filename)

                    # 将数据写入新的CSV文件
                    with open(output_csv_path, 'w', newline='', encoding='utf-8') as csv_file:
                        writer = csv.writer(csv_file)
                        writer.writerow(headers)
                        writer.writerows(found_matches)
                    
                    print(f"  -> 成功找到 {len(found_matches)} 组数据，已保存到: {output_csv_filename}")

                else:
                    print(f"  -> 未在此文件中找到任何 L0 统计数据。")

            except Exception as e:
                print(f"  -> 处理文件 {filename} 时发生错误: {e}")

    print(f"\n处理完成。共为 {processed_files_count} 个日志文件生成了对应的CSV。")


if __name__ == "__main__":
    process_logs_to_individual_csvs(LOG_DIRECTORY)