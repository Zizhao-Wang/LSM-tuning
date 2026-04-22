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



// void PerformanceMonitor::UpdateIntervalStats() {
//   // 2. 将 Batch 数据累加到你的 Interval 统计结构中
//   interval_stats_for_overflow.write_micros += perf_ctx_.batch_write_micros;
//   interval_stats_for_overflow.write_count += perf_ctx_.batch_write_count;

//   interval_stats_for_overflow.read_micros  += perf_ctx_.batch_read_micros;
//   interval_stats_for_overflow.read_count  += perf_ctx_.batch_read_count;

//   interval_stats_for_overflow.seek_micros  += perf_ctx_.batch_seek_micros;
//   interval_stats_for_overflow.seek_count  += perf_ctx_.batch_seek_count;

//   perf_ctx_.ResetBatch();
// }

// ============================================================================
// SATune: Get the number of active (non-empty) levels
// ============================================================================


void PerformanceMonitor::PerformInitialTuning(const MutableCFOptions* current_options, uint64_t total_stall_micros, TuningResult& result) {
  ROCKS_LOG_INFO(tuning_log_, "Starting initial tuning (read_ratio=%.2f)\n", read_ratio);
  
  // FlushStats* stats_ptr = db_im->GetFlushStats();
  // stats_ptr->CalculateCV();
        
  // uint64_t C0_base_size = stats_ptr->GetAverageFileSizeinL0();
  // if (C0_base_size == 0) {
  //   C0_base_size = 1048576; 
  // }
        
  // double CVcap = 1.0;
  // double alpha = 1.0;
  // double CV_raw = stats_ptr->pooled.CV;
  // double CV_norm = std::min(CV_raw / CVcap, 1.0);
  // double scale_factor = 10.0 * (1.0 + alpha * CV_norm);
  // uint64_t C1_target = static_cast<uint64_t>(C0_base_size * scale_factor);

  // fprintf(stderr, "[L1 SIZE TUNING] CV_raw   = %.6f CVcap    = %.6f  CV_norm  = %.6f  alpha    = %.6f  scale    = %.6f  base_C0  = %.2f MB  target_C1= %.2f MB\n",
  // CV_raw, CVcap, CV_norm, alpha, scale_factor, C0_base_size / 1048576.0, C1_target / 1048576.0);

  // Step 1: 调整 L0 compaction trigger (C0)
  int old_C0 = current_options->level0_file_num_compaction_trigger;
  int new_C0 = old_C0;
  
  if (old_C0 > 1) {
    result.needs_change = true;
    if (total_stall_micros == 0) {
      new_C0 = 1;
      ROCKS_LOG_INFO(tuning_log_, "[Initial Tuning] No stall detected. L0 trigger: %d → %d\n",old_C0, new_C0);
    } else {
      // 有 stall：保守调整，根据 read_ratio 缩放
      new_C0 = std::max(1, static_cast<int>(old_C0 * read_ratio));
      ROCKS_LOG_INFO(tuning_log_,"[Initial Tuning] Stall detected (%lu us). L0 trigger: %d → %d\n",
         total_stall_micros, old_C0, new_C0);
    }
    result.new_l0_trigger = new_C0;
  }
  
  // Step 2: 计算 write slowdown trigger (WST)
  // Formula: WST = C0 + floor(C0 * (1-r)^2)
  int effective_C0 = (new_C0 == old_C0) ? old_C0 : new_C0;
  double wst_term = effective_C0 * std::pow(1.0 - read_ratio, 2);
  result.new_slowdown_trigger = effective_C0 + static_cast<int>(std::floor(wst_term));
  
  // Step 3: 记录调优次数
  tuning_num_stats++;
  
  ROCKS_LOG_INFO(tuning_log_, "[Initial Tuning Result] C0=%d, WST=%d (tuning #%lu)\n",
    result.new_l0_trigger, result.new_slowdown_trigger, tuning_num_stats);
  
  return ;
}

void DBImpl::ApplyTuning(ColumnFamilyData* cfd, const TuningResult& tuning) {
  if (cfd == nullptr || !tuning.needs_change) {
    return;
  }
  
  std::unordered_map<std::string, std::string> options_map;
  // 1. 构建参数 Map
  if (tuning.new_l0_trigger >= 1 && tuning.new_slowdown_trigger >= tuning.new_l0_trigger) {
    options_map["level0_file_num_compaction_trigger"] = std::to_string(tuning.new_l0_trigger);
    options_map["level0_slowdown_writes_trigger"] = std::to_string(tuning.new_slowdown_trigger);
  }
  if (tuning.new_l1_size > 0) options_map["max_bytes_for_level_base"] = std::to_string(tuning.new_l1_size); // 256 MB
  if (tuning.new_level_multiplier >= 2.0) options_map["max_bytes_for_level_multiplier"] = std::to_string(tuning.new_level_multiplier);
  if (options_map.empty()) return;

  // options_map["level0_file_num_compaction_trigger"] = std::to_string(4);
  // options_map["level0_slowdown_writes_trigger"] = std::to_string(16);
  // options_map["max_bytes_for_level_base"] = std::to_string(256);


  Status s = cfd->SetOptions(initial_db_options_, options_map);
  if (s.ok()) {
    const auto* new_opts = cfd->GetLatestMutableCFOptions();
    auto* vstorage = cfd->current()->storage_info();
    vstorage->ComputeCompactionScore(*cfd->ioptions(), *new_opts);
    // (B)Refresh Write Stall (Resolves Write Stall latency)
    cfd->RecalculateWriteStallConditions(*new_opts);

    ROCKS_LOG_INFO(tuning_log_, "[SATune tuning finished] Apply Successful: %s | L0=%d, WST=%d, L1Size=%.2f MB, Multiplier=%.2f\n\n",
      tuning.reason.c_str(), tuning.new_l0_trigger, tuning.new_slowdown_trigger, tuning.new_l1_size/1048576.0, tuning.new_level_multiplier);
  } else {
    ROCKS_LOG_INFO(tuning_log_, "[SATune] ✗ Failed to apply tuning: %s\n\n",  s.ToString().c_str());
  }
} 

void PerformanceMonitor::NormalTuning(ColumnFamilyData* cfd,int level,TuningResult& result,
  uint64_t cumulative_stall_time,uint64_t avgfilesize,const MutableCFOptions* current_options) {
  
  // It menas the first compaction in the "level" is triggered!
  NormalTuningStats normal_tuning_stats = perf_ctx_.GetNormalTuning();
  double avg_read = normal_tuning_stats.AvgReadMicros();
  double avg_write = normal_tuning_stats.AvgWriteMicros();
        
  // Define threshold for significant difference (e.g., 10% or 15%)
  constexpr double kSignificantDifferenceRatio = 1.05;  // 5% difference
  constexpr double kMinDifferenceThreshold = 1.0 / kSignificantDifferenceRatio; 
  double read_write_ratio = normal_tuning_stats.write_time_us > 0 ? normal_tuning_stats.read_time_us / normal_tuning_stats.write_time_us : 1.0;

  // Determine direction only if difference is significant
  TuningDirection direction;
  if (read_write_ratio > kSignificantDifferenceRatio) {
    // Read is significantly slower (5%+) → optimize for reads
    direction = TuningDirection::kReadOptimization;
  } else if (read_write_ratio < kMinDifferenceThreshold) {
    // Write is significantly slower (5%+) → optimize for writes
    direction = TuningDirection::kWriteOptimization;
  } else {
    // Difference too small (<5%) → no tuning needed
    direction = TuningDirection::kBalanced;
  }

  // Skip tuning if workload is balanced
  if (direction == TuningDirection::kBalanced) {
      ROCKS_LOG_INFO(tuning_log_,"[Normal L%d→L%d Tuning] Read: %.2f µs (avg %.2f, n=%lu), "
       "Write: %.2f µs (avg %.2f, n=%lu), ratio=%.2f → Balanced, skip\n",
       level, level + 1, normal_tuning_stats.read_time_us, avg_read, normal_tuning_stats.read_count,
       normal_tuning_stats.write_time_us, avg_write, normal_tuning_stats.write_count,
       read_write_ratio);
    return ;
  }

  const char* dir_str = (direction == TuningDirection::kReadOptimization) ? "READ" : "WRITE";
  int64_t sign = (direction == TuningDirection::kReadOptimization) ? 1 : -1;
  ROCKS_LOG_INFO(tuning_log_,"[Normal L%d→L%d Tuning] Read: %.2f µs (avg %.2f, n=%lu), Write: %.2f µs (avg %.2f, n=%lu) → %s optimization\n",
    level, level + 1,normal_tuning_stats.read_time_us, avg_read, normal_tuning_stats.read_count,
    normal_tuning_stats.write_time_us, avg_write, normal_tuning_stats.write_count, dir_str);
  

  if(level==0){
    // Adjust L1 size
    int64_t C0_trigger = current_options->level0_file_num_compaction_trigger;
    int64_t min_l1_size_constraint = static_cast<int64_t>(1.2 * C0_trigger * cached_avg_l0_file_size_);

    int64_t actual_l1_size = GetActualL1TotalSize(cfd);
    int64_t max_l1_constraint = actual_l1_size > 0 ? static_cast<int64_t>(2.0 * actual_l1_size) : INT64_MAX;

    int64_t old_C1 = current_options->max_bytes_for_level_base;
    int64_t delta = sign * current_options->target_file_size_base;
    int64_t new_C1 = old_C1 + delta;
    if (new_C1 < min_l1_size_constraint) {
      ROCKS_LOG_INFO(tuning_log_,"[Normal L0->L1 Tuning #%lu]: Skipping - new L1 size %.2f MiB would violate "
        "constraint (< 1.2*C0*avg_L0_size = 1.2*%ld*%.2f = %.2f MiB)\n",
        tuning_num_stats + 1,new_C1 / 1048576.0,C0_trigger,cached_avg_l0_file_size_ / 1048576.0,min_l1_size_constraint / 1048576.0);
      return ;
    }

    if (sign > 0 && actual_l1_size > 0 && new_C1 > max_l1_constraint) {
      ROCKS_LOG_INFO(tuning_log_,"[Normal L0->L1 Tuning #%lu]: Skipping - new L1 size %.2f MiB would exceed "
        "upper bound (> 2.0*actual_L1 = 2.0*%.2f = %.2f MiB). Config already much larger than reality.\n",
        tuning_num_stats + 1,new_C1 / 1048576.0,actual_l1_size / 1048576.0, max_l1_constraint / 1048576.0);
      return ;
    }
    constexpr int64_t kMinL1Size = 67108864;  // 64 MiB
    if (new_C1 < kMinL1Size) {
      new_C1 = kMinL1Size;
    }
    ROCKS_LOG_INFO(tuning_log_,"[Normal L0->L1 Tuning #%lu]: L1 size %.2f → %.2f MiB (%s)\n",
      tuning_num_stats + 1, old_C1 / 1048576.0, new_C1 / 1048576.0, dir_str);
    if(result.new_l1_size == -1){
      result.new_l1_size = new_C1;
      result.needs_change = true;
    }
  }else{
    // Adjust level multiplier
    double old_f = current_options->max_bytes_for_level_multiplier;
    double new_f = old_f + sign * 0.01;
    constexpr double kMinMultiplier = 2.0;
    if (new_f < kMinMultiplier) {
      new_f = kMinMultiplier;
    }
    ROCKS_LOG_INFO(tuning_log_,"[Normal L%d->L%d Tuning #%lu]: Multiplier %.2f → %.2f (%s)\n",
      level, level + 1, tuning_num_stats + 1, old_f, new_f, dir_str);
    if(result.new_level_multiplier == -1.0){
      result.new_level_multiplier = new_f;
      result.needs_change = true;
    }
  }
}




void PerformanceMonitor::LGTuning(int level, ColumnFamilyData* cfd, const MutableCFOptions* current_options, 
  uint64_t cumulative_stall_time, TuningResult& result){
  // level_first_creation[level]=="false" menas a LG compaction 
  // It menas we are encountering an evaluated process to find an appropriate choice
  // Now, we need to re-evaluate the trade-off between "structures without tuning" and "structures with tuning"
  // If without tuning, the "total_elapsed_time"

  // we need to estimate the performance when tuning is not applied.
  // specifically, we compare two part's performance
  // 1. degradation of write performance because of stall time
  // 2. improvement of read performance  because of tuning 

  // i.e., the average cost of a get operation by increasing a level.
  TuningStatsSnapshot snapshot = perf_ctx_.GetLGTuningStats();

  double average_read_time_level = perf_ctx_.level_stats[level].LG_tuning_read_micros;
  uint64_t level_search_count =  perf_ctx_.level_stats[level].LG_tuning_read_count;
  double point_query_benefit = (double)level_search_count * average_read_time_level;

  double range_query_benefit = 0.0;
  auto range_type = snapshot.ClassifyRangeType();

  if (range_type == RangeType::kShortRange) {
    // SATune: For short range queries, NOT adding a level has benefit
    // because accessing fewer levels means lower latency per key
          
    // Estimate: if we add a level, each short range query will have 
    // additional cost due to seeking in more levels
    double avg_seek_time = snapshot.AvgSeekMicros();  // 平均 Seek 耗时
    uint64_t total_seek_ops = snapshot.seek_count;
          
    // Assume adding a level increases seek cost by ~20% (可调参数)
    double additional_cost_per_seek = avg_seek_time * 0.2;
    range_query_benefit = additional_cost_per_seek * total_seek_ops;
          
    ROCKS_LOG_INFO(tuning_log_, "[DEBUG] Level %d -> Short Range Detected (r=%.3f), Range Benefit: %.2f ns (Seek Count: %lu * Additional Cost: %.2f ns)\n",
      level, snapshot.RangeToPointCostRatio(), range_query_benefit, total_seek_ops, additional_cost_per_seek);          
  } else if (range_type == RangeType::kLongRange) {
    // SATune: For long range queries, benefit is 0
    range_query_benefit = 0.0;
    ROCKS_LOG_INFO(tuning_log_, "[DEBUG] Level %d -> Long Range Detected (r=%.3f), Range Benefit: 0\n",
      level, snapshot.RangeToPointCostRatio());     
  }

  double total_benefit = point_query_benefit + range_query_benefit;
  int64_t current_base = current_options->max_bytes_for_level_base;
  int current_multiplier = current_options->max_bytes_for_level_multiplier;
  

  if (cumulative_stall_time > total_benefit) {
    // Cost > Benefit: Add a level and reduce the size of current level
    ROCKS_LOG_INFO(tuning_log_, "[DECISION] Level %d (Hits: %llu) -> Add level (Cost %.2f > Benefit %.2f)\n",
      level, (unsigned long long)level_search_count, (double)cumulative_stall_time, total_benefit);
    UpdateLevelCreation(level);
    if(level==1){
      // SATune: For L1, directly adjust max_bytes_for_level_base
      auto* vstorage = cfd->current()->storage_info();
      int64_t actual_l1_size = GetActualL1TotalSize(cfd);
      int64_t C0_trigger = current_options->level0_file_num_compaction_trigger;
      int64_t l1_avg_file_size = actual_l1_size / std::max(1, vstorage->NumLevelFiles(1)); 

      int64_t min_l1_size_constraint = static_cast<int64_t>(1.2 * C0_trigger * cached_avg_l0_file_size_);
      int64_t old_C1 = current_options->max_bytes_for_level_base;
      
      int new_l1_size = current_base - l1_avg_file_size;
      if (new_l1_size < min_l1_size_constraint) {
        ROCKS_LOG_INFO(tuning_log_,"[LG L%d->L%d Tuning #%lu]: Skipping - new L1 size %.2f MiB would violate "
          "constraint (< 1.2*C0*avg_L0_size = 1.2*%ld*%.2f = %.2f MiB)\n",
          level, level+1, tuning_num_stats + 1,new_l1_size / 1048576.0,C0_trigger,cached_avg_l0_file_size_ / 1048576.0,min_l1_size_constraint / 1048576.0);
        return ;
      }
      if(result.new_l1_size == -1){
        result.new_l1_size = current_base - l1_avg_file_size;
        result.needs_change = true;
      }
    }else{
      // SATune: For L2+, adjust multiplier based on level depth
      // Target: increase this level's size by 10%
      double target_percentage = -0.10;  // 10% increase
      double multiplier_delta = 0.01; //CalculateMultiplierDelta(level, current_multiplier, target_percentage);
      double new_multiplier = current_multiplier + multiplier_delta;      
      if(result.new_level_multiplier == -1.0){
        result.new_level_multiplier = new_multiplier; 
        result.needs_change = true;
      }
    } 
    
  } else {
    // Benefit >= Cost: Do not add a level
    ROCKS_LOG_INFO(tuning_log_, "[DECISION] Level %d -> Keep current structure (Benefit %.2f >= Cost %.2f)\n",
      level, total_benefit, (double)cumulative_stall_time);
    if (level == 1) {
      // SATune: For L1, decrease base by avg file size
      auto* vstorage = cfd->current()->storage_info();
      int64_t l1_avg_file_size = GetActualL1TotalSize(cfd) / std::max(1, vstorage->NumLevelFiles(1));
      if(result.new_l1_size == -1){
        result.new_l1_size = current_base + l1_avg_file_size;
        result.needs_change = true;
      }    
    } else {
      // SATune: For L2+, decrease multiplier
      // Target: decrease this level's size by 10%
      double target_percentage = 0.10;  // -10% decrease
      double multiplier_delta = 0.01;// CalculateMultiplierDelta(level, current_multiplier, target_percentage);
      double new_multiplier = std::max(2.0,current_multiplier + multiplier_delta);
      if(result.new_level_multiplier == -1.0){
        result.new_level_multiplier = new_multiplier;
        result.needs_change = true;
      }  
    }
  }
} 


// ============================================================================
// SATune: Handle LG tuning for first compaction at each level
// Strategy:
//   - Read slower: Increase C1 (level=1) or increase f (level>1)
//   - Write slower: Decrease C1 (level=1) or decrease f (level>1)
// Only adjust if L0 tuning hasn't already changed the parameter
// ============================================================================
void PerformanceMonitor::HandleLGTuningWithoutStall(int level,TuningResult& result,
   const MutableCFOptions* current_options,uint64_t cumulative_stall_time) {
  
  // Step 1: Get LG tuning statistics
  bool read_slower = perf_ctx_.IsReadSlowerForLGTuning();
  if (level == 1) {
    // Level 1: Adjust C1 (max_bytes_for_level_base)
    // Check if L0 tuning already changed this parameter
    if (result.new_l1_size != -1) {
      ROCKS_LOG_INFO(tuning_log_,"[LG Tuning L1] Skipped: L0 tuning already adjusted C1:%ld\n",result.new_l1_size);
      return;
    }
    
    int64_t old_c1 = current_options->max_bytes_for_level_base;
    int64_t delta = current_options->target_file_size_base;
    int64_t new_c1;
    
    if (read_slower) {
      // Read slower → increase C1
      new_c1 = old_c1 + delta;
      ROCKS_LOG_INFO(tuning_log_,"[LG Tuning L1→L2] #%lu, Read slower: C1 %.2f→%.2f MB (+%.2f MB)\n",
        tuning_num_stats + 1,old_c1 / (1024.0 * 1024.0),new_c1 / (1024.0 * 1024.0), delta / (1024.0 * 1024.0));
    } else {
      // Write slower → decrease C1
      new_c1 = old_c1 - delta;
      // Ensure not below minimum (1.2 * C0 * avg_l0_file_size)
      int current_trigger = current_options->level0_file_num_compaction_trigger;
      int64_t min_c1 = static_cast<int64_t>(current_trigger * 1.2 * cached_avg_l0_file_size_);
      new_c1 = std::max(new_c1, min_c1);
      ROCKS_LOG_INFO(tuning_log_,"[LG Tuning L1→L2] #%lu, Write slower: C1 %.2f→%.2f MB (min=%.2f MB)\n",
        tuning_num_stats + 1, old_c1 / (1024.0 * 1024.0), new_c1 / (1024.0 * 1024.0), min_c1 / (1024.0 * 1024.0));
    }
    
    result.new_l1_size = new_c1;
    result.needs_change = true;
  } else {
    // Level > 1: Adjust f (max_bytes_for_level_multiplier)
    // Check if L0 tuning already changed this parameter
    if (result.new_level_multiplier != -1.0) {
      ROCKS_LOG_INFO(tuning_log_,"[LG Tuning L%d] Skipped: L0 tuning already adjusted multiplier\n", level);
      return;
    }
    
    double old_f = current_options->max_bytes_for_level_multiplier;
    double delta_f = 0.01;
    double new_f;
    
    if (read_slower) {
      // Read slower → increase f
      new_f = old_f + delta_f;
      ROCKS_LOG_INFO(tuning_log_,"[LG Tuning L%d→L%d] #%lu, Read slower: f %.2f→%.2f (+%.2f)\n",
        level, level + 1, tuning_num_stats + 1,old_f, new_f, delta_f);
    } else {
      // Write slower → decrease f
      new_f = old_f - delta_f;
      // Ensure not below minimum (typically 1.0)
      new_f = std::max(2.0, new_f);
      ROCKS_LOG_INFO(tuning_log_,"[LG Tuning L%d→L%d] #%lu, Write slower: f %.2f→%.2f (min=1.0)\n",
        level, level + 1, tuning_num_stats + 1,old_f, new_f);
    }
    
    result.new_level_multiplier = new_f;
    result.needs_change = true;
  }
  tuning_num_stats++;
  perf_ctx_.ResetLGTuning();
  level_stall_stats_ptr_->ResetLGTuningStallStats();
}

// ============================================================================
// SATune: Notify compaction completed - reset force flags
// ============================================================================


TuningResult PerformanceMonitor::CheckAndHandleBatch(DBImpl* db_im, int level, ColumnFamilyData* cfd) {
  TuningResult result; // This is used to record the final setting parameters

  TuningStatsSnapshot stats = perf_ctx_.GetOverallStats();
  // UpdateIntervalStats();
  if(is_the_first_tuning_try){
    read_ratio = stats.WeightedReadRatio();
    level_stall_stats_ptr_ = cfd->GetMutableLevelStallStats();
  }

  uint64_t total_stall_micros = level_stall_stats_ptr_->GetBatchTotalStallMicros();
  const MutableCFOptions* current_options = cfd->GetCurrentMutableCFOptions();


  if(is_the_first_tuning_try){
    cached_avg_l0_file_size_ = GetAverageL0FileSize(cfd);
    PerformInitialTuning(current_options, total_stall_micros,result);
    is_the_first_tuning_try = false;
    return result;
  }


    if (cfd) {
    auto* vstorage = cfd->current()->storage_info();
    
    ROCKS_LOG_INFO(tuning_log_,"[SATune LSM State] Input level=%d | Current C0=%d, WST=%d, SST=%d L1=%.2f, F=%.2f\n",
      level,current_options->level0_file_num_compaction_trigger,
      current_options->level0_slowdown_writes_trigger,current_options->level0_stop_writes_trigger,
      current_options->max_bytes_for_level_base/1048576.0, current_options->max_bytes_for_level_multiplier);
    
    // 输出每个level的文件数和大小
    std::string level_info = "[SATune LSM State] Level files: ";
    for (int i = 0; i < vstorage->num_levels(); i++) {
      int num_files = vstorage->NumLevelFiles(i);
      uint64_t level_bytes = vstorage->NumLevelBytes(i);
      double level_mb = level_bytes / (1024.0 * 1024.0);
      
      if (num_files > 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "L%d:%d files(%.2f MB) ", i, num_files, level_mb);
        level_info += buf;
      }
    }
    ROCKS_LOG_INFO(tuning_log_, "%s\n", level_info.c_str());
    
    // 输出active levels
    int active_levels = GetActiveLevels(cfd);
    ROCKS_LOG_INFO(tuning_log_, "[SATune LSM State] Active levels: %d\n", active_levels);
    
    // 输出当前的compaction score
    std::string score_info = "[SATune LSM State] Compaction scores: ";
    for (int i = 0; i < vstorage->num_levels() - 1; i++) {
      double score = vstorage->CompactionScore(i);
      if (score > 0.0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "L%d:%.2f ", i, score);
        score_info += buf;
      }
    }
    ROCKS_LOG_INFO(tuning_log_, "%s\n", score_info.c_str());
  }

  if (IsCompactionInProgress(level)) {
    ROCKS_LOG_INFO(tuning_log_,"[SATune InProgress] L%d compaction in progress, skipping tuning (accumulating stats for next real compaction)\n",
      level);
    return result;
  }

  // ============================================================================
  // SATune: Check if we should force compaction for this level
  // ============================================================================
  if (level == 0 && force_l0_compaction_) {
    ROCKS_LOG_INFO(tuning_log_,"[SATune Force] Forcing L0->L1 compaction, skipping tuning optimizations\n");
    return result;
  }
  
  if (level == 1 && force_l1_compaction_) {
    ROCKS_LOG_INFO(tuning_log_,"[SATune Force] Forcing L1->L2 compaction, skipping tuning optimizations\n");
    return result;
  }

  // Step 2: check if the number_of_l0_files exceeds the level0_compaction_trigger
  // Based on our setting of "CO/SLOW_DOWN/STOP", only one possible way can lead to the stall or unexpected L0_trigger
  // that is increased "L1"(C1) leads to unexpected "num_l0_files"
  // if this situation happens, it means we need to decrease the "C1"
  uint64_t current_actual_l1_size =  current_options->target_file_size_base;
  //before starting the tuning programing, we need to record the old (may not) parameters in the "tuning_records_" structure 
  // tuning_records_.StartTuning(current_options->level0_file_num_compaction_trigger,current_options->max_bytes_for_level_base, current_options->max_bytes_for_level_multiplier);
  uint64_t cumulative_stall_time = level_stall_stats_ptr_->GetBatchTotalStallMicros();
  HandleL0Overflow(cfd, level, result,current_options);



  // Step 3: If stall dose happens, evaluate the cost and benefit to determine the correct strategy
  // the process of step 3 should as follows:
  // 3.1. If no stalls, just decrease the C0 until the compaction during the first L0->L1 happens
  // when the first compaction of a specific level happens, we should evaluate the size?
  double level_score = cfd->current()->storage_info()->CompactionScore(level);
  uint64_t avgfilesize =  current_options->target_file_size_base;

  if(cumulative_stall_time <= 0 && level_first_creation[level]){
    // level_first_creation[level]=="false" menas a LG compaction
    HandleLGTuningWithoutStall(level,result,current_options,cumulative_stall_time);
  }else if(cumulative_stall_time > 0 && level_first_creation[level]){
    LGTuning(level, cfd, current_options, cumulative_stall_time, result);
    perf_ctx_.ResetLGTuning();
    level_stall_stats_ptr_->ResetLGTuningStallStats();
  }else if(cumulative_stall_time <= 0 && !level_first_creation[level]){
    // It menas the first compaction in the "level" is triggered!
    // It means we are experiencing a normal tuning process?
    if (ShouldPerformNormalTuning(level)){
      int old_C0 = current_options->level0_file_num_compaction_trigger;
      int old_wst = current_options->level0_slowdown_writes_trigger;
      int active_levels = GetActiveLevels(cfd);
      if(old_C0 > 1 && active_levels <=2 ){
        int new_C0 = old_C0-1;  int new_wst = old_wst-1>=new_C0?old_wst-1:old_wst;
        ROCKS_LOG_INFO(tuning_log_, "[Normal L%d->L%d tuning]: The %lu-th tuning, Adjusting: L0 %d->%d, WST %d->%d (read_ratio=%.2f)\n",
          level, level + 1, tuning_num_stats + 1,old_C0, new_C0, old_wst, new_wst,read_ratio);
        if(result.new_l0_trigger == -1 && result.new_slowdown_trigger == -1){
          result.new_l0_trigger = new_C0; result.new_slowdown_trigger = new_wst;
          result.needs_change = true;
        }  
        tuning_num_stats++;
      }
      level_stall_stats_ptr_->ResetNormalTuningStallStats();
    }
  }else if(cumulative_stall_time > 0 && !level_first_creation[level]){
    if (ShouldPerformNormalTuning(level)) {
      NormalTuning(cfd, level, result, cumulative_stall_time, avgfilesize, current_options);
      tuning_num_stats++;
      perf_ctx_.ResetNormalTuning();
      level_stall_stats_ptr_->ResetNormalTuningStallStats();
    }else{
      int deepest = GetDeepestExistingLevel();
      ROCKS_LOG_INFO(tuning_log_, "[Skipping Normal Tuning] L%d→L%d is not the deepest level(deepest is L%d→L%d)\n",
        level, level + 1, deepest, deepest + 1);
    }
  }
    // tuning_records_.EndTuning(result.new_l0_trigger,result.new_l1_size, result.new_level_multiplier, current_actual_l1_size);
    return result;
  }

}