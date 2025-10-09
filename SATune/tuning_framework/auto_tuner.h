// auto_tuner.h - 独立的自动调优模块
#ifndef AUTO_TUNER_H
#define AUTO_TUNER_H

#include "workload_monitor.h"
#include <cstdio>

namespace leveldb{
  class DBImpl;

  class AutoTuner {
  public:
    // 检查并触发自动调优
    static void check_and_trigger_tuning(DBImpl* db, const WorkloadMonitor& monitor);
      
    // 自动调优逻辑
    static void trigger_auto_tuning(DBImpl* db, const WorkloadMonitor& monitor);
      
  private:
    // 具体的优化策略
    static void optimize_for_read_heavy_workload(DBImpl* db);
    static void optimize_for_write_heavy_workload(DBImpl* db);
    static void optimize_for_balanced_workload(DBImpl* db);
  };
  
}
#endif // AUTO_TUNER_H