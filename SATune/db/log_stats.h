// db/log_stats.h

#ifndef STORAGE_LEVELDB_DB_LOG_STATS_H_
#define STORAGE_LEVELDB_DB_LOG_STATS_H_

#include <cstdint>
#include <vector>
#include <atomic>

namespace leveldb {

// A type to identify the source of a LogAndApply call for debugging and stats.
enum class CallerView { kFlush, kCompaction, kUnknown };

// Holds the details of a single conflict event detected in LogAndApply.
struct ConflictLog {
  CallerView type;  // The type of caller that experienced the conflict.
  uint64_t base_version_id;  // The version ID the operation started with.
  uint64_t new_version_id;   // The new version ID that caused the conflict.
};

// Aggregates all statistics related to LogAndApply conflicts.
struct LogAndApplyStats {
  std::atomic<uint64_t> flush_conflicts{0};
  std::atomic<uint64_t> compaction_conflicts{0};

  // A limited-size log of the most recent conflicts for detailed debugging.
  std::vector<ConflictLog> recent_conflicts;
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_LOG_STATS_H_