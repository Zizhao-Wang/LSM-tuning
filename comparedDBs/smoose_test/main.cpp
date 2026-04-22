#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <thread>

#include <gflags/gflags.h> 


#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/write_batch.h"
#include "rocksdb/slice.h"
#include "rocksdb/statistics.h"
#include "rocksdb/table.h"
#include "rocksdb/table_properties.h" // 新增头文件以提供完整的表定义
#include "rocksdb/filter_policy.h"
#include "rocksdb/perf_context.h"
#include "rocksdb/iostats_context.h"
#include "mixgraph_workload.h"

DEFINE_int64(ops_delay_us, 0, "Delay in microseconds after each operation (0 = no delay)");
DEFINE_int32(range_query_percent, 0, "Percentage of GET operations to convert to range query (0-100)");
DEFINE_int64(scan_length, 100, "Number of keys to scan in each range query");
DEFINE_int32(range_query_source, 0, "Source operation type to convert to range query: 0=GET, 1=PUT");

DEFINE_string(data_file_path, "", "Path to the CSV data file.");
DEFINE_int64(num, 1000000, "Number of key-value pairs to write.");
DEFINE_int32(key_size, 20, "Size of the key in bytes.");
DEFINE_int32(value_size, 100, "Size of the value in bytes.");
DEFINE_int32(entries_per_batch, 1, "Number of entries per write batch.");
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

DEFINE_int32(perf_level, 1, "Performance monitoring level (1-5). 1=disabled, 5=full detail");

DEFINE_int64(block_cache_mb, 128, "Block cache size in MB (e.g., 8, 64, 128)");
DEFINE_int64(table_cache_size, 1000, "Table cache size (number of open files)");

// for mixgraph
DEFINE_int64(mix_max_scan_len, 10000, "The max scan length of Iterator");
DEFINE_int64(keyrange_num, 1,
             "The number of key ranges that are in the same prefix "
             "group, each prefix range will have its key access distribution");
DEFINE_double(mix_get_ratio, 1.0,
              "The ratio of Get queries of mix_graph workload");
DEFINE_double(mix_put_ratio, 0.0,
              "The ratio of Put queries of mix_graph workload");
DEFINE_double(mix_seek_ratio, 0.0,
              "The ratio of Seek queries of mix_graph workload");
DEFINE_int32(value_size_max, 102400, "Max size of random value");
DEFINE_int32(value_size_min, 100, "Min size of random value");

DEFINE_double(compression_ratio, 0.5,
              "Arrange to generate values that shrink to this fraction of "
              "their original size after compression");
DEFINE_int64(mix_max_value_size, 1024, "The max value size of this workload");
DEFINE_bool(sine_mix_rate, false,
            "Enable the sine QPS control on the mix workload");
DEFINE_double(keyrange_dist_a, 0.0,
              "The parameter 'a' of prefix average access distribution "
              "f(x)=a*exp(b*x)+c*exp(d*x)");
DEFINE_double(keyrange_dist_b, 0.0,
              "The parameter 'b' of prefix average access distribution "
              "f(x)=a*exp(b*x)+c*exp(d*x)");
DEFINE_double(keyrange_dist_c, 0.0,
              "The parameter 'c' of prefix average access distribution"
              "f(x)=a*exp(b*x)+c*exp(d*x)");
DEFINE_double(keyrange_dist_d, 0.0,
              "The parameter 'd' of prefix average access distribution"
              "f(x)=a*exp(b*x)+c*exp(d*x)");
DEFINE_double(key_dist_a, 0.0,
              "The parameter 'a' of key access distribution model f(x)=a*x^b");
DEFINE_double(key_dist_b, 0.0,
              "The parameter 'b' of key access distribution model f(x)=a*x^b");
DEFINE_int32(duration, 0,
             "Time in seconds for the random-ops tests to run."
             " When 0 then num & reads determine the test duration");
DEFINE_int64(reads, -1,
             "Number of read operations to do.  "
             "If negative, do FLAGS_num reads.");
DEFINE_int32(ops_between_duration_checks, 1000,
             "Check duration limit every x ops");
DEFINE_int32(prefix_size, 0,
             "control the prefix size for HashSkipList and plain table");

// Dynamic workload flags
DEFINE_int64(workload_num, 0, "Number of operations per workload phase. "
             "When >0, data_file_path is treated as comma-separated list of files.");
DEFINE_string(key_size_per_workload, "",
              "Comma-separated list of key sizes for each workload. Example: '24,24,44'");
DEFINE_string(value_size_per_workload, "",
              "Comma-separated list of value sizes for each workload. Example: '1000,1000,4266'");
DEFINE_string(level_capacities_per_workload, "",
              "Semicolon-separated list of level_capacities for each workload. "
              "Each entry is comma-separated. Example: '7,7,7,8,3;10,11,11,8'");
DEFINE_string(run_numbers_per_workload, "",
              "Semicolon-separated list of run_numbers for each workload. "
              "Each entry is comma-separated. Example: '7,7,7,8,3;10,11,11,7'");
 
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
      printMemoryUsage();

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


// ========== Helper: split string by delimiter ==========
static std::vector<std::string> StringSplit(const std::string& s, char delim) {
  std::vector<std::string> result;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    // trim whitespace
    size_t start = item.find_first_not_of(" \t");
    size_t end = item.find_last_not_of(" \t");
    if (start != std::string::npos) {
      result.push_back(item.substr(start, end - start + 1));
    }
  }
  return result;
}

// ========== Dynamic Workload Execution ==========
void ExecuteDynamicWorkload(rocksdb::DB* db) {
  if (FLAGS_data_file_path.empty()) {
    fprintf(stderr, "Error: You must specify --data_file_path\n");
    return;
  }
  if (FLAGS_workload_num <= 0) {
    fprintf(stderr, "Error: --workload_num must be > 0 for dynamic workload\n");
    return;
  }

  // Parse comma-separated data file paths
  std::vector<std::string> data_file_paths = StringSplit(FLAGS_data_file_path, ',');
  const size_t num_workloads = data_file_paths.size();
  fprintf(stderr, "Dynamic workload: %zu workload phases, %ld ops each\n",
          num_workloads, FLAGS_workload_num);

  // Parse per-workload key_size
  std::vector<int> key_sizes;
  if (!FLAGS_key_size_per_workload.empty()) {
    auto ks_list = StringSplit(FLAGS_key_size_per_workload, ',');
    for (auto& s : ks_list) key_sizes.push_back(std::stoi(s));
  }

  // Parse per-workload value_size
  std::vector<int> value_sizes;
  if (!FLAGS_value_size_per_workload.empty()) {
    auto vs_list = StringSplit(FLAGS_value_size_per_workload, ',');
    for (auto& s : vs_list) value_sizes.push_back(std::stoi(s));
  }

  // Parse per-workload level_capacities (semicolon-separated groups)
  std::vector<std::vector<uint64_t>> all_level_capacities;
  if (!FLAGS_level_capacities_per_workload.empty()) {
    auto groups = StringSplit(FLAGS_level_capacities_per_workload, ';');
    for (auto& g : groups) {
      auto vals = StringSplit(g, ',');
      std::vector<uint64_t> caps;
      for (auto& v : vals) caps.push_back(std::stoull(v));
      all_level_capacities.push_back(caps);
    }
  }

  // Parse per-workload run_numbers (semicolon-separated groups)
  std::vector<std::vector<int>> all_run_numbers;
  if (!FLAGS_run_numbers_per_workload.empty()) {
    auto groups = StringSplit(FLAGS_run_numbers_per_workload, ';');
    for (auto& g : groups) {
      auto vals = StringSplit(g, ',');
      std::vector<int> runs;
      for (auto& v : vals) runs.push_back(std::stoi(v));
      all_run_numbers.push_back(runs);
    }
  }

  // ========== Load all keys and ops from all files ==========
  std::vector<uint64_t> keys;
  std::vector<std::string> ops_type;
  keys.reserve(num_workloads * FLAGS_workload_num);
  ops_type.reserve(num_workloads * FLAGS_workload_num);

  for (size_t w = 0; w < num_workloads; w++) {
    std::ifstream csv_file(data_file_paths[w]);
    if (!csv_file.is_open()) {
      fprintf(stderr, "Error: Unable to open file: %s\n", data_file_paths[w].c_str());
      return;
    }
    std::string line;
    std::getline(csv_file, line); // skip header

    int64_t loaded = 0;
    while (loaded < FLAGS_workload_num && std::getline(csv_file, line)) {
      std::stringstream line_stream(line);
      std::string cell;
      std::vector<std::string> row_data;
      while (std::getline(line_stream, cell, ',')) {
        row_data.push_back(cell);
      }
      if (row_data.size() >= 6) {
        keys.push_back(std::stoull(row_data[1]));
        ops_type.push_back(row_data[5]);
        loaded++;
      }
    }
    csv_file.close();

    if (loaded < FLAGS_workload_num) {
      fprintf(stderr, "Warning: File %s only loaded %ld ops, expected %ld\n",
              data_file_paths[w].c_str(), loaded, FLAGS_workload_num);
    }
    fprintf(stderr, "Workload %zu: loaded %ld ops from %s\n",
            w, loaded, data_file_paths[w].c_str());
  }

  const int64_t total_ops = static_cast<int64_t>(keys.size());
  fprintf(stderr, "Total operations across all workloads: %ld\n", total_ops);

  // ========== Execution ==========
  int64_t num_reads = 0, num_writes = 0;
  int64_t num_found = 0, num_not_found = 0;
  int64_t num_read_errors = 0, num_write_errors = 0;
  double total_read_time = 0.0, total_write_time = 0.0;
  double total_operation_time = 0.0;
  int64_t total_operations_executed = 0;

  rocksdb::WriteOptions write_options;
  rocksdb::ReadOptions read_options;
  if (FLAGS_sync) write_options.sync = true;

  auto start_time = std::chrono::high_resolution_clock::now();
  auto last_report_time = start_time;
  int64_t ops_at_last_report = 0;

  size_t current_workload_idx = 0;
  int64_t next_workload_boundary = FLAGS_workload_num;
  int32_t k_size = FLAGS_key_size;
  int32_t v_size = FLAGS_value_size;

  // Per-workload stats
  auto workload_start_time = start_time;
  int64_t wl_reads = 0, wl_writes = 0;
  double wl_read_time = 0.0, wl_write_time = 0.0;

  // Apply initial workload config (workload 0)
  if (!key_sizes.empty()) k_size = key_sizes[0];
  if (!value_sizes.empty()) v_size = value_sizes[0];

  if (!all_level_capacities.empty() && !all_run_numbers.empty()) {
    // Format for SetOptions vector: colon-separated values, e.g. "7:7:7:8:3"
    std::string cap_str;
    for (size_t j = 0; j < all_level_capacities[0].size(); j++) {
      if (j > 0) cap_str += ":";
      cap_str += std::to_string(all_level_capacities[0][j]);
    }
    std::string run_str;
    for (size_t j = 0; j < all_run_numbers[0].size(); j++) {
      if (j > 0) run_str += ":";
      run_str += std::to_string(all_run_numbers[0][j]);
    }

    rocksdb::Status s = db->SetOptions({
        {"level_capacities", cap_str},
        {"run_numbers", run_str}
    });
    if (s.ok()) {
      fprintf(stderr, "Workload 0: SetOptions level_capacities=%s run_numbers=%s [OK]\n",
              cap_str.c_str(), run_str.c_str());
    } else {
      fprintf(stderr, "Workload 0: SetOptions FAILED: %s\n", s.ToString().c_str());
    }
  }

  fprintf(stderr, "\n========== Starting Dynamic Workload Execution ==========\n");
  fprintf(stderr, "Workload 0: file=%s key_size=%d value_size=%d\n",
          data_file_paths[0].c_str(), k_size, v_size);

  for (int64_t i = 0; i < total_ops; i++) {
    uint64_t key_num = keys[i];
    char key_buffer[100];
    snprintf(key_buffer, sizeof(key_buffer), "%0*llu", k_size, (unsigned long long)key_num);

    if (ops_type[i] == "get" || ops_type[i] == "gets") {
      auto op_start = std::chrono::high_resolution_clock::now();
      std::string value;
      rocksdb::Status s = db->Get(read_options, key_buffer, &value);
      auto op_end = std::chrono::high_resolution_clock::now();
      double op_time = std::chrono::duration<double>(op_end - op_start).count();
      total_read_time += op_time;
      total_operation_time += op_time;
      total_operations_executed++;
      wl_read_time += op_time;

      if (s.ok()) num_found++;
      else if (s.IsNotFound()) num_not_found++;
      else { num_read_errors++; }
      num_reads++;
      wl_reads++;

    } else if (ops_type[i] == "add" || ops_type[i] == "set") {
      auto op_start = std::chrono::high_resolution_clock::now();
      std::string value(v_size, 'a');
      rocksdb::Status s = db->Put(write_options, key_buffer, value);
      auto op_end = std::chrono::high_resolution_clock::now();
      double op_time = std::chrono::duration<double>(op_end - op_start).count();
      total_write_time += op_time;
      total_operation_time += op_time;
      total_operations_executed++;
      wl_write_time += op_time;

      if (!s.ok()) { num_write_errors++; }
      num_writes++;
      wl_writes++;
    }

    // Periodic progress report
    if (FLAGS_stats_interval_ops > 0 &&
        (i + 1 - ops_at_last_report >= FLAGS_stats_interval_ops)) {
      auto current_time = std::chrono::high_resolution_clock::now();
      double report_secs = std::chrono::duration<double>(current_time - last_report_time).count();
      int64_t ops_in_interval = (i + 1) - ops_at_last_report;
      double throughput = (report_secs > 0) ? (ops_in_interval / report_secs) : 0;
      double total_elapsed = std::chrono::duration<double>(current_time - start_time).count();

      fprintf(stdout, "[%.1fs] Ops: %ld (R:%ld/W:%ld) [WL %zu], Throughput: %.2f ops/sec\n",
              total_elapsed, i + 1, num_reads, num_writes, current_workload_idx, throughput);
      last_report_time = current_time;
      ops_at_last_report = i + 1;
      fflush(stdout);
    }

    // ========== Workload boundary check ==========
    if (i + 1 == next_workload_boundary && i + 1 < total_ops) {
      // Print per-workload summary
      auto wl_end = std::chrono::high_resolution_clock::now();
      double wl_duration = std::chrono::duration<double>(wl_end - workload_start_time).count();
      int64_t wl_total = wl_reads + wl_writes;

      fprintf(stdout, "\n--- Workload %zu Summary ---\n", current_workload_idx);
      fprintf(stdout, "  File: %s\n", data_file_paths[current_workload_idx].c_str());
      fprintf(stdout, "  Ops: %ld (R:%ld W:%ld), Time: %.2fs, Throughput: %.2f ops/sec\n",
              wl_total, wl_reads, wl_writes, wl_duration,
              wl_duration > 0 ? wl_total / wl_duration : 0);
      if (wl_reads > 0)
        fprintf(stdout, "  Avg read latency: %.2f us\n", (wl_read_time / wl_reads) * 1e6);
      if (wl_writes > 0)
        fprintf(stdout, "  Avg write latency: %.2f us\n", (wl_write_time / wl_writes) * 1e6);
      fprintf(stdout, "----------------------------\n");
      fflush(stdout);

      // Switch to next workload
      current_workload_idx++;
      next_workload_boundary += FLAGS_workload_num;

      // Reset per-workload stats
      workload_start_time = std::chrono::high_resolution_clock::now();
      wl_reads = 0; wl_writes = 0;
      wl_read_time = 0.0; wl_write_time = 0.0;

      // Dynamic key_size
      if (current_workload_idx < key_sizes.size()) {
        k_size = key_sizes[current_workload_idx];
      }
      // Dynamic value_size
      if (current_workload_idx < value_sizes.size()) {
        v_size = value_sizes[current_workload_idx];
      }

      // Dynamic level_capacities + run_numbers via SetOptions
      if (current_workload_idx < all_level_capacities.size() &&
          current_workload_idx < all_run_numbers.size()) {
        std::string cap_str;
        for (size_t j = 0; j < all_level_capacities[current_workload_idx].size(); j++) {
          if (j > 0) cap_str += ":";
          cap_str += std::to_string(all_level_capacities[current_workload_idx][j]);
        }
        std::string run_str;
        for (size_t j = 0; j < all_run_numbers[current_workload_idx].size(); j++) {
          if (j > 0) run_str += ":";
          run_str += std::to_string(all_run_numbers[current_workload_idx][j]);
        }

        rocksdb::Status s = db->SetOptions({
            {"level_capacities", cap_str},
            {"run_numbers", run_str}
        });
        if (s.ok()) {
          fprintf(stderr, "Workload %zu: SetOptions level_capacities=%s run_numbers=%s [OK]\n",
                  current_workload_idx, cap_str.c_str(), run_str.c_str());
        } else {
          fprintf(stderr, "Workload %zu: SetOptions FAILED: %s\n",
                  current_workload_idx, s.ToString().c_str());
        }
      }

      fprintf(stderr, "\n========== Workload switch: %zu -> %zu ==========\n",
              current_workload_idx - 1, current_workload_idx);
      fprintf(stderr, "Workload %zu: file=%s key_size=%d value_size=%d\n",
              current_workload_idx,
              current_workload_idx < data_file_paths.size() ?
                data_file_paths[current_workload_idx].c_str() : "?",
              k_size, v_size);
    }
  }

  // Print last workload summary
  {
    auto wl_end = std::chrono::high_resolution_clock::now();
    double wl_duration = std::chrono::duration<double>(wl_end - workload_start_time).count();
    int64_t wl_total = wl_reads + wl_writes;
    fprintf(stdout, "\n--- Workload %zu Summary ---\n", current_workload_idx);
    fprintf(stdout, "  File: %s\n",
            current_workload_idx < data_file_paths.size() ?
              data_file_paths[current_workload_idx].c_str() : "?");
    fprintf(stdout, "  Ops: %ld (R:%ld W:%ld), Time: %.2fs, Throughput: %.2f ops/sec\n",
            wl_total, wl_reads, wl_writes, wl_duration,
            wl_duration > 0 ? wl_total / wl_duration : 0);
    if (wl_reads > 0)
      fprintf(stdout, "  Avg read latency: %.2f us\n", (wl_read_time / wl_reads) * 1e6);
    if (wl_writes > 0)
      fprintf(stdout, "  Avg write latency: %.2f us\n", (wl_write_time / wl_writes) * 1e6);
    fprintf(stdout, "----------------------------\n");
  }

  // Overall summary
  auto end_time = std::chrono::high_resolution_clock::now();
  double duration = std::chrono::duration<double>(end_time - start_time).count();
  double overall_avg_latency = (total_operations_executed > 0) ?
      (total_operation_time / total_operations_executed) * 1e6 : 0;

  fprintf(stdout, "\n--- Overall Dynamic Workload Summary ---\n");
  fprintf(stdout, "Total workloads:      %zu\n", num_workloads);
  fprintf(stdout, "Total operations:     %ld\n", total_ops);
  fprintf(stdout, "Read operations:      %ld (%.1f%%)\n",
          num_reads, (num_reads * 100.0) / std::max(total_ops, 1L));
  fprintf(stdout, "Write operations:     %ld (%.1f%%)\n",
          num_writes, (num_writes * 100.0) / std::max(total_ops, 1L));
  fprintf(stdout, "Total time:           %.2f seconds\n", duration);
  fprintf(stdout, "Total throughput:     %.2f ops/sec\n", total_ops / duration);
  fprintf(stdout, "Overall avg latency:  %.2f us\n", overall_avg_latency);
  if (num_reads > 0) {
    fprintf(stdout, "  Read throughput:    %.2f ops/sec\n", num_reads / total_read_time);
    fprintf(stdout, "  Avg read latency:   %.2f us\n", (total_read_time / num_reads) * 1e6);
  }
  if (num_writes > 0) {
    fprintf(stdout, "  Write throughput:   %.2f ops/sec\n", num_writes / total_write_time);
    fprintf(stdout, "  Avg write latency:  %.2f us\n", (total_write_time / num_writes) * 1e6);
  }
  fprintf(stdout, "Keys found: %ld, Not found: %ld, Read errors: %ld, Write errors: %ld\n",
          num_found, num_not_found, num_read_errors, num_write_errors);
  fprintf(stdout, "----------------------------------------\n");
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

      std::string stats_str;
      if (db->GetProperty("rocksdb.stats", &stats_str)) {
        fprintf(stdout, "%s\n", stats_str.c_str());
      }    

      fprintf(stdout, "[%.1fs] Ops: %ld (R:%ld/W:%ld), Throughput: %.2f ops/sec\n",
              total_elapsed_time, i + 1, num_reads, num_writes, throughput);
      printMemoryUsage();
      last_report_time = current_time;
      ops_at_last_report = i + 1;

      fflush(stdout);
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


void ExecuteWorkload_withStdDev(rocksdb::DB* db) {
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

  // Welford's online algorithm 用于计算标准差
  // 总体操作
  double overall_mean = 0.0;
  double overall_M2 = 0.0;
  int64_t overall_count = 0;
  
  // 读操作
  double read_mean = 0.0;
  double read_M2 = 0.0;
  int64_t read_count = 0;
  
  // 写操作
  double write_mean = 0.0;
  double write_M2 = 0.0;
  int64_t write_count = 0;

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
      double op_time_us = op_time * 1000000; // 转换为微秒
      
      total_read_time += op_time;
      total_operation_time += op_time;
      total_operations_executed++;
      
      // 更新总体的统计
      overall_count++;
      double overall_delta = op_time_us - overall_mean;
      overall_mean += overall_delta / overall_count;
      double overall_delta2 = op_time_us - overall_mean;
      overall_M2 += overall_delta * overall_delta2;
      
      // 更新读操作的统计
      read_count++;
      double read_delta = op_time_us - read_mean;
      read_mean += read_delta / read_count;
      double read_delta2 = op_time_us - read_mean;
      read_M2 += read_delta * read_delta2;
      
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
      
      double op_time = std::chrono::duration<double>(write_op_end - write_op_start).count();
      double op_time_us = op_time * 1000000; // 转换为微秒
      
      total_write_time += op_time;
      
      // 更新总体的统计
      overall_count++;
      double overall_delta = op_time_us - overall_mean;
      overall_mean += overall_delta / overall_count;
      double overall_delta2 = op_time_us - overall_mean;
      overall_M2 += overall_delta * overall_delta2;
      
      // 更新写操作的统计
      write_count++;
      double write_delta = op_time_us - write_mean;
      write_mean += write_delta / write_count;
      double write_delta2 = op_time_us - write_mean;
      write_M2 += write_delta * write_delta2;
      
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
      printMemoryUsage();
      last_report_time = current_time;
      ops_at_last_report = i + 1;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  // 计算标准差
  double overall_variance = (overall_count > 1) ? (overall_M2 / overall_count) : 0.0;
  double overall_stddev = std::sqrt(overall_variance);
  
  double read_variance = (read_count > 1) ? (read_M2 / read_count) : 0.0;
  double read_stddev = std::sqrt(read_variance);
  
  double write_variance = (write_count > 1) ? (write_M2 / write_count) : 0.0;
  double write_stddev = std::sqrt(write_variance);

  double overall_avg_latency = overall_mean;  // 已经是微秒了

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
  
  fprintf(stdout, "\nMicroseconds per operation:\n");
  fprintf(stdout, "  Count: %ld  Average: %.4f  StdDev: %.2f\n", 
          overall_count, overall_avg_latency, overall_stddev);
  
  fprintf(stdout, "\n  Read performance:\n");
  if (num_reads > 0) {
    fprintf(stdout, "    Microseconds per read:\n");
    fprintf(stdout, "    Count: %ld  Average: %.4f  StdDev: %.2f\n",
            read_count, read_mean, read_stddev);
    fprintf(stdout, "    - Read throughput:    %.2f ops/sec\n", num_reads / total_read_time);
  }
  fprintf(stdout, "  Write performance:\n");
  if (num_writes > 0) {
    fprintf(stdout, "    Microseconds per write:\n");
    fprintf(stdout, "    Count: %ld  Average: %.4f  StdDev: %.2f\n",
            write_count, write_mean, write_stddev);
    fprintf(stdout, "    - Write throughput:   %.2f ops/sec\n", num_writes / total_write_time);
  }
  fprintf(stdout, "------------------------------\n");
}


void PrintDetailedStats(const std::vector<int>& ops, const std::string& label) {
  int64_t total = ops.size();
  if (total == 0) return;
    
  // 分成 10 段来统计
  int num_segments = 10;
  int64_t segment_size = total / num_segments;
    
  fprintf(stderr, "\n========== %s ==========\n", label.c_str());
  fprintf(stderr, "Segment\t\tRange\t\t\tGET\tPUT\tRMW\tRANGE\n");
  fprintf(stderr, "----------------------------------------------------------------------\n");
    
  int64_t total_get = 0, total_put = 0, total_rmw = 0, total_range = 0;
    
  for (int seg = 0; seg < num_segments; seg++) {
      int64_t start = seg * segment_size;
      int64_t end = (seg == num_segments - 1) ? total : (seg + 1) * segment_size;
      
      int64_t seg_get = 0, seg_put = 0, seg_rmw = 0, seg_range = 0;
      
      for (int64_t i = start; i < end; i++) {
        switch (ops[i]) {
          case 0: seg_get++; break;
          case 1: seg_put++; break;
          case 2: seg_rmw++; break;
          case 3: seg_range++; break;
        }
      }
      
      total_get += seg_get;
      total_put += seg_put;
      total_rmw += seg_rmw;
      total_range += seg_range;
      
      // 计算这一段的百分比
      int64_t seg_total = end - start;
      fprintf(stderr, "[%d] %d%%-%d%%\t[%ld - %ld]\t%ld(%.1f%%)\t%ld(%.1f%%)\t%ld(%.1f%%)\t%ld(%.1f%%)\n",
              seg,
              seg * 10, (seg + 1) * 10,
              start, end - 1,
              seg_get, (double)seg_get / seg_total * 100,
              seg_put, (double)seg_put / seg_total * 100,
              seg_rmw, (double)seg_rmw / seg_total * 100,
              seg_range, (double)seg_range / seg_total * 100);
  }
    
  fprintf(stderr, "----------------------------------------------------------------------\n");
  fprintf(stderr, "TOTAL:\t\t[0 - %ld]\t%ld(%.1f%%)\t%ld(%.1f%%)\t%ld(%.1f%%)\t%ld(%.1f%%)\n\n",
    total - 1,total_get, (double)total_get / total * 100,
    total_put, (double)total_put / total * 100,
    total_rmw, (double)total_rmw / total * 100,
    total_range, (double)total_range / total * 100);
}


void ConvertGetToRangeQuery2(std::vector<int>& workload_operations, int range_query_percent) {
  if (range_query_percent <= 0 || range_query_percent > 100) {
    fprintf(stderr, "range_query_percent is %d, no conversion needed\n", range_query_percent);
    return;
  }
  
  // 确定要转换的源操作类型
  int source_op_type = FLAGS_range_query_source;
  const char* source_name = (source_op_type == 0) ? "GET" : (source_op_type == 1) ? "PUT" : "UNKNOWN";
  
  if (source_op_type != 0 && source_op_type != 1) {
    fprintf(stderr, "Invalid range_query_source: %d, must be 0 (GET) or 1 (PUT)\n", source_op_type);
    return;
  }
  
  // 转换前的统计
  PrintDetailedStats(workload_operations, "BEFORE CONVERSION");
  
  // 收集所有目标操作类型的索引
  std::vector<int64_t> source_indices;
  int64_t total_ops = workload_operations.size();
  
  for (int64_t i = 0; i < total_ops; i++) {
    if (workload_operations[i] == source_op_type) {
      source_indices.push_back(i);
    }
  }
  
  int64_t total_source_count = source_indices.size();
  
  if (total_source_count == 0) {
    fprintf(stderr, "No %s operations found to convert\n", source_name);
    return;
  }
  
  // 计算需要转换的数量
  int64_t num_to_convert = (int64_t)(total_source_count * range_query_percent / 100.0);
  
  fprintf(stderr, "Plan: Convert %ld %s to RANGE QUERY (%d%% of all %s)\n", 
          num_to_convert, source_name, range_query_percent, source_name);
  
  // 随机打乱所有目标操作的索引
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(source_indices.begin(), source_indices.end(), g);
  
  // 取前 num_to_convert 个转换成 RANGE QUERY
  for (int64_t i = 0; i < num_to_convert; i++) {
    workload_operations[source_indices[i]] = 3;
  }
  
  // 转换后的统计
  PrintDetailedStats(workload_operations, "AFTER CONVERSION");
  fflush(stderr);
}


void ExecuteWorkload_with_range(rocksdb::DB* db) {
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
  std::vector<int> ops_type;  // 改成 int: 0=get, 1=put, 3=range
  std::string line;
  std::getline(csv_file, line); // 跳过标题行

  fprintf(stderr, "Loading keys from CSV file...\n");
  for (int64_t i = 0; i < FLAGS_num + 1000 && std::getline(csv_file, line); ++i) {
    std::stringstream line_stream(line);
    std::string cell;
    std::vector<std::string> row_data;
    
    while (getline(line_stream, cell, ',')) {
      row_data.push_back(cell);
    }

    if (row_data.size() >= 6) {
      keys.push_back(std::stoull(row_data[1]));
      
      // 解析操作类型
      if (row_data[5] == "get" || row_data[5] == "gets") {
        ops_type.push_back(0);  // GET
      } else if (row_data[5] == "add" || row_data[5] == "set") {
        ops_type.push_back(1);  // PUT
      } else {
        ops_type.push_back(-1); // 未知
      }
    }
  }
  csv_file.close();

  if (keys.empty()) {
    fprintf(stderr, "Error: No valid keys found in CSV file\n");
    return;
  }

  fprintf(stderr, "Loaded %zu keys from CSV file\n", keys.size());

  // 转换部分 GET 为 RANGE QUERY
  if (FLAGS_range_query_percent > 0) {
    ConvertGetToRangeQuery2(ops_type, FLAGS_range_query_percent);
  }


  // 计算读写操作数量
  const int64_t total_ops = FLAGS_num;

  fprintf(stderr, "\n--- Mixed Workload Configuration ---\n");
  fprintf(stderr, "Total operations:    %ld\n", total_ops);
  fprintf(stderr, "Range query percent: %d%%\n", FLAGS_range_query_percent);
  fprintf(stderr, "Scan length:         %ld\n", FLAGS_scan_length);
  fprintf(stderr, "Key size:            %d\n", FLAGS_key_size);
  fprintf(stderr, "Value size:          %d\n", FLAGS_value_size);
  fprintf(stderr, "------------------------------------\n\n");

  // 统计变量
  int64_t num_reads = 0;
  int64_t num_writes = 0;
  int64_t num_range_queries = 0;
  int64_t num_found = 0;
  int64_t num_not_found = 0;
  int64_t num_read_errors = 0;
  int64_t num_write_errors = 0;
  int64_t total_keys_scanned = 0;

  rocksdb::WriteOptions write_options;
  rocksdb::ReadOptions read_options;

  auto start_time = std::chrono::high_resolution_clock::now();
  auto last_report_time = start_time;
  int64_t ops_at_last_report = 0;

  // 用于分别计算读写性能
  double total_read_time = 0.0;
  double total_write_time = 0.0;
  double total_range_time = 0.0;
  
  double total_operation_time = 0.0;
  int64_t total_operations_executed = 0;

  if (FLAGS_sync) {
    write_options.sync = true;
  }

  // 执行混合工作负载
  for (int64_t i = 0; i < total_ops; i++) {
    uint64_t key_num = keys[i];
    
    // 格式化 key
    char key_buffer[100];
    snprintf(key_buffer, sizeof(key_buffer), "%0*llu", FLAGS_key_size, (unsigned long long)key_num);

    if (ops_type[i] == 0) {  // GET
      auto read_op_start = std::chrono::high_resolution_clock::now();
      std::string value;
      rocksdb::Status s = db->Get(read_options, key_buffer, &value);
      auto read_op_end = std::chrono::high_resolution_clock::now();

      double op_time = std::chrono::duration<double>(read_op_end - read_op_start).count();
      total_read_time += op_time;
      total_operation_time += op_time;
      total_operations_executed++;
      
      if (s.ok()) {
        num_found++;
      } else if (s.IsNotFound()) {
        num_not_found++;
      } else {
        num_read_errors++;
        fprintf(stderr, "Read error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_reads++;

    } else if (ops_type[i] == 1) {  // PUT
      auto write_op_start = std::chrono::high_resolution_clock::now();
      std::string value(FLAGS_value_size, 'a');
      rocksdb::Status s = db->Put(write_options, key_buffer, value);
      auto write_op_end = std::chrono::high_resolution_clock::now();

      double op_time = std::chrono::duration<double>(write_op_end - write_op_start).count();
      total_write_time += op_time;
      total_operation_time += op_time;
      total_operations_executed++;
      
      if (!s.ok()) {
        num_write_errors++;
        fprintf(stderr, "Write error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_writes++;

    } else if (ops_type[i] == 3) {  // RANGE QUERY
      auto range_op_start = std::chrono::high_resolution_clock::now();
      
      rocksdb::Iterator* iter = db->NewIterator(read_options);
      int64_t keys_scanned = 0;
      
      if (iter != nullptr) {
        iter->Seek(key_buffer);
        
        if (iter->Valid() && iter->key().ToString() == std::string(key_buffer)) {
          num_found++;
        }
        
        // 扫描 scan_length 个 key
        for (int64_t j = 0; j < FLAGS_scan_length && iter->Valid(); j++) {
          // 读取 key 和 value（模拟实际使用）
          rocksdb::Slice iter_key = iter->key();
          rocksdb::Slice iter_value = iter->value();
          (void)iter_key;   // 防止编译器警告
          (void)iter_value;
          keys_scanned++;
          iter->Next();
          
          if (!iter->status().ok()) {
            fprintf(stderr, "Iterator error: %s\n", iter->status().ToString().c_str());
            break;
          }
        }
        delete iter;
      }
      
      auto range_op_end = std::chrono::high_resolution_clock::now();
      
      double op_time = std::chrono::duration<double>(range_op_end - range_op_start).count();
      total_range_time += op_time;
      total_operation_time += op_time;
      total_operations_executed++;
      total_keys_scanned += keys_scanned;
      num_range_queries++;
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

      fprintf(stdout, "[%.1fs] Ops: %ld (R:%ld/W:%ld/RQ:%ld), Throughput: %.2f ops/sec\n",
              total_elapsed_time, i + 1, num_reads, num_writes, num_range_queries, throughput);
      printMemoryUsage();
      last_report_time = current_time;
      ops_at_last_report = i + 1;
      fflush(stdout);
    
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  double overall_avg_latency = (total_operations_executed > 0) ? 
                              (total_operation_time / total_operations_executed) * 1000000 : 0;

  // 打印最终统计信息
  fprintf(stdout, "\n--- Performance of Workload Summary ---\n");
  fprintf(stdout, "Total operations:     %ld\n", total_ops);
  fprintf(stdout, "Read operations:      %ld (%.1f%%)\n", 
          num_reads, (num_reads * 100.0) / total_ops);
  fprintf(stdout, "Write operations:     %ld (%.1f%%)\n", 
          num_writes, (num_writes * 100.0) / total_ops);
  fprintf(stdout, "Range queries:        %ld (%.1f%%)\n", 
          num_range_queries, (num_range_queries * 100.0) / total_ops);
  
  fprintf(stdout, "\nRead Results:\n");
  fprintf(stdout, "  Keys found:         %ld (%.1f%%)\n", 
          num_found, (num_found * 100.0) / std::max(num_reads + num_range_queries, 1L));
  fprintf(stdout, "  Keys not found:     %ld (%.1f%%)\n", 
          num_not_found, (num_not_found * 100.0) / std::max(num_reads, 1L));
  fprintf(stdout, "  Read errors:        %ld\n", num_read_errors);
  
  fprintf(stdout, "\nWrite Results:\n");
  fprintf(stdout, "  Write errors:       %ld\n", num_write_errors);
  
  fprintf(stdout, "\nRange Query Results:\n");
  fprintf(stdout, "  Total keys scanned: %ld\n", total_keys_scanned);
  fprintf(stdout, "  Avg keys per scan:  %.1f\n", 
          num_range_queries > 0 ? (double)total_keys_scanned / num_range_queries : 0);
  
  fprintf(stdout, "\nPerformance:\n");
  fprintf(stdout, "  Total time:           %.2f seconds\n", duration.count());
  fprintf(stdout, "  Total throughput:     %.2f ops/sec\n", total_ops / duration.count());
  fprintf(stdout, "  Overall avg latency:  %.2f us\n", overall_avg_latency);
  
  fprintf(stdout, "\n  Read performance:\n");
  if (num_reads > 0) {
    fprintf(stdout, "    - Read throughput:    %.2f ops/sec\n", num_reads / total_read_time);
    fprintf(stdout, "    - Avg read latency:   %.2f us\n", (total_read_time / num_reads) * 1000000);
  }
  
  fprintf(stdout, "\n  Write performance:\n");
  if (num_writes > 0) {
    fprintf(stdout, "    - Write throughput:   %.2f ops/sec\n", num_writes / total_write_time);
    fprintf(stdout, "    - Avg write latency:  %.2f us\n", (total_write_time / num_writes) * 1000000);
  }
  
  fprintf(stdout, "\n  Range query performance:\n");
  if (num_range_queries > 0) {
    fprintf(stdout, "    - Range throughput:   %.2f ops/sec\n", num_range_queries / total_range_time);
    fprintf(stdout, "    - Avg range latency:  %.2f us\n", (total_range_time / num_range_queries) * 1000000);
    fprintf(stdout, "    - Avg latency per key: %.2f us\n", 
            (total_range_time / total_keys_scanned) * 1000000);
  }
  
  fprintf(stdout, "------------------------------\n");
}

void ExecuteWorkload_with_range_with_delay(rocksdb::DB* db) {
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
  std::vector<int> ops_type;  // 改成 int: 0=get, 1=put, 3=range
  std::string line;
  std::getline(csv_file, line); // 跳过标题行


  fprintf(stderr, "Loading keys from CSV file...\n");
  for (int64_t i = 0; i < FLAGS_num + 1000 && std::getline(csv_file, line); ++i) {
    std::stringstream line_stream(line);
    std::string cell;
    std::vector<std::string> row_data;
    
    while (getline(line_stream, cell, ',')) {
      row_data.push_back(cell);
    }

    if (row_data.size() >= 6) {
      keys.push_back(std::stoull(row_data[1]));
      
      // 解析操作类型
      if (row_data[5] == "get" || row_data[5] == "gets") {
        ops_type.push_back(0);  // GET
      } else if (row_data[5] == "add" || row_data[5] == "set") {
        ops_type.push_back(1);  // PUT
      } else {
        ops_type.push_back(-1); // 未知
      }
    }
  }
  csv_file.close();

  if (keys.empty()) {
    fprintf(stderr, "Error: No valid keys found in CSV file\n");
    return;
  }

  fprintf(stderr, "Loaded %zu keys from CSV file\n", keys.size());

  // 转换部分 GET 为 RANGE QUERY
  if (FLAGS_range_query_percent > 0) {
    ConvertGetToRangeQuery2(ops_type, FLAGS_range_query_percent);
  }


  // 计算读写操作数量
  const int64_t total_ops = FLAGS_num;

  fprintf(stderr, "\n--- Mixed Workload Configuration ---\n");
  fprintf(stderr, "Total operations:    %ld\n", total_ops);
  fprintf(stderr, "Range query percent: %d%%\n", FLAGS_range_query_percent);
  fprintf(stderr, "Scan length:         %ld\n", FLAGS_scan_length);
  fprintf(stderr, "Key size:            %d\n", FLAGS_key_size);
  fprintf(stderr, "Value size:          %d\n", FLAGS_value_size);
  fprintf(stderr, "------------------------------------\n\n");

  // 统计变量
  int64_t num_reads = 0;
  int64_t num_writes = 0;
  int64_t num_range_queries = 0;
  int64_t num_found = 0;
  int64_t num_not_found = 0;
  int64_t num_read_errors = 0;
  int64_t num_write_errors = 0;
  int64_t total_keys_scanned = 0;

  rocksdb::WriteOptions write_options;
  rocksdb::ReadOptions read_options;

  auto start_time = std::chrono::high_resolution_clock::now();
  auto last_report_time = start_time;
  int64_t ops_at_last_report = 0;

  // 用于分别计算读写性能
  double total_read_time = 0.0;
  double total_write_time = 0.0;
  double total_range_time = 0.0;
  
  double total_operation_time = 0.0;
  int64_t total_operations_executed = 0;

  if (FLAGS_sync) {
    write_options.sync = true;
  }

  if (FLAGS_ops_delay_us > 0) {
    double max_ops_per_sec = 1000000.0 / FLAGS_ops_delay_us;
    fprintf(stderr, "Rate limiting enabled: %ld us delay per op (max ~%.1f ops/sec)\n",FLAGS_ops_delay_us, max_ops_per_sec);
  }
  // 执行混合工作负载
  for (int64_t i = 0; i < total_ops; i++) {
    uint64_t key_num = keys[i];
    
    // 格式化 key
    char key_buffer[100];
    snprintf(key_buffer, sizeof(key_buffer), "%0*llu", FLAGS_key_size, (unsigned long long)key_num);

    if (ops_type[i] == 0) {  // GET
      auto read_op_start = std::chrono::high_resolution_clock::now();
      std::string value;
      rocksdb::Status s = db->Get(read_options, key_buffer, &value);
      auto read_op_end = std::chrono::high_resolution_clock::now();

      double op_time = std::chrono::duration<double>(read_op_end - read_op_start).count();
      total_read_time += op_time;
      total_operation_time += op_time;
      total_operations_executed++;
      
      if (s.ok()) {
        num_found++;
      } else if (s.IsNotFound()) {
        num_not_found++;
      } else {
        num_read_errors++;
        fprintf(stderr, "Read error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_reads++;

    } else if (ops_type[i] == 1) {  // PUT
      auto write_op_start = std::chrono::high_resolution_clock::now();
      std::string value(FLAGS_value_size, 'a');
      rocksdb::Status s = db->Put(write_options, key_buffer, value);
      auto write_op_end = std::chrono::high_resolution_clock::now();

      double op_time = std::chrono::duration<double>(write_op_end - write_op_start).count();
      total_write_time += op_time;
      total_operation_time += op_time;
      total_operations_executed++;
      
      if (!s.ok()) {
        num_write_errors++;
        fprintf(stderr, "Write error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_writes++;

    } else if (ops_type[i] == 3) {  // RANGE QUERY
      auto range_op_start = std::chrono::high_resolution_clock::now();
      
      rocksdb::Iterator* iter = db->NewIterator(read_options);
      int64_t keys_scanned = 0;
      
      if (iter != nullptr) {
        iter->Seek(key_buffer);
        
        if (iter->Valid() && iter->key().ToString() == std::string(key_buffer)) {
          num_found++;
        }
        
        // 扫描 scan_length 个 key
        for (int64_t j = 0; j < FLAGS_scan_length && iter->Valid(); j++) {
          // 读取 key 和 value（模拟实际使用）
          rocksdb::Slice iter_key = iter->key();
          rocksdb::Slice iter_value = iter->value();
          (void)iter_key;   // 防止编译器警告
          (void)iter_value;
          keys_scanned++;
          iter->Next();
          
          if (!iter->status().ok()) {
            fprintf(stderr, "Iterator error: %s\n", iter->status().ToString().c_str());
            break;
          }
        }
        delete iter;
      }
      
      auto range_op_end = std::chrono::high_resolution_clock::now();
      
      double op_time = std::chrono::duration<double>(range_op_end - range_op_start).count();
      total_range_time += op_time;
      total_operation_time += op_time;
      total_operations_executed++;
      total_keys_scanned += keys_scanned;
      num_range_queries++;
    }

    // 定期报告进度
    if (FLAGS_stats_interval_ops > 0 && (i + 1 - ops_at_last_report >= FLAGS_stats_interval_ops)) {
      auto current_time = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> report_duration = current_time - last_report_time;
      
      const int64_t ops_in_interval = (i + 1) - ops_at_last_report;
      const double throughput = (report_duration.count() > 0) ? 
                               (ops_in_interval / report_duration.count()) : 0;
      const double total_elapsed_time = 
          std::chrono::duration<double>(current_time - start_time).count();

      // --- 新增：获取当前系统时间 ---
      auto now = std::chrono::system_clock::now();
      std::time_t now_c = std::chrono::system_clock::to_time_t(now);
      struct tm *parts = std::localtime(&now_c);
      // ---------------------------

      // 修改 fprintf，在最前面加上 [%02d:%02d:%02d] 显示 时:分:秒
      fprintf(stdout, "[%02d:%02d:%02d] [%.1fs] Ops: %ld (R:%ld/W:%ld/RQ:%ld), Throughput: %.2f ops/sec\n",
              parts->tm_hour, parts->tm_min, parts->tm_sec,  // 这里传入时、分、秒
              total_elapsed_time, i + 1, num_reads, num_writes, num_range_queries, throughput);
      printMemoryUsage();
      last_report_time = current_time;
      ops_at_last_report = i + 1;
      fflush(stdout);
    }

    if (FLAGS_ops_delay_us > 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(FLAGS_ops_delay_us));
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  double overall_avg_latency = (total_operations_executed > 0) ? 
                              (total_operation_time / total_operations_executed) * 1000000 : 0;

  // 打印最终统计信息
  fprintf(stdout, "\n--- Performance of Workload Summary ---\n");
  fprintf(stdout, "Total operations:     %ld\n", total_ops);
  fprintf(stdout, "Read operations:      %ld (%.1f%%)\n", 
          num_reads, (num_reads * 100.0) / total_ops);
  fprintf(stdout, "Write operations:     %ld (%.1f%%)\n", 
          num_writes, (num_writes * 100.0) / total_ops);
  fprintf(stdout, "Range queries:        %ld (%.1f%%)\n", 
          num_range_queries, (num_range_queries * 100.0) / total_ops);
  
  fprintf(stdout, "\nRead Results:\n");
  fprintf(stdout, "  Keys found:         %ld (%.1f%%)\n", 
          num_found, (num_found * 100.0) / std::max(num_reads + num_range_queries, 1L));
  fprintf(stdout, "  Keys not found:     %ld (%.1f%%)\n", 
          num_not_found, (num_not_found * 100.0) / std::max(num_reads, 1L));
  fprintf(stdout, "  Read errors:        %ld\n", num_read_errors);
  
  fprintf(stdout, "\nWrite Results:\n");
  fprintf(stdout, "  Write errors:       %ld\n", num_write_errors);
  
  fprintf(stdout, "\nRange Query Results:\n");
  fprintf(stdout, "  Total keys scanned: %ld\n", total_keys_scanned);
  fprintf(stdout, "  Avg keys per scan:  %.1f\n", 
          num_range_queries > 0 ? (double)total_keys_scanned / num_range_queries : 0);
  
  fprintf(stdout, "\nPerformance:\n");
  fprintf(stdout, "  Total time:           %.2f seconds\n", duration.count());
  fprintf(stdout, "  Total throughput:     %.2f ops/sec\n", total_ops / duration.count());
  fprintf(stdout, "  Overall avg latency:  %.2f us\n", overall_avg_latency);
  
  fprintf(stdout, "\n  Read performance:\n");
  if (num_reads > 0) {
    fprintf(stdout, "    - Read throughput:    %.2f ops/sec\n", num_reads / total_read_time);
    fprintf(stdout, "    - Avg read latency:   %.2f us\n", (total_read_time / num_reads) * 1000000);
  }
  
  fprintf(stdout, "\n  Write performance:\n");
  if (num_writes > 0) {
    fprintf(stdout, "    - Write throughput:   %.2f ops/sec\n", num_writes / total_write_time);
    fprintf(stdout, "    - Avg write latency:  %.2f us\n", (total_write_time / num_writes) * 1000000);
  }
  
  fprintf(stdout, "\n  Range query performance:\n");
  if (num_range_queries > 0) {
    fprintf(stdout, "    - Range throughput:   %.2f ops/sec\n", num_range_queries / total_range_time);
    fprintf(stdout, "    - Avg range latency:  %.2f us\n", (total_range_time / num_range_queries) * 1000000);
    fprintf(stdout, "    - Avg latency per key: %.2f us\n", 
            (total_range_time / total_keys_scanned) * 1000000);
  }
  
  fprintf(stdout, "------------------------------\n");
}


void fillcluster(rocksdb::DB* db) {
  if (FLAGS_data_file_path.empty()) {
    fprintf(stderr, "Error: You must specify --data_file_path\n");
    return;
  }

  // ========== Phase 1: 一次性读取 CSV 中所有 key ==========
  std::ifstream csv_file(FLAGS_data_file_path);
  if (!csv_file.is_open()) {
    fprintf(stderr, "Error: Unable to open file: %s\n", FLAGS_data_file_path.c_str());
    return;
  }

  std::string line;
  std::getline(csv_file, line); // 跳过标题行

  std::vector<uint64_t> keys;
  keys.reserve(FLAGS_num);
  while (std::getline(csv_file, line)) {
    // 快速定位第二列：找第一个逗号之后、第二个逗号之前的内容
    auto p1 = line.find(',');
    if (p1 == std::string::npos) continue;
    auto p2 = line.find(',', p1 + 1);
    std::string cell = (p2 == std::string::npos)
                         ? line.substr(p1 + 1)
                         : line.substr(p1 + 1, p2 - p1 - 1);
    if (cell.empty()) continue;
    keys.push_back(std::stoull(cell));
    if (static_cast<int64_t>(keys.size()) >= FLAGS_num) break;
  }
  csv_file.close();

  fprintf(stderr, "Loaded %zu keys from CSV.\n", keys.size());
  fprintf(stderr, "num_: %ld  entries_per_batch_: %d  key_size: %d  value_size: %d\n",
          FLAGS_num, FLAGS_entries_per_batch, FLAGS_key_size, FLAGS_value_size);

  if (keys.empty()) {
    fprintf(stderr, "Error: No valid keys found in CSV.\n");
    return;
  }

  // ========== Phase 2: 批量写入 ==========
  rocksdb::WriteBatch batch;
  rocksdb::WriteOptions write_options;
  if (FLAGS_sync) {
    write_options.sync = true;
  }

  const std::string value_str(FLAGS_value_size, 'a');
  const rocksdb::Slice val(value_str);
  char key_buffer[100];

  int64_t num_written = 0;
  int64_t num_written_at_last_report = 0;
  auto start_time = std::chrono::high_resolution_clock::now();
  auto last_report_time = start_time;

  const int64_t total = static_cast<int64_t>(keys.size());

  for (int64_t i = 0; i < total; i += FLAGS_entries_per_batch) {
    batch.Clear();
    int64_t end = std::min(i + FLAGS_entries_per_batch, total);
    for (int64_t j = i; j < end; j++) {
      snprintf(key_buffer, sizeof(key_buffer), "%0*llu",
               FLAGS_key_size, (unsigned long long)keys[j]);
      batch.Put(key_buffer, val);
      num_written++;
    }

    rocksdb::Status s = db->Write(write_options, &batch);
    if (!s.ok()) {
      fprintf(stderr, "FATAL: Batch write error: %s\n", s.ToString().c_str());
      exit(1);
    }

    // 周期性报告
    if (FLAGS_stats_interval_ops > 0 &&
        (num_written - num_written_at_last_report >= FLAGS_stats_interval_ops)) {
      auto current_time = std::chrono::high_resolution_clock::now();
      double report_secs = std::chrono::duration<double>(current_time - last_report_time).count();
      double total_secs  = std::chrono::duration<double>(current_time - start_time).count();
      double throughput   = (report_secs > 0) ? (num_written - num_written_at_last_report) / report_secs : 0;

      std::string stats_str;
      if (db->GetProperty("rocksdb.stats", &stats_str)) {
        fprintf(stdout, "%s\n", stats_str.c_str());
      }
      fprintf(stdout, "[%.1fs] ... Keys written: %ld, Interval Throughput: %.2f ops/sec\n",
              total_secs, num_written, throughput);
      printMemoryUsage();

      last_report_time = current_time;
      num_written_at_last_report = num_written;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  double duration = std::chrono::duration<double>(end_time - start_time).count();

  fprintf(stdout, "\n--- LoadDataFromCSV Summary ---\n");
  fprintf(stdout, "Keys written:      %ld\n", num_written);
  fprintf(stdout, "Total time:        %.2f seconds\n", duration);
  fprintf(stdout, "Ops per second:    %.2f\n", num_written / duration);
  fprintf(stdout, "-------------------------------\n");
}

rocksdb::Slice AllocateKey(std::unique_ptr<const char[]>* key_guard) {
  char* data = new char[FLAGS_key_size];
  const char* const_data = data;
  key_guard->reset(const_data);
  return rocksdb::Slice(key_guard->get(), FLAGS_key_size);
}



// 新增函数：打印当前进程的内存使用情况 (适用于 Linux)


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

void PrintPerfStatsIfEnabled() {
  if (FLAGS_perf_level < 2) {
    fprintf(stdout, "[Perf] perf_level=%d → Performance statistics disabled.\n", FLAGS_perf_level);
    return;
  }

  // 指针版本
  const auto* perf_context = rocksdb::get_perf_context();
  const auto* iostats_context = rocksdb::get_iostats_context();

  fprintf(stdout, "\n=== RocksDB PerfContext (perf_level=%d) ===\n", FLAGS_perf_level);
  fprintf(stdout, "%s\n", perf_context->ToString().c_str());

  fprintf(stdout, "\n=== RocksDB IOStatsContext ===\n");
  fprintf(stdout, "%s\n", iostats_context->ToString().c_str());
  fprintf(stdout, "=========================================\n");
}


void CreateMergedDataset() {
  // ========== 写死的参数 ==========
  const std::string cluster40_path = "/mnt/workloads/second_cluster40.sort";
  const std::string cluster12_path = "/mnt/workloads/second_cluster12.sort";
  const std::string output_path = "/mnt/workloads/cluster40_range.sort";
  const int64_t merge_num = 100000000;
  const int range_query_percent = 50;
  const int range_query_source = 1;  // 从 PUT 转换

  fprintf(stderr, "=== Creating Merged Dataset ===\n");
  fprintf(stderr, "Cluster40 (keys): %s\n", cluster40_path.c_str());
  fprintf(stderr, "Cluster12 (ops):  %s\n", cluster12_path.c_str());
  fprintf(stderr, "Output:           %s\n", output_path.c_str());
  fprintf(stderr, "Records:          %ld\n", merge_num);
  fprintf(stderr, "Range query %%:    %d\n", range_query_percent);
  fprintf(stderr, "Source op type:   %d (0=GET, 1=PUT)\n", range_query_source);
  fprintf(stderr, "================================\n\n");

  // ========== Step 1: 读取 cluster12 的操作类型 ==========
  fprintf(stderr, "Step 1: Loading operations from cluster12...\n");
  
  std::ifstream cluster12_file(cluster12_path);
  if (!cluster12_file.is_open()) {
    fprintf(stderr, "Error: Cannot open cluster12 file: %s\n", cluster12_path.c_str());
    return;
  }

  std::vector<int> workload_operations;
  workload_operations.reserve(merge_num + 1000);
  
  std::string line;
  
  int64_t loaded_ops = 0;
  while (std::getline(cluster12_file, line) && loaded_ops < merge_num + 1000) {
    std::stringstream ss(line);
    std::string cell;
    std::vector<std::string> row;
    
    while (std::getline(ss, cell, ',')) {
      row.push_back(cell);
    }
    
    if (row.size() >= 6) {
      int op_type = -1;
      if (row[5] == "get" || row[5] == "gets") {
        op_type = 0;
      } else if (row[5] == "set" || row[5] == "add") {
        op_type = 1;
      }
      
      if (op_type >= 0) {
        workload_operations.push_back(op_type);
        loaded_ops++;
      }
    }
    
    if (loaded_ops % 10000000 == 0) {
      fprintf(stderr, "  Loaded: %ld\n", loaded_ops);
    }
  }
  cluster12_file.close();
  
  fprintf(stderr, "Loaded %ld operations from cluster12\n\n", loaded_ops);
  
  // ========== Step 2: 转换部分操作为 RANGE QUERY ==========
  fprintf(stderr, "Step 2: Converting operations to RANGE QUERY...\n");
  
  // 临时设置 FLAGS 供 ConvertGetToRangeQuery2 使用
  int old_source = FLAGS_range_query_source;
  FLAGS_range_query_source = range_query_source;
  
  ConvertGetToRangeQuery2(workload_operations, range_query_percent);
  
  FLAGS_range_query_source = old_source;
  
  // ========== Step 3: 读取 cluster40 的 key 并合并输出 ==========
  fprintf(stderr, "\nStep 3: Merging cluster40 keys with converted operations...\n");
  
  std::ifstream cluster40_file(cluster40_path);
  if (!cluster40_file.is_open()) {
    fprintf(stderr, "Error: Cannot open cluster40 file: %s\n", cluster40_path.c_str());
    return;
  }
  
  std::ofstream output_file(output_path);
  if (!output_file.is_open()) {
    fprintf(stderr, "Error: Cannot create output file: %s\n", output_path.c_str());
    cluster40_file.close();
    return;
  }
  
  // 操作类型映射
  auto get_op_string = [](int op_type) -> const char* {
    switch (op_type) {
      case 0: return "get";
      case 1: return "set";
      case 2: return "cas";
      case 3: return "seek";
      default: return "unknown";
    }
  };
  
  int64_t processed = 0;
  int64_t count_get = 0, count_put = 0, count_range = 0;
  
  while (std::getline(cluster40_file, line) && processed < merge_num) {
    std::stringstream ss(line);
    std::string cell;
    std::vector<std::string> row;
    
    while (std::getline(ss, cell, ',')) {
      row.push_back(cell);
    }
    
    if (row.size() >= 2 && processed < (int64_t)workload_operations.size()) {
      int new_op_type = workload_operations[processed];
      const char* new_op_str = get_op_string(new_op_type);
      
      // 统计
      switch (new_op_type) {
        case 0: count_get++; break;
        case 1: count_put++; break;
        case 3: count_range++; break;
      }
      
      // 输出格式: key,op (只保留第2列key和操作类型)
      output_file << row[1] << "," << new_op_str << "\n";
      
      processed++;
      
      // 每 1000 万输出进度
      if (processed % 10000000 == 0) {
        fprintf(stderr, "  Processed: %ld / %ld (%.1f%%)\n", 
                processed, merge_num, (double)processed / merge_num * 100);
      }
    }
  }
  
  cluster40_file.close();
  output_file.close();
  
  // ========== 最终统计 ==========
  fprintf(stderr, "\n=== Merge Complete ===\n");
  fprintf(stderr, "Total records:  %ld\n", processed);
  fprintf(stderr, "GET:            %ld (%.2f%%)\n", count_get, (double)count_get / processed * 100);
  fprintf(stderr, "PUT:            %ld (%.2f%%)\n", count_put, (double)count_put / processed * 100);
  fprintf(stderr, "RANGE:          %ld (%.2f%%)\n", count_range, (double)count_range / processed * 100);
  fprintf(stderr, "Output file:    %s\n", output_path.c_str());
  fprintf(stderr, "======================\n");
}


void CreateMergedDataset2() {
  const std::string cluster40_path = "/mnt/workloads/second_cluster40.sort";
  const std::string cluster12_path = "/mnt/workloads/second_cluster12.sort";
  const std::string output_path = "/mnt/workloads/cluster40_range.sort";
  const int64_t merge_num = 100000000;

  fprintf(stderr, "=== Creating Merged Dataset ===\n");
  fprintf(stderr, "Cluster40 (keys): %s\n", cluster40_path.c_str());
  fprintf(stderr, "Cluster12 (ops):  %s\n", cluster12_path.c_str());
  fprintf(stderr, "Output:           %s\n", output_path.c_str());
  fprintf(stderr, "Records:          %ld\n", merge_num);
  fprintf(stderr, "================================\n\n");

  std::ifstream cluster40_file(cluster40_path);
  std::ifstream cluster12_file(cluster12_path);
  std::ofstream output_file(output_path);

  if (!cluster40_file.is_open()) {
    fprintf(stderr, "Error: Cannot open %s\n", cluster40_path.c_str());
    return;
  }
  if (!cluster12_file.is_open()) {
    fprintf(stderr, "Error: Cannot open %s\n", cluster12_path.c_str());
    return;
  }
  if (!output_file.is_open()) {
    fprintf(stderr, "Error: Cannot create %s\n", output_path.c_str());
    return;
  }

  std::string line40, line12;
  int64_t processed = 0;
  int64_t count_get = 0, count_put = 0, count_other = 0;

  while (processed < merge_num && 
         std::getline(cluster40_file, line40) && 
         std::getline(cluster12_file, line12)) {
    
    // 从 cluster40 提取第二列 (key)
    std::stringstream ss40(line40);
    std::string cell;
    std::vector<std::string> row40;
    while (std::getline(ss40, cell, ',')) {
      row40.push_back(cell);
    }

    // 从 cluster12 提取第六列 (op)
    std::stringstream ss12(line12);
    std::vector<std::string> row12;
    while (std::getline(ss12, cell, ',')) {
      row12.push_back(cell);
    }

    if (row40.size() >= 2 && row12.size() >= 6) {
      std::string key = row40[1];
      std::string op = row12[5];

      // 统计
      if (op == "get" || op == "gets") {
        count_get++;
      } else if (op == "set" || op == "add") {
        count_put++;
      } else {
        count_other++;
      }

      // 输出格式: 0,key,0,0,0,op,0
      output_file << "0," << key << ",0,0,0," << op << ",0\n";
      processed++;

      if (processed % 10000000 == 0) {
        fprintf(stderr, "Processed: %ld / %ld (%.1f%%)\n", 
                processed, merge_num, (double)processed / merge_num * 100);
      }
    }
  }

  cluster40_file.close();
  cluster12_file.close();
  output_file.close();

  fprintf(stderr, "\n=== Complete ===\n");
  fprintf(stderr, "Total:  %ld\n", processed);
  fprintf(stderr, "GET:    %ld (%.2f%%)\n", count_get, (double)count_get / processed * 100);
  fprintf(stderr, "PUT:    %ld (%.2f%%)\n", count_put, (double)count_put / processed * 100);
  fprintf(stderr, "Other:  %ld (%.2f%%)\n", count_other, (double)count_other / processed * 100);
  fprintf(stderr, "Output: %s\n", output_path.c_str());
}

int main(int argc, char** argv) {
  // 解析命令行参数
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  rocksdb::DB* db;
  rocksdb::Options options;

    // --- 根据读写比例动态配置 Smoose 参数 ---
    // 这是基于用户提供的针对不同工作负载的调优值
  // if (FLAGS_workload == "mixed") {
  //   if (std::abs(FLAGS_read_ratio - 0.5) < 0.01) {
  //     // 平衡读写 (50% 读 / 50% 写)
  //     fprintf(stderr, "Applying balanced workload Smoose configuration.\n");
  //     options.level_capacities = {7, 7, 7, 8,3};
  //     options.run_numbers = {7, 7, 7, 8, 3};
  //   } else if (std::abs(FLAGS_read_ratio - 0.9) < 0.01 || std::abs(FLAGS_read_ratio - 1.0) < 0.01) {
  //     // 重度读 (90%-100% 读)
  //     fprintf(stderr, "Applying read-heavy workload Smoose configuration.\n");
  //     options.level_capacities = {27,27,13};
  //     options.run_numbers = {2,2,1};
  //   } else {
  //     // 默认配置为重度写 (例如: 10% 读 / 90% 写)
  //     fprintf(stderr, "Applying write-heavy (default) workload Smoose configuration.\n");
  //     options.level_capacities = {10, 11, 11, 8};
  //     options.run_numbers = {10, 11, 11, 7};
  //   }
  // } else {
  //   // 对于 "fill" 负载，使用重度写配置
  //   fprintf(stderr, "Applying write-heavy configuration for 'fill' workload.\n");
  //   options.level_capacities = {10, 11, 11, 8};
  //   options.run_numbers = {10, 11, 11, 7};
  // }

  // cluster 48
  options.level_capacities = {11,12,11,6};
  options.run_numbers = {2,2,2,1};

  // cluster 51 with 20% range query
  options.level_capacities = {7,7,7,7,3};
  options.run_numbers = {2,2,2,2,1};

  // cluster 12
  options.level_capacities = {11,11,12,6};
  options.run_numbers = {2,2,2,1};

  //cluster 40 49 13
  options.level_capacities = {7,7,7,8,3};
  options.run_numbers = {7,7,7,8,3};

  // cluster 35 51 30 1 
  // options.level_capacities = {27,27,13};
  // options.run_numbers = {2,2,1};

  // // cluster-12 with range 
  // options.level_capacities = {11,11,12,6};
  // options.run_numbers = {2,2,2,1};

  options.level_capacities = {10, 11, 11, 8};
  options.run_numbers = {10, 11, 11, 7};

  fprintf(stdout, "=== Cluster Configuration ===\n");
  fprintf(stdout, "level_capacities: ");
  for (size_t i = 0; i < options.level_capacities.size(); i++) {
    fprintf(stdout, "%ld", options.level_capacities[i]);
    if (i < options.level_capacities.size() - 1) {
      fprintf(stdout, ", ");
    }
  }
  fprintf(stdout, "\n");

  fprintf(stdout, "run_numbers: ");
  for (size_t i = 0; i < options.run_numbers.size(); i++) {
    fprintf(stdout, "%d", options.run_numbers[i]);
      if (i < options.run_numbers.size() - 1) {
      fprintf(stdout, ", ");
    }
  }
  fprintf(stdout, "\n");


  options.create_if_missing = true;
  options.use_direct_io_for_flush_and_compaction= true;
  options.use_direct_reads = true;
  options.allow_mmap_reads = false;          // 禁用内存映射读取
  options.allow_mmap_writes = false;         // 禁用内存映射写入
  // options.pin_l0_filter_and_index_blocks_in_cache =false;
  // options.pin_top_level_index_and_filter = false;
  // options.cache_index_and_filter_blocks = false;

  // 强制使用传统的POSIX IO
  // options.access_hint_on_compaction_start = rocksdb::Options::NONE;
  // =================================================

  size_t block_cache_size_bytes = static_cast<size_t>(FLAGS_block_cache_mb) * 1024 * 1024;
  std::shared_ptr<rocksdb::Cache> block_cache = rocksdb::NewLRUCache(
    block_cache_size_bytes,  // 128MB 缓存大小
    -1,                   // shard数量，用于减少锁竞争
    false,              // 不严格限制容量
    0.0                 // high_pri_pool_ratio，用于索引和filter blocks
  );
  options.max_open_files = static_cast<int>(FLAGS_table_cache_size);

  
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

  // --- Perf Level ---
  if (FLAGS_perf_level <= 1) {
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kDisable);
    fprintf(stdout, "[Perf] perf_level=%d (disabled)\n", FLAGS_perf_level);
  } else if (FLAGS_perf_level <= 3) {
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kEnableCount);
    fprintf(stdout, "[Perf] perf_level=%d (basic counters)\n", FLAGS_perf_level);
  } else if (FLAGS_perf_level == 4) {
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kEnableTimeExceptForMutex);
    fprintf(stdout, "[Perf] perf_level=4 (timing without mutex)\n");
  } else {
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kEnableTimeAndCPUTimeExceptForMutex);
    fprintf(stdout, "[Perf] perf_level=5 (full detail)\n");
  }

  // 重置本线程的统计（强烈推荐，避免历史噪音）
  rocksdb::get_perf_context()->Reset();
  rocksdb::get_iostats_context()->Reset();


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
    // ExecuteWorkload_withStdDev(db);
    // ExecuteWorkload_with_range(db);
  } else if (FLAGS_workload == "mixed_dynamic") {
    ExecuteDynamicWorkload(db);
  } else if (FLAGS_workload == "mixed_delay") { 
    ExecuteWorkload_with_range_with_delay(db); 
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
  PrintPerfStatsIfEnabled();

  printMemoryUsage();

  printf_mem(db);

  // 关闭并删除数据库对象
  db->Close();
  delete db;
  std::cout << "Database closed." << std::endl;

  return 0;
}