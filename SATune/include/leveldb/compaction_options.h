// 文件：include/leveldb/compaction_options.h
#ifndef STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_H_
#define STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_H_

#include <cstddef>
#include <string>

#include "leveldb/export.h"

namespace leveldb {

// L0/Compaction 相关参数的集成 struct，提供默认值
struct LEVELDB_EXPORT CompactionOptions {

  // 当 L0 文件数 > level0_compaction_trigger 时触发 L0→L1 compaction。
  int level0_compaction_trigger=4;

  // 当 L0 文件数 >= level0_slowdown_writes_trigger 时让写入线程 sleep。
  int level0_slowdown_writes_trigger=12;

  // 当 L0 文件数 >= level0_stop_writes_trigger 时阻止新的写入。
  int level0_stop_writes_trigger=20;

  int64_t file_size_generated_in_compaction = 64LL * 1024 * 1024;

    // 每个 level 的总数据量基数（L1 的最大总数据量），默认为 64MiB
  int64_t max_bytes_for_level1_base  = 512LL * 1024 * 1024;

  // 每上升一级，max_bytes_for_level_base 乘以此值，默认为 10
  double max_bytes_for_level1_multiplier    = 10.0;

  // the default size of block is 4KiB
  size_t block_size=4*1024;

  // 1. 乘数切换的层级
  //    例如，如果设置为3，则L4的大小将由L3的大小乘以新的乘数决定。
  //    L2->L3 仍然使用旧的乘数。
  int level_for_multiplier_switch;

  // 2. 切换后使用的新乘数
  double max_bytes_for_level_multiplier_after_switch;

};

}  // namespace leveldb
#endif  // STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_H_
