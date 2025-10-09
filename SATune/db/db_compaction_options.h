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

  std::atomic<int64_t> total_flush_time_us;    // 总时间
  std::atomic<int32_t> flush_count;            // 记录次数
  std::atomic<int32_t> max_samples;            // 最大采样次数
  std::atomic<int64_t> average_flush_time_us;

  std::atomic<bool>    sampling_enabled;      // 是否完成采样

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
    max_bytes_for_level_multiplier_after_switch(o.max_bytes_for_level_multiplier_after_switch),
    total_flush_time_us(0),
    flush_count(0),
    max_samples(o.level0_compaction_trigger),          
    average_flush_time_us(0),
    sampling_enabled(true) {}

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

  void SetL0TriggersCustom(int new_C0) {
    int new_slowdown;
    int new_stop;
    
    // 这里放你自己推导的公式
    // 例如：
    // new_slowdown = your_formula_for_slowdown(new_C0);
    // new_stop = your_formula_for_stop(new_C0, new_slowdown);
    
    // 临时示例公式（你需要替换成你的实际公式）
    new_slowdown = new_C0 + 5;  // 替换成你的公式
    new_stop = new_C0 + 10;     // 替换成你的公式
    
    // 原子存储
    level0_compaction_trigger.store(new_C0, std::memory_order_relaxed);
    level0_slowdown_writes_trigger.store(new_slowdown, std::memory_order_relaxed);
    level0_stop_writes_trigger.store(new_stop, std::memory_order_relaxed);
  }


  void EnableFlushTimeSampling(bool enable) {
    sampling_enabled.store(enable, std::memory_order_relaxed);
  }


  bool ShouldSampleFlushTime() const {
    int32_t max_sample_count = max_samples.load(std::memory_order_relaxed);
    return flush_count.load(std::memory_order_relaxed) < max_sample_count;
  }

  void RecordFlushTime(int64_t flush_time_us) {
    int32_t current_count = flush_count.load(std::memory_order_relaxed);
    int32_t max_sample_count = max_samples.load(std::memory_order_relaxed);
    
    // 如果还没达到最大采样数，继续采样
    if (current_count < max_sample_count) {
      total_flush_time_us.fetch_add(flush_time_us, std::memory_order_relaxed);
      current_count = flush_count.fetch_add(1, std::memory_order_relaxed) + 1;
      
      // 如果这是最后一次采样，计算并缓存平均值
      if (current_count == max_sample_count) {
        int64_t total = total_flush_time_us.load(std::memory_order_relaxed);
        int64_t average = total / max_sample_count;
        average_flush_time_us.store(total / max_sample_count, std::memory_order_relaxed);
        // fprintf(stderr, "[FlushTime] Sampling completed!\n");
        // fprintf(stderr, "[FlushTime] Total samples: %d\n", max_sample_count);
        // fprintf(stderr, "[FlushTime] Total time: %lld μs\n", static_cast<long long>(total));
        // fprintf(stderr, "[FlushTime] Average time: %lld μs (%.3f ms)\n", static_cast<long long>(average), average / 1000.0);
        // fprintf(stderr, "[FlushTime] Min observed: ? μs (not tracked)\n");
        // fprintf(stderr, "[FlushTime] Max observed: ? μs (not tracked)\n");
        // fprintf(stderr, "========================================\n");
      }
    }
  }

  // --- Get Average Flush Time ---
  int64_t GetAverageFlushTime() const {
    int32_t count = flush_count.load(std::memory_order_relaxed);
    int32_t max_sample_count = max_samples.load(std::memory_order_relaxed);
    
    if (count == 0) return 0;
    
    if (count >= max_sample_count) {
      // 采样完成，返回缓存的平均值
      return average_flush_time_us.load(std::memory_order_relaxed);
    } else {
      // 还在采样中，实时计算
      int64_t total = total_flush_time_us.load(std::memory_order_relaxed);
      return total / count;
    }
  }

};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_ATOMIC_H_