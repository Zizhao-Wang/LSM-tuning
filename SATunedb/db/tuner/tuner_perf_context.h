/**
 * tuner_perf_context.h
 * 
 * 一个轻量级的性能监控模块，用于统计 RocksDB 操作的时间
 * 设计思路类似于 RocksDB 的 perf_context，但完全独立
 * 
 * 使用方法:
 *   // 在函数开头使用宏，会自动在作用域结束时统计时间
 *   TUNER_PERF_TIMER_GUARD(write);
 *   
 *   // 获取统计结果
 *   auto& ctx = tuner_perf_context;
 *   printf("write_micros: %lu\n", ctx.write_micros);
 *   
 *   // 重置统计
 *   ctx.Reset();
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <chrono>
#include <string>
#include <ostream>
#include <fstream>
#include <mutex>
#include <atomic>

#include "rocksdb/rocksdb_namespace.h"
#include "rocksdb/env.h"
#include "rocksdb/file_system.h"
#include "rocksdb/system_clock.h"

namespace ROCKSDB_NAMESPACE  {

enum RangeType {
  kNoRange,      // 没有 range query
  kShortRange,   // r >= 0.5
  kLongRange,    // r <= 0.1
  kMediumRange   // 0.1 < r < 0.5
};

struct TuningStatsSnapshot {
  uint64_t write_micros;
  uint64_t read_micros;
  uint64_t seek_micros;
  
  uint64_t write_count;
  uint64_t read_count;
  uint64_t seek_count;

  // ============================================================================
  // SATune: Seek length tracking - total Next() calls during seeks
  // ============================================================================
  uint64_t seek_length_count;  // Total Next() calls

  double AvgWriteMicros() const {
    return (write_count > 0) ? static_cast<double>(write_micros) / write_count : 0.0;
  }
  
  double AvgReadMicros() const {
    return (read_count > 0) ? static_cast<double>(read_micros) / read_count : 0.0;
  }
  
  double AvgSeekMicros() const {
    return (seek_count > 0) ? static_cast<double>(seek_micros) / seek_count : 0.0;
  }

  // SATune: Average seek length (Next() calls per Seek())
  double AvgSeekLength() const {
    return (seek_count > 0) ? static_cast<double>(seek_length_count) / seek_count : 0.0;
  }

  // SATune: Total operations count
  uint64_t TotalOps() const {
    return write_count + read_count + seek_count;
  }

  // SATune: Weighted read count
  // Counts: Get operations + Seek operations + Next/Prev calls
  // Each seek sequence = 1 (Seek) + N (Next calls)
  uint64_t WeightedReadCount() const {
    return read_count + seek_count + seek_length_count;
  }

  // SATune: Weighted total operations count
  // write_count + (read_count + seek_count + seek_length_count)
  
  uint64_t WeightedTotalOps() const {
    return write_count + WeightedReadCount();
  }
  
  // SATune: Weighted read ratio (0.0 to 1.0)
  // Returns the proportion of read operations in the workload
  double WeightedReadRatio() const {
    uint64_t total = WeightedTotalOps();
    return (total > 0) ? static_cast<double>(WeightedReadCount()) / total : 0.0;
  }

  //==============================
  double AvgCostPerKeyInRange() const {
    if (seek_count == 0 || seek_length_count == 0) return 0.0;
    // Total time for all range queries / total keys scanned
    return AvgSeekMicros() / AvgSeekLength();
  }

    // SATune: Calculate cost ratio: c_range / c_point
  // Returns: r = (T_range / n_keys) / T_point
  double RangeToPointCostRatio() const {
    double c_point = AvgReadMicros();  // 点查询平均耗时
    if (c_point < 1.0) return 0.0;  // 避免除零
    
    double c_range = AvgCostPerKeyInRange();  // range 中每个 key 的成本
    return c_range / c_point;
  }

  RangeType ClassifyRangeType(double short_threshold = 0.5, double long_threshold = 0.1) const {
    if (seek_count == 0) return kNoRange;
    
    double r = RangeToPointCostRatio();
    if (r >= short_threshold) return kShortRange;
    if (r <= long_threshold) return kLongRange;
    return kShortRange;
  }

  //============================
};

struct NormalTuningStats {
  double read_time_us;         // Total read time (read + seek)
  double write_time_us;        // Total write time
  uint64_t read_count;         // Total read operations (read + seek)
  uint64_t write_count;        // Total write operations
  
  NormalTuningStats() 
      : read_time_us(0.0), 
        write_time_us(0.0),
        read_count(0),
        write_count(0) {}
  
  // Calculate average read latency
  double AvgReadMicros() const {
    return (read_count > 0) ? (read_time_us / read_count) : 0.0;
  }
  
  // Calculate average write latency
  double AvgWriteMicros() const {
    return (write_count > 0) ? (write_time_us / write_count) : 0.0;
  }
};

struct TunerPerfContext {
  static constexpr int kMaxLevels = 8;  // memtable + L0-L6

  // ========== Global Statistics ==========
  std::atomic<uint64_t> write_micros{0};
  std::atomic<uint64_t> read_micros{0};
  std::atomic<uint64_t> seek_micros{0};
  std::atomic<uint64_t> write_count{0};
  std::atomic<uint64_t> read_count{0};
  std::atomic<uint64_t> seek_count{0};
  // ============================================================================
  // SATune: Seek length tracking (how many Next() calls per Seek)
  // ============================================================================
  std::atomic<uint64_t> seek_length_count{0};        // Total Next() calls

  //============ for L0 overflowing ==========
  std::atomic<uint64_t> batch_write_micros{0};  // Total write latency in batch
  std::atomic<uint64_t> batch_write_count{0};  // Total write count

  std::atomic<uint64_t> batch_seek_micros{0};   // Total seek latency
  std::atomic<uint64_t> batch_seek_count{0};   // Total seek operations

  std::atomic<uint64_t> batch_read_micros{0};   // Total read latency 
  std::atomic<uint64_t> batch_read_count{0};   // Total read ops
  //============== for L0 overflowing==========



  // ========== Per-Level Read Statistics ==========
  // Index mapping: [0]=memtable, [1]=L0, [2]=L1, ..., [7]=L6
  struct LevelReadStats {
    std::atomic<uint64_t> read_micros{0};
    std::atomic<uint64_t> read_count{0};
    std::atomic<uint64_t> LG_tuning_read_micros{0};
    std::atomic<uint64_t> LG_tuning_read_count{0};
    
    void Reset() {
      read_micros.store(0, std::memory_order_relaxed);
      read_count.store(0, std::memory_order_relaxed);
    }
    
    void ResetBatch() {
      LG_tuning_read_micros.store(0, std::memory_order_relaxed);
      LG_tuning_read_count.store(0, std::memory_order_relaxed);
    }
  };


  std::atomic<uint64_t> LG_tuning_write_micros{0};
  std::atomic<uint64_t> LG_tuning_read_micros{0};
  std::atomic<uint64_t> LG_tuning_seek_micros{0};
  std::atomic<uint64_t> LG_tuning_write_count{0};
  std::atomic<uint64_t> LG_tuning_read_count{0};
  std::atomic<uint64_t> LG_tuning_seek_count{0};
  std::atomic<uint64_t> LG_tuning_seek_length_count{0};  // Batch total Next() calls

  std::array<LevelReadStats, kMaxLevels> level_stats;

  std::atomic<uint64_t> normal_tuning_seek_micros{0};
  std::atomic<uint64_t> normal_tuning_read_micros{0};
  std::atomic<uint64_t> normal_tuning_write_micros{0};
  std::atomic<uint64_t> normal_tuning_seek_count{0};
  std::atomic<uint64_t> normal_tuning_read_count{0};
  std::atomic<uint64_t> normal_tuning_write_count{0}; 

  // Helper: Convert LSM level to array index
  // -1 (memtable) -> 0, 0 (L0) -> 1, 1 (L1) -> 2, ...
  static int LevelToIndex(int level) {
    return level + 1;
  }

  void ResetNormalTuning();
  NormalTuningStats GetAndResetNormalTuning();
  NormalTuningStats GetNormalTuning() const;

  bool CheckBatchWriteHeavy() const;

  bool IsReadSlowerForL0Overflow() const;
  // SATune: Calculate absolute latency gap between Read (Point+Seek) and Write
  // Formula: Gap = | avg_read_composite - avg_write |
  double CalculateLatencyGap(bool* is_read_slower) const;
  void ResetOverflowBatch();

  void Reset();
  void ResetLGTuning();
  void ResetAll();
    
  std::string ToString() const;
    
  double AvgWriteNanos() const;
  double AvgReadNanos() const;
  double AvgSeekNanos() const;
    
  double BatchAvgWriteNanos() const;
  double BatchAvgReadNanos() const;
  double BatchAvgSeekNanos() const;

  TuningStatsSnapshot GetLGTuningStats() const;
  bool IsReadSlowerForLGTuning() const;

  TuningStatsSnapshot GetOverallStats() const;
};

inline TunerPerfContext tuner_perf_context;

struct TimerTarget {
  std::atomic<uint64_t>* time_metric;  
  std::atomic<uint64_t>* count_metric;  
  
  TimerTarget(std::atomic<uint64_t>* time, std::atomic<uint64_t>* count = nullptr)
    : time_metric(time), count_metric(count) {}
};

class TunerPerfTimer {
  public:

  TunerPerfTimer(std::initializer_list<TimerTarget> targets,SystemClock* clock);
  ~TunerPerfTimer();
  TunerPerfTimer(const TunerPerfTimer&) = delete;
  TunerPerfTimer& operator=(const TunerPerfTimer&) = delete;
    
  private:
  std::vector<TimerTarget> targets_;
  SystemClock* clock_;
  uint64_t start_;
};

class TunerPerfTimerGuardWithLevel {
 public:
  TunerPerfTimerGuardWithLevel(
      std::atomic<uint64_t>* global_micros,
      std::atomic<uint64_t>* global_count,
      std::atomic<uint64_t>* batch_micros,
      std::atomic<uint64_t>* batch_count,
      std::atomic<uint64_t>* overflow_micros,
      std::atomic<uint64_t>* overflow_count,
      std::atomic<uint64_t>* normal_tuning_batch_micros,
      std::atomic<uint64_t>* normal_tuning_batch_count,
      SystemClock* clock)
      : global_micros_(global_micros),
        global_count_(global_count),
        batch_micros_(batch_micros),
        batch_count_(batch_count),
        overflow_micros_(overflow_micros),
        overflow_count_(overflow_count),
        normal_tuning_batch_micros_(normal_tuning_batch_micros),  
        normal_tuning_batch_count_(normal_tuning_batch_count),       
        clock_(clock),
        start_time_(clock->NowMicros()) {}

  ~TunerPerfTimerGuardWithLevel() {
    uint64_t elapsed_micros = clock_->NowMicros() - start_time_;
    
    // 全局统计
    global_micros_->fetch_add(elapsed_micros);
    global_count_->fetch_add(1);
    batch_micros_->fetch_add(elapsed_micros);
    batch_count_->fetch_add(1);

    overflow_micros_->fetch_add(elapsed_micros);
    overflow_count_->fetch_add(1);

    normal_tuning_batch_micros_->fetch_add(elapsed_micros);
    normal_tuning_batch_count_->fetch_add(1);
  }

 private:
  std::atomic<uint64_t>* global_micros_;
  std::atomic<uint64_t>* global_count_;
  std::atomic<uint64_t>* batch_micros_;
  std::atomic<uint64_t>* batch_count_;
  std::atomic<uint64_t>* overflow_micros_;
  std::atomic<uint64_t>* overflow_count_;
  std::atomic<uint64_t>* normal_tuning_batch_micros_;
  std::atomic<uint64_t>* normal_tuning_batch_count_;
  SystemClock* clock_;
  uint64_t start_time_;
};

// ============================================================================
// SATune: Timer with extra counter increment (for "write" tracking)
// ============================================================================ 
#define TUNER_PERF_TIMER_GUARD(metric, clock) \
  TunerPerfTimer tuner_timer_##metric({ \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.metric##_micros, \
    &ROCKSDB_NAMESPACE::tuner_perf_context.metric##_count}, \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.LG_tuning_##metric##_micros, \
    &ROCKSDB_NAMESPACE::tuner_perf_context.LG_tuning_##metric##_count}, \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.batch_##metric##_micros, \
    &ROCKSDB_NAMESPACE::tuner_perf_context.batch_##metric##_count} \
  },clock)

// ============================================================================
// SATune: Timer with extra counter increment ("write" tracking with adding normal tuning )
// ============================================================================ 
#define TUNER_PERF_TIMER_GUARD_WITH_NORMAL_TUNING(metric, clock) \
  TunerPerfTimer tuner_timer_##metric({ \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.metric##_micros, \
    &ROCKSDB_NAMESPACE::tuner_perf_context.metric##_count}, \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.LG_tuning_##metric##_micros, \
    &ROCKSDB_NAMESPACE::tuner_perf_context.LG_tuning_##metric##_count}, \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.batch_##metric##_micros, \
    &ROCKSDB_NAMESPACE::tuner_perf_context.batch_##metric##_count}, \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.normal_tuning_##metric##_micros, \
    &ROCKSDB_NAMESPACE::tuner_perf_context.normal_tuning_##metric##_count}  /* 只累加时间，不计数 */ \
  },clock)        

// ============================================================================
// SATune: Timer with extra counter increment (for seek performance tracking and seek length tracking)
// ============================================================================        
#define TUNER_PERF_TIMER_GUARD_WITH_COUNT_AND_NORMAL_TUNING(metric, counter, clock1) \
  TunerPerfTimer tuner_timer_##metric({ \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.metric##_micros, \
      &ROCKSDB_NAMESPACE::tuner_perf_context.metric##_count}, \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.LG_tuning_##metric##_micros, \
      &ROCKSDB_NAMESPACE::tuner_perf_context.LG_tuning_##metric##_count}, \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.batch_##metric##_micros, \
      &ROCKSDB_NAMESPACE::tuner_perf_context.batch_##metric##_count}, \
    {&ROCKSDB_NAMESPACE::tuner_perf_context.normal_tuning_##metric##_micros, \
      &ROCKSDB_NAMESPACE::tuner_perf_context.normal_tuning_##metric##_count} \
  }, clock1); \
  ROCKSDB_NAMESPACE::tuner_perf_context.counter.fetch_add(1, std::memory_order_relaxed); \
  ROCKSDB_NAMESPACE::tuner_perf_context.LG_tuning_##counter.fetch_add(1, std::memory_order_relaxed)

// ============================================================================
// SATune: Timer with extra counter increment (for "read with multiply levels" tracking)
// ============================================================================         
#define TUNER_PERF_TIMER_GUARD_WITH_LEVEL(metric, clock) \
  TunerPerfTimerGuardWithLevel tuner_timer_##metric( \
        &(tuner_perf_context.metric##_micros), \
        &(tuner_perf_context.metric##_count), \
        &(tuner_perf_context.LG_tuning_##metric##_micros), \
        &(tuner_perf_context.LG_tuning_##metric##_count), \
        &(tuner_perf_context.batch_##metric##_micros), \
        &(tuner_perf_context.batch_##metric##_count), \
        &(tuner_perf_context.normal_tuning_##metric##_micros), \
        &(tuner_perf_context.normal_tuning_##metric##_count), clock)

class TuningLogger : public Logger {
public:

  TuningLogger(const std::shared_ptr<FileSystem>& fs, const std::string& file_name,const InfoLogLevel log_level = InfoLogLevel::INFO_LEVEL)
  : Logger(log_level), 
  fs_(fs),
  mutex_() {
    
    IOOptions io_opts;
    IODebugContext dbg_ctx;

    // 2. 调用 FileSystem 的工厂方法 NewLogger
    // 这行代码完全复刻了 AutoRollLogger::ResetLogger() 里的逻辑
    // 实际上，fs_ 会根据操作系统创建一个 PosixLogger (或其他实现) 并赋值给 result
    std::shared_ptr<Logger> result;
    Status s = fs_->NewLogger(file_name, io_opts, &result, &dbg_ctx);

    if (s.ok() && result != nullptr) {
      // 3. 将生产出来的底层 Logger 接管过来
      // reset 之后，logger_ 就指向了真正会写文件的那个对象 (例如 PosixLogger)
      logger_=result;
      logger_->SetInfoLogLevel(log_level);
      fprintf(stdout, "[TuningLogger] Successfully opened log file: %s\n", file_name.c_str());
    } else {
      fprintf(stderr, "[TuningLogger] FAILED to open log file: %s. Reason: %s\n", file_name.c_str(), s.ToString().c_str());
    }
  }

  // 析构函数
  ~TuningLogger() override {
    CloseImpl();
  }

  using Logger::Logv;
  void Logv(const char* format, va_list ap) override {
    // 1. 加锁：保护内部的 logger_ 指针不被并发修改
    std::lock_guard<std::mutex> lock(mutex_);

    // 2. 检查底层工人是否存在
    if (logger_) {
      // 3. 转发调用！
      // TuningLogger 不做任何实际的 IO 操作，全部委托给 logger_
      logger_->Logv(format, ap);
    }
  }

  // 【核心功能 2】获取文件大小
  size_t GetLogFileSize() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logger_) {
      return logger_->GetLogFileSize();
    }
    return 0;
  }

  // 【核心功能 3】刷盘
  void Flush() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logger_) {
      logger_->Flush();
    }
  }

  // 既然继承了 Logger，就要处理 InfoLogLevel
  void SetInfoLogLevel(const InfoLogLevel log_level) override {
    Logger::SetInfoLogLevel(log_level);
    
    std::lock_guard<std::mutex> lock(mutex_);
    // 再设置内部工人的
    if (logger_) {
      logger_->SetInfoLogLevel(log_level);
    }
  }
  
  InfoLogLevel GetInfoLogLevel() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logger_) {
        return logger_->GetInfoLogLevel();
    }
    return Logger::GetInfoLogLevel();
  }

protected:
  // 实现关闭逻辑
  Status CloseImpl() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logger_) {
      return logger_->Close();
    }
    return Status::OK();
  }

private:
  // --- 你要求的三个核心变量 ---
  
  // 1. 真正的干活的人 (Worker)
  // 在 Linux 上它通常是 PosixLogger
  std::shared_ptr<Logger> logger_;

  // 2. 工厂 (Factory)
  // 用来创建上面的 logger_，以及处理底层 IO
  std::shared_ptr<FileSystem> fs_;

  // 3. 锁 (Guard)
  // 保护 logger_ 指针的并发安全
  mutable std::mutex mutex_; // 对应 RocksDB 的 mutable port::Mutex mutex_;
}; 


Status CreateTunerLoggerFromOptions(const std::string& dbname,const DBOptions& options, std::shared_ptr<Logger>* logger);

}  // namespace ROCKSDB