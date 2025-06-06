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

namespace leveldb {

	// Structure to hold key frequency variance statistics and other per-file tuning info.
	// Structure to hold key frequency variance statistics for an L0 file.
  struct L0VarianceStats {
    double variance = 0.0;
    int64_t table_number=0;
    int64_t unique_keys = 0; // Optional: Store the number of unique keys.
    int64_t total_keys = 0;  // Optional: Store the total number of keys.
    int64_t discarded_keys = 0; // Number of key versions that are not the latest user key.
    int64_t default_totalkeys_in_C0 = 0;
    int64_t average_kvs_in_each_file = 0;
    int64_t expected_file_to_tuning = 0;

    // Default constructor.
    L0VarianceStats() = default;

    // Parameterized constructor.
    L0VarianceStats(double v, int64_t uk, int64_t tk)
    :variance(v), unique_keys(uk), total_keys(tk) {}

    // Calculates the ratio of discarded keys to total keys.
    double discard_ratio() const {
      return (total_keys > 0) ? static_cast<double>(discarded_keys) / total_keys: 0.0;
    }

    // Resets the statistics (except for default_totalkeys_in_C0).
    // 重置统计数据 (除了 default_totalkeys_in_C0 基准值)。
    void Reset() {
      variance = 0.0;
      table_number = 0;
      unique_keys = 0;
      total_keys = 0;
      discarded_keys = 0;
      // We typically DON'T reset default_totalkeys_in_C0 as it's a baseline.
      // 我们通常不重置 default_totalkeys_in_C0，因为它是一个基准值。
      average_kvs_in_each_file = 0;
      expected_file_to_tuning = 0;
    }
  };

	// Structure to hold statistics collected during L0 compaction for C0 tuning.
  struct L0TuningStatsInCompaction {
    // Total input keys read from L0 (+ L1 overlaps) during L0 compaction.
    // L0 压缩期间从 L0 (+ L1 重叠部分) 读取的总输入键数。
    uint64_t total_keys_in = 0;

    // Unique input keys read from L0 (+ L1 overlaps) during L0 compaction.
    // L0 压缩期间从 L0 (+ L1 重叠部分) 读取的唯一输入键数。
    uint64_t unique_keys_in = 0;

    // Total output keys written to L1 during L0 compaction.
    // L0 压缩期间写入 L1 的总输出键数。
    uint64_t total_keys_out = 0;

    // Number of L0 files involved in this compaction.
    // 本次 L0 压缩涉及的 L0 文件数量。
    int num_l0_files = 0;

    // Average unique keys per L0 input file.
    // 每个 L0 输入文件的平均唯一键数。
    double average_unique_keys_per_file = 0.0;

    // Default constructor to initialize all members to zero/default.
    // 默认构造函数，将所有成员初始化为零或默认值。
    L0TuningStatsInCompaction() = default;
    // Method to calculate the average after collection (optional).
    // 收集后计算平均值的方法（可选）。
    void CalculateAverage() {
      if (num_l0_files > 0) {
        average_unique_keys_per_file =
            static_cast<double>(unique_keys_in) / num_l0_files;
      } else {
        average_unique_keys_per_file = 0.0;
      }
    }

    // Method to reset the stats (optional, useful for reuse).
    // 重置统计数据的方法（可选，便于重用）。
    void Reset() {
        total_keys_in = 0;
        unique_keys_in = 0;
        total_keys_out = 0;
        num_l0_files = 0;
        average_unique_keys_per_file = 0.0;
    }
  };
	// You can define related constants here as well.
	// 您也可以在这里定义相关的常量。
	const int kL0TuningTargetBatchSize = 4;
	const double kL0TuningSkewSimilarityThreshold = 0.20;

	enum class L0TuningSystemState : uint8_t { // 使用 uint8_t 节省空间
    IDLE,                       // 正常批次收集中 (L0 未压缩)
    L0_COMPACTION_RUNNING,      // L0 压缩正在进行
    AWAITING_BASELINE           // 等待建立基准 (可选，也可以通过 l0variancestats_.table_number == 0 判断)
	};


}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_L0_TUNING_H_