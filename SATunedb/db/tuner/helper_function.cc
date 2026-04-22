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

// ============================================================================
// SATune: Get the number of active (non-empty) levels
// ============================================================================
int PerformanceMonitor::GetActiveLevels(ColumnFamilyData* cfd) const {
  if (!cfd) {
    return 0;
  }
  
  auto* vstorage = cfd->current()->storage_info();
  int active_levels = 0;
  
  for (int i = 0; i < vstorage->num_levels(); i++) {
    if (vstorage->NumLevelFiles(i) > 0) {
      active_levels++;
    }
  }
  
  return active_levels;
}

int64_t PerformanceMonitor::GetActualL1TotalSize(ColumnFamilyData* cfd) {
  auto* vstorage = cfd->current()->storage_info();
  const std::vector<FileMetaData*>& l1_files = vstorage->LevelFiles(1);
  
  if (l1_files.empty()) {
    return 0;
  }
  
  uint64_t total_size = 0;
  for (const auto* file : l1_files) {
    total_size += file->fd.GetFileSize();
  }
  
  return static_cast<int64_t>(total_size);
}


int64_t PerformanceMonitor::GetAverageL0FileSize(ColumnFamilyData* cfd) {
  if (cached_avg_l0_file_size_ > 0) {
    return cached_avg_l0_file_size_;
  }
  
  // 获取 L0 的所有文件
  auto* vstorage = cfd->current()->storage_info();
  const std::vector<FileMetaData*>& l0_files = vstorage->LevelFiles(0);
  
  if (l0_files.empty()) {
    cached_avg_l0_file_size_ = 67108864; //default number
    ROCKS_LOG_INFO(tuning_log_, "[L0 Avg File Size] No files in L0, using default: %.2f MiB\n", cached_avg_l0_file_size_ / 1048576.0);
    return cached_avg_l0_file_size_;
  }
  
  // 计算平均大小
  uint64_t total_size = 0;
  for (const auto* file : l0_files) {
    total_size += file->fd.GetFileSize();
  }
  
  cached_avg_l0_file_size_ = static_cast<int64_t>(total_size / l0_files.size());
  
  ROCKS_LOG_INFO(tuning_log_,"[L0 Avg File Size] Calculated from %zu files: %.2f MiB (total: %.2f MiB)\n",
    l0_files.size(),cached_avg_l0_file_size_ / 1048576.0, total_size / 1048576.0);
  
  return cached_avg_l0_file_size_;
}

int PerformanceMonitor::GetDeepestExistingLevel() const {
  for (int i = 6; i >= 0; --i) {
    if (!level_first_creation[i]) {
      return i;
    }
  }
  return 0;
}

bool PerformanceMonitor::ShouldPerformNormalTuning(int level) const {
  int deepest_level = GetDeepestExistingLevel();
  return (level == deepest_level);
}




// SATune: Calculate multiplier delta to achieve target percentage change at given level
// Formula: delta = multiplier * ((1 + percentage)^(1/(level-1)) - 1)
// Example: To increase L3 by 10%, with multiplier=10, level=3
//          delta = 10 * ((1.1)^(1/2) - 1) ≈ 0.488
double PerformanceMonitor::CalculateMultiplierDelta(int level, double current_multiplier, 
  double target_percentage_change) {
  if (level <= 1) return 0.0;  // L0 and L1 don't use multiplier
    
  // target_percentage_change: 0.1 means increase 10%, -0.1 means decrease 10%
  double factor = 1.0 + target_percentage_change;
    
  // Calculate: multiplier * (factor^(1/(level-1)) - 1)
  double exponent = 1.0 / (double)(level - 1);
  double new_multiplier_ratio = std::pow(factor, exponent);
  double delta = current_multiplier * (new_multiplier_ratio - 1.0);
  return delta;
}


// ============================================================================
// SATune: Notify compaction completed - reset force flags
// ============================================================================

void PerformanceMonitor::MarkCompactionReady(int output_level) {
  if (output_level == 1 && force_l0_compaction_) {
    compaction_being_forced_ = 0;  // L0->L1
    ROCKS_LOG_INFO(tuning_log_,"[SATune Force] L0->L1 compaction ready to execute (cumulative C0 increase: %d >= %d)\n",
      c0_cumulative_increase_, kC0CumulativeThreshold);
  } else if (output_level == 2 && force_l1_compaction_) {
    compaction_being_forced_ = 1;  // L1->L2
    ROCKS_LOG_INFO(tuning_log_,  "[SATune Force] L1->L2 compaction ready to execute (max_exceed >= 2 && active_levels <= 2)\n");
  }
} 

void PerformanceMonitor::NotifyCompactionCompleted(int level) {
  if (level == 0) {
    if (force_l0_compaction_) {
      ROCKS_LOG_INFO(tuning_log_,"[SATune Force] L0->L1 compaction completed, resetting force flag (cumulative increase was: %d)\n",
        c0_cumulative_increase_);
      force_l0_compaction_ = false;
      c0_cumulative_increase_ = 0;
      c0_baseline_ = -1;
      compaction_being_forced_ = -1;
    }
  } else if (level == 1) {
    if (force_l1_compaction_) {
      ROCKS_LOG_INFO(tuning_log_,"[SATune Force] L1->L2 compaction completed, resetting force flag\n");
      force_l1_compaction_ = false;
      force_l1_compaction_ = false;
      compaction_being_forced_ = -1;

      if(level_first_creation[1]==true){
        level_first_creation[1] = false;
      }
    
    }
  }
}

// ============================================================================
// SATune: Check if this is a forced compaction
// ============================================================================
bool PerformanceMonitor::IsForceCompaction(int level) const {
  return compaction_being_forced_ == level;
}

// SATune: Mark that compaction is in progress for this level
void PerformanceMonitor::MarkCompactionInProgress(int level) {
  if (level >= 0 && level < 7) {
    level_compaction_in_progress_[level] = true;
    ROCKS_LOG_INFO(tuning_log_,"[SATune InProgress] L%d compaction started, will accumulate stats\n",level);
  }
}

// SATune: Clear compaction in-progress flag
void PerformanceMonitor::ClearCompactionInProgress(int level) {
  if (level >= 0 && level < 7) {
    level_compaction_in_progress_[level] = false;
    ROCKS_LOG_INFO(tuning_log_,"[SATune InProgress] L%d compaction completed, ready for tuning\n",level);
  }
}


// SATune: Check if compaction is in progress for this level
bool PerformanceMonitor::IsCompactionInProgress(int level) const {
  if (level >= 0 && level < 7) {
    return level_compaction_in_progress_[level];
  }
  return false;
}

}