import csv
import random

# 读取原始 CSV 文件中的所有 Key
input_filename = 'uniform_read_keys16_value128_1B.csv'
output_filename = 'random_selected_keys.csv'
num_keys_to_select = 100  # 你可以设置选择多少个 key

# 打开输入文件并读取 Key
with open(input_filename, mode='r') as infile:
    csv_reader = csv.reader(infile)
    headers = next(csv_reader)  # 假设第一行是 header
    all_keys = [row[0] for row in csv_reader]  # 假设 Key 在第一列

# 随机选择一定数量的 key
selected_keys = random.sample(all_keys, num_keys_to_select)

# 写入到新的 CSV 文件
with open(output_filename, mode='w', newline='') as outfile:
    csv_writer = csv.writer(outfile)
    csv_writer.writerow(['Key'])  # 写入表头
    for key in selected_keys:
        csv_writer.writerow([key])

print(f"{num_keys_to_select} keys have been randomly selected and written to {output_filename}")
