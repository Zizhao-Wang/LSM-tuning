// include/leveldb/compaction_options_atomic.h
#ifndef STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_ATOMIC_H_
#define STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_ATOMIC_H_

#include <atomic>
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

  // 从非原子版初始化
  explicit CompactionOptionsAtomic(const CompactionOptions& o)
    : level0_compaction_trigger(o.level0_compaction_trigger),
    level0_slowdown_writes_trigger(o.level0_slowdown_writes_trigger),
    level0_stop_writes_trigger(o.level0_stop_writes_trigger),
    file_size_generated_in_compaction(o.file_size_generated_in_compaction),
    max_bytes_for_level1_base(o.max_bytes_for_level1_base),
    max_bytes_for_level1_multiplier(o.max_bytes_for_level1_multiplier),
    block_size(o.block_size) {}
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_ATOMIC_H_