#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
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


int main(int argc, char** argv) {
  // 解析命令行参数
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  rocksdb::DB* db;
  rocksdb::Options options;

  // --- 这是 smoose 的关键配置部分 ---
  options.level_capacities = {10,11,11,8}; // a vector of capacities
  options.run_numbers = {10,11,11,7};
  // --- 关键配置结束 ---

  options.create_if_missing = true;
  options.use_direct_io_for_flush_and_compaction= true;
  // options.write_buffer_size = 2097152;
  // options.IncreaseParallelism(); // 使用推荐的并行设置
  // options.OptimizeLevelStyleCompaction(); // 使用推荐的LSM-Tree优化

  fprintf(stderr,"The memtable size is:%lu \n",options.write_buffer_size);
  rocksdb::BlockBasedTableOptions table_options;
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
  fillcluster(db);

  std::string stats_str;
  if (db->GetProperty("rocksdb.stats", &stats_str)) {
    fprintf(stdout, "\n--- RocksDB Statistics ---\n");
    fprintf(stdout, "%s\n", stats_str.c_str());
    fprintf(stdout, "--------------------------\n");
  }

  // 关闭并删除数据库对象
  db->Close();
  delete db;
  std::cout << "Database closed." << std::endl;

  return 0;
}