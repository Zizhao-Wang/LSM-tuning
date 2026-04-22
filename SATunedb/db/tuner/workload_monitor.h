// db_impl_tuning.h - DBImpl与自动调优的集成
#ifndef TUNING_FRAMEWORK_WORKLOAD_MONITOR
#define TUNING_FRAMEWORK_WORKLOAD_MONITOR

#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cmath>
#include <vector>

#include "rocksdb/rocksdb_namespace.h"
#include "db/tuner/l0_tuning.h"
#include "db/tuner/tuner_perf_context.h"
#include "rocksdb/env.h"
#include "db/column_family.h" 

namespace ROCKSDB_NAMESPACE{

  class DBImpl;

  struct TuningResult {
    bool needs_change = false;

    int new_l0_trigger = -1;
    int64_t  new_l1_size = -1; 
    double new_level_multiplier = -1.0;
    // new segment：WST (Write Stall Trigger / Slowdown Trigger) in the SATune paper.
    int new_slowdown_trigger = -1;

    std::string reason;  //tuning reason
  };


  enum class TuningActionType {
    NO_ACTION        = 0x00000000,
    L0_TRIGGER       = 0x00000001,
    L1_SIZE          = 0x00000002,
    LEVEL_MULTIPLIER = 0x00000004,
    ALL_PARAMETER    = 0x00000008, 
  };

  struct SimpleTuningRecord {
    bool has_pending_changes = false;
    uint32_t changed_params = 0;  // 用你的enum作为位掩码
    
    // 保存原始值用于回滚
    int old_l0_trigger = 0;
    int old_l1_size = 0; 
    double old_level_multiplier = 0;

    // 保存变化量（新增）
    int l0_trigger_delta = 0;        // L0_TRIGGER变化量：+3表示增加3，-2表示减少2
    int l1_size_delta = 0;           // L1_SIZE变化量
    double level_multiplier_delta = 0;  // LEVEL_MULTIPLIER变化量

    int new_l0_trigger = 0;
    int new_l1_size = 0; 
    double new_level_multiplier = 0;

    int actual_l1_size = 0;

    // 标记参数已修改
    void MarkChanged(TuningActionType param) {
      changed_params |= static_cast<uint32_t>(param);
      has_pending_changes = true;
    }
    
    // 检查参数是否被修改
    bool IsChanged(TuningActionType param) {
      return (changed_params & static_cast<uint32_t>(param)) != 0;
    }

    bool CheckAndPauseL0Recording() const {
      return (changed_params & static_cast<uint32_t>(TuningActionType::L0_TRIGGER)) ||
        (changed_params & static_cast<uint32_t>(TuningActionType::L1_SIZE));
    }

    // 开始新的tuning session - 仅记录当前值作为基准
    void StartTuning(int current_l0_trigger, int current_l1_size, double current_level_multiplier) {

      // 保存当前值作为本次tuning的起点
      changed_params = 0;

      old_l0_trigger = current_l0_trigger;
      old_l1_size = current_l1_size;
      old_level_multiplier = current_level_multiplier;
      
      // 注意：不要重置 changed_params，这会丢失历史记录！
      // 只重置 has_pending_changes 表示新的tuning开始
      has_pending_changes = false;
      
      fprintf(stdout, "Started tuning session with baseline: L0=%d, L1=%d, LM=%.2f\n", current_l0_trigger, current_l1_size, current_level_multiplier);
    }

    // 结束tuning session - 计算delta并更新changed_params
    void EndTuning(int final_l0_trigger, int final_l1_size, double final_level_multiplier, 
        int fianl_actual_l1_size) {
      
      // 处理 -1 的情况：-1 表示不改变，使用原值
      if (final_l0_trigger == -1) {
        new_l0_trigger = old_l0_trigger;  // 保持原值
      } else {
        new_l0_trigger = final_l0_trigger;
      }
      
      if (final_l1_size == -1) {
        new_l1_size = old_l1_size;  // 保持原值
      } else {
        new_l1_size = final_l1_size;
      }

      if (final_level_multiplier == -1) {
        new_level_multiplier = old_level_multiplier;  // 保持原值
      } else {
        new_level_multiplier = final_level_multiplier;
      }

      actual_l1_size = fianl_actual_l1_size;

      // 计算各参数的变化量
      l0_trigger_delta = new_l0_trigger - old_l0_trigger;
      l1_size_delta = new_l1_size - old_l1_size;
      level_multiplier_delta = new_level_multiplier - old_level_multiplier;
      
      // 根据delta更新changed_params位掩码
      if (l0_trigger_delta != 0) {
        MarkChanged(TuningActionType::L0_TRIGGER);
        fprintf(stdout, "L0_TRIGGER changed by %+d (from %d to %d)\n",  l0_trigger_delta, old_l0_trigger, new_l0_trigger);
      }
      
      if (l1_size_delta != 0) {
        MarkChanged(TuningActionType::L1_SIZE);
        // fprintf(stdout, "L1_SIZE changed by %.2f (from %.2fMiB to %.2fMiB)\n",  (l1_size_delta/1048576.0), (old_l1_size/1048576.0), (new_l1_size/1048576.0));
      }
      
      if (level_multiplier_delta != 0) {
        MarkChanged(TuningActionType::LEVEL_MULTIPLIER);
        fprintf(stdout, "LEVEL_MULTIPLIER changed by %.2f (from %.2f to %.2f)\n",  level_multiplier_delta, old_level_multiplier, new_level_multiplier);
      }
      
      //maybe we need to record the final values in last tuning point
      //Yes, we did it.
      
      // fprintf(stdout, "Ended tuning session. Changed params mask: 0x%X\n", changed_params);
      
      // If any parameters change, set the pending flag.
      has_pending_changes = (changed_params != 0);
    }


    // Check whether the specific "bit" is set to 1 
    // 例子: IsParamModified(0x00000002) 检查第2位是否为1 (L1_SIZE是否被修改)
    // 例子: IsParamModified(0x00000004) 检查第3位是否为1 (LEVEL_MULTIPLIER是否被修改)
    // 原理: changed_params=0x00000006 & 0x00000002 = 0x00000002 != 0, 返回true
    bool IsSpecificParamModified(uint32_t param_mask) {
      return (changed_params & param_mask) != 0;
    }
    
    // 清除记录
    void Clear() {
      has_pending_changes = false;
      changed_params = 0;
    }
    
    // 打印修改的参数
    void PrintChanges() {
      fprintf(stdout,"Changed params (0x%X): ", changed_params);
      if (IsChanged(TuningActionType::L0_TRIGGER)){
        fprintf(stdout, "L0_TRIGGER changed, ");
      } 
      if (IsChanged(TuningActionType::L1_SIZE)){
        fprintf(stdout, "L1_SIZE changed, ");
      } 
      if (IsChanged(TuningActionType::LEVEL_MULTIPLIER)) {
        fprintf(stdout, "LEVEL_MULTIPLIER changed, ");
      }
      printf("\n");
    }
  };

  struct IntervalPerformanceStats {
    uint64_t write_micros;
    uint64_t read_micros;
    uint64_t seek_micros;
    
    uint64_t write_count;
    uint64_t read_count;
    uint64_t seek_count;

    IntervalPerformanceStats() 
    : write_micros(0),read_micros(0), seek_micros(0),write_count(0), read_count(0), seek_count(0) {}

    // ============================================================================
    // SATune: Seek length tracking - total Next() calls during seeks
    // ============================================================================
    uint64_t seek_length_count;  // Total Next() calls

    double AvgWriteNanos() const {
      return (write_count > 0) ? static_cast<double>(write_micros) / write_count : 0.0;
    }
    
    double AvgReadNanos() const {
      return (read_count > 0) ? static_cast<double>(read_micros) / read_count : 0.0;
    }
    
    double AvgSeekNanos() const {
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
    void Reset() {
      write_micros = 0;
      read_micros  = 0;
      seek_micros  = 0;
        
      write_count = 0;
      read_count  = 0;
      seek_count  = 0;
    }
  };

  enum class TuningDirection {
    kReadOptimization = 0,   // 优化读性能
    kWriteOptimization = 1,  // 优化写性能
    kBalanced = 2,           // 平衡模式（预留）
    kUnknown = 3             // 未确定（初始状态）
  };

  class PerformanceMonitor {
    public:
    explicit PerformanceMonitor(TunerPerfContext& tuner_perf_context_param, std::shared_ptr<Logger> logger) 
    : perf_ctx_(tuner_perf_context_param),
    tuning_log_(logger),
    read_ratio(0.0),
    level_stall_stats_ptr_(nullptr),
    interval_stats_for_overflow{}, 
    cached_avg_l0_file_size_(-1), 
    tuning_num_stats(0),
    is_the_first_tuning_try(true),
    level_first_creation{false, true, true, true, true, true, true},
    consecutive_write_stall_count_(0),
    consecutive_read_heavy_overflow_count_(0),
    c0_cumulative_increase_(0),
    c0_baseline_(-1),
    force_l0_compaction_(false),
    force_l1_compaction_(false),
    compaction_being_forced_(-1),
    just_increased_for_read_heavy_(false){
      tuning_records_ = SimpleTuningRecord();
      for (int i = 0; i < 7; i++) {
        level_compaction_in_progress_[i] = false;
      }  
    }

  void StartTuning() {
    perf_ctx_.ResetAll();
    batch_counter_++; 
  }

  void UpdateLevelCreation(int level) {
      if (level >= 0 && level < 7 && !level_first_creation[level]) {
        level_first_creation[level] = false;
      }
    }

  int GetActiveLevels(ColumnFamilyData* cfd) const;

  int GetDeepestExistingLevel() const;
  // 检查是否应该执行 Normal Tuning
  bool ShouldPerformNormalTuning(int level) const;

  int64_t GetAverageL0FileSize(ColumnFamilyData* cfd);

  int64_t GetActualL1TotalSize(ColumnFamilyData* cfd);

  void  PerformInitialTuning(const MutableCFOptions* current_options, uint64_t total_stall_micros, TuningResult& result);


  void NormalTuning(ColumnFamilyData* cfd,int level, TuningResult& result, 
    uint64_t cumulative_stall_time, uint64_t avgfilesize, const MutableCFOptions* current_options);

  double CalculateMultiplierDelta(int level, double current_multiplier, double target_percentage_change);

  void LGTuning(int level, ColumnFamilyData* cfd, const MutableCFOptions* current_options, 
    uint64_t cumulative_stall_time, TuningResult& result);  

  void HandleLGTuningWithoutStall(int level,TuningResult& result,
    const MutableCFOptions* current_options, uint64_t cumulative_stall_time);


  void HandleL0StallOnly(ColumnFamilyData* cfd,TuningResult& result, 
    const MutableCFOptions* current_options);

  void HandleL0OverflowOnly(ColumnFamilyData* cfd,TuningResult& result,  
    const MutableCFOptions* current_options,int max_exceed);

  void HandleL0StallAndOverflow(ColumnFamilyData* cfd, TuningResult& result,
     const MutableCFOptions* current_options, int max_exceed);

  void HandleL0Overflow(ColumnFamilyData* cfd, int level,
    TuningResult& result,const MutableCFOptions* current_options);

  // ============================================================================
  // SATune: Mark that the forced compaction is ready to execute
  // ============================================================================
  void MarkCompactionReady(int output_level);

  void NotifyCompactionCompleted(int level);

  bool IsForceCompaction(int level) const;

  TuningResult CheckAndHandleBatch(DBImpl* db_im, int level, ColumnFamilyData* cfd = nullptr);


  // ============================================================================
  // SATune: Compaction in-progress tracking (防止浪费统计数据)
  // ============================================================================
  void MarkCompactionInProgress(int level);     // 标记compaction开始

  void ClearCompactionInProgress(int level);    // 清除compaction标记

  bool IsCompactionInProgress(int level) const; // 检查是否在进行中

  const SimpleTuningRecord& GetTuningRecords(){
    return tuning_records_;
  }

  void UpdateIntervalStats();

  private:

    TunerPerfContext& perf_ctx_; 
    std::shared_ptr<Logger> tuning_log_;
    double read_ratio;
    ColumnFamilyData::LevelStallStats* level_stall_stats_ptr_;
    IntervalPerformanceStats interval_stats_for_overflow;
    // 缓存的 L0 平均文件大小（workload 特性不变，只计算一次）
    int64_t cached_avg_l0_file_size_;

    // LevelStallStats lastbatch_stall_;     
    // LevelStallStats currentbatch_stall_; 

    // LevelStallStats cumulative_current_stall_; 
    // LevelStallStats cumulative_last_stall_; 
    
    int64_t total_ops=0;
    int64_t batch_ops=0;
    uint64_t batch_counter_ = 0;
    
    uint64_t tuning_num_stats;
    bool is_the_first_tuning_try;

    // This struct is used to record the operations in the last tuning point
    // sometimes, we need rollback the paramters in the "CompactionOptions" struct
    SimpleTuningRecord tuning_records_;
    bool level_first_creation[7];
    TuningDirection tuning_direction_;

    int consecutive_write_stall_count_;
    int consecutive_read_heavy_overflow_count_;



    // ============================================================================
    // SATune: Force L0 compaction tracking
    // ============================================================================
    int c0_cumulative_increase_;                // C0累计增长量
    int c0_baseline_;                           // C0基线值（用于计算增长量）
    bool force_l0_compaction_;                  // 是否强制L0->L1 compaction
    bool force_l1_compaction_;                  // 是否强制L1->L2 compaction
    int compaction_being_forced_;               // 当前正在被强制执行的level (-1表示无)

      
    // SATune: Compaction in-progress tracking (per-level)
    bool level_compaction_in_progress_[7];
    
    static constexpr int kC0CumulativeThreshold = 16;  // C0累计增长阈值

    bool just_increased_for_read_heavy_;
  };

}
#endif // DB_IMPL_TUNING_H
