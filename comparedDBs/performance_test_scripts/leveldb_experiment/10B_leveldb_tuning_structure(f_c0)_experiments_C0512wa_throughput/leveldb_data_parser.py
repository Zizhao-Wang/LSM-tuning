import re
import csv

def parse_leveldb_log(log_file_path, output_csv_path):
    """
    解析LevelDB日志文件，提取throughput和WriteAmplification数据
    
    Args:
        log_file_path: 输入的日志文件路径
        output_csv_path: 输出的CSV文件路径
    """
    
    results = []
    entry_number = 1
    
    with open(log_file_path, 'r', encoding='utf-8') as file:
        content = file.read()
    
    # 分割日志内容，寻找每个时间戳开始的块
    # 匹配格式：2025-09-20 01:03:45 ... thread 0: (10000000,10000000) ops and (197001.7,197001.7) ops/second
    timestamp_pattern = r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) \.\.\. thread 0: \([^)]+\) ops and \([^,]+,([0-9.]+)\) ops/second'
    
    # 匹配WriteAmplification行
    wa_pattern = r'user_io:[^W]+WriteAmplification: ([0-9.]+)'
    
    # 找到所有时间戳行
    timestamp_matches = re.finditer(timestamp_pattern, content)
    
    # 先转换为列表以便调试
    timestamp_matches_list = list(re.finditer(timestamp_pattern, content))
    print(f"找到 {len(timestamp_matches_list)} 个时间戳匹配")
    
    # 如果没有找到匹配，尝试更简单的模式进行调试
    if len(timestamp_matches_list) == 0:
        print("调试：尝试寻找时间戳行...")
        simple_timestamp_pattern = r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) \.\.\. thread 0:'
        simple_matches = re.findall(simple_timestamp_pattern, content)
        print(f"找到 {len(simple_matches)} 个简单时间戳匹配")
        
        # 寻找 ops/second 行
        ops_pattern = r'ops and \([^)]+\) ops/second'
        ops_matches = re.findall(ops_pattern, content)
        print(f"找到 {len(ops_matches)} 个 ops/second 匹配")
        
        # 寻找WriteAmplification行
        wa_matches = re.findall(wa_pattern, content)
        print(f"找到 {len(wa_matches)} 个 WriteAmplification 匹配")
        
        # 显示前几行内容以便调试
        lines = content.split('\n')[:20]
        print("\n前20行内容:")
        for i, line in enumerate(lines):
            print(f"{i+1}: {line}")
    
    for match in timestamp_matches_list:
        timestamp = match.group(1)
        throughput = float(match.group(2))  # 第二个数值（累积吞吐量）
        
        # 从当前匹配位置开始，向后查找下一个WriteAmplification
        start_pos = match.end()
        
        # 在当前匹配后面寻找WriteAmplification
        remaining_content = content[start_pos:]
        wa_match = re.search(wa_pattern, remaining_content)
        
        if wa_match:
            write_amplification = float(wa_match.group(1))
            
            # 添加到结果列表
            results.append([entry_number, throughput, write_amplification])
            entry_number += 1
            
            print(f"Entry {entry_number-1}: Throughput={throughput:.1f}, WriteAmplification={write_amplification:.4f}")
    
    # 写入TSV文件（使用制表符分隔）
    with open(output_csv_path, 'w', newline='', encoding='utf-8') as tsvfile:
        writer = csv.writer(tsvfile, delimiter='\t')
        # 写入表头
        writer.writerow(['Entry_Number', 'Throughput_ops_per_sec', 'WriteAmplification'])
        # 写入数据
        writer.writerows(results)
    
    print(f"\n解析完成！共提取了 {len(results)} 组数据")
    print(f"结果已保存到: {output_csv_path}")
    
    return results

def preview_results(results, num_rows=5):
    """
    预览提取的结果
    """
    print(f"\n前 {min(num_rows, len(results))} 行数据预览:")
    print("编号\t吞吐量(ops/sec)\t写放大")
    print("-" * 40)
    for i in range(min(num_rows, len(results))):
        entry_num, throughput, wa = results[i]
        print(f"{entry_num}\t{throughput:.1f}\t\t{wa:.4f}")

# 使用示例
if __name__ == "__main__":
    import os
    
    # 获取当前脚本所在的目录
    current_dir = os.path.dirname(os.path.abspath(__file__))
    
    # 设置输入和输出文件路径（都在当前脚本目录下）
    input_log_file = os.path.join(current_dir, "leveldb_1B_val_128_mem64MB_zipf1.1_factor10_level1base512MiB.log")  # 输入日志文件
    output_csv_file = os.path.join(current_dir, "leveldb_performance_data.csv")  # 输出CSV文件
    
    print(f"当前工作目录: {current_dir}")
    print(f"输入文件路径: {input_log_file}")
    print(f"输出文件路径: {output_csv_file}")
    print("-" * 50)
    
    try:
        # 解析日志文件
        results = parse_leveldb_log(input_log_file, output_csv_file)
        
        # 预览结果
        if results:
            preview_results(results)
            
            # 显示统计信息
            throughputs = [r[1] for r in results]
            write_amps = [r[2] for r in results]
            
            print(f"\n统计信息:")
            print(f"吞吐量范围: {min(throughputs):.1f} - {max(throughputs):.1f} ops/sec")
            print(f"写放大范围: {min(write_amps):.4f} - {max(write_amps):.4f}")
            print(f"平均吞吐量: {sum(throughputs)/len(throughputs):.1f} ops/sec")
            print(f"平均写放大: {sum(write_amps)/len(write_amps):.4f}")
        else:
            print("未找到匹配的数据，请检查日志文件格式")
            
    except FileNotFoundError:
        print(f"错误: 找不到文件 '{input_log_file}'")
        print("请确保在脚本同目录下放置名为 'leveldb_log.txt' 的日志文件")
    except Exception as e:
        print(f"处理过程中出现错误: {e}")