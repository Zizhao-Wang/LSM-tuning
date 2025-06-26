// include/leveldb/compaction_options_atomic.h
#ifndef STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_ATOMIC_H_
#define STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_ATOMIC_H_

#include <atomic>
#include <algorithm> // 用于 std::max
#include <cmath> 

#include "leveldb/compaction_options.h"

namespace leveldb {

struct CompactionOptionsAtomic {
  std::atomic<int>    level0_compaction_trigger;
  std::atomic<int>    level0_slowdown_writes_trigger;
  std::atomic<int>    level0_stop_writes_trigger;

  std::atomic<int64_t> file_size_generated_in_compaction;

  std::atomic<int64_t> max_bytes_for_level1_base;
  std::atomic<double>  max_bytes_for_level1_multiplier;

  std::atomic<size_t>  block_size;

  std::atomic<int> level_for_multiplier_switch;

  // 2. 切换后使用的新乘数
  std::atomic<double> max_bytes_for_level_multiplier_after_switch;

  // 从非原子版初始化
  explicit CompactionOptionsAtomic(const CompactionOptions& o)
    : level0_compaction_trigger(o.level0_compaction_trigger),
    level0_slowdown_writes_trigger(o.level0_slowdown_writes_trigger),
    level0_stop_writes_trigger(o.level0_stop_writes_trigger),
    file_size_generated_in_compaction(o.file_size_generated_in_compaction),
    max_bytes_for_level1_base(o.max_bytes_for_level1_base),
    max_bytes_for_level1_multiplier(o.max_bytes_for_level1_multiplier),
    block_size(o.block_size),
    level_for_multiplier_switch(o.level_for_multiplier_switch),
    max_bytes_for_level_multiplier_after_switch(o.max_bytes_for_level_multiplier_after_switch) {}

  // --- New Function: Set L0 Triggers (Internal Logic) ---
  void SetL0Triggers_Internal(int new_C0) {
    int new_slowdown;
    int new_stop;
        
    if (new_C0 <= 20) {
      // 如果new_C0在20以下（包含20），固定使用25和36
      new_slowdown = 25;
      new_stop = 36;
    } else {
      // 如果new_C0大于20，使用动态计算
      new_slowdown = static_cast<int>(std::ceil(new_C0 * 1.5));  // 1.5倍，向上取整
      new_stop = new_C0 * 2;  // 2倍
            
      // 确保最小差距（防止计算误差导致的问题）
      new_slowdown = std::max(new_C0 + 1, new_slowdown);
      new_stop = std::max(new_slowdown + 1, new_stop);
    }
        
    // 原子存储
    level0_compaction_trigger.store(new_C0, std::memory_order_relaxed);
    level0_slowdown_writes_trigger.store(new_slowdown, std::memory_order_relaxed);
    level0_stop_writes_trigger.store(new_stop, std::memory_order_relaxed);
  }
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_ATOMIC_H_