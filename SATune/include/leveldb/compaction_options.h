// 文件：include/leveldb/compaction_options.h
#ifndef STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_H_
#define STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_H_

#include <cstddef>
#include <string>

#include "leveldb/export.h"

namespace leveldb {

// L0/Compaction 相关参数的集成 struct，提供默认值
struct LEVELDB_EXPORT CompactionOptions {



  // 当 L0 文件数 > compaction_trigger 时触发 L0→L1 compaction。
  int compaction_trigger=4;

  // 当 L0 文件数 >= slowdown_writes_trigger 时让写入线程 sleep。
  int slowdown_writes_trigger=12;

  // 当 L0 文件数 >= stop_writes_trigger 时阻止新的写入。
  int stop_writes_trigger=20;

  int64_t file_size_generated_in_compaction=64*1024*1024;

  // the default size of block is 4KiB
  size_t block_size=4*1024;

};

}  // namespace leveldb
#endif  // STORAGE_LEVELDB_INCLUDE_COMPACTION_OPTIONS_H_
