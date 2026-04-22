// auto_tuner.cc - 自动调优实现
#include "workload_monitor.h"
#include "db/db_impl/db_impl.h"
#include "db/tuner/tuner_perf_context.h"
#include "util/mutexlock.h"
#include "db/version_set.h"
#include "logging/logging.h"

namespace ROCKSDB_NAMESPACE{

class DBImpl; 
class VersionSet;
class Version;


void PerformanceMonitor::HandleL0StallOnly(ColumnFamilyData* cfd,
  TuningResult& result, const MutableCFOptions* current_options) {

  // Step 1: Check if write-heavy
  // if (!perf_ctx_.CheckBatchWriteHeavy()) {


  //   return;
  // }
  
  consecutive_write_stall_count_ = 0;
  int current_trigger = current_options->level0_file_num_compaction_trigger;
  int current_wst = current_options->level0_slowdown_writes_trigger;
  result.new_l0_trigger = current_trigger;
  result.new_slowdown_trigger = current_wst+2;
  result.needs_change = true;
  just_increased_for_read_heavy_ = true;
  ROCKS_LOG_INFO(tuning_log_,"[SATune] L0 stall detected but NOT write-heavy (read latency > write latency). "
    "Strategy: Keep trigger=%d unchanged, increase slowdown %d→%d to provide buffer. Reason: Read-heavy workload doesn't need aggressive compaction.\n",
      current_trigger, current_wst, current_wst + 2);
  perf_ctx_.ResetOverflowBatch();
  level_stall_stats_ptr_->ResetL0OverflowStallStats();

  // // Step 2: Check if L2 exists
  // int active_levels = GetActiveLevels(cfd);
  // bool has_l2 = (active_levels > 2) && (!level_first_creation[1]);
  
  // int current_trigger = current_options->level0_file_num_compaction_trigger;
  // int current_wst = current_options->level0_slowdown_writes_trigger;
  // uint64_t current_c1 = current_options->max_bytes_for_level_base;
  
  // result.needs_change = true;
  
  // if (!has_l2) {
  //   // Sub-case 3.1: L2 doesn't exist → reduce L1 by fixed step
  //   uint64_t target_file_base = current_options->target_file_size_base;
  //   int64_t min_l1_size = static_cast<int64_t>(current_trigger * 1.2 * cached_avg_l0_file_size_);
    
  //   int64_t l1_after_reduction = static_cast<int64_t>(current_c1) -  static_cast<int64_t>(target_file_base);
    
  //   if (l1_after_reduction >= min_l1_size) {
  //     // Can reduce by full target_file_base
  //     result.new_l1_size = static_cast<uint64_t>(l1_after_reduction);
  //   } else if (current_c1 > static_cast<uint64_t>(min_l1_size)) {
  //     // Can only reduce to min
  //     result.new_l1_size = static_cast<uint64_t>(min_l1_size);
  //   } else {
  //     // Already at min, no change
  //     result.new_l1_size = current_c1;
  //   }
    
  //   // Only force L1→L2 if LSM has <= 2 levels
  //   if (active_levels <= 2 || level_first_creation[1]) {
  //     force_l1_compaction_ = true;
  //     ROCKS_LOG_INFO(tuning_log_,"[SATune L0 Stall] Write-heavy, no L2: L1 %.2f→%.2f MB, levels=%d, force L1→L2\n",
  //       current_c1 / (1024.0 * 1024.0), result.new_l1_size / (1024.0 * 1024.0), active_levels);
  //   } else {
  //     ROCKS_LOG_INFO(tuning_log_,"[SATune L0 Stall] Write-heavy, no L2: L1 %.2f→%.2f MB, levels=%d\n",
  //       current_c1 / (1024.0 * 1024.0),result.new_l1_size / (1024.0 * 1024.0),  active_levels);
  //   }
  // } else {
  //   // ------------------------------------------------------------------------
  //   // Sub-case 3.2: L2 exists → only increase WST
  //   // ------------------------------------------------------------------------
  //   result.new_l0_trigger = current_trigger;       // C0 unchanged
  //   result.new_slowdown_trigger = current_wst + 1; // WST +1
  //   if (consecutive_write_stall_count_ >= 3) {
  //       result.new_l0_trigger = current_trigger;
  //       result.new_slowdown_trigger = current_wst + 2;  // gap+1
  //       consecutive_write_stall_count_ = 0;
  //       ROCKS_LOG_INFO(tuning_log_, "[SATune L0 Stall] Write-heavy, L2 exists: count=3/3, C0 %d→%d, WST %d→%d\n",
  //         current_trigger, result.new_l0_trigger,  current_wst, result.new_slowdown_trigger);
  //   } else {
  //     ROCKS_LOG_INFO(tuning_log_,"[SATune L0 Stall] Write-heavy, L2 exists: count=%d/3, C0=%d, WST %d→%d\n",
  //       consecutive_write_stall_count_, current_trigger, current_wst, result.new_slowdown_trigger);
  //   }
  //   ROCKS_LOG_INFO(tuning_log_,"[SATune L0 Stall] Write-heavy, L2 exists: C0=%d, WST %d→%d\n",
  //     current_trigger, current_wst, result.new_slowdown_trigger);
  // }
  
  // Reset batch statistics
  perf_ctx_.ResetOverflowBatch();
  level_stall_stats_ptr_->ResetL0OverflowStallStats();
}

void PerformanceMonitor::HandleL0OverflowOnly(ColumnFamilyData* cfd, TuningResult& result, 
  const MutableCFOptions* current_options, int max_exceed) {

  int current_trigger = current_options->level0_file_num_compaction_trigger;
  uint64_t current_c1 = current_options->max_bytes_for_level_base;
  uint64_t target_file_base = current_options->target_file_size_base;
  
  result.needs_change = true;
  if (!perf_ctx_.CheckBatchWriteHeavy()) {
    int current_wst = current_options->level0_slowdown_writes_trigger;
    result.new_l0_trigger = current_trigger+max_exceed;
    result.new_slowdown_trigger = current_wst+(max_exceed+2);
    just_increased_for_read_heavy_ = true;
    ROCKS_LOG_INFO(tuning_log_,"[SATune] L0 overflow but read-heavy: trigger %d→%d, slowdown %d→%d (gap +1)\n",
      current_trigger, current_trigger + max_exceed, current_wst, current_wst + (max_exceed + 1));
    return;
  }

  // Step 1: Calculate L1 minimum threshold (1.2x of expected L0 total size)
  int64_t min_l1_size = static_cast<int64_t>(current_trigger * 1.2 * cached_avg_l0_file_size_);
  // Step 2: Try to reduce L1 by fixed amount (target_file_base)
  int64_t l1_after_reduction = static_cast<int64_t>(current_c1) -  static_cast<int64_t>(target_file_base);
  
  if (l1_after_reduction >= min_l1_size) {
    // Sub-case 2.1: Can reduce L1 by target_file_base
    result.new_l1_size = static_cast<uint64_t>(l1_after_reduction);
    
    // Only force L1→L2 if LSM has <= 2 levels
    int active_levels = GetActiveLevels(cfd);
    if (active_levels <= 2 || level_first_creation[1]) {
      force_l1_compaction_ = true;
      ROCKS_LOG_INFO(tuning_log_, "[SATune L0 Overflow] L1 %.2f→%.2f MB (-%d MB), levels=%d, force L1→L2\n",
        current_c1 / (1024.0 * 1024.0),result.new_l1_size / (1024.0 * 1024.0),
        static_cast<int>(target_file_base / (1024 * 1024)), active_levels);
    } else {
      ROCKS_LOG_INFO(tuning_log_,"[SATune L0 Overflow] L1 %.2f→%.2f MB (-%d MB), levels=%d\n",
        current_c1 / (1024.0 * 1024.0),result.new_l1_size / (1024.0 * 1024.0),
        static_cast<int>(target_file_base / (1024 * 1024)),active_levels);
    } 
  } else if (current_c1 > static_cast<uint64_t>(min_l1_size)) {
    // Sub-case 2.2: Can reduce to min, but not by full target_file_base
    result.new_l1_size = static_cast<uint64_t>(min_l1_size);
    
    int active_levels = GetActiveLevels(cfd);
    if (active_levels <= 2 || level_first_creation[1]) {
      force_l1_compaction_ = true;
      ROCKS_LOG_INFO(tuning_log_,"[SATune L0 Overflow] L1 %.2f→%.2f MB (to min), levels=%d, force L1→L2\n",
        current_c1 / (1024.0 * 1024.0),result.new_l1_size / (1024.0 * 1024.0), active_levels);
    } else {
      ROCKS_LOG_INFO(tuning_log_, "[SATune L0 Overflow] L1 %.2f→%.2f MB (to min), levels=%d\n",
        current_c1 / (1024.0 * 1024.0),result.new_l1_size / (1024.0 * 1024.0), active_levels);
    }
  } else {
    // Sub-case 2.3: L1 at minimum → increase C0 instead
    result.new_l0_trigger = current_trigger + max_exceed; 
    // Track cumulative C0 increase
    if (c0_baseline_ < 0) {
      c0_baseline_ = current_trigger;
    }
    int delta = result.new_l0_trigger - c0_baseline_;
    c0_cumulative_increase_ += delta;
    c0_baseline_ = result.new_l0_trigger;
    
    // Check force L0 compaction
    if (c0_cumulative_increase_ >= kC0CumulativeThreshold) {
      force_l0_compaction_ = true;
      ROCKS_LOG_INFO(tuning_log_,"[SATune Force] Cumulative C0 increase %d >= %d, will force L0→L1\n",
        c0_cumulative_increase_, kC0CumulativeThreshold);
    }
    
    // Check force L1 compaction (only for <= 2 levels)
    if (cfd && max_exceed >= 2) {
      int active_levels = GetActiveLevels(cfd);
      if (active_levels <= 2 || level_first_creation[1]) {
        force_l1_compaction_ = true;
        ROCKS_LOG_INFO(tuning_log_,"[SATune Force] max_exceed=%d, levels=%d, will force L1→L2\n", max_exceed, active_levels);
      }
    }
    
    // Check if L1 needs to be increased to match new C0 minimum
    int64_t new_min_l1_size = static_cast<int64_t>(result.new_l0_trigger * 1.2 * cached_avg_l0_file_size_);
    
    if (current_c1 < static_cast<uint64_t>(new_min_l1_size)) {
      result.new_l1_size = static_cast<uint64_t>(new_min_l1_size);
      ROCKS_LOG_INFO(tuning_log_,"[SATune L0 Overflow] C0 %d→%d (exceed=%d, cumul=%d), L1 %.2f→%.2f MB\n",
        current_trigger, result.new_l0_trigger, max_exceed, c0_cumulative_increase_,
        current_c1 / (1024.0 * 1024.0), result.new_l1_size / (1024.0 * 1024.0));
    } else {
      ROCKS_LOG_INFO(tuning_log_, "[SATune L0 Overflow] C0 %d→%d (exceed=%d, cumul=%d)\n",
        current_trigger, result.new_l0_trigger, max_exceed, c0_cumulative_increase_);
    }
  }
  
  perf_ctx_.ResetOverflowBatch();
  level_stall_stats_ptr_->ResetL0OverflowStallStats();
}


// SATune: Handle both stall and overflow (Case 4)
// Strategy:
//   - Read slower: Decrease C0 (more aggressive compaction)
//   - Write slower: Prioritize reducing L1, only increase C0 if L1 at minimum
void PerformanceMonitor::HandleL0StallAndOverflow(ColumnFamilyData* cfd,TuningResult& result,
    const MutableCFOptions* current_options,int max_exceed) {
  
  int current_trigger = current_options->level0_file_num_compaction_trigger;
  int current_wst = current_options->level0_slowdown_writes_trigger;
  uint64_t current_c1 = current_options->max_bytes_for_level_base;
  uint64_t target_file_base = current_options->target_file_size_base;
  
  result.needs_change = true;
  

  // Step 1: Calculate latency gap to determine read vs write slower
  bool read_slower = false;
  double delta = perf_ctx_.CalculateLatencyGap(&read_slower);
  
  // Step 2: Check if force L1→L2 compaction needed (independent of later logic)
  if (cfd && max_exceed >= 2 && !read_slower) {
    int active_levels = GetActiveLevels(cfd);
    if (active_levels <= 2 || level_first_creation[1]) {
      force_l1_compaction_ = true;
      ROCKS_LOG_INFO(tuning_log_,"[SATune Force] max_exceed=%d >= 2, levels=%d, will force L1→L2\n",
        max_exceed, active_levels);
    }
  }else if(cfd && max_exceed >= 2 && read_slower && max_exceed < 3){
    if (consecutive_read_heavy_overflow_count_ >= 10) {
      result.new_l0_trigger = current_trigger + 1;
      consecutive_read_heavy_overflow_count_ = 0;
    }else{
      result.new_l0_trigger = current_trigger;
      consecutive_read_heavy_overflow_count_++;
    }
    result.new_slowdown_trigger = current_wst+1;
    just_increased_for_read_heavy_ = true;
    ROCKS_LOG_INFO(tuning_log_,"[SATune] L0 OverflowandStall but read-heavy: trigger %d→%d, slowdown %d→%d (gap +1)\n",
      current_trigger, result.new_l0_trigger, current_wst, result.new_slowdown_trigger);
    result.needs_change=true;
    return ;
  }

  if (delta == 0.0) {
    // No clear direction, use max_exceed to adjust
    // result.new_l0_trigger = current_trigger + max_exceed;
    // int new_gap = (current_wst - current_trigger) + max_exceed;
    // result.new_slowdown_trigger = result.new_l0_trigger + new_gap;
    
    // ROCKS_LOG_INFO(tuning_log_,"[SATune L0] Stall+overflow, delta=0: C0 %d→%d, WST %d→%d\n",
    //   current_trigger, result.new_l0_trigger, current_wst, result.new_slowdown_trigger);
    
    // perf_ctx_.ResetOverflowBatch();
    // level_stall_stats_ptr_->ResetL0OverflowStallStats();
    return;
  }
  
  // Step 3: Calculate adaptive adjustment
  const int delta_0 = 1;
  const double beta = 2.0;
  double log_term = std::log2(1.0 + delta) / 2.0;  // log base 4
  int delta_adj = delta_0 + static_cast<int>(beta * log_term);
  delta_adj = std::max(4, delta_adj);
  
  int wst_c0_gap = current_wst - current_trigger;
  
  if (read_slower) {
    // Sub-case 4.1: Read slower → decrease C0 (more aggressive compaction)
    if(just_increased_for_read_heavy_){
      ROCKS_LOG_INFO(tuning_log_,"[SATune L0] Stall+overflow, read slower BUT already increased params for read-heavy earlier. "
        "Skip reduction to avoid conflict. Keep C0=%d, WST=%d\n", current_trigger, current_wst);
      result.needs_change = false;
      return ;
    }else{
      result.new_l0_trigger = std::max(1, current_trigger - delta_adj);
      int new_gap = std::max(1, wst_c0_gap - delta_adj);
      result.new_slowdown_trigger = result.new_l0_trigger + new_gap;
      
      ROCKS_LOG_INFO(tuning_log_,"[SATune L0] Stall+overflow, read slower: C0 %d→%d, WST %d→%d (gap=%.1fµs, adj=%d)\n",
        current_trigger, result.new_l0_trigger, current_wst, result.new_slowdown_trigger,delta, delta_adj);
    }
  } else {

    // Sub-case 4.2: Write slower → prioritize reducing L1
    int64_t min_l1_size = static_cast<int64_t>( current_trigger * 1.2 * cached_avg_l0_file_size_);
    int64_t l1_after_reduction = static_cast<int64_t>(current_c1) -  static_cast<int64_t>(target_file_base);
    
    if (l1_after_reduction >= min_l1_size) {
      // Can reduce L1 by full target_file_base
      result.new_l1_size = static_cast<uint64_t>(l1_after_reduction);
      ROCKS_LOG_INFO(tuning_log_,"[SATune L0] Stall+overflow, write slower: L1 %.2f→%.2f MB (gap=%.1fµs, adj=%d)\n",
        current_c1 / (1024.0 * 1024.0),result.new_l1_size / (1024.0 * 1024.0), delta, delta_adj);
      
    } else if (current_c1 > static_cast<uint64_t>(min_l1_size)) {
      // Can only reduce to min
      result.new_l1_size = static_cast<uint64_t>(min_l1_size);
      ROCKS_LOG_INFO(tuning_log_,"[SATune L0] Stall+overflow, write slower: L1 %.2f→%.2f MB (to min, gap=%.1fµs, adj=%d)\n",
        current_c1 / (1024.0 * 1024.0), result.new_l1_size / (1024.0 * 1024.0), delta, delta_adj);
      
    } else {
      // L1 at minimum, increase C0 instead
      result.new_l0_trigger = current_trigger + delta_adj;
      int new_gap = wst_c0_gap + delta_adj;
      result.new_slowdown_trigger = result.new_l0_trigger + new_gap;
      
      ROCKS_LOG_INFO(tuning_log_,"[SATune L0] Stall+overflow, write slower, L1 at min: C0 %d→%d, WST %d→%d (gap=%.1fµs, adj=%d)\n",
        current_trigger, result.new_l0_trigger, current_wst, result.new_slowdown_trigger,delta, delta_adj);
    }
  }
  
  // --------------------------------------------------------------------------
  // Step 4: Track cumulative C0 increase
  // --------------------------------------------------------------------------
  if (c0_baseline_ < 0) {
    c0_baseline_ = current_trigger;
  }
  
  int delta_c0 = result.new_l0_trigger - c0_baseline_;
  if (delta_c0 > 0) {
    c0_cumulative_increase_ += delta_c0;
    c0_baseline_ = result.new_l0_trigger;
    if (c0_cumulative_increase_ >= kC0CumulativeThreshold) {
      force_l0_compaction_ = true;
      ROCKS_LOG_INFO(tuning_log_,"[SATune Force] Cumulative C0 increase %d >= %d, will force L0→L1\n",
        c0_cumulative_increase_, kC0CumulativeThreshold);
    }
  } else if (delta_c0 < 0) {
    c0_cumulative_increase_ = std::max(0, c0_cumulative_increase_ + delta_c0);
    c0_baseline_ = result.new_l0_trigger;
  }
  
  // --------------------------------------------------------------------------
  // Step 5: Check if L1 needs to be adjusted to match C0 minimum
  // --------------------------------------------------------------------------
  int64_t expected_l0_total_bytes = static_cast<int64_t>(result.new_l0_trigger * 1.2 * cached_avg_l0_file_size_);
  
  if (static_cast<int64_t>(current_c1) < expected_l0_total_bytes) {
    result.new_l1_size = static_cast<uint64_t>(expected_l0_total_bytes);
  }
  
  perf_ctx_.ResetOverflowBatch();
  level_stall_stats_ptr_->ResetL0OverflowStallStats();
}


void PerformanceMonitor::HandleL0Overflow(ColumnFamilyData* cfd, int level,
  TuningResult& result,const MutableCFOptions* current_options){

  if (!level_stall_stats_ptr_ || !current_options) {
    return;
  }
  
  // Get batch statistics
  bool has_stall = level_stall_stats_ptr_->HasBatchStall();
  bool has_overflow = level_stall_stats_ptr_->HasBatchOverflow();
  int max_exceed = level_stall_stats_ptr_->batch_max_l0_exceed_trigger.load(std::memory_order_relaxed);
  int current_trigger = current_options->level0_file_num_compaction_trigger;
  int current_wst = current_options->level0_slowdown_writes_trigger;
  int current_c1 = current_options->max_bytes_for_level_base;
  int c0_wst_gap = current_wst - current_trigger;
  

  // Case 1: No stall, no overflow
  if (!has_stall && !has_overflow) {
    return;
  }
  

  // Case 3: Only stall (no overflow)
  if (has_stall && !has_overflow) {
    HandleL0StallOnly(cfd, result, current_options);
    return;
  }

  // Case 2: Only overflow (no stall) → Directly increase trigger
  if (has_overflow && !has_stall) {
    HandleL0OverflowOnly(cfd, result, current_options,max_exceed);
    return;
  }
  
  // ============================================================================
  // Case 4: Both stall and overflow → Adaptive adjustment
  // ============================================================================

  HandleL0StallAndOverflow( cfd,result,current_options,max_exceed);
}
}