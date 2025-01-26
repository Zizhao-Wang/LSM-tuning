import os
import re

# 输出当前工作目录，确认脚本运行的环境
print("当前工作目录：", os.getcwd())

# 定义目录和文件列表
directories = [
    "./10B_leveldb_tuning_structure(f_c0)_experiments_C010",
    "./10B_leveldb_tuning_structure(f_c0)_experiments_C0100",
    "./10B_leveldb_tuning_structure(f_c0)_experiments_C0500",
    "./10B_leveldb_tuning_structure(f_c0)_experiments_C01000",
    "./10B_leveldb_tuning_structure(f_c0)_experiments_C02000",
    "./10B_leveldb_tuning_structure(f_c0)_experiments_C05000"
]

# 文件名模式（可以调整为你的实际文件命名规则）
file_pattern = "leveldb_1B_val_128_mem64MB_zipf"

# 正则表达式提取WriteAmplification数据
pattern = re.compile(r"WriteAmplification: (\d+\.\d+)")

# 保存结果的txt文件
output_file = "write_amplification_results.txt"

# 打开文件准备写入
with open(output_file, 'w') as output:
    # 遍历每个目录
    for directory in directories:
        if os.path.exists(directory):  # 检查目录是否存在
            print(f"正在处理目录：{directory}")
            # 获取目录中的所有文件
            for filename in os.listdir(directory):
                # 如果文件名匹配指定的模式
                if filename.startswith(file_pattern):
                    file_path = os.path.join(directory, filename)
                    
                    # 打开文件读取内容
                    with open(file_path, 'r') as file:
                        lines = file.readlines()
                    
                    # 找到最后一行中符合正则的WriteAmplification数据
                    last_match = None
                    for line in reversed(lines):
                        match = pattern.search(line)
                        if match:
                            last_match = match
                            break
                    
                    # 如果找到了WriteAmplification数据，写入结果
                    if last_match:
                        write_amplification = last_match.group(1)
                        output.write(f"{filename}\t{write_amplification}\n")
                    else:
                        # 如果没有找到WriteAmplification数据，记录为Nodata
                        output.write(f"{filename}\tNodata\n")
        else:
            print(f"目录不存在: {directory}")

print("WriteAmplification数据提取完毕，结果已保存到write_amplification_results.txt")
