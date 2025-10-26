// auto_tuner.cc - 自动调优实现
#include "auto_tuner.h"
#include "workload_monitor.h"
#include "db/db_impl.h"
#include "util/mutexlock.h"
#include "db/version_set.h"
#include "db/memtable.h" 

namespace leveldb{

  // 前向声明
  class DBImpl; 
  class VersionSet;
  class Version;

  void AutoTuner::check_and_trigger_tuning(DBImpl* db, const WorkloadMonitor& monitor) {
    if (monitor.has_workload_changed()) {
      fprintf(stdout, "🔄 Workload pattern changed! Triggering auto-tuning...\n");
      trigger_auto_tuning(db, monitor);
    }
  }

  void AutoTuner::trigger_auto_tuning(DBImpl* db, const WorkloadMonitor& monitor) {
    auto stats = monitor.get_detailed_stats();
    double current_ratio = stats.current_ratio;
      
    fprintf(stdout, "Auto-tuning based on R/W ratio: %.3f\n", current_ratio);
      
    if (current_ratio > 10.0) {
      // 读多写少的场景优化
      optimize_for_read_heavy_workload(db);
    } else if (current_ratio < 0.5) {
      // 写多读少的场景优化
      optimize_for_write_heavy_workload(db);
    } else {
      // 读写平衡的场景优化
      optimize_for_balanced_workload(db);
    }
  }

  void AutoTuner::optimize_for_read_heavy_workload(DBImpl* db) {
    fprintf(stdout, "📚 Optimizing for read-heavy workload:\n");
    fprintf(stdout, "  - Increasing MemTable size\n");
    fprintf(stdout, "  - Enabling bloom filters\n");
    fprintf(stdout, "  - Reducing compaction frequency\n");
    fprintf(stdout, "  - Increasing block cache size\n");
    fprintf(stdout, "  - Enabling index/filter caching\n");
      
    // 这里调用DBImpl的具体配置方法
    // db->SetMemTableSize(larger_size);
    // db->EnableBloomFilter(true);
    // db->SetCompactionFrequency(lower_frequency);
    // db->SetBlockCacheSize(larger_cache_size);
  }

  void AutoTuner::optimize_for_write_heavy_workload(DBImpl* db) {
    fprintf(stdout, "✍️  Optimizing for write-heavy workload:\n");
    fprintf(stdout, "  - Decreasing MemTable size for faster flushes\n");
    fprintf(stdout, "  - Increasing compaction threads\n");
    fprintf(stdout, "  - Adjusting LSM tree levels\n");
    fprintf(stdout, "  - Increasing write buffer size\n");
    fprintf(stdout, "  - Optimizing compaction strategy\n");
      
    // 这里调用DBImpl的具体配置方法
    // db->SetMemTableSize(smaller_size);
    // db->SetCompactionThreads(more_threads);
    // db->SetLSMLevels(optimized_levels);
    // db->SetWriteBufferSize(larger_buffer);
  }

  void AutoTuner::optimize_for_balanced_workload(DBImpl* db) {
    fprintf(stdout, "⚖️  Optimizing for balanced workload:\n");
    fprintf(stdout, "  - Using moderate configuration\n");
    fprintf(stdout, "  - Balanced compaction settings\n");
    fprintf(stdout, "  - Standard cache sizes\n");
      
    // 这里调用DBImpl的具体配置方法
    // db->SetToDefaultConfig();
  }

  LSMStateSnapshot DBImpl::GetLSMStateSnapshot() const {
    LSMStateSnapshot snapshot;
    
    MutexLock l(&metadata_mutex_);

    // 从私有成员 versions_ 中获取数据
    Version* current_version = versions_->current();

    // 1. 填充各层级文件数量
    snapshot.level0_file_count = current_version->NumFiles(0);
    snapshot.level1_file_count = current_version->NumFiles(1);


    // 3. 填充 Memtable 大小
    snapshot.memtable_size_bytes = mem_->ApproximateMemoryUsage();

    return snapshot;
  }


  // bool PerformanceMonitor::CheckAndHandleBatch(DBImpl* db_im, bool is_tuning) {
  //   if (!ShouldStartNewBatch()){
  //     return false;
  //   } 

  //   // fprintf(stdout, "Check the state!\n");
  //   if(!is_first_l0_compaction){
  //     // fprintf(stdout,"[Tuning Info] System not ready for tuning (L0 files:). Waiting...\n");
  //     return false;
  //   }

  //   auto batch_stats = EndBatch();
  //   if(is_tuning){
  //     profiler_->GetStats().PrintThroughputStats(batch_stats);
  //     if (baseline_.ops_per_sec == 0) {
  //       SetBaseline(batch_stats);
  //       printf("[Batch Info] Baseline set - OPS: %.2f\n", baseline_.ops_per_sec);
  //     } else {
  //       // 已有基准，更新last_batch用于后续比较
  //       last_batch_ = batch_stats;
  //       printf("[Batch Info] Performance vs Baseline: %.2f%%\n",(batch_stats.ops_per_sec / baseline_.ops_per_sec - 1.0) * 100.0);
  //     }
  //     StartBatch();
  //   }else{
  //     // during the process from last compaction to next compaction, if we found that the performance degrades, we should...
  //     if (baseline_.ops_per_sec != 0 && (batch_stats.ops_per_sec / baseline_.ops_per_sec - 1.0)<-0.1){
  //       fprintf(stdout,"something wrong!\n");
  //     }
  //   }    

  //   // Now we need to do somethings to tune the LSM-tree?
  //   // Now I think we have two choices:
  //   // 1. No stall, if read operations exist, we should lower the CT0, right?

  //   uint64_t total_write_time =  profiler_->GetStats().GetWriteTime(); // nanoseconds
  //   uint64_t total_stall_time =  db_im->GetL0StallStats().total_slow_or_stop_time.load(); //microsseconds

  //   // 转换write_time从纳秒到微秒用于比较
  //   uint64_t total_write_time_us = total_write_time / 1000;
  //   // 计算stall占write时间的百分比
  //   double stall_vs_write_time = (total_write_time_us > 0) ? ((double)total_stall_time / (double)total_write_time_us) * 100.0 : 0.0;
  //   // fprintf(stdout, "\n=== Stall Time Statistics ===\n");
  //   // fprintf(stdout, "Total write time:     %lu ns (%lu us)\n", total_write_time, total_write_time_us);
  //   // fprintf(stdout, "Total stall time:     %lu us\n", total_stall_time);
  //   // fprintf(stdout, "Stall / Write ratio:  %.2f%%\n", stall_vs_write_time);
  //   // fprintf(stdout, "=============================\n\n");

  //   if(is_tuning){
  //     CompactionOptionsAtomic* compaction_Options = db_im->GetCompactionOptions();
  //     uint64_t total_ops = profiler_->GetStats().total_get_count.load()+profiler_->GetStats().total_get_count.load();
  //     double read_ratio = profiler_->GetStats().total_get_count.load() / (double)total_ops;
  //     if(stall_vs_write_time < 0.1){
  //       int old_C0 = compaction_Options->level0_compaction_trigger.load();
  //       if(old_C0 > 1){
  //         int new_C0 = old_C0*read_ratio;
  //         fprintf(stdout, "The %lu tuning, Adjusting L0 trigger from %d to %d (read_ratio=%.2f)\n",tuning_num_stats, old_C0, new_C0, read_ratio);
  //         db_im->GetCompactionOptions()->SetL0TriggersCustom(new_C0);
  //         tuning_num_stats++;
  //       }
  //     }else{

  //     }
  //   }

  //   return true;
  // }

  bool PerformanceMonitor::CheckAndHandleBatch(DBImpl* db_im, int level, bool is_tuning) {
    if (!ShouldStartNewBatch()){
      return false;
    } 

    Logger* tuning_log_ = db_im->GetTuningLogger();
    CompactionOptionsAtomic* compaction_Options = db_im->GetCompactionOptions();
    TuningResult result; // This is used to record the final setting parameters

    auto batch_stats = EndBatchStats();
    if(is_tuning){
      // profiler_->GetStats().PrintThroughputStats(batch_stats);
      if (cumulative_current_throughput_.Iops_per_sec == 0 && cumulative_last_throughput_.Iops_per_sec == 0) {
        // set the "current"
        cumulative_last_throughput_= batch_stats;
        lastbatch_throughput_= batch_stats;
        
        // set the "stall" statistics
        cumulative_last_stall_ = db_im->GetL0StallStats();
        lastbatch_stall_ = db_im->GetL0StallStats();
        fprintf(stdout, "[Batch Info] The 1-th batch data is setted - IOPS: %.2f\n", cumulative_last_throughput_.Iops_per_sec);
        Log(tuning_log_,"[Batch Info] The 1-th batch data is setted - IOPS: %.2f\n", cumulative_last_throughput_.Iops_per_sec); 
      } else {
        // A baseline has been established; update “current” for subsequent comparisons.
        cumulative_last_throughput_= cumulative_current_throughput_;
        lastbatch_throughput_= lastbatch_throughput_;

        cumulative_current_throughput_ = EndCumulativeStats();
        currentbatch_throughput_ = batch_stats;
        
        lastbatch_stall_ = currentbatch_stall_;
        cumulative_last_stall_ = cumulative_current_stall_;

        const LevelStallStats& new_cumulative = db_im->GetL0StallStats();
        currentbatch_stall_.CalculateDeltaAndUpdate(new_cumulative, cumulative_current_stall_);
        cumulative_current_stall_ = db_im->GetL0StallStats();
        fprintf(stdout, "[Batch Info] Performance vs Baseline: %.2f%%\n",(batch_stats.Iops_per_sec / cumulative_last_throughput_.Iops_per_sec - 1.0) * 100.0);
        Log(tuning_log_,"[Batch Info] Performance vs Baseline: %.2f%%\n",(batch_stats.Iops_per_sec / cumulative_last_throughput_.Iops_per_sec - 1.0) * 100.0);
      }
      // start a new batch if the last batch ended.
      StartBatch();
    }   
    
    // if we found that the actual "number_of_l0_files" > pre-defined(pre-tuned) "compaction_Options->level0_compaction_trigger"
    // then we should reduce the "C1" to the last setted value
    // we should not evaluate the trade-offs between "unexpected increased stalls and unexpected reads latency because of increased number_of_l0_files" 
    // and "the benefit of read performance because of decrease number_of_levels in the LSM-tree".
    // obviously, it's not a deserved trade-off.
    if(is_tuning){
      //Step 1:In the first loop, we just start a try for tuning. 
      uint64_t total_ops = profiler_->GetStats().total_get_count.load()+profiler_->GetStats().total_put_count.load();
      double read_ratio = profiler_->GetStats().total_get_count.load() / (double)total_ops;

      if(is_the_first_tuning_try){
        FlushStats* stats_ptr = db_im->GetFlushStats();
        stats_ptr->CalculateCV();
        
        uint64_t C0_base_size = stats_ptr->GetAverageFileSizeinL0();
        if (C0_base_size == 0) {
          C0_base_size = 1048576; 
        }
        
        double CVcap = 1.0;
        double alpha = 1.0;
        double CV_raw = stats_ptr->pooled.CV;
        double CV_norm = std::min(CV_raw / CVcap, 1.0);
        double scale_factor = 10.0 * (1.0 + alpha * CV_norm);
        uint64_t C1_target = static_cast<uint64_t>(C0_base_size * scale_factor);

        fprintf(stderr, "[L1 SIZE TUNING] CV_raw   = %.6f CVcap    = %.6f  CV_norm  = %.6f  alpha    = %.6f  scale    = %.6f  base_C0  = %.2f MB  target_C1= %.2f MB\n",
        CV_raw, CVcap, CV_norm, alpha, scale_factor,
        C0_base_size / 1048576.0, C1_target / 1048576.0);
        exit(0);

        if(lastbatch_stall_.total_slow_or_stop_time==0){
          int old_C0 = compaction_Options->level0_compaction_trigger.load();
          if(old_C0 > 1){
            result.needs_change = true;
            int new_C0 = old_C0*read_ratio;
            fprintf(stdout, "There is no stall in the first tuning, Adjusting L0 trigger from %d to %d (read_ratio=%.2f)\n", old_C0, new_C0, read_ratio);
            Log(tuning_log_,"There is no stall in the first tuning, Adjusting L0 trigger from %d to %d (read_ratio=%.2f)\n", old_C0, new_C0, read_ratio);            
            // db_im->GetCompactionOptions()->SetL0TriggersCustom(new_C0);
            result.new_l0_trigger = new_C0;
            tuning_num_stats++;
          }
        }else{
          // take a try to reduce C0
          int old_C0 = compaction_Options->level0_compaction_trigger.load();
          if(old_C0 > 1){
            result.needs_change = true;
            int new_C0 = old_C0 - 1;
            fprintf(stdout, "There is no stall in the first tuning, Adjusting L0 trigger from %d to %d (read_ratio=%.2f)\n", old_C0, new_C0, read_ratio);
            Log(tuning_log_,"There is no stall in the first tuning, Adjusting L0 trigger from %d to %d (read_ratio=%.2f)\n", old_C0, new_C0, read_ratio);        
            // db_im->GetCompactionOptions()->SetL0TriggersCustom(new_C0);
            result.new_l0_trigger = new_C0;
            tuning_num_stats++;
          }
        }
        is_the_first_tuning_try = false;
        return true;
      }

      // Step 2: check if the number_of_l0_files exceeds the level0_compaction_trigger
      // Based on our setting of "CO, SLOW_DOWN, STOP", only one possible way can lead to the stall or unexpected L0_trigger
      // that is increased "L1"(C1) leads to unexpected "num_l0_files"
      // if this situation happens, it means we need to decrease the "C1"
      const VersionSet* get_versionset_ =  db_im->GetVersionSet();
      int current_actual_l1_size =  get_versionset_->NumLevelBytes(1);
      //before starting the tuning programing, we need to record the old (may not) parameters in the "tuning_records_" structure 
      tuning_records_.StartTuning(compaction_Options->level0_compaction_trigger.load(),
        compaction_Options->max_bytes_for_level1_base.load(), compaction_Options->max_bytes_for_level1_multiplier.load());

      if(currentbatch_stall_.l0_exceed_trigger_count.load()>50){
        fprintf(stdout, "[L0 Overflow Statistics]: total_l0_exceed_files=%lu, total_stall_trigger_count=%lu the l0_max_exceeded_files=%lu\n", 
          currentbatch_stall_.l0_exceed_total_files.load(),currentbatch_stall_.l0_exceed_trigger_count.load(), currentbatch_stall_.l0_max_exceeded_files.load());
        Log(tuning_log_,"[L0 Overflow Statistics]: total_l0_exceed_files=%lu, total_stall_trigger_count=%lu the l0_max_exceeded_files=%lu\n", 
          currentbatch_stall_.l0_exceed_total_files.load(),currentbatch_stall_.l0_exceed_trigger_count.load(), currentbatch_stall_.l0_max_exceeded_files.load()); 
        int l0_exceed_number = currentbatch_stall_.l0_max_exceeded_files.load() - compaction_Options->level0_compaction_trigger.load();
        
        if(currentbatch_stall_.l0_max_exceeded_files <=1){
          // two categories
          // 1. reduce L1 if there are more than 2 active levels in the LSM
          // 2. Do not proceed if less than 3 levels in the LSM
          int active_levels = get_versionset_->GetActivaLevelsInLSM();
          FlushStats* stats_ptr = db_im->GetFlushStats();
          if(active_levels>=3){
            //reduce the L1 by "Average FileSize in L0"
            int new_C1 = compaction_Options->max_bytes_for_level1_base.load() - stats_ptr->GetAverageFileSizeinL0();
            result.new_l1_size = new_C1;
            tuning_num_stats++;
          }
        }else{
          // basically, we have two possible ways to face this situation:
          // 1. We can tune to the last setting if it was changed or setted in last time (the first loop)
          // 2. We can appropriately reduce a little bit for the L1_SIZE (max_bytes_for_level1_base)
          //definitely, we need to reduce the "max_bytes_for_level1_base", the input parameter should be "0x00000002"
          if (tuning_records_.IsSpecificParamModified(0x00000002)) { 
            result.needs_change = true;
            int new_C1 = tuning_records_.old_l1_size;
            // compaction_Options->max_bytes_for_level1_base.store(new_C1);
            result.new_l1_size = new_C1;
            fprintf(stdout, "[L1 Rollback] Reverting L1 to previous value: %d\n", result.new_l1_size);
          }else{
            // It means that the current status of "L1" is a little bit higher, we may need to reduce the unexpected "L1 increase"
            if(tuning_records_.actual_l1_size < current_actual_l1_size){
              int setted_l1 = compaction_Options->max_bytes_for_level1_base;
              // 计算 L1 实际大小的增长百分比
              double growth_ratio = (double)(current_actual_l1_size - tuning_records_.actual_l1_size) / tuning_records_.actual_l1_size;
              // 按照相同的百分比缩小 setted_l1
              // 例如：如果 L1 实际增长了 20%，就把 setted_l1 减少 20%
              double reduction_factor = 1.0 - growth_ratio;
              // 确保 reduction_factor 在合理范围内（比如不能减少超过 50%）
              reduction_factor = std::max(0.5, reduction_factor);
              result.new_l1_size = (int)(setted_l1 * reduction_factor);
              fprintf(stdout, "[L1 Adjustment] Actual L1 grew %.1f%% (%u→%u) (%.1fMiB→%.1fMiB), reducing target %d→%d (%.1fMiB→%.1fMiB)\n",
                growth_ratio * 100, tuning_records_.actual_l1_size, current_actual_l1_size,tuning_records_.actual_l1_size / (1048576.0), 
                current_actual_l1_size / (1048576.0), setted_l1, result.new_l1_size,setted_l1 / (1048576.0), result.new_l1_size / (1048576.0));
              Log(tuning_log_, "[L1 Adjustment] Actual L1 grew %.1f%% (%u→%u) (%.1fMiB→%.1fMiB), reducing target %d→%d (%.1fMiB→%.1fMiB)\n",
                growth_ratio * 100, tuning_records_.actual_l1_size, current_actual_l1_size,tuning_records_.actual_l1_size / (1048576.0), 
                current_actual_l1_size / (1048576.0),setted_l1, result.new_l1_size,setted_l1 / (1048576.0), result.new_l1_size / (1048576.0));
            }else{
              fprintf(stdout, "[L1 Check] No adjustment needed (actual=%u, target=%lu)\n", current_actual_l1_size, compaction_Options->max_bytes_for_level1_base.load());
              Log(tuning_log_, "[L1 Check] No adjustment needed (actual=%u, target=%lu)\n", current_actual_l1_size, compaction_Options->max_bytes_for_level1_base.load());
            }
          }
        }
      }

      // Step 3: If stall dose happens, evaluate the cost and benefit to determine the correct strategy
      // the process of step 3 should as follows:
      // 3.1. If no stalls, just decrease the C0 until the compaction during the first L0->L1 happens
      // when the first compaction of a specific level happens, we should evaluate the size?
       
      int stall_different = currentbatch_stall_.CalculateStallTimeDifference(lastbatch_stall_);
      // Now we need to do somethings to tune the LSM-tree?
      // Now I think we have two choices:
      // 1. No stall, if read operations exist, we should lower the CT0
      uint64_t total_write_time =  profiler_->GetStats().GetWriteTime(); // nanoseconds
      uint64_t total_stall_time =  cumulative_current_stall_.total_slow_or_stop_time.load(); //microsseconds
      // 转换write_time从纳秒到微秒用于比较
      uint64_t total_write_time_us = total_write_time / 1000;
      // 计算stall占write时间的百分比
      double stall_vs_write_time = (total_write_time_us > 0) ? ((double)total_stall_time / (double)total_write_time_us) * 100.0 : 0.0;

      if(stall_different <= 0 && !level_first_creation[level]){
        
        if(level==1){
          int old_C1 = compaction_Options->max_bytes_for_level1_base.load();
          int new_C1 = old_C1+(50*1048576);
          fprintf(stdout, "[First L%d->L%d  compaction]: The %lu-th tuning, Adjusting L1 size from %.2f MiB to %.2f MiB (read_ratio=%.2f)\n",level,level+1,tuning_num_stats+1, old_C1 / (1048576.0), new_C1 / (1048576.0), read_ratio);
          Log(tuning_log_,"[First L%d->L%d first compaction]: The %lu-th tuning, Adjusting L1 size from %.2f MiB to %.2f MiB (read_ratio=%.2f)\n",level,level+1,tuning_num_stats+1, old_C1/ (1048576.0), new_C1 / (1048576.0), read_ratio);
          result.new_l1_size = new_C1;
        }else{
          // that means we need to increase f
          double old_f = compaction_Options->max_bytes_for_level1_multiplier.load();
          double new_f = old_f+0.5;
          fprintf(stdout, "[First L%d->L%d compaction]: The %lu-th tuning, Adjusting multiplier(F) from %.2f to %.2f (read_ratio=%.2f)\n",
            level, level+1, tuning_num_stats+1, old_f, new_f, read_ratio);
          Log(tuning_log_, "[First L%d->L%d first compaction]: The %lu-th tuning, Adjusting multiplier from %.2f to %.2f (read_ratio=%.2f)\n",
            level, level+1, tuning_num_stats+1, old_f, new_f, read_ratio);
          result.new_level_multiplier = new_f;
        }
        result.needs_change = true;
        // db_im->GetCompactionOptions()->SetL0TriggersCustom(new_C0);
        tuning_num_stats++;
      }else if(stall_different <= 0 && level_first_creation[level]){
        // It menas the first compaction in the "level" is triggered!
        // It means we are experiencing a normal tuning process?
        int old_C0 = compaction_Options->level0_compaction_trigger.load();
        if(old_C0 > 1){
          result.needs_change = true;
          int new_C0 = old_C0*read_ratio;
          fprintf(stdout, "[Normal L%d->L%d compaction]: The %lu-th tuning, Adjusting L0 trigger from %d to %d (read_ratio=%.2f)\n",level,level+1,tuning_num_stats+1, old_C0, new_C0, read_ratio);
          Log(tuning_log_,"[Normal L%d->L%d compaction]: The %lu-th tuning, Adjusting L0 trigger from %d to %d (read_ratio=%.2f)\n",level,level+1,tuning_num_stats+1, old_C0, new_C0, read_ratio);
          // db_im->GetCompactionOptions()->SetL0TriggersCustom(new_C0);
          result.new_l0_trigger = new_C0;
          tuning_num_stats++;
        }
      }else if(stall_different > 0 && level_first_creation[level]){
        // It menas the first compaction in the "level" is triggered!
        if(level==1){
          int old_C1 = compaction_Options->max_bytes_for_level1_base.load();
          int new_C1 = old_C1-(50*1048576);
          fprintf(stdout, "[First L%d->L%d compaction in stall]: The %lu-th tuning, Adjusting L1 size from %.2f MiB to %.2f MiB (read_ratio=%.2f)\n",level,level+1,tuning_num_stats+1, old_C1 / (1048576.0), new_C1 / (1048576.0), read_ratio);
          Log(tuning_log_,"[First L%d->L%d compaction in stall]: The %lu-th tuning, Adjusting L1 size from %.2f MiB to %.2f MiB (read_ratio=%.2f)\n",level,level+1,tuning_num_stats+1, old_C1/ (1048576.0), new_C1 / (1048576.0), read_ratio);
          result.new_l1_size = new_C1;
        }else{
          // that means we need to decrease "f"
          double old_f = compaction_Options->max_bytes_for_level1_multiplier.load();
          double new_f = old_f-0.5;
          fprintf(stdout, "[First L%d->L%d compaction in stall]: The %lu-th tuning, Adjusting multiplier(F) from %.2f to %.2f (read_ratio=%.2f)\n",
            level, level+1, tuning_num_stats+1, old_f, new_f, read_ratio);
          Log(tuning_log_, "[First L%d->L%d compaction in stall]: The %lu-th tuning, Adjusting multiplier from %.2f to %.2f (read_ratio=%.2f)\n",
            level, level+1, tuning_num_stats+1, old_f, new_f, read_ratio);
          result.new_level_multiplier = new_f;
        }
        exit(0);
      }else if(stall_different > 0 && !level_first_creation[level]){
        // It menas we are encountering an evaluated process to find an appropriate choice
        // Now, we need to re-evaluate the trade-off between "structures without tuning" and "structures with tuning"
        // If without tuning, the "total_elapsed_time"

        // we need to estimate the performance when tuning is not applied.
        // specifically, we compare two part's performance
        // 1. degradation of write performance because of stall time
        // 2. improvement of read performance  because of tuning 

        // i.e., the average cost of a get operation by increasing a level.
        double average_read_time_level = profiler_->GetStats().GetLevelAverageSearchTimeNs(level);
        uint64_t level_search_count =  profiler_->GetStats().GetLevelSearchCount(level);
        double benefit = (double)level_search_count * average_read_time_level;
        fprintf(stdout, "[DEBUG] Level %d -> Stall Time Cost: %ld | Benefit: %.2f (Search Count: %lu * Avg Read Time: %.2f ns)\n",
          level,total_stall_time,benefit,level_search_count, average_read_time_level);
        
        if(total_stall_time > benefit){
          // add a level and reduce the size of "Level", cost > benefit
          
        }else{
          // do not add a level, cost > benefit
          
        }
        exit(0);
      }
      tuning_records_.EndTuning(result.new_l0_trigger,result.new_l1_size, result.new_level_multiplier, current_actual_l1_size );
    }

    
    return true;
  }

  void PerformanceMonitor::ApplyTuningWithExemption(DBImpl* db_im){
    Logger* tuning_log_ = db_im->GetTuningLogger();
    CompactionOptionsAtomic* compaction_Options = db_im->GetCompactionOptions();
    if(tuning_records_.l0_trigger_delta != 0){
      compaction_Options->level0_compaction_trigger.store(tuning_records_.new_l0_trigger);
    }
    
    // 应用其他参数变更
    if (tuning_records_.l1_size_delta != 0) {
      compaction_Options->max_bytes_for_level1_base.store(tuning_records_.new_l1_size);
    }
    
    if (tuning_records_.level_multiplier_delta != 0) {
      compaction_Options->max_bytes_for_level1_multiplier.store(tuning_records_.new_level_multiplier);
    }

    if (tuning_records_.has_pending_changes) {
      fprintf(stdout, "[TUNING APPLIED] L0=%d(%+d), L1=%lu(%+d), LevelM=%.2f(%.2f)  | Changed: 0x%X\n",
        compaction_Options->level0_compaction_trigger.load(), tuning_records_.l0_trigger_delta,compaction_Options->max_bytes_for_level1_base.load(), tuning_records_.l1_size_delta,
        compaction_Options->max_bytes_for_level1_multiplier.load(), tuning_records_.level_multiplier_delta, tuning_records_.changed_params);
      Log(tuning_log_,"[TUNING APPLIED] L0=%d(%+d), L1=%lu(%+d), LevelM=%.2f(%.2f) | Changed: 0x%X\n \
          [TUNING Finished] =======================================\n\n",
        compaction_Options->level0_compaction_trigger.load(), tuning_records_.l0_trigger_delta,compaction_Options->max_bytes_for_level1_base.load(), tuning_records_.l1_size_delta,
        compaction_Options->max_bytes_for_level1_multiplier.load(), tuning_records_.level_multiplier_delta, tuning_records_.changed_params);
    }

  }

  bool PerformanceMonitor::ChechAndDecideStartWriteTuning(const FlushStats& flush_stats_){
    if(flush_stats_.average_discard_ratio_in_flush >= 0.85){

    }
  }


  void PerformanceMonitor::set_first_l0_compaction() {
    is_first_l0_compaction = true;
  }

  void PerformanceMonitor::set_first_l1_compaction() {
    is_first_l1_compaction = true;
  }

}