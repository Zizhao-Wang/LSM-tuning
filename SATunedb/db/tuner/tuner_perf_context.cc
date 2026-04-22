/**
 * tuner_perf.cc
 * 
 * TunerPerfContext 和 TunerPerfTimer 的函数实现
 */

#include "db/tuner/tuner_perf_context.h"

#include <cstdio>

#include "rocksdb/options.h"

namespace ROCKSDB_NAMESPACE {

// ============================================================================
// TunerPerfContext 实现
// ============================================================================

void TunerPerfContext::ResetNormalTuning() {
  normal_tuning_read_micros.store(0, std::memory_order_relaxed);
  normal_tuning_write_micros.store(0, std::memory_order_relaxed);
  normal_tuning_seek_micros.store(0, std::memory_order_relaxed);
  normal_tuning_seek_count.store(0, std::memory_order_relaxed);
  normal_tuning_read_count.store(0, std::memory_order_relaxed);
  normal_tuning_write_count.store(0, std::memory_order_relaxed);
}

NormalTuningStats TunerPerfContext::GetAndResetNormalTuning() {
  NormalTuningStats stats;
  
  // Read time (read + seek)
  uint64_t read_micros1 = normal_tuning_read_micros.exchange(0, std::memory_order_relaxed);
  uint64_t seek_micros1 = normal_tuning_seek_micros.exchange(0, std::memory_order_relaxed);
  stats.read_time_us = static_cast<double>(read_micros + seek_micros);
  
  // Write time
  stats.write_time_us = static_cast<double>(normal_tuning_write_micros.exchange(0, std::memory_order_relaxed));
  
  // Read count (read + seek)
  uint64_t read_cnt = normal_tuning_read_count.exchange(0, std::memory_order_relaxed);
  uint64_t seek_cnt = normal_tuning_seek_count.exchange(0, std::memory_order_relaxed);
  stats.read_count = read_cnt + seek_cnt;
  
  // Write count
  stats.write_count = normal_tuning_write_count.exchange(0, std::memory_order_relaxed);
  
  return stats;
}

NormalTuningStats TunerPerfContext::GetNormalTuning() const {
  NormalTuningStats stats;
  
  // Read time (read + seek) - 只读取，不重置
  uint64_t read_us = normal_tuning_read_micros.load(std::memory_order_relaxed);
  uint64_t seek_us = normal_tuning_seek_micros.load(std::memory_order_relaxed);
  stats.read_time_us = static_cast<double>(read_us + seek_us);
  
  // Write time
  stats.write_time_us = static_cast<double>(
      normal_tuning_write_micros.load(std::memory_order_relaxed));
  
  // Read count (read + seek)
  uint64_t read_cnt = normal_tuning_read_count.load(std::memory_order_relaxed);
  uint64_t seek_cnt = normal_tuning_seek_count.load(std::memory_order_relaxed);
  stats.read_count = read_cnt + seek_cnt;
  
  // Write count
  stats.write_count = normal_tuning_write_count.load(std::memory_order_relaxed);
  
  return stats;
}

bool TunerPerfContext::CheckBatchWriteHeavy() const {
  uint64_t w_lat = batch_write_micros.load(std::memory_order_relaxed);
  uint64_t r_lat = batch_read_micros.load(std::memory_order_relaxed) +  batch_seek_micros.load(std::memory_order_relaxed);

  if (w_lat == 0 && r_lat) {
    return false;
  }
  return w_lat > r_lat;
}

bool TunerPerfContext::IsReadSlowerForL0Overflow() const {
  uint64_t w_micros = batch_write_micros.load(std::memory_order_relaxed);
  uint64_t w_count = batch_write_count.load(std::memory_order_relaxed);
  
  uint64_t r_micros = batch_read_micros.load(std::memory_order_relaxed);
  uint64_t r_count = batch_read_count.load(std::memory_order_relaxed);
  uint64_t s_micros = batch_seek_micros.load(std::memory_order_relaxed);
  uint64_t s_count = batch_seek_count.load(std::memory_order_relaxed);
  

  if (w_count == 0 || (r_count + s_count) == 0) {
    return false;
  }
  
  // Simple comparison
  return (double)(r_micros + s_micros) > (double)w_micros;
}


double TunerPerfContext::CalculateLatencyGap(bool* is_read_slower) const {
  // ============================================================================
  // SATune: Calculate normalized latency gap between read and write
  // Returns:
  //   > 0: read is slower (should decrease C0)
  //   < 0: write is slower (should increase C0)
  //   = 0: gap is insignificant (< 10% of larger value)
  // ============================================================================

  uint64_t w_micros = batch_write_micros.load(std::memory_order_relaxed);
  uint64_t w_count = batch_write_count.load(std::memory_order_relaxed);


  uint64_t r_micros = batch_read_micros.load(std::memory_order_relaxed);
  uint64_t r_count = batch_read_count.load(std::memory_order_relaxed);
  uint64_t s_micros = batch_seek_micros.load(std::memory_order_relaxed);
  uint64_t s_count = batch_seek_count.load(std::memory_order_relaxed);

  // 4. Calculate Average Write Latency (check for div by zero)
  double avg_write = (w_count > 0) ? (double)w_micros / w_count : 0.0;

  // 5. Calculate Average "Composite" Read Latency
  uint64_t total_read_micros = r_micros + s_micros;
  uint64_t total_read_count = r_count + s_count;
  double avg_read = (total_read_count > 0) ? 
      (double)total_read_micros / total_read_count : 0.0;
  
  if (is_read_slower != nullptr) {
    if (total_read_micros > w_micros) {
      *is_read_slower = true;  // 读比写慢
    } else {
      *is_read_slower = false; // 写比读慢 (或相等)
    }
  }  
  
  if (w_count == 0 || total_read_count == 0) {
    return 0.0; 
  }

  // Calculate gap as percentage of the larger value
  // Example: read=40, write=30 → gap=10, max=40 → percentage=0.25 (25%)
  double diff = std::abs(avg_read - avg_write);
  double max_latency = std::max(avg_read, avg_write);
  double gap_percentage = diff / max_latency;  // 0.0 to 1.0  

  if (gap_percentage < 0.05) {
    return 0.0;  // Insignificant gap, no tuning needed
  }

  // Return signed gap:
  //   Positive if read > write (read is slower, should decrease C0)
  //   Negative if write > read (write is slower, should increase C0)
  return diff;
}



void TunerPerfContext::ResetOverflowBatch() {
  // 1. Reset Write Stats
  batch_write_micros.store(0, std::memory_order_relaxed);
  batch_write_count.store(0, std::memory_order_relaxed);

  // 2. Reset Seek Stats
  batch_seek_micros.store(0, std::memory_order_relaxed);
  batch_seek_count.store(0, std::memory_order_relaxed);

  // 3. Reset Point Read Stats
  batch_read_micros.store(0, std::memory_order_relaxed);
  batch_read_count.store(0, std::memory_order_relaxed);
}

void TunerPerfContext::Reset() {
  write_micros.store(0);
  read_micros.store(0);
  seek_micros.store(0);
  write_count.store(0);
  read_count.store(0);
  seek_count.store(0);
  seek_length_count.store(0);
  LG_tuning_seek_length_count.store(0);
}

void TunerPerfContext::ResetLGTuning() {
  LG_tuning_write_micros.store(0);
  LG_tuning_read_micros.store(0);
  LG_tuning_seek_micros.store(0);
  LG_tuning_write_count.store(0);
  LG_tuning_read_count.store(0);
  LG_tuning_seek_count.store(0);

  for (auto& stat : level_stats) {
    stat.ResetBatch();
  }

  LG_tuning_seek_length_count.store(0);
}

void TunerPerfContext::ResetAll() {
  Reset();
}

std::string TunerPerfContext::ToString() const {
  char buf[512];
  snprintf(buf, sizeof(buf),
    "write: count=%lu, time=%.3f ms\n"
    "read:  count=%lu, time=%.3f ms\n"
    "seek:  count=%lu, time=%.3f ms\n",
    write_count.load(), write_micros.load() / 1e6,read_count.load(), read_micros.load() / 1e6, seek_count.load(), seek_micros.load() / 1e6);
  return std::string(buf);
}

double TunerPerfContext::AvgWriteNanos() const {
  uint64_t count = write_count.load();
  return count > 0 ? (double)write_micros.load() / count : 0;
}

double TunerPerfContext::AvgReadNanos() const {
  uint64_t count = read_count.load();
  return count > 0 ? (double)read_micros.load() / count : 0;
}

double TunerPerfContext::AvgSeekNanos() const {
  uint64_t count = seek_count.load();
  return count > 0 ? (double)seek_micros.load() / count : 0;
}

double TunerPerfContext::BatchAvgWriteNanos() const {
  uint64_t count = LG_tuning_write_count.load();
  return count > 0 ? (double)LG_tuning_write_micros.load() / count : 0;
}

double TunerPerfContext::BatchAvgReadNanos() const {
  uint64_t count = LG_tuning_read_count.load();
  return count > 0 ? (double)LG_tuning_read_micros.load() / count : 0;
}

double TunerPerfContext::BatchAvgSeekNanos() const {
  uint64_t count = LG_tuning_seek_count.load();
  return count > 0 ? (double)LG_tuning_seek_micros.load() / count : 0;
}

TuningStatsSnapshot TunerPerfContext::GetLGTuningStats() const {
  TuningStatsSnapshot snapshot; 
  // 一次性把原子变量的值 load 出来赋值给普通变量
  snapshot.write_micros = LG_tuning_write_micros.load(std::memory_order_relaxed);
  snapshot.read_micros  = LG_tuning_read_micros.load(std::memory_order_relaxed);
  snapshot.seek_micros  = LG_tuning_seek_micros.load(std::memory_order_relaxed);
    
  snapshot.write_count = LG_tuning_write_count.load(std::memory_order_relaxed);
  snapshot.read_count  = LG_tuning_read_count.load(std::memory_order_relaxed);
  snapshot.seek_count  = LG_tuning_seek_count.load(std::memory_order_relaxed);

  // ============================================================================
  // SATune: Load seek length (total Next() calls)
  // ============================================================================
  snapshot.seek_length_count = LG_tuning_seek_length_count.load(std::memory_order_relaxed);
  
  return snapshot;
}

bool TunerPerfContext::IsReadSlowerForLGTuning() const {
  
  // Load all statistics
  uint64_t lg_write_micros = LG_tuning_write_micros.load(std::memory_order_relaxed);
  uint64_t lg_read_micros = LG_tuning_read_micros.load(std::memory_order_relaxed);
  uint64_t lg_seek_micros = LG_tuning_seek_micros.load(std::memory_order_relaxed);

  // Calculate total read latency (read + seek)
  uint64_t total_read_micros = lg_read_micros + lg_seek_micros;
   
  // Determine if read is slower based on total latency
  return total_read_micros > lg_write_micros;
}


TuningStatsSnapshot TunerPerfContext::GetOverallStats() const {
  TuningStatsSnapshot snapshot; 
  // 一次性把原子变量的值 load 出来赋值给普通变量
  snapshot.write_micros = write_micros.load(std::memory_order_relaxed);
  snapshot.read_micros  = read_micros.load(std::memory_order_relaxed);
  snapshot.seek_micros  = seek_micros.load(std::memory_order_relaxed);
    
  snapshot.write_count = write_count.load(std::memory_order_relaxed);
  snapshot.read_count  = read_count.load(std::memory_order_relaxed);
  snapshot.seek_count  = seek_count.load(std::memory_order_relaxed);

  // ============================================================================
  // SATune: Load seek length (total Next() calls)
  // ============================================================================
  snapshot.seek_length_count = seek_length_count.load(std::memory_order_relaxed);
  
  return snapshot;
}

// ============================================================================
// TunerPerfTimer implementation
// ============================================================================

TunerPerfTimer::TunerPerfTimer(std::initializer_list<TimerTarget> targets, SystemClock* clock)
  : targets_(targets),
  clock_(clock){
  start_ = clock_->NowMicros();
}

TunerPerfTimer::~TunerPerfTimer() {
  uint64_t elapsed = clock_->NowMicros() - start_;

  for (const auto& target : targets_) {
    if (target.time_metric) {
      target.time_metric->fetch_add(elapsed);
    }
    if (target.count_metric) {
      target.count_metric->fetch_add(1);
    }
  }
}



// ============================================================================
// TunerPerfTimer 实现
// ============================================================================

Status CreateTunerLoggerFromOptions(const std::string& dbname,
                               const DBOptions& options,
                               std::shared_ptr<Logger>* logger) {
  if (options.tuning_log) {
    *logger = options.tuning_log;  
    return Status::OK();
  }

  Env* env = options.env;
  std::string db_absolute_path;
  Status s = env->GetAbsolutePath(dbname, &db_absolute_path);
  if (!s.ok()) {
    return s;
  }

  // 如果用户指定了 tuning_log_dir
  std::string log_dir = options.tuning_log_dir.empty() ? db_absolute_path : options.tuning_log_dir;
  std::string log_file = log_dir + "/satune_tuning_log.txt";

  // 创建 tuning_log 目录（如果不存在）
  s = env->CreateDirIfMissing(log_dir);
  if (!s.ok()) {
    return s;
  }

  // ============================ Modification============================
  // Old code: *logger = std::make_shared<TuningLogger>(log_file);
  // New code: Get FileSystem from options.env and pass it
  // Note: The Env class has a method GetFileSystem() which returns a std::shared_ptr<FileSystem>
  *logger = std::make_shared<TuningLogger>(env->GetFileSystem(), log_file, options.info_log_level);
  // ========================================================================

  // 【调试代码】打印出生地址
  fprintf(stderr, "[DEBUG] Created TuningLogger at address: %p\n", logger->get());
  // ======================================================================

  return Status::OK();
}



}  // namespace ROCKSDB_NAMESPACE
