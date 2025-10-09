#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <random>

#include <gflags/gflags.h> 


#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/write_batch.h"
#include "rocksdb/slice.h"
#include "rocksdb/statistics.h"
#include "rocksdb/table.h"
#include "rocksdb/table_properties.h" // 新增头文件以提供完整的表定义
#include "rocksdb/filter_policy.h"


DEFINE_string(data_file_path, "", "Path to the CSV data file.");
DEFINE_int64(num, 1000000, "Number of key-value pairs to write.");
DEFINE_int32(key_size, 20, "Size of the key in bytes.");
DEFINE_int32(value_size, 100, "Size of the value in bytes.");
DEFINE_int32(entries_per_batch, 1000, "Number of entries per write batch.");
DEFINE_string(db, "", "Use the db with the following name.");
DEFINE_int64(stats_interval_ops, 100000, "Interval in keys for printing statistics. Set to 0 to disable.");
DEFINE_int32(bits_per_key, 10, "Bits per key for Bloom filter."); // 新增 bits_per_key 参数
DEFINE_bool(sync, false, "Sync all writes to disk");
// 在 main 函数中添加调用选项
// 可以通过添加一个新的 flag 来选择运行哪个函数
DEFINE_string(workload, "fill", "Workload type: 'fill' or 'mixed'");

// 首先添加新的 gflags
DEFINE_double(read_ratio, 0.5, "Read ratio (0.0 to 1.0)");
DEFINE_double(write_ratio, 0.5, "Write ratio (0.0 to 1.0)");
DEFINE_bool(verify_reads, false, "Verify read values against expected values");

void mixedWorkload(rocksdb::DB* db) {
  if (FLAGS_data_file_path.empty()) {
    fprintf(stderr, "Error: You must specify --data_file_path\n");
    return;
  }

  std::ifstream csv_file(FLAGS_data_file_path);
  if (!csv_file.is_open()) {
    fprintf(stderr, "Error: Unable to open file: %s\n", FLAGS_data_file_path.c_str());
    return;
  }

  // 首先加载所有的 keys 到内存中
  std::vector<uint64_t> keys;
  std::string line;
  std::getline(csv_file, line); // 跳过标题行

  fprintf(stderr, "Loading keys from CSV file...\n");
  for (int64_t i = 0; i < FLAGS_num+1000 && std::getline(csv_file, line); ++i) {
    std::stringstream line_stream(line);
    std::string cell;
    std::vector<std::string> row_data;
    
    while (getline(line_stream, cell, ',')) {
      row_data.push_back(cell);
    }

    if (row_data.size() >= 2) {
      keys.push_back(std::stoull(row_data[1]));
    }
  }
  csv_file.close();

  if (keys.empty()) {
    fprintf(stderr, "Error: No valid keys found in CSV file\n");
    return;
  }

  fprintf(stderr, "Loaded %zu keys from CSV file\n", keys.size());

  // 计算读写操作数量
  const int64_t total_ops = FLAGS_num;
  const int64_t read_ops = static_cast<int64_t>(total_ops * FLAGS_read_ratio);
  const int64_t write_ops = static_cast<int64_t>(total_ops * FLAGS_write_ratio);

  fprintf(stderr, "\n--- Mixed Workload Configuration ---\n");
  fprintf(stderr, "Total operations:  %ld\n", total_ops);
  fprintf(stderr, "Read operations:   %ld (%.1f%%)\n", read_ops, FLAGS_read_ratio * 100);
  fprintf(stderr, "Write operations:  %ld (%.1f%%)\n", write_ops, FLAGS_write_ratio * 100);
  fprintf(stderr, "Key size:          %d\n", FLAGS_key_size);
  fprintf(stderr, "Value size:        %d\n", FLAGS_value_size);
  fprintf(stderr, "------------------------------------\n\n");

  // 随机数生成器
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> key_dist(0, keys.size() - 1);
  std::uniform_real_distribution<double> op_dist(0.0, 1.0);

  // 统计变量
  int64_t num_reads = 0;
  int64_t num_writes = 0;
  int64_t num_found = 0;
  int64_t num_not_found = 0;
  int64_t num_read_errors = 0;
  int64_t num_write_errors = 0;

  rocksdb::WriteOptions write_options;
  rocksdb::ReadOptions read_options;

  if (FLAGS_sync) {
    write_options.sync = true;
  }

  auto start_time = std::chrono::high_resolution_clock::now();
  auto last_report_time = start_time;
  int64_t ops_at_last_report = 0;

  // 用于分别计算读写性能
  auto read_start_time = std::chrono::high_resolution_clock::now();
  auto write_start_time = std::chrono::high_resolution_clock::now();
  double total_read_time = 0.0;
  double total_write_time = 0.0;

  // 执行混合工作负载
  for (int64_t i = 0; i < total_ops; i++) {
    // 随机选择一个 key
    size_t key_idx = key_dist(gen);
    uint64_t key_num = keys[key_idx];
    
    // 格式化 key
    char key_buffer[100];
    snprintf(key_buffer, sizeof(key_buffer), "%0*llu", FLAGS_key_size, (unsigned long long)key_num);

    // 根据读写比例决定操作类型
    bool is_read = (op_dist(gen) < FLAGS_read_ratio);

    if (is_read) {
      // 执行读操作
      auto read_op_start = std::chrono::high_resolution_clock::now();
      std::string value;
      rocksdb::Status s = db->Get(read_options, key_buffer, &value);
      auto read_op_end = std::chrono::high_resolution_clock::now();
      total_read_time += std::chrono::duration<double>(read_op_end - read_op_start).count();
      
      if (s.ok()) {
        num_found++;
        
        // 如果启用了验证，检查值是否正确
        if (FLAGS_verify_reads) {
          std::string expected_value(FLAGS_value_size, 'a');
          if (value != expected_value) {
            fprintf(stderr, "Warning: Value mismatch for key %s\n", key_buffer);
          }
        }
      } else if (s.IsNotFound()) {
        num_not_found++;
      } else {
        num_read_errors++;
        fprintf(stderr, "Read error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_reads++;
    } else {
      // 执行写操作
      auto write_op_start = std::chrono::high_resolution_clock::now();
      std::string value(FLAGS_value_size, 'a');
      rocksdb::Status s = db->Put(write_options, key_buffer, value);
      auto write_op_end = std::chrono::high_resolution_clock::now();
      total_write_time += std::chrono::duration<double>(write_op_end - write_op_start).count();
      
      if (!s.ok()) {
        num_write_errors++;
        fprintf(stderr, "Write error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_writes++;
    }

    // 定期报告进度
    if (FLAGS_stats_interval_ops > 0 && 
        (i + 1 - ops_at_last_report >= FLAGS_stats_interval_ops)) {
      auto current_time = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> report_duration = current_time - last_report_time;
      
      const int64_t ops_in_interval = (i + 1) - ops_at_last_report;
      const double throughput = (report_duration.count() > 0) ? 
                               (ops_in_interval / report_duration.count()) : 0;
      const double total_elapsed_time = 
          std::chrono::duration<double>(current_time - start_time).count();

      fprintf(stdout, "[%.1fs] Ops: %ld (R:%ld/W:%ld), Throughput: %.2f ops/sec\n",
              total_elapsed_time, i + 1, num_reads, num_writes, throughput);

      last_report_time = current_time;
      ops_at_last_report = i + 1;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  // 打印最终统计信息
  fprintf(stdout, "\n--- Mixed Workload Summary ---\n");
  fprintf(stdout, "Total operations:     %ld\n", total_ops);
  fprintf(stdout, "Read operations:      %ld (%.1f%%)\n", 
          num_reads, (num_reads * 100.0) / total_ops);
  fprintf(stdout, "Write operations:     %ld (%.1f%%)\n", 
          num_writes, (num_writes * 100.0) / total_ops);
  fprintf(stdout, "\nRead Results:\n");
  fprintf(stdout, "  Keys found:         %ld (%.1f%%)\n", 
          num_found, (num_found * 100.0) / std::max(num_reads, 1L));
  fprintf(stdout, "  Keys not found:     %ld (%.1f%%)\n", 
          num_not_found, (num_not_found * 100.0) / std::max(num_reads, 1L));
  fprintf(stdout, "  Read errors:        %ld\n", num_read_errors);
  fprintf(stdout, "\nWrite Results:\n");
  fprintf(stdout, "  Write errors:       %ld\n", num_write_errors);
  fprintf(stdout, "\nPerformance:\n");
  fprintf(stdout, "  Total time:         %.2f seconds\n", duration.count());
  fprintf(stdout, "  Total throughput:   %.2f ops/sec\n", total_ops / duration.count());
  fprintf(stdout, "  Read performance:\n");
  if (num_reads > 0) {
    fprintf(stdout, "    - Read throughput:    %.2f ops/sec\n", num_reads / total_read_time);
    fprintf(stdout, "    - Avg read latency:   %.2f us\n", (total_read_time / num_reads) * 1000000);
  }
  fprintf(stdout, "  Write performance:\n");
  if (num_writes > 0) {
    fprintf(stdout, "    - Write throughput:   %.2f ops/sec\n", num_writes / total_write_time);
    fprintf(stdout, "    - Avg write latency:  %.2f us\n", (total_write_time / num_writes) * 1000000);
  }
  fprintf(stdout, "------------------------------\n");
}



void ExecuteWorkload(rocksdb::DB* db) {
  if (FLAGS_data_file_path.empty()) {
    fprintf(stderr, "Error: You must specify --data_file_path\n");
    return;
  }

  std::ifstream csv_file(FLAGS_data_file_path);
  if (!csv_file.is_open()) {
    fprintf(stderr, "Error: Unable to open file: %s\n", FLAGS_data_file_path.c_str());
    return;
  }

  // 首先加载所有的 keys 到内存中
  std::vector<uint64_t> keys;
  std::vector<std::string> ops_type;
  std::string line;
  std::getline(csv_file, line); // 跳过标题行

  fprintf(stderr, "Loading keys from CSV file...\n");
  for (int64_t i = 0; i < FLAGS_num+1000 && std::getline(csv_file, line); ++i) {
    std::stringstream line_stream(line);
    std::string cell;
    std::vector<std::string> row_data;
    
    while (getline(line_stream, cell, ',')) {
      row_data.push_back(cell);
    }

    if (row_data.size() >= 2) {
      keys.push_back(std::stoull(row_data[1]));
    }
    ops_type.push_back(row_data[5]);
  }
  csv_file.close();

  if (keys.empty()) {
    fprintf(stderr, "Error: No valid keys found in CSV file\n");
    return;
  }

  fprintf(stderr, "Loaded %zu keys from CSV file\n", keys.size());

  // 计算读写操作数量
  const int64_t total_ops = FLAGS_num;
  const int64_t read_ops = static_cast<int64_t>(total_ops * FLAGS_read_ratio);
  const int64_t write_ops = static_cast<int64_t>(total_ops * FLAGS_write_ratio);

  fprintf(stderr, "\n--- Mixed Workload Configuration ---\n");
  fprintf(stderr, "Total operations:  %ld\n", total_ops);
  fprintf(stderr, "Read operations:   %ld (%.1f%%)\n", read_ops, FLAGS_read_ratio * 100);
  fprintf(stderr, "Write operations:  %ld (%.1f%%)\n", write_ops, FLAGS_write_ratio * 100);
  fprintf(stderr, "Key size:          %d\n", FLAGS_key_size);
  fprintf(stderr, "Value size:        %d\n", FLAGS_value_size);
  fprintf(stderr, "------------------------------------\n\n");


  // 统计变量
  int64_t num_reads = 0;
  int64_t num_writes = 0;
  int64_t num_found = 0;
  int64_t num_not_found = 0;
  int64_t num_read_errors = 0;
  int64_t num_write_errors = 0;

  rocksdb::WriteOptions write_options;
  rocksdb::ReadOptions read_options;

  auto start_time = std::chrono::high_resolution_clock::now();
  auto last_report_time = start_time;
  int64_t ops_at_last_report = 0;

  // 用于分别计算读写性能
  auto read_start_time = std::chrono::high_resolution_clock::now();
  auto write_start_time = std::chrono::high_resolution_clock::now();
  double total_read_time = 0.0;
  double total_write_time = 0.0;
  
  double total_operation_time = 0.0;
  int64_t total_operations_executed = 0;

  if (FLAGS_sync) {
    write_options.sync = true;
  }

  // 执行混合工作负载
  for (int64_t i = 0; i < total_ops; i++) {
    // 随机选择一个 key
    uint64_t key_num = keys[i];
    
    // 格式化 key
    char key_buffer[100];
    snprintf(key_buffer, sizeof(key_buffer), "%0*llu", FLAGS_key_size, (unsigned long long)key_num);

    if (ops_type[i]=="get"||ops_type[i]=="gets") {
      // 执行读操作
      auto read_op_start = std::chrono::high_resolution_clock::now();
      std::string value;
      rocksdb::Status s = db->Get(read_options, key_buffer, &value);
      auto read_op_end = std::chrono::high_resolution_clock::now();

      double op_time = std::chrono::duration<double>(read_op_end - read_op_start).count();
      total_read_time += op_time;
      total_operation_time += op_time;  // 添加到总体时间
      total_operations_executed++;
      
      if (s.ok()) {
        num_found++;
        
        // 如果启用了验证，检查值是否正确
        if (FLAGS_verify_reads) {
          std::string expected_value(FLAGS_value_size, 'a');
          if (value != expected_value) {
            fprintf(stderr, "Warning: Value mismatch for key %s\n", key_buffer);
          }
        }
      } else if (s.IsNotFound()) {
        num_not_found++;
      } else {
        num_read_errors++;
        fprintf(stderr, "Read error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_reads++;
    } else if(ops_type[i]=="add"||ops_type[i]=="set") {
      // 执行写操作
      auto write_op_start = std::chrono::high_resolution_clock::now();
      std::string value(FLAGS_value_size, 'a');
      rocksdb::Status s = db->Put(write_options, key_buffer, value);
      auto write_op_end = std::chrono::high_resolution_clock::now();
      total_write_time += std::chrono::duration<double>(write_op_end - write_op_start).count();
      
      if (!s.ok()) {
        num_write_errors++;
        fprintf(stderr, "Write error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_writes++;
    }else{}

    // 定期报告进度
    if (FLAGS_stats_interval_ops > 0 && 
        (i + 1 - ops_at_last_report >= FLAGS_stats_interval_ops)) {
      auto current_time = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> report_duration = current_time - last_report_time;
      
      const int64_t ops_in_interval = (i + 1) - ops_at_last_report;
      const double throughput = (report_duration.count() > 0) ? 
                               (ops_in_interval / report_duration.count()) : 0;
      const double total_elapsed_time = 
          std::chrono::duration<double>(current_time - start_time).count();

      fprintf(stdout, "[%.1fs] Ops: %ld (R:%ld/W:%ld), Throughput: %.2f ops/sec\n",
              total_elapsed_time, i + 1, num_reads, num_writes, throughput);

      last_report_time = current_time;
      ops_at_last_report = i + 1;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  double overall_avg_latency = (total_operations_executed > 0) ? 
                              (total_operation_time / total_operations_executed) * 1000000 : 0;

  // 打印最终统计信息
  // 打印最终统计信息
  fprintf(stdout, "\n--- Performance of Workload Summary ---\n");
  fprintf(stdout, "Total operations:     %ld\n", total_ops);
  fprintf(stdout, "Read operations:      %ld (%.1f%%)\n", 
          num_reads, (num_reads * 100.0) / total_ops);
  fprintf(stdout, "Write operations:     %ld (%.1f%%)\n", 
          num_writes, (num_writes * 100.0) / total_ops);
  fprintf(stdout, "\nRead Results:\n");
  fprintf(stdout, "  Keys found:         %ld (%.1f%%)\n", 
          num_found, (num_found * 100.0) / std::max(num_reads, 1L));
  fprintf(stdout, "  Keys not found:     %ld (%.1f%%)\n", 
          num_not_found, (num_not_found * 100.0) / std::max(num_reads, 1L));
  fprintf(stdout, "  Read errors:        %ld\n", num_read_errors);
  fprintf(stdout, "\nWrite Results:\n");
  fprintf(stdout, "  Write errors:       %ld\n", num_write_errors);
  fprintf(stdout, "\nPerformance:\n");
  fprintf(stdout, "  Total time:         %.2f seconds\n", duration.count());
  fprintf(stdout, "  Total throughput:   %.2f ops/sec\n", total_ops / duration.count());
  fprintf(stdout, "  Overall avg latency: %.2f us\n", overall_avg_latency);  // 新增总体平均延迟
  fprintf(stdout, "\n  Read performance:\n");
  if (num_reads > 0) {
    fprintf(stdout, "    - Read throughput:    %.2f ops/sec\n", num_reads / total_read_time);
    fprintf(stdout, "    - Avg read latency:   %.2f us\n", (total_read_time / num_reads) * 1000000);
  }
  fprintf(stdout, "  Write performance:\n");
  if (num_writes > 0) {
    fprintf(stdout, "    - Write throughput:   %.2f ops/sec\n", num_writes / total_write_time);
    fprintf(stdout, "    - Avg write latency:  %.2f us\n", (total_write_time / num_writes) * 1000000);
  }
  fprintf(stdout, "------------------------------\n");
}


void fillcluster(rocksdb::DB* db) {
  if (FLAGS_data_file_path.empty()) {
    fprintf(stderr, "Error: You must specify --data_file_path\n");
    return;
  }

  std::ifstream csv_file(FLAGS_data_file_path);
  if (!csv_file.is_open()) {
    fprintf(stderr, "Error: Unable to open file: %s\n", FLAGS_data_file_path.c_str());
    return;
  }

  rocksdb::WriteBatch batch;
  rocksdb::WriteOptions write_options;
  // write_options.sync = true; // 如果需要同步写入，可以打开此选项

  if (FLAGS_sync) {
    write_options.sync = true;
  }

  std::string line;
  std::getline(csv_file, line);// 读取并丢弃 CSV 文件的标题行 (如果存在)

  int64_t num_written = 0;
  int64_t num_written_at_last_report = 0;
  auto start_time = std::chrono::high_resolution_clock::now();
  auto last_report_time = start_time;

  fprintf(stderr, "num_: %ld entries_per_batch_:%d\n The key size:%d value size:%d \n"
    , FLAGS_num, FLAGS_entries_per_batch,FLAGS_key_size, FLAGS_value_size);
  for (int64_t i = 0; i < FLAGS_num; i += FLAGS_entries_per_batch) {
    batch.Clear();
    for (int64_t j = 0; j < FLAGS_entries_per_batch ; j++) {
      if (!std::getline(csv_file, line)) {
        fprintf(stderr, "Warning: Reached end of CSV file before writing %ld keys. Wrote %ld keys.\n", FLAGS_num, num_written);
        i = FLAGS_num; // 跳出外层循环
        break;
      }

      std::stringstream line_stream(line);
      std::string cell;
      std::vector<std::string> row_data;
      while (getline(line_stream, cell, ',')) {
        row_data.push_back(cell);
      }

      // 假设 key 在 CSV 的第二列 (index 1)
      if (row_data.size() < 2) {
        fprintf(stderr, "Warning: Skipping invalid CSV row: %s\n", line.c_str());
        continue;
      }

      // 格式化 Key
      char key_buffer[100];
      const uint64_t k = std::stoull(row_data[1]);
      snprintf(key_buffer, sizeof(key_buffer), "%0*llu", FLAGS_key_size, (unsigned long long)k);
      // 生成 Value
      std::string value_str(FLAGS_value_size, 'a');
      rocksdb::Slice val(value_str);

      batch.Put(key_buffer, val);
      num_written++;
    }

    if (batch.Count() > 0) {
      rocksdb::Status s = db->Write(write_options, &batch);
      if (!s.ok()) {
        fprintf(stderr, "FATAL: Batch write error: %s\n", s.ToString().c_str());
        exit(1);
      }
    }

    if (FLAGS_stats_interval_ops > 0 && (num_written - num_written_at_last_report >= FLAGS_stats_interval_ops)) {
      auto current_time = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> report_duration = current_time - last_report_time;
            
      const int64_t ops_in_interval = num_written - num_written_at_last_report;
      const double throughput = (report_duration.count() > 0) ? (ops_in_interval / report_duration.count()) : 0;
      const double total_elapsed_time = std::chrono::duration<double>(current_time - start_time).count();

      fprintf(stdout, "[%.1fs] ... Keys written: %ld, Interval Throughput: %.2f ops/sec\n",
                    total_elapsed_time, num_written, throughput);

      // 为下一个间隔重置
      last_report_time = current_time;
      num_written_at_last_report = num_written;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  fprintf(stdout, "\n--- LoadDataFromCSV Summary ---\n");
  fprintf(stdout, "Keys written:      %ld\n", num_written);
  fprintf(stdout, "Total time:        %.2f seconds\n", duration.count());
  fprintf(stdout, "Ops per second:    %.2f\n", num_written / duration.count());
  fprintf(stdout, "-------------------------------\n");

  csv_file.close();
}

// 新增函数：打印当前进程的内存使用情况 (适用于 Linux)
void printMemoryUsage() {
  std::ifstream status_file("/proc/self/status");
  if (!status_file.is_open()) {
    fprintf(stderr, "Warning: Could not open /proc/self/status to read memory usage.\n");
    return;
  }

  std::string line;
  fprintf(stdout, "\n--- Process Memory Usage ---\n");
  while (std::getline(status_file, line)) {
    // VmRSS: Resident Set Size - 物理内存使用量
    if (line.rfind("VmRSS:", 0) == 0) {
      fprintf(stdout, "%s\n", line.c_str());
      break; // 找到后即可退出
    }
  }
  fprintf(stdout, "----------------------------\n");
  status_file.close();
}

void printf_mem(rocksdb::DB* db){
  std::string block_cache_usage_str;
  if (db->GetProperty("rocksdb.block-cache-usage", &block_cache_usage_str)) {
    fprintf(stderr, "rocksdb.block-cache-usage COUNT : %s\n", block_cache_usage_str.c_str());
  } else {
    fprintf(stderr, "Failed to get rocksdb.block-cache-usage\n");
  }

  std::string block_cache_pinned_usage_str;
  if (db->GetProperty("rocksdb.block-cache-pinned-usage", &block_cache_pinned_usage_str)) {
    fprintf(stderr, "rocksdb.block-cache-pinned-usage COUNT : %s\n", block_cache_pinned_usage_str.c_str());
  } else {
    fprintf(stderr, "Failed to get rocksdb.block-cache-pinned-usage\n");
  }

  // --- 获取 TableReader 内存使用量估值 ---
  std::string estimate_table_readers_mem_str;
  if (db->GetProperty("rocksdb.estimate-table-readers-mem", &estimate_table_readers_mem_str)) {
    fprintf(stderr, "rocksdb.estimate-table-readers-mem COUNT : %s\n", estimate_table_readers_mem_str.c_str());
  } else {
    fprintf(stderr, "Failed to get rocksdb.estimate-table-readers-mem\n");
  }
}

int main(int argc, char** argv) {
  // 解析命令行参数
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  rocksdb::DB* db;
  rocksdb::Options options;

    // --- 根据读写比例动态配置 Smoose 参数 ---
    // 这是基于用户提供的针对不同工作负载的调优值
  if (FLAGS_workload == "mixed") {
    if (std::abs(FLAGS_read_ratio - 0.5) < 0.01) {
      // 平衡读写 (50% 读 / 50% 写)
      fprintf(stderr, "Applying balanced workload Smoose configuration.\n");
      options.level_capacities = {7, 7, 7, 8,3};
      options.run_numbers = {7, 7, 7, 8, 3};
    } else if (std::abs(FLAGS_read_ratio - 0.9) < 0.01 || std::abs(FLAGS_read_ratio - 1.0) < 0.01) {
      // 重度读 (90%-100% 读)
      fprintf(stderr, "Applying read-heavy workload Smoose configuration.\n");
      options.level_capacities = {27,27,13};
      options.run_numbers = {2,2,1};
    } else {
      // 默认配置为重度写 (例如: 10% 读 / 90% 写)
      fprintf(stderr, "Applying write-heavy (default) workload Smoose configuration.\n");
      options.level_capacities = {10, 11, 11, 8};
      options.run_numbers = {10, 11, 11, 7};
    }
  } else {
    // 对于 "fill" 负载，使用重度写配置
    fprintf(stderr, "Applying write-heavy configuration for 'fill' workload.\n");
    options.level_capacities = {10, 11, 11, 8};
    options.run_numbers = {10, 11, 11, 7};
  }

  options.create_if_missing = true;
  options.use_direct_io_for_flush_and_compaction= true;
  options.use_direct_reads = true;
  options.allow_mmap_reads = false;          // 禁用内存映射读取
  options.allow_mmap_writes = false;         // 禁用内存映射写入

  // 强制使用传统的POSIX IO
  // options.access_hint_on_compaction_start = rocksdb::Options::NONE;
  // =================================================

  std::shared_ptr<rocksdb::Cache> block_cache = rocksdb::NewLRUCache(
    128 * 1024 * 1024,  // 512MB 缓存大小
    8,                   // shard数量，用于减少锁竞争
    false,              // 不严格限制容量
    0.5                 // high_pri_pool_ratio，用于索引和filter blocks
  );
  options.max_open_files = 1000;
  
  // options.write_buffer_size = 2097152;
  // options.IncreaseParallelism(); // 使用推荐的并行设置
  // options.OptimizeLevelStyleCompaction(); // 使用推荐的LSM-Tree优化

  fprintf(stderr,"The memtable size is:%lu \n",options.write_buffer_size);
  rocksdb::BlockBasedTableOptions table_options;
  table_options.block_cache = block_cache;
  table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(FLAGS_bits_per_key));
  options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));

  // 打开数据库
  std::cout << "Opening database at: " << FLAGS_db << std::endl;
  rocksdb::Status status = rocksdb::DB::Open(options, FLAGS_db, &db);

  if (!status.ok()) {
    std::cerr << "Failed to open database: " << status.ToString() << std::endl;
    return 1;
  }
  std::cout << "Database opened successfully!" << std::endl;

  // --- 调用写入函数进行测试 ---
 // 验证读写比例
  if (FLAGS_workload == "mixed") {
    double ratio_sum = FLAGS_read_ratio + FLAGS_write_ratio;
    if (std::abs(ratio_sum - 1.0) > 0.001) {  // 允许小的浮点误差
      fprintf(stderr, "Error: read_ratio (%.2f) + write_ratio (%.2f) = %.2f, must equal 1.0\n", FLAGS_read_ratio, FLAGS_write_ratio, ratio_sum);
      fprintf(stderr, "Normalizing ratios...\n");
      FLAGS_read_ratio = FLAGS_read_ratio / ratio_sum;
      FLAGS_write_ratio = FLAGS_write_ratio / ratio_sum;
      fprintf(stderr, "Adjusted: read_ratio = %.2f, write_ratio = %.2f\n", 
              FLAGS_read_ratio, FLAGS_write_ratio);
    }
  }

  if (FLAGS_workload == "fill") {
    fillcluster(db);
  } else if (FLAGS_workload == "mixed") {
    // mixedWorkload(db);
    ExecuteWorkload(db);
  } else {
    fprintf(stderr, "Error: Unknown workload type: %s\n", FLAGS_workload.c_str());
  }  

  std::string stats_str;
  if (db->GetProperty("rocksdb.stats", &stats_str)) {
    fprintf(stdout, "\n--- RocksDB Statistics ---\n");
    fprintf(stdout, "%s\n", stats_str.c_str());
    fprintf(stdout, "--------------------------\n");
  }

  // 在关闭数据库前打印内存使用情况
  printMemoryUsage();
  printf_mem(db);

  // 关闭并删除数据库对象
  db->Close();
  delete db;
  std::cout << "Database closed." << std::endl;

  return 0;
}