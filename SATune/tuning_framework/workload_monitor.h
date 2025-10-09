// db_impl_tuning.h - DBImpl与自动调优的集成
#ifndef TUNING_FRAMEWORK_WORKLOAD_MONITOR
#define TUNING_FRAMEWORK_WORKLOAD_MONITOR

#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cmath>
#include <vector>

#include "leveldb/performance_profile.h"
#include "db/l0_tuning.h"

namespace leveldb{

  class DBImpl;

  class WorkloadMonitor {
  private:
    struct WorkloadStats {
      std::atomic<uint64_t> read_count{0};
      std::atomic<uint64_t> write_count{0};
      std::atomic<uint64_t> range_read_count{0};
      std::chrono::steady_clock::time_point start_time;
          
      WorkloadStats() : start_time(std::chrono::steady_clock::now()) {}
          
      void reset() {
        read_count.store(0);
        write_count.store(0);
        start_time = std::chrono::steady_clock::now();
      }
          
      double get_read_write_ratio() const {
        uint64_t reads = read_count.load();
        uint64_t writes = write_count.load();
        if (writes == 0) return reads > 0 ? INFINITY : 0.0;
        return static_cast<double>(reads) / writes;
      }
          
      uint64_t get_total_ops() const {
        return read_count.load() + write_count.load();
      }
          
      double get_duration_seconds() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
        return duration.count() / 1000.0;
      }
    };
      
    WorkloadStats current_workload;
    WorkloadStats previous_workload;
      
    mutable std::mutex stats_mutex;
      
    // 配置参数
    double ratio_change_threshold;    // 比例变化阈值
    uint64_t min_ops_for_comparison;  // 最小操作数才进行比较
    double min_time_window_seconds;   // 最小时间窗口
      
  public:
      explicit WorkloadMonitor(double threshold = 0.2, uint64_t min_ops = 100,double min_time = 5.0) 
        : ratio_change_threshold(threshold),min_ops_for_comparison(min_ops), min_time_window_seconds(min_time) {}
      
      // 记录读操作
      void record_read() {
        current_workload.read_count.fetch_add(1);
      }
      
      // 记录写操作
      void record_write() {
        current_workload.write_count.fetch_add(1);
      }
      
      // 批量记录操作
      void record_operations(uint64_t reads, uint64_t writes) {
        current_workload.read_count.fetch_add(reads);
        current_workload.write_count.fetch_add(writes);
      }
      
      // 获取当前读写比例
      double get_current_ratio() const {
        return current_workload.get_read_write_ratio();
      }
      
      // 获取上一个时间窗口的读写比例
      double get_previous_ratio() const {
        std::lock_guard<std::mutex> lock(stats_mutex);
        return previous_workload.get_read_write_ratio();
      }
      
      // 检查工作负载是否发生了显著变化
      bool has_workload_changed() const {
        std::lock_guard<std::mutex> lock(stats_mutex);
          
        // 检查是否有足够的数据进行比较
        if (current_workload.get_total_ops() < min_ops_for_comparison ||
          previous_workload.get_total_ops() < min_ops_for_comparison) {
          return false;
        }
          
        double current_ratio = current_workload.get_read_write_ratio();
        double prev_ratio = previous_workload.get_read_write_ratio();
          
          // 处理无穷大的情况
        if (std::isinf(current_ratio) && std::isinf(prev_ratio)) {
          return false;  // 都是只读，没有变化
        }
        if (std::isinf(current_ratio) || std::isinf(prev_ratio)) {
          return true;   // 一个是只读一个不是，有变化
        }
          
          // 计算比例变化幅度
        double ratio_change = std::abs(current_ratio - prev_ratio) / std::max(prev_ratio, 1e-9); 
        return ratio_change > ratio_change_threshold;
      }
      
      // 轮转统计窗口（将当前统计变为前一个，重置当前统计）
      void rotate_window() {
        std::lock_guard<std::mutex> lock(stats_mutex);
          
        // 检查时间窗口是否足够
        if (current_workload.get_duration_seconds() < min_time_window_seconds) {
          return;  // 时间窗口太短，不进行轮转
        }
          
        // 保存当前统计到previous
        previous_workload.read_count.store(current_workload.read_count.load());
        previous_workload.write_count.store(current_workload.write_count.load());
        previous_workload.start_time = current_workload.start_time;
          
        // 重置当前统计
        current_workload.reset();
      }
      
      // 获取详细统计信息
      struct DetailedStats {
        uint64_t current_reads;
        uint64_t current_writes;
        double current_ratio;
        double current_duration;
        uint64_t previous_reads;
        uint64_t previous_writes;
        double previous_ratio;
        bool workload_changed;
      };
      
      DetailedStats get_detailed_stats() const {
        std::lock_guard<std::mutex> lock(stats_mutex);
            
        DetailedStats stats;
        stats.current_reads = current_workload.read_count.load();
        stats.current_writes = current_workload.write_count.load();
        stats.current_ratio = current_workload.get_read_write_ratio();
        stats.current_duration = current_workload.get_duration_seconds();
        stats.previous_reads = previous_workload.read_count.load();
        stats.previous_writes = previous_workload.write_count.load();
        stats.previous_ratio = previous_workload.get_read_write_ratio();
        stats.workload_changed = has_workload_changed();
            
        return stats;
      }
      
      // 打印统计信息
      void print_stats() const {
        auto stats = get_detailed_stats();
          
        fprintf(stdout, "=== Workload Monitor Stats ===\n");
        fprintf(stdout, "Current Window:\n");
        fprintf(stdout, "  Reads: %lu, Writes: %lu\n", stats.current_reads, stats.current_writes);
        fprintf(stdout, "  Ratio: %.3f, Duration: %.2fs\n", stats.current_ratio, stats.current_duration);
          
        fprintf(stdout, "Previous Window:\n");
        fprintf(stdout, "  Reads: %lu, Writes: %lu\n", stats.previous_reads, stats.previous_writes);
        fprintf(stdout, "  Ratio: %.3f\n", stats.previous_ratio);
          
        fprintf(stdout, "Workload Changed: %s\n", stats.workload_changed ? "YES" : "NO");
        fprintf(stdout, "==============================\n");
      }
      
      // 重置所有统计
      void reset() {
        std::lock_guard<std::mutex> lock(stats_mutex);
        current_workload.reset();
        previous_workload.reset();
      }
  };


  struct TuningResult {
    bool needs_change = false;
    
    // new value
    int new_l0_trigger = -1;
    int new_l1_size = -1; 
    int new_level_multiplier = -1;
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
    int old_level_multiplier = 0;

    // 保存变化量（新增）
    int l0_trigger_delta = 0;        // L0_TRIGGER变化量：+3表示增加3，-2表示减少2
    int l1_size_delta = 0;           // L1_SIZE变化量
    int level_multiplier_delta = 0;  // LEVEL_MULTIPLIER变化量

    int new_l0_trigger = 0;
    int new_l1_size = 0; 
    int new_level_multiplier = 0;

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

    // 开始新的tuning session - 仅记录当前值作为基准
    void StartTuning(int current_l0_trigger, int current_l1_size, int current_level_multiplier) {

      // 保存当前值作为本次tuning的起点
      changed_params = 0;

      old_l0_trigger = current_l0_trigger;
      old_l1_size = current_l1_size;
      old_level_multiplier = current_level_multiplier;
      
      // 注意：不要重置 changed_params，这会丢失历史记录！
      // 只重置 has_pending_changes 表示新的tuning开始
      has_pending_changes = false;
      
      fprintf(stdout, "Started tuning session with baseline: L0=%d, L1=%d, LM=%d\n", 
        current_l0_trigger, current_l1_size, current_level_multiplier);
    }

    // 结束tuning session - 计算delta并更新changed_params
    void EndTuning(int final_l0_trigger, int final_l1_size, int final_level_multiplier, 
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
        fprintf(stdout, "L1_SIZE changed by %+d (from %d to %d)\n",  l1_size_delta, old_l1_size, new_l1_size);
      }
      
      if (level_multiplier_delta != 0) {
        MarkChanged(TuningActionType::LEVEL_MULTIPLIER);
        fprintf(stdout, "LEVEL_MULTIPLIER changed by %+d (from %d to %d)\n",  level_multiplier_delta, old_level_multiplier, new_level_multiplier);
      }
      
      //maybe we need to record the final values in last tuning point
      //Yes, we did it.
      
      fprintf(stdout, "Ended tuning session. Changed params mask: 0x%X\n", changed_params);
      
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


class PerformanceMonitor {
  public:

  explicit PerformanceMonitor(PerformanceProfiler* profiler, uint64_t batch_size = 10000) 
    : profiler_(profiler), batch_size_(batch_size),is_first_l0_compaction(false),
    tuning_num_stats(0),is_first_l1_compaction(false) {}

  // 开始新的批次监控
  void StartBatch() {
    if (!profiler_ || !profiler_->IsEnabled()){
      return ;
    }

    batch_counter_++; 
    batch_ops = 0;
    profiler_->GetStats().ResetBatchCounters();
  }
    
  // 结束批次并获取当前批次性能
  ThroughputStats EndCumulativeStats() {
    if (!profiler_ || !profiler_->IsEnabled()) {
      return ThroughputStats();
    }
      
    return profiler_->GetStats().GetTotalThroughputStats();
  }
    
  ThroughputStats EndBatchStats() {
    if (!profiler_ || !profiler_->IsEnabled()) {
      return ThroughputStats();
    }
      
    return profiler_->GetStats().GetBatchThroughputStats();
  }
    
  // 开始测试微调策略
  void StartTuningTest(const std::string& strategy_name) {
    current_tuning_strategy_ = strategy_name;
  }
    
  // 比较当前批次与基准性能
  double CompareWithBaseline(const ThroughputStats& current_batch) {
    if (cumulative_last_throughput_.Iops_per_sec == 0) {
      return 0.0;  // no throughput data
    }
        
    double baseline_ops = cumulative_last_throughput_.Iops_per_sec;
    double current_ops = current_batch.Iops_per_sec;
        
    return (current_ops - baseline_ops) / baseline_ops;
  }
    
  const std::string& GetCurrentStrategy() const { 
    return current_tuning_strategy_; 
  }
    
  // 获取基准性能
  const ThroughputStats& GetCurrentBatch() const { return cumulative_current_throughput_; }
    
  // 获取上一批次性能
  const ThroughputStats& GetLastBatch() const { return cumulative_last_throughput_; }
    
  // 重置监控器
  void Reset() {
    cumulative_current_throughput_ = ThroughputStats();
    cumulative_last_throughput_ = ThroughputStats();
    current_tuning_strategy_.clear();
    batch_counter_ = 0;
  }
  
  void Addops(){
    total_ops++;
    batch_ops++;
  }

  // 检查当前批次是否已达到批次大小上限
  bool ShouldStartNewBatch() {
    if (!profiler_ || !profiler_->IsEnabled()){
      fprintf(stdout,"No profiler. Waiting...\n");
      return false;
    } 

    if(batch_ops>=batch_size_){
      return true;
    }
    return false;
  }

  bool CheckAndHandleBatch(DBImpl* db_im, int level, bool is_tuning=false);

  bool ChechAndDecideStartWriteTuning(const FlushStats& flush_stats_);

  void set_first_l0_compaction();

  void set_first_l1_compaction();

  const SimpleTuningRecord& GetTuningRecords(){
    return tuning_records_;
  }

  void ApplyTuningWithExemption(DBImpl* db_im);

private:

  PerformanceProfiler* profiler_;

  // 
  ThroughputStats currentbatch_throughput_;           
  ThroughputStats lastbatch_throughput_;    

  ThroughputStats cumulative_current_throughput_;           
  ThroughputStats cumulative_last_throughput_; 

  //
  LevelStallStats lastbatch_stall_;     
  LevelStallStats currentbatch_stall_; 

  LevelStallStats cumulative_current_stall_; 
  LevelStallStats cumulative_last_stall_; 


  std::string current_tuning_strategy_;
  
  int64_t total_ops=0;
  int64_t batch_ops=0;
  uint64_t batch_size_;                    // 批次大小（操作数量）
  uint64_t batch_counter_ = 0;
  uint64_t tuning_num_stats;

  bool is_first_l0_compaction;
  bool is_first_l1_compaction = 0;


  // This struct is used to record the operations in the last tuning point
  // sometimes, we need rollback the paramters in the "CompactionOptions" struct
  SimpleTuningRecord tuning_records_;

  bool level_first_creation[7] = {true, false, false, false, false, false, false};

};

}
#endif // DB_IMPL_TUNING_H
