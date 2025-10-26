#include "leveldb/performance_profile.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <mutex>

namespace leveldb {

// 全局配置选项
static PerformanceProfileOptions g_profile_options;
static std::mutex g_options_mutex;


// 一行代码打印性能报告的便利函数
void PrintPerformanceProfileIfEnabled(bool show_details) {
  if (!PerformanceProfileOptions::IsProfilingEnabled()) {
    return;
  }
    
  auto& profiler = PerformanceProfiler::GlobalInstance();
  const auto& stats = profiler.GetStats();
    
  uint64_t total_gets = stats.total_get_count.load();
  if (total_gets == 0) {
    return; // 没有Get操作，不输出
  }
    
  if (show_details) {
    // 详细报告
    fprintf(stdout, "\n--- Performance Profile ---\n");
    std::string report = profiler.GenerateReport();
    fprintf(stdout, "%s", report.c_str());
    fprintf(stdout, "----------------------------\n");
  } else {
    // 简化报告 - 一行显示
    double avg_time_us = (stats.total_get_time_ns.load() / total_gets) / 1000.0;
    fprintf(stdout, "Profile: %lu Gets, %.2f μs/op avg\n", total_gets, avg_time_us);
  }
}


// PerformanceProfileOptions implementation
void PerformanceProfileOptions::SetGlobalOptions(const PerformanceProfileOptions& options) {
  std::lock_guard<std::mutex> lock(g_options_mutex);
  g_profile_options = options;
}

const PerformanceProfileOptions& PerformanceProfileOptions::GetGlobalOptions() {
    std::lock_guard<std::mutex> lock(g_options_mutex);
    return g_profile_options;
}

bool PerformanceProfileOptions::IsProfilingEnabled() {
  std::lock_guard<std::mutex> lock(g_options_mutex);
  return g_profile_options.enable_profiling;
}

// PerformanceStats implementation
void PerformanceStats::Reset() {
     
    // Memory tables
    memtable_lookup_count = 0;
    memtable_lookup_time_ns = 0;
    immutable_lookup_count = 0;
    immutable_lookup_time_ns = 0;

    fileread_search_time_ns = 0;
    fileread_search_count = 0;

    // File metadata search
    Level0_file_metadata_search_time_ns = 0;
    Level0_file_metadata_search_count = 0;
    OtherLevel_file_metadata_search_time_ns = 0;
    OtherLevel_file_metadata_search_count = 0;

    // File system operations
    // Level 0 File system operations
    level0_file_open_count = 0;
    level0_file_open_time_ns = 0;
    level0_table_open_count = 0;
    level0_table_open_time_ns = 0;

    // Other Levels File system operations  
    other_levels_file_open_count = 0;
    other_levels_file_open_time_ns = 0;
    other_levels_table_open_count = 0;
    other_levels_table_open_time_ns = 0;
    for (int i = 0; i < 7; i++) {
      level_search_time_ns[i].store(0);
      level_search_count[i].store(0);
    }

    // Level 0 Data block access operations
    level0_index_block_search_count = 0;
    level0_index_block_search_time_ns = 0;
    level0_bloom_filter_check_count = 0;
    level0_bloom_filter_check_time_ns = 0;
    level0_data_block_read_count = 0;
    level0_data_block_read_time_ns = 0;
    level0_bloom_filter_positive_count = 0;
    level0_bloom_filter_negative_count = 0;

    // Other Levels Data block access operations
    other_levels_index_block_search_count = 0;
    other_levels_index_block_search_time_ns = 0;
    other_levels_bloom_filter_check_count = 0;
    other_levels_bloom_filter_check_time_ns = 0;
    other_levels_data_block_read_count = 0;
    other_levels_data_block_read_time_ns = 0;
    other_levels_bloom_filter_positive_count = 0;
    other_levels_bloom_filter_negative_count = 0;


    total_get_count = 0;
    total_get_time_ns = 0;
    batch_get_count = 0;
    batch_get_time_ns = 0;

    total_put_count = 0;
    total_put_time_ns = 0;
    batch_put_count = 0;
    batch_put_time_ns = 0;
}

void PerformanceStats::ResetBatchCounters() {
  batch_get_count = 0;
  batch_get_time_ns = 0;

  batch_put_count = 0;
  batch_put_time_ns = 0;
}


// Get the batch data of PerformanceStats
ThroughputStats PerformanceStats::GetBatchThroughputStats() const {
  ThroughputStats stats;

  stats.total_gets = batch_get_count.load();
  stats.get_time_sec = batch_get_time_ns.load() / 1e9;

  stats.total_puts = batch_put_count.load();
  stats.put_time_sec = batch_put_time_ns.load() / 1e9;

  stats.total_ops = stats.total_gets + stats.total_puts;
  stats.total_time_sec = stats.get_time_sec + stats.put_time_sec;

  stats.read_ops_per_sec  = (stats.get_time_sec > 0) ? (stats.total_gets / stats.get_time_sec) : 0.0;
  stats.write_ops_per_sec = (stats.put_time_sec > 0) ? (stats.total_puts / stats.put_time_sec) : 0.0;
  stats.Iops_per_sec = (stats.total_time_sec > 0) ? (stats.total_ops / stats.total_time_sec): 0.0;

  return stats;
}

// Get the cumulative data of PerformanceStats
ThroughputStats PerformanceStats::GetTotalThroughputStats() const {
  ThroughputStats stats;

  stats.total_gets = total_get_count.load();
  stats.get_time_sec = total_get_time_ns.load() / 1e9;

  stats.total_puts = total_put_count.load();
  stats.put_time_sec = total_put_time_ns.load() / 1e9;

  stats.total_ops = stats.total_gets + stats.total_puts;
  stats.total_time_sec = stats.get_time_sec + stats.put_time_sec;

  stats.read_ops_per_sec  = (stats.get_time_sec > 0) ? (stats.total_gets / stats.get_time_sec) : 0.0;
  stats.write_ops_per_sec = (stats.put_time_sec > 0) ? (stats.total_puts / stats.put_time_sec) : 0.0;
  stats.Iops_per_sec = (stats.total_time_sec > 0) ? (stats.total_ops / stats.total_time_sec): 0.0;

  return stats;
}

double PerformanceStats::GetLevelAverageSearchTimeNs(int level) const {
  uint64_t count = 0;
  uint64_t time_ns = 0;
    
  if (level == 0) {
    count = level0_search_count.load();
    time_ns = level0_search_time_ns.load();
  } else if (level > 0 && level < 7) {
    count = level_search_count[level].load();
    time_ns = level_search_time_ns[level].load();
  } else {
    return 0.0;  // 无效的 level
  }
    
  if (count == 0) {
    return 0.0;  // 避免除零
  }
    
  return static_cast<double>(time_ns) / count;
}

uint64_t PerformanceStats::GetLevelSearchCount(int level) const {
  uint64_t count = 0;
  count = level_search_count[level].load();
  return count;
}


// 实现格式化吞吐率报告的函数
std::string PerformanceStats::GenerateThroughputReport() const {
  const auto throughput = GetTotalThroughputStats();  // 调用计算函数
  char buffer[1024];  // 假设报告内容不会超过缓冲区大小

  int offset = 0;
  offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                     "\n--- Throughput Report (ops/sec) ---\n");

  offset += snprintf(
      buffer + offset, sizeof(buffer) - offset,
      "%-18s: %.2f ops/s (%llu ops in %.2fs)\n", "Read Throughput",
      throughput.read_ops_per_sec,
      static_cast<unsigned long long>(throughput.total_gets),
      throughput.get_time_sec);

  offset += snprintf(
      buffer + offset, sizeof(buffer) - offset,
      "%-18s: %.2f ops/s (%llu ops in %.2fs)\n", "Write Throughput",
      throughput.write_ops_per_sec,
      static_cast<unsigned long long>(throughput.total_puts),
      throughput.put_time_sec);

  offset += snprintf(
      buffer + offset, sizeof(buffer) - offset,
      "%-18s: %.2f ops/s (%llu ops in %.2fs)\n", "Total Throughput",
      throughput.Iops_per_sec,
      static_cast<unsigned long long>(throughput.total_ops),
      throughput.total_time_sec);

  return std::string(buffer);
}

void PerformanceStats::PrintThroughputStats(const ThroughputStats& stats) {
  printf("[Batch Performance] Total operations: %lu (Gets: %lu, Puts: %lu)\n",
    stats.total_ops, stats.total_gets, stats.total_puts);
  printf("[Batch Throughput] Total IOPS: %.2f, Read IOPS: %.2f, Write IOPS: %.2f\n",
    stats.Iops_per_sec, stats.read_ops_per_sec, stats.write_ops_per_sec);
  printf("[Batch Timing] Total Time: %.3fs (Get: %.3fs, Put: %.3fs)\n",
    stats.total_time_sec, stats.get_time_sec, stats.put_time_sec);
}

void PerformanceStats::PrintThroughputReport() const {
  fprintf(stdout, "%s", GenerateThroughputReport().c_str());
}


void PerformanceStats::PrintReport() const {
    std::cout << GenerateReport() << std::endl;
}

bool PerformanceStats::ShouldAutoReport() const {
  const auto& options = PerformanceProfileOptions::GetGlobalOptions();
  return options.report_interval_operations > 0 && total_get_count.load() % options.report_interval_operations == 0;
}

// PerformanceProfiler implementation
PerformanceProfiler::PerformanceProfiler(const PerformanceProfileOptions& options) 
    : options_(options) {
}

PerformanceProfiler& PerformanceProfiler::GlobalInstance() {
  static PerformanceProfiler instance(PerformanceProfileOptions::GetGlobalOptions());
  return instance;
}

PerformanceProfiler::~PerformanceProfiler() {
  if (options_.enable_profiling && options_.auto_report_on_destruction) {
    PrintReport();
  }
}

void PerformanceProfiler::CheckAutoReport() {
  if (stats_.ShouldAutoReport()) {
    PrintReport();
  }
}

// PerformanceTimer implementation
PerformanceTimer::PerformanceTimer(PerformanceProfiler* profiler,std::atomic<uint64_t> PerformanceStats::* time_counter, std::atomic<uint64_t> PerformanceStats::* count_counter)
  : time_counter_(nullptr),count_counter_(nullptr),enabled_(false),is_dual_mode_(false) {
    
  if (profiler && profiler->IsEnabled()) {
    time_counter_ = &(profiler->GetStats().*time_counter);
    if (count_counter) {
      count_counter_ = &(profiler->GetStats().*count_counter);
    }

    enabled_ = true;
    is_dual_mode_= false;
    start_time_ = std::chrono::high_resolution_clock::now();
  }
}


PerformanceTimer::PerformanceTimer(PerformanceProfiler* profiler,
    std::atomic<uint64_t> PerformanceStats::* total_time_counter,std::atomic<uint64_t> PerformanceStats::* batch_time_counter,
    std::atomic<uint64_t> PerformanceStats::* total_count_counter,std::atomic<uint64_t> PerformanceStats::* batch_count_counter)
  :time_counter_(nullptr),count_counter_(nullptr),enabled_(false),is_dual_mode_(true){

  if (profiler && profiler->IsEnabled()) {
    time_counter_ = &(profiler->GetStats().*total_time_counter);
    if (total_count_counter) {
      count_counter_ = &(profiler->GetStats().*total_count_counter);
    }

    batch_time_counter_ = &(profiler->GetStats().*batch_time_counter);
    if (batch_count_counter) {
      batch_count_counter_ = &(profiler->GetStats().*batch_count_counter);
    }

    enabled_ = true;
    is_dual_mode_= true;
    start_time_ = std::chrono::high_resolution_clock::now();
  }
}

PerformanceTimer::PerformanceTimer(PerformanceProfiler* profiler, 
  int level,std::atomic<uint64_t> (PerformanceStats::*time_array)[7], std::atomic<uint64_t> (PerformanceStats::*count_array)[7])
  : time_counter_(nullptr), count_counter_(nullptr), enabled_(false), is_dual_mode_(false) {
    
  if (profiler && profiler->IsEnabled() && level >= 0 && level < 7) {
    auto& stats = profiler->GetStats();
    time_counter_ = &((stats.*time_array)[level]);
    count_counter_ = &((stats.*count_array)[level]);
    
    enabled_ = true;
    is_dual_mode_ = false;
    start_time_ = std::chrono::high_resolution_clock::now();
  }
}

// PerformanceTimer::~PerformanceTimer() {
//   if (enabled_ && time_counter_) {
//     auto end_time = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_);
//     time_counter_->fetch_add(duration.count());
//     if (count_counter_) {
//       count_counter_->fetch_add(1);
//     }
//   }
// }

PerformanceTimer::~PerformanceTimer() {
  if (!enabled_) return;
  
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
    end_time - start_time_).count();
  
  if (is_dual_mode_) {
    if (time_counter_) {
      time_counter_->fetch_add(duration, std::memory_order_relaxed);
    }
    if (count_counter_) {
      count_counter_->fetch_add(1, std::memory_order_relaxed);
    }
    if (batch_time_counter_) {
      batch_time_counter_->fetch_add(duration, std::memory_order_relaxed);
    }
    if (batch_count_counter_) {
      batch_count_counter_->fetch_add(1, std::memory_order_relaxed);
    }
  } else {
    // 原有的单一记录模式
    if (time_counter_) {
      time_counter_->fetch_add(duration, std::memory_order_relaxed);
    }
    if (count_counter_) {
      count_counter_->fetch_add(1, std::memory_order_relaxed);
    }
  }
}


std::string PerformanceStats::GenerateReport() const {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(3);
    
  ss << "=== LevelDB Get Performance Report ===" << std::endl;
  ss << "Total Get Operations: " << total_get_count.load() << std::endl;
  ss << "Total Get Time: " << total_get_time_ns.load() / 1000000.0 << " ms" << std::endl;
    
  if (total_get_count.load() > 0) {
    ss << "Average Get Time: " << (total_get_time_ns.load() / total_get_count.load()) / 1000000.0 << " ms" << std::endl;
  }
    
  ss << std::endl << "--- In-Memory MemTables ---" << std::endl;
    
  auto print_detailed_stat = [&ss, this](const char* name, uint64_t count, uint64_t time_ns, uint64_t level_total_time = 0) {
    if (count > 0) {
      ss << name << ": " << count << " ops, " << time_ns / 1000000.0 << " ms, " 
         << (time_ns / count) / 1000.0 << " μs/op";
      
      // 占总时间的百分比
      if (total_get_time_ns.load() > 0) {
        ss << " (" << std::setprecision(1) << (time_ns * 100.0 / total_get_time_ns.load()) << "% of total";
      }
      
      // 占Level时间的百分比（如果提供了level_total_time）
      if (level_total_time > 0) {
        ss << ", " << std::setprecision(1) << (time_ns * 100.0 / level_total_time) << "% of level";
      }
      
      ss << ")" << std::endl;
    } else {
      ss << name << ": No operations" << std::endl;
    }
    ss << std::setprecision(3); // 恢复精度
  };
    
  print_detailed_stat("Memtable Lookups", memtable_lookup_count.load(), memtable_lookup_time_ns.load());
  print_detailed_stat("Immutable Lookups", immutable_lookup_count.load(), immutable_lookup_time_ns.load());
    
  ss << std::endl << "--- File Metadata Search ---" << std::endl;
  print_detailed_stat("Overall file checking", fileread_search_count.load(), fileread_search_time_ns.load());
  print_detailed_stat("  - Level 0 Metadata Search", Level0_file_metadata_search_count.load(), Level0_file_metadata_search_time_ns.load());
  uint64_t l0_count = level0_search_count.load();
  if (l0_count > 0) {
    uint64_t l0_time = level0_search_time_ns.load();
    ss << "  L0: " << l0_count << " searches, " 
      << std::fixed << std::setprecision(2) << (l0_time / 1000000.0) << " ms, "
      << std::fixed << std::setprecision(2) << ((l0_time / (double)l0_count) / 1000.0) << " μs/search" << std::endl;
  }
  print_detailed_stat("  - Other Levels Metadata Search", OtherLevel_file_metadata_search_count.load(), OtherLevel_file_metadata_search_time_ns.load());
  // Level 1-6
  for (int level = 1; level < 7; level++) {
    uint64_t count = level_search_count[level].load();
    if (count > 0) {
      uint64_t time = level_search_time_ns[level].load();
      ss << "  L" << level << ": " << count << " searches, "
        << std::fixed << std::setprecision(2) << (time / 1000000.0) << " ms, "
        << std::fixed << std::setprecision(2) << ((time / (double)count) / 1000.0) << " μs/search" << std::endl;
    }
  }
  
  // 计算Level 0和Other Levels的总时间，用于后续百分比计算
  uint64_t total_level0_time = level0_file_open_time_ns.load() + level0_table_open_time_ns.load() + 
                              level0_bloom_filter_check_time_ns.load() + level0_index_block_search_time_ns.load() + 
                              level0_data_block_read_time_ns.load();
                              
  uint64_t total_other_levels_time = other_levels_file_open_time_ns.load() + other_levels_table_open_time_ns.load() + 
                                    other_levels_bloom_filter_check_time_ns.load() + other_levels_index_block_search_time_ns.load() + 
                                    other_levels_data_block_read_time_ns.load();
  
  // ==== LEVEL 0 OPERATIONS ====
  ss << std::endl << "=== LEVEL 0 OPERATIONS ===" << std::endl;
  
  // Level 0 File System Operations
  ss << "-- File System Operations --" << std::endl;
  print_detailed_stat("File Open", level0_file_open_count.load(), level0_file_open_time_ns.load(), total_level0_time);
  print_detailed_stat("Table Open", level0_table_open_count.load(), level0_table_open_time_ns.load(), total_level0_time);
  
  // Level 0 Data Block Operations
  ss << "-- Data Block Operations --" << std::endl;
  print_detailed_stat("Bloom Filter Check", level0_bloom_filter_check_count.load(), level0_bloom_filter_check_time_ns.load(), total_level0_time);
  print_detailed_stat("Index Block Search", level0_index_block_search_count.load(), level0_index_block_search_time_ns.load(), total_level0_time);
  print_detailed_stat("Data Block Read", level0_data_block_read_count.load(), level0_data_block_read_time_ns.load(), total_level0_time);
  
  // Level 0 Bloom Filter Statistics
  uint64_t level0_bf_positive = level0_bloom_filter_positive_count.load();
  uint64_t level0_bf_negative = level0_bloom_filter_negative_count.load();
  uint64_t level0_bf_total = level0_bf_positive + level0_bf_negative;
  
  ss << "-- Bloom Filter Results --" << std::endl;
  if (level0_bf_total > 0) {
    ss << "Total: " << level0_bf_total << ", ";
    ss << "Positive: " << level0_bf_positive << " (" << std::setprecision(1) << (level0_bf_positive * 100.0 / level0_bf_total) << "%), ";
    ss << "Negative: " << level0_bf_negative << " (" << std::setprecision(1) << (level0_bf_negative * 100.0 / level0_bf_total) << "%)" << std::endl;
  } else {
    ss << "No Bloom Filter checks performed" << std::endl;
  }
  
  // Level 0 Total Time Summary
  ss << "-- Level 0 Summary --" << std::endl;
  if (total_level0_time > 0) {
    ss << "Total Time: " << total_level0_time / 1000000.0 << " ms (" 
       << std::setprecision(1) << (total_level0_time * 100.0 / total_get_time_ns.load()) << "% of total time)" << std::endl;
  } else {
    ss << "No Level 0 operations performed" << std::endl;
  }
  
  // ==== OTHER LEVELS OPERATIONS ====
  ss << std::endl << "=== OTHER LEVELS OPERATIONS ===" << std::endl;
  
  // Other Levels File System Operations
  ss << "-- File System Operations --" << std::endl;
  print_detailed_stat("File Open", other_levels_file_open_count.load(), other_levels_file_open_time_ns.load(), total_other_levels_time);
  print_detailed_stat("Table Open", other_levels_table_open_count.load(), other_levels_table_open_time_ns.load(), total_other_levels_time);
  
  // Other Levels Data Block Operations
  ss << "-- Data Block Operations --" << std::endl;
  print_detailed_stat("Bloom Filter Check", other_levels_bloom_filter_check_count.load(), other_levels_bloom_filter_check_time_ns.load(), total_other_levels_time);
  print_detailed_stat("Index Block Search", other_levels_index_block_search_count.load(), other_levels_index_block_search_time_ns.load(), total_other_levels_time);
  print_detailed_stat("Data Block Read", other_levels_data_block_read_count.load(), other_levels_data_block_read_time_ns.load(), total_other_levels_time);
  
  // Other Levels Bloom Filter Statistics
  uint64_t other_levels_bf_positive = other_levels_bloom_filter_positive_count.load();
  uint64_t other_levels_bf_negative = other_levels_bloom_filter_negative_count.load();
  uint64_t other_levels_bf_total = other_levels_bf_positive + other_levels_bf_negative;
  
  ss << "-- Bloom Filter Results --" << std::endl;
  if (other_levels_bf_total > 0) {
    ss << "Total: " << other_levels_bf_total << ", ";
    ss << "Positive: " << other_levels_bf_positive << " (" << std::setprecision(1) << (other_levels_bf_positive * 100.0 / other_levels_bf_total) << "%), ";
    ss << "Negative: " << other_levels_bf_negative << " (" << std::setprecision(1) << (other_levels_bf_negative * 100.0 / other_levels_bf_total) << "%)" << std::endl;
  } else {
    ss << "No Bloom Filter checks performed" << std::endl;
  }
  
  // Other Levels Total Time Summary
  ss << "-- Other Levels Summary --" << std::endl;
  if (total_other_levels_time > 0) {
    ss << "Total Time: " << total_other_levels_time / 1000000.0 << " ms (" 
       << std::setprecision(1) << (total_other_levels_time * 100.0 / total_get_time_ns.load()) << "% of total time)" << std::endl;
  } else {
    ss << "No Other Levels operations performed" << std::endl;
  }
  
  // ==== COMBINED STATISTICS ====
  ss << std::endl << "=== COMBINED STATISTICS ===" << std::endl;
  
  // Combined totals for each operation type
  ss << "-- Combined Operation Times --" << std::endl;
  uint64_t combined_file_open = level0_file_open_time_ns.load() + other_levels_file_open_time_ns.load();
  uint64_t combined_table_open = level0_table_open_time_ns.load() + other_levels_table_open_time_ns.load();
  uint64_t combined_bloom_filter = level0_bloom_filter_check_time_ns.load() + other_levels_bloom_filter_check_time_ns.load();
  uint64_t combined_index_search = level0_index_block_search_time_ns.load() + other_levels_index_block_search_time_ns.load();
  uint64_t combined_data_read = level0_data_block_read_time_ns.load() + other_levels_data_block_read_time_ns.load();
  
  auto print_combined_stat = [&ss, this](const char* name, uint64_t time_ns) {
    if (time_ns > 0) {
      ss << name << ": " << time_ns / 1000000.0 << " ms (" 
         << std::setprecision(1) << (time_ns * 100.0 / total_get_time_ns.load()) << "% of total time)" << std::endl;
    } else {
      ss << name << ": No time consumed" << std::endl;
    }
    ss << std::setprecision(3); // 恢复精度
  };
  
  print_combined_stat("Total File Open", combined_file_open);
  print_combined_stat("Total Table Open", combined_table_open);
  print_combined_stat("Total Bloom Filter Check", combined_bloom_filter);
  print_combined_stat("Total Index Block Search", combined_index_search);
  print_combined_stat("Total Data Block Read", combined_data_read);
  
  // Combined Bloom Filter Statistics
  ss << "-- Combined Bloom Filter Results --" << std::endl;
  uint64_t total_bf_positive = level0_bf_positive + other_levels_bf_positive;
  uint64_t total_bf_negative = level0_bf_negative + other_levels_bf_negative;
  uint64_t total_bf_checks = total_bf_positive + total_bf_negative;
  
  if (total_bf_checks > 0) {
    ss << "Total Checks: " << total_bf_checks << ", ";
    ss << "Positive: " << total_bf_positive << " (" << std::setprecision(1) << (total_bf_positive * 100.0 / total_bf_checks) << "%), ";
    ss << "Negative: " << total_bf_negative << " (" << std::setprecision(1) << (total_bf_negative * 100.0 / total_bf_checks) << "%)" << std::endl;
  } else {
    ss << "No Bloom Filter checks performed across all levels" << std::endl;
  }
  
  // Final High-Level Summary
  ss << "-- Final Summary --" << std::endl;
  if (total_get_time_ns.load() > 0) {
    auto total_time = total_get_time_ns.load();
    uint64_t memory_time = memtable_lookup_time_ns.load() + immutable_lookup_time_ns.load();
    uint64_t metadata_time = fileread_search_time_ns.load();
    
    auto print_final_percentage = [&ss, total_time](const char* name, uint64_t time_ns) {
      if (time_ns > 0) {
        ss << name << ": " << time_ns / 1000000.0 << " ms (" 
           << std::setprecision(1) << (time_ns * 100.0 / total_time) << "% of total time)" << std::endl;
      } else {
        ss << name << ": No time consumed" << std::endl;
      }
      ss << std::setprecision(3); // 恢复精度
    };
    
    print_final_percentage("Memory Operations", memory_time);
    print_final_percentage("In-disk LSM Searching", metadata_time);
    print_final_percentage("Level 0 Operations", total_level0_time);
    print_final_percentage("Other Levels Operations", total_other_levels_time);
  }
    
  ss << "==============================================" << std::endl;
  return ss.str();
}


} // namespace leveldb