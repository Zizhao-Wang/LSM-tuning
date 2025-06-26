import os
import re
import pandas as pd

# ==============================================================================
# --- 配置区: 请在这里修改 ---
# ==============================================================================

# 自动获取当前脚本所在的目录，作为所有相对路径的前缀
SCRIPT_DIRECTORY = os.path.dirname(os.path.realpath(__file__))

# --- 1. 文件选择配置 ---
# 在这个列表中手动指定您想要处理的一个或多个日志文件的【相对路径】。
FILES_TO_PARSE = [
    "Compaction_2B_key44_val155_Cluster40_mem64M_L1100_CT04_F5_Switch3_2F30_L1-6_on_1.92TDisk.log",
    # "logs/another_test_run.log", # 您可以按需添加更多文件
]

# --- 2. 指定您想分析的 Compaction Level ---
FROM_LEVEL = 1
TO_LEVEL = 2

# ==============================================================================
# --- 核心代码: 通常无需修改 ---
# ==============================================================================

def parse_compaction_logs(files_to_parse, from_level, to_level, base_dir):
    """
    根据手动指定的文件列表和Level，提取Compaction Summary信息，
    并保存到新的CSV文件中。
    """
    headers = [
        "timestamp", "thread_id", "from_level", "to_level", 
        "read_files", "read_bytes", "write_files", "write_bytes", "duration_micros"
    ]
    all_extracted_data = []

    # --- 最终版、最灵活的正则表达式 ---
    # 使用 .*? 来灵活跳过关键词之间的任何字符，不再关心分隔符'|'
    compaction_regex = re.compile(
        r"([\d/.:-]+)\s+"                            # 1. Timestamp (修正字符类)
        r"(\d+)\s+"                                  # 2. Thread ID
        r"\[Compaction Summary\]\s+"                 # 匹配 "[Compaction Summary]" 
        r"L(\d+)->L(\d+)\s*\|\s*"                    # 3, 4. Levels，匹配 "|" 分隔符
        r"Read:\s*(\d+)\s*files\s*\((\d+)\s*bytes\)\s*\|\s*"  # 5, 6. Read stats
        r"Write:\s*(\d+)\s*files\s*\((\d+)\s*bytes\)\s*\|\s*" # 7, 8. Write stats
        r"Time:\s*(\d+)\s*micros"                    # 9. Time
    )

    print(f"开始处理指定文件，目标 Level: L{from_level}->L{to_level}")
    print(f"脚本根目录: {base_dir}")

    for relative_path in files_to_parse:
        # 将相对路径和脚本根目录拼接成绝对路径
        absolute_path = os.path.join(base_dir, relative_path)

        if not os.path.exists(absolute_path):
            print(f"- 警告: 文件 '{absolute_path}' 不存在，已跳过。")
            continue

        print(f"  -> 正在扫描文件: {relative_path}")
        
        try:
            with open(absolute_path, 'r', encoding='utf-8') as f:
                for line in f:
                    match = compaction_regex.match(line)
                    if not match:
                        continue

                    (timestamp, thread_id, src_lvl, dst_lvl, read_files, 
                     read_bytes, write_files, write_bytes, duration) = match.groups()

                    if int(src_lvl) == from_level and int(dst_lvl) == to_level:
                        all_extracted_data.append([
                            timestamp, thread_id, int(src_lvl), int(dst_lvl),
                            int(read_files), int(read_bytes), int(write_files), 
                            int(write_bytes), int(duration)
                        ])
        except Exception as e:
            print(f"处理文件 {os.path.basename(absolute_path)} 时发生错误: {e}")

    if not all_extracted_data:
        print("\n处理完成，但未在指定文件中找到符合条件的 Compaction 日志。")
        return
        
    df = pd.DataFrame(all_extracted_data, columns=headers)
    
    # 根据指定的 Level 生成输出文件名
    output_filename = f"compaction_summary_L{from_level}_to_L{to_level}.csv"
    # 将输出文件也保存在脚本所在的目录下
    full_output_path = os.path.join(base_dir, output_filename)
    
    try:
        df.to_csv(full_output_path, index=False)
        print(f"\n成功！共提取到 {len(df)} 条数据。")
        print(f"数据已保存到文件: '{full_output_path}'")
    except IOError as e:
        print(f"\n错误：无法写入CSV文件。请检查权限。({e})")


if __name__ == "__main__":
    parse_compaction_logs(FILES_TO_PARSE, FROM_LEVEL, TO_LEVEL, SCRIPT_DIRECTORY)

