// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Header file for L0 Compaction Tuning related data structures.
// 用于 L0 压缩调整相关数据结构的头文件。

#ifndef STORAGE_LEVELDB_DB_L0_TUNING_H_
#define STORAGE_LEVELDB_DB_L0_TUNING_H_

#include <cstdint>
#include <atomic>

#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {

	// // Structure to hold key frequency variance statistics and other per-file tuning info.
	// // Structure to hold key frequency variance statistics for an L0 file.
  // struct L0VarianceStats {
  //   double variance = 0.0;
  //   int64_t table_number=0;
  //   int64_t unique_keys = 0; // Optional: Store the number of unique keys.
  //   int64_t total_keys = 0;  // Optional: Store the total number of keys.
  //   int64_t discarded_keys = 0; // Number of key versions that are not the latest user key.
  //   int64_t default_totalkeys_in_C0 = 0;
  //   int64_t average_kvs_in_each_file = 0;
  //   int64_t expected_file_to_tuning = 0;

  //   // Default constructor.
  //   L0VarianceStats() = default;

  //   // Parameterized constructor.
  //   L0VarianceStats(double v, int64_t uk, int64_t tk)
  //   :variance(v), unique_keys(uk), total_keys(tk) {}

  //   // Calculates the ratio of discarded keys to total keys.
  //   double discard_ratio() const {
  //     return (total_keys > 0) ? static_cast<double>(discarded_keys) / total_keys: 0.0;
  //   }

  //   // Resets the statistics (except for default_totalkeys_in_C0).
  //   // 重置统计数据 (除了 default_totalkeys_in_C0 基准值)。
  //   void Reset() {
  //     variance = 0.0;
  //     table_number = 0;
  //     unique_keys = 0;
  //     total_keys = 0;
  //     discarded_keys = 0;
  //     // We typically DON'T reset default_totalkeys_in_C0 as it's a baseline.
  //     // 我们通常不重置 default_totalkeys_in_C0，因为它是一个基准值。
  //     average_kvs_in_each_file = 0;
  //     expected_file_to_tuning = 0;
  //   }
  // };

	// // Structure to hold statistics collected during L0->L1 compaction for C0 tuning.
  // struct L0TuningStatsInCompaction {
  //   // Total input keys read from L0 (+ L1 overlaps) during L0 compaction.
  //   uint64_t total_keys_in = 0;

  //   // Unique input keys read from L0 (+ L1 overlaps) during L0 compaction.
  //   uint64_t unique_keys_in = 0;

  //   // Total output keys written to L1 during L0 compaction.
  //   uint64_t total_keys_out = 0;

  //   // Number of L0 files involved in this compaction.
  //   int num_l0_files = 0;

  //   // Average unique keys per L0 input file.
  //   double average_unique_keys_per_file = 0.0;

  //   // Number of L1 files involved in this compaction (overlapping files).
  //   int num_l1_files_in = 0;

  //   // Ratio of output keys to input keys (total_keys_out / total_keys_in).
  //   // A lower value indicates higher compaction efficiency (more data discarded).
  //   double discard_ratio = 0.0;



  //   // Default constructor to initialize all members to zero/default.
  //   L0TuningStatsInCompaction() = default;


  //   // Method to calculate the average after collection (optional).
  //   void CalculateDerivedStats() {
  //     if (num_l0_files > 0) {
  //       average_unique_keys_per_file = static_cast<double>(unique_keys_in) / num_l0_files;
  //     } else {
  //       average_unique_keys_per_file = 0.0;
  //     }
        
  //     // 计算输出/输入比率 (即您定义的 discard_ratio)
  //     if (total_keys_in > 0) {
  //       discard_ratio = static_cast<double>(total_keys_out) / total_keys_in;
  //     } else {
  //       discard_ratio = 0.0;
  //     }      
  //   }

  //   // Method to reset the stats (optional, useful for reuse).
  //   void Reset() {
  //     total_keys_in = 0;
  //     unique_keys_in = 0;
  //     total_keys_out = 0;
  //     num_l0_files = 0;
  //     average_unique_keys_per_file = 0.0;
  //     num_l1_files_in = 0;
  //     discard_ratio = 0.0;
  //   }

  //   /**
  //    * @brief 使用 fprintf 以整齐、美观的格式打印统计信息。
  //    * @param out 输出文件指针，默认为 stdout (标准输出)。
  //    */
  //   void Print(FILE* out = stdout) const {
  //     fprintf(out, " L0 Compaction Tuning Stats \n");
  //     fprintf(out, "------------------------------------------------\n");
  //     fprintf(out, "Total keys in: %llu\n", (unsigned long long)total_keys_in);
  //     fprintf(out, "Unique keys in: %llu\n", (unsigned long long)unique_keys_in);
  //     fprintf(out, "Total keys out: %llu\n", (unsigned long long)total_keys_out);
  //     fprintf(out, "Number of L0 files: %d\n", num_l0_files);
  //     fprintf(out, "Number of L1 files (overlap): %d\n", num_l1_files_in);
  //     fprintf(out, "Average unique keys per file: %.2f\n", average_unique_keys_per_file);
  //     fprintf(out, "Output/Input ratio: %.2f\n", discard_ratio);
  //     fprintf(out, "------------------------------------------------\n");
  //   }
  // };
	// // You can define related constants here as well.
	// const int kL0TuningTargetBatchSize = 4;
	// const double kL0TuningSkewSimilarityThreshold = 0.20;

	// enum class L0TuningSystemState : uint8_t { // 使用 uint8_t 节省空间
  //   IDLE,                       // 正常批次收集中 (L0 未压缩)
  //   L0_COMPACTION_RUNNING,      // L0 压缩正在进行
  //   AWAITING_BASELINE           // 等待建立基准 (可选，也可以通过 l0variancestats_.table_number == 0 判断)
	// };



  struct TableStats {
    double mu;         // 每个table的平均出现次数 = total_keys / unique_keys
    double var;        // 每个table的方差（BuildTableWithVarianceWiMerge计算所得）
    uint64_t n;        // 样本数，可以取 unique_keys 或 total_keys，看你一致性定义
  };

  // 聚合最近若干个flush文件的统计
  struct PooledStats {
    double mu;
    double var;
    double CV;
  };

  struct FlushStats {
    // 原有的丢弃比例
    double average_discard_ratio_in_flush;
    
    // Flush频率相关
    uint64_t user_bytes_written;          // 用户写入的字节数
    uint64_t total_flush_count;           // 总flush次数
    uint64_t total_bytes_flushed;         // 总共flush的字节数
    
    // 写放大相关 (Write Amplification)
    double write_amplification_ratio;     // 写放大比例
    uint64_t actual_bytes_written;        // 实际写入磁盘的字节数

    std::vector<TableStats> recent_tables_;
    PooledStats pooled;
    
    // 构造函数初始化
    FlushStats() 
      :average_discard_ratio_in_flush(0.0),
      user_bytes_written(0),
      total_flush_count(0),
      write_amplification_ratio(1.0),
      actual_bytes_written(0) {}
    
    // 更新写放大比例的辅助方法
    void update_write_amplification() {
      if (user_bytes_written > 0) {
        write_amplification_ratio = static_cast<double>(actual_bytes_written) / user_bytes_written;
      }
    }
    
    uint64_t GetAverageFileSizeinL0() const{
      return total_bytes_flushed/total_flush_count;
    }

    void CalculateCV(){
      pooled = AggregateFlushStats(recent_tables_);
    }

    PooledStats AggregateFlushStats(const std::vector<TableStats>& tables) {
      PooledStats result{0.0, 0.0, 0.0};
      if (tables.empty()) return result;

      // Step 1: 加权平均得到总体均值
      long double total_n = 0.0;
      long double weighted_mu_sum = 0.0;
      for (size_t i = 0; i < tables.size(); ++i) {
        const auto& t = tables[i];
        total_n += t.n;
        weighted_mu_sum += t.mu * t.n;
        fprintf(stderr, "[Table %02zu] n=%llu, mu=%.6f, var=%.6f, std=%.6f\n", i, (unsigned long long)t.n, t.mu, t.var, std::sqrt(t.var));
      }
      if (total_n <= 1) return result;
      double mu_total = weighted_mu_sum / total_n;

      // Step 2: 合并方差（pooled variance）
      long double numerator = 0.0;
      for (const auto& t : tables) {
          numerator += (t.n - 1) * t.var + t.n * (t.mu - mu_total) * (t.mu - mu_total);
      }
      double var_total = numerator / (total_n - 1);
      double std_total = std::sqrt(var_total);

      // Step 3: 计算 CV (Coefficient of Variation)
      double CV = (mu_total > 0.0) ? std_total / mu_total : 0.0;

      result.mu = mu_total;
      result.var = var_total;
      result.CV = CV;
      fprintf(stderr,"[POOLED RESULT] total_tables=%zu, total_n=%.0Lf\n"
        "mu_total=%.6f var_total=%.6f std_total=%.6f CV=%.6f\n",
        tables.size(), total_n,mu_total, var_total, std_total, CV);

      return result;
    }
  };


  // // This structure is used to record the slow_down/stop events during user writing. 
  // struct LevelStallStats {
  //   // Statistics for short (1ms) slowdowns when L0 file count is high.
  //   std::atomic<uint64_t> slowdown_micros{0};
  //   std::atomic<uint64_t> slowdown_count{0};
  //   // Statistics for hard stops when L0 file count or memtable count is critical.
  //   std::atomic<uint64_t> stop_micros{0};
  //   std::atomic<uint64_t> stop_count{0};
  //   std::atomic<uint64_t> total_slow_or_stop_time{0};

  //   std::atomic<uint64_t> flush_stall_time{0};

  //   // New: L0 Overlimit Detection Statistics
  //   std::atomic<uint64_t> l0_exceed_trigger_count{0};
  //   std::atomic<uint64_t> l0_exceed_total_files{0};
  //   std::atomic<uint64_t> l0_max_exceeded_files{0};

  //   LevelStallStats() = default;

  //   LevelStallStats(const LevelStallStats& other) {
  //     slowdown_micros.store(other.slowdown_micros.load());
  //     slowdown_count.store(other.slowdown_count.load());
  //     stop_micros.store(other.stop_micros.load());
  //     stop_count.store(other.stop_count.load());
  //     total_slow_or_stop_time.store(other.total_slow_or_stop_time.load());
  //   }

  //   void RecordL0Exceed(const int current_files, int trigger_limit) {
  //     if (current_files > trigger_limit) {
  //       int exceeded = current_files - trigger_limit;
  //       l0_exceed_trigger_count.fetch_add(1);
  //       l0_exceed_total_files.fetch_add(exceeded);

  //       // 更新最大超出值（原子操作）
  //       uint64_t current_max = l0_max_exceeded_files.load();
  //       while (exceeded > current_max) {
  //         if (l0_max_exceeded_files.compare_exchange_weak(current_max, exceeded)) {
  //           break;
  //         }
  //       }
  //       // fprintf(stdout, "[L0_EXCEED] current_files=%d, trigger_limit=%d, exceeded=%d, exceed_count=%lu, max_exceeded=%lu\n",
  //       //   current_files, trigger_limit, exceeded, l0_exceed_trigger_count.load(), l0_max_exceeded_files.load());
  //     }
  //   }

  //   void CalculateDeltaAndUpdate(const LevelStallStats& current, const LevelStallStats& previous) {
  //     slowdown_micros.store(current.slowdown_micros.load() - previous.slowdown_micros.load());
  //     slowdown_count.store(current.slowdown_count.load() - previous.slowdown_count.load());
  //     stop_micros.store(current.stop_micros.load() - previous.stop_micros.load());
  //     stop_count.store(current.stop_count.load() - previous.stop_count.load());
  //     total_slow_or_stop_time.store(current.total_slow_or_stop_time.load() - previous.total_slow_or_stop_time.load());

  //     //
  //     l0_exceed_trigger_count.store(current.l0_exceed_trigger_count-previous.l0_exceed_trigger_count);
  //     l0_exceed_total_files.store(current.l0_exceed_total_files-previous.l0_exceed_total_files);
  //     l0_max_exceeded_files.store(current.l0_max_exceeded_files-previous.l0_max_exceeded_files);
  //   }


  //   // 自定义赋值操作符
  //   LevelStallStats& operator=(const LevelStallStats& other) {
  //     if (this != &other) {
  //       slowdown_micros.store(other.slowdown_micros.load());
  //       slowdown_count.store(other.slowdown_count.load());
  //       stop_micros.store(other.stop_micros.load());
  //       stop_count.store(other.stop_count.load());
  //       total_slow_or_stop_time.store(other.total_slow_or_stop_time.load());
  //     }
  //     return *this;
  //   }

  //   int64_t CalculateStallTimeDifference(const LevelStallStats& other) {
  //     uint64_t current_stall_time = total_slow_or_stop_time.load();
  //     uint64_t last_stall_time = other.total_slow_or_stop_time.load();
        
  //     // 返回差值（current - last）
  //     // 正值表示当前batch stall时间增加了（性能变差）
  //     // 负值表示当前batch stall时间减少了（性能变好）
  //     return static_cast<int64_t>(current_stall_time) - static_cast<int64_t>(last_stall_time);
  //   }   

  // };

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_L0_TUNING_H_