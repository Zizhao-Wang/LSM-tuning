// ========== include/leveldb/performance_profile.h ==========

#ifndef STORAGE_LEVELDB_INCLUDE_LEVELDB_PERFORMANCE_PROFILE_H_
#define STORAGE_LEVELDB_INCLUDE_LEVELDB_PERFORMANCE_PROFILE_H_

#include <chrono>
#include <atomic>
#include <string>
#include <memory>

namespace leveldb {

// 性能profile配置选项
struct PerformanceProfileOptions {
  bool enable_profiling = false;
  bool auto_report_on_destruction = false;
  uint64_t report_interval_operations = 0;  // 0表示不自动报告
    
  PerformanceProfileOptions() = default;
    
  // 静态方法设置全局选项（兼容旧API）
  static void SetGlobalOptions(const PerformanceProfileOptions& options);
  static const PerformanceProfileOptions& GetGlobalOptions();
  static bool IsProfilingEnabled();
};

struct ThroughputStats {
  double read_ops_per_sec = 0.0;
  double write_ops_per_sec = 0.0;
  double Iops_per_sec = 0.0;

  //time/gets
  uint64_t total_gets = 0;
  double get_time_sec = 0.0;

  //time/puts
  uint64_t total_puts = 0;
  double put_time_sec = 0.0;

  //time/all ops
  uint64_t total_ops = 0;
  double total_time_sec = 0.0;
};

// 性能统计数据
struct PerformanceStats {
  // Memtable和Immutable Memtable查找
  std::atomic<uint64_t> memtable_lookup_count{0};
  std::atomic<uint64_t> memtable_lookup_time_ns{0};
    
  std::atomic<uint64_t> immutable_lookup_count{0};
  std::atomic<uint64_t> immutable_lookup_time_ns{0};
    
  // check for 'current''s check
  std::atomic<uint64_t> fileread_search_time_ns{0};
  std::atomic<uint64_t> fileread_search_count{0};

  // 阶段1：文件元数据搜索（包含Level 0和其他Level的二分查找）
  std::atomic<uint64_t> Level0_file_metadata_search_time_ns{0};
  std::atomic<uint64_t> Level0_file_metadata_search_count{0};
  std::atomic<uint64_t> OtherLevel_file_metadata_search_time_ns{0}; // L1+
  std::atomic<uint64_t> OtherLevel_file_metadata_search_count{0};

  std::atomic<uint64_t> level0_search_time_ns;
  std::atomic<uint64_t> level0_search_count;
  std::atomic<uint64_t> level_search_time_ns[7];
  std::atomic<uint64_t> level_search_count[7];

  // 文件系统操作
  std::atomic<uint64_t> level0_file_open_count{0};  // NewRandomAccessFile调用次数
  std::atomic<uint64_t> level0_file_open_time_ns{0}; // NewRandomAccessFile时间
  std::atomic<uint64_t> level0_table_open_count{0};  // Table::Open调用次数
  std::atomic<uint64_t> level0_table_open_time_ns{0}; // Table::Open时间

  std::atomic<uint64_t> other_levels_file_open_count{0};  
  std::atomic<uint64_t> other_levels_file_open_time_ns{0}; 
  std::atomic<uint64_t> other_levels_table_open_count{0};  
  std::atomic<uint64_t> other_levels_table_open_time_ns{0}; 

  // Level 0 专门统计
  std::atomic<uint64_t> level0_index_block_search_count{0};
  std::atomic<uint64_t> level0_index_block_search_time_ns{0};
  std::atomic<uint64_t> level0_bloom_filter_check_count{0};
  std::atomic<uint64_t> level0_bloom_filter_check_time_ns{0};
  std::atomic<uint64_t> level0_data_block_read_count{0};
  std::atomic<uint64_t> level0_data_block_read_time_ns{0};
  std::atomic<uint64_t> level0_bloom_filter_positive_count{0};
  std::atomic<uint64_t> level0_bloom_filter_negative_count{0};

  // 其他Level统计
  std::atomic<uint64_t> other_levels_index_block_search_count{0};
  std::atomic<uint64_t> other_levels_index_block_search_time_ns{0};
  std::atomic<uint64_t> other_levels_bloom_filter_check_count{0};
  std::atomic<uint64_t> other_levels_bloom_filter_check_time_ns{0};
  std::atomic<uint64_t> other_levels_data_block_read_count{0};
  std::atomic<uint64_t> other_levels_data_block_read_time_ns{0};
  std::atomic<uint64_t> other_levels_bloom_filter_positive_count{0};
  std::atomic<uint64_t> other_levels_bloom_filter_negative_count{0};

  // PUT/WRITE operations 
  std::atomic<uint64_t> total_put_count{0};
  std::atomic<uint64_t> total_put_time_ns{0};
  std::atomic<uint64_t> batch_put_count{0};
  std::atomic<uint64_t> batch_put_time_ns{0};

  // GET operations summary
  std::atomic<uint64_t> total_get_count{0};
  std::atomic<uint64_t> total_get_time_ns{0};
  std::atomic<uint64_t> batch_get_count{0};
  std::atomic<uint64_t> batch_get_time_ns{0};

  // 重置所有统计数据
  void Reset();

  uint64_t GetWriteTime() const{
    return total_put_time_ns.load();
  }

  double GetLevelAverageSearchTimeNs(int level) const;

  void ResetBatchCounters();
    
  // 生成性能报告
  std::string GenerateReport() const;
    
  // 新增：计算并返回吞吐率统计数据
  ThroughputStats GetBatchThroughputStats() const;

  ThroughputStats GetTotalThroughputStats() const;

  void  PrintThroughputStats(const ThroughputStats& stats);

  // 根据吞吐率数据生成格式化的报告字符串
  std::string GenerateThroughputReport() const;

  void PrintThroughputReport() const;

  // 打印报告到stdout
  void PrintReport() const;
    
  // 检查是否需要自动报告
  bool ShouldAutoReport() const;
};

// 性能profile管理器 - 支持per-DB实例
class PerformanceProfiler {
  public:
    // 构造函数接受配置选项
    explicit PerformanceProfiler(const PerformanceProfileOptions& options = PerformanceProfileOptions());   
    // 静态方法获取全局实例（兼容旧API）
    static PerformanceProfiler& GlobalInstance();
    // 获取统计数据
    PerformanceStats& GetStats() { return stats_; }
    const PerformanceStats& GetStats() const { return stats_; }
    // 重置统计数据
    void Reset() { stats_.Reset(); }  
    // 生成并返回报告
    std::string GenerateReport() const { return stats_.GenerateReport(); }
    // 打印报告
    void PrintReport() const { stats_.PrintReport(); }
    // 检查并执行自动报告
    void CheckAutoReport();
    // 检查是否启用profiling
    bool IsEnabled() const { return options_.enable_profiling; }
    // 获取配置
    const PerformanceProfileOptions& GetOptions() const { return options_; }

  private:
    ~PerformanceProfiler();
    PerformanceProfileOptions options_;
    PerformanceStats stats_;
};

// RAII性能计时器 - 支持per-DB profiler
class PerformanceTimer {
public:
  PerformanceTimer(PerformanceProfiler* profiler,
    std::atomic<uint64_t> PerformanceStats::* time_counter, 
    std::atomic<uint64_t> PerformanceStats::* count_counter = nullptr);

  PerformanceTimer(PerformanceProfiler* profiler,
    std::atomic<uint64_t> PerformanceStats::* total_time_counter,std::atomic<uint64_t> PerformanceStats::* batch_time_counter,
    std::atomic<uint64_t> PerformanceStats::* total_count_counter,std::atomic<uint64_t> PerformanceStats::* batch_count_counter); 

  PerformanceTimer(PerformanceProfiler* profiler, 
    int level,std::atomic<uint64_t> (PerformanceStats::*time_array)[7], std::atomic<uint64_t> (PerformanceStats::*count_array)[7]); 

  ~PerformanceTimer();

private:
  std::chrono::high_resolution_clock::time_point start_time_;
  std::atomic<uint64_t>* time_counter_;
  std::atomic<uint64_t>* count_counter_;

  // 批次计数器
  std::atomic<uint64_t>* batch_time_counter_;
  std::atomic<uint64_t>* batch_count_counter_;

  bool is_dual_mode_;
  bool enabled_;
};

struct LSMStateSnapshot {
  // 构造函数，方便初始化
  LSMStateSnapshot()
    :level0_file_count(0),level1_file_count(0),memtable_size_bytes(0),is_first_l0_full(false) {}

  // Level-0 的 SST 文件数量
  size_t level0_file_count;
  // Level-1 的 SST 文件数量
  size_t level1_file_count;

  // 当前 Memtable 的大致大小（字节）
  uint64_t memtable_size_bytes;

  bool is_first_l0_full;
  // uint64_t block_cache_usage;
};

} // namespace leveldb

// ========== 便利函数声明 ==========
namespace leveldb {
  // 一行代码打印性能报告的便利函数
  void PrintPerformanceProfileIfEnabled(bool show_details = false);
}

// 便利宏定义 - 支持per-DB profiler和全局profiler
#define LEVELDB_PROFILE_TIMER_WITH_PROFILER(profiler, time_member, count_member) \
    leveldb::PerformanceTimer timer(profiler, &leveldb::PerformanceStats::time_member, \
                                   &leveldb::PerformanceStats::count_member)


#define LEVELDB_PROFILE_ENABLED() leveldb::PerformanceProfileOptions::IsProfilingEnabled()


//==================================================================
//         Macros for Profiling In-Memory Lookups
//==================================================================

// Profiles lookups in the active, writable MemTable.
#define LEVELDB_PROFILE_MEMTABLE_LOOKUP() \
    LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                       memtable_lookup_time_ns, memtable_lookup_count)

#define LEVELDB_PROFILE_IMMUTABLE_LOOKUP() \
    LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                       immutable_lookup_time_ns, immutable_lookup_count)


//==================================================================
//      Macros for Profiling Metadata(file ranges) Lookups
//==================================================================
#define LEVELDB_PROFILE_FILE_READ() \
    LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                       fileread_search_time_ns, fileread_search_count)

#define LEVELDB_PROFILE_FILE_Level0METADATA_SEARCH() \
    LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                       Level0_file_metadata_search_time_ns, Level0_file_metadata_search_count)

#define LEVELDB_PROFILE_FILE_OtherLevelMETADATA_SEARCH() \
    LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                       OtherLevel_file_metadata_search_time_ns, OtherLevel_file_metadata_search_count)

//==================================================================
//      Macros for Profiling in-file Lookups
//==================================================================
#define LEVELDB_PROFILE_LEVEL0_SEARCH() \
    LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                       level0_search_time_ns, level0_search_count)

#define LEVELDB_PROFILE_LEVEL_SEARCH(level) \
    leveldb::PerformanceTimer timer(&leveldb::PerformanceProfiler::GlobalInstance(), level, \
                                    &leveldb::PerformanceStats::level_search_time_ns, &leveldb::PerformanceStats::level_search_count)


//==================================================================
//    Macros for Profiling Level 0 File System Operations
//==================================================================
// Profiles Level 0 NewRandomAccessFile operations
#define LEVELDB_PROFILE_LEVEL0_FILE_OPEN() \
  LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                      level0_file_open_time_ns, level0_file_open_count)

// Profiles Level 0 Table::Open operations
#define LEVELDB_PROFILE_LEVEL0_TABLE_OPEN() \
  LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                      level0_table_open_time_ns, level0_table_open_count)

//==================================================================
//    Macros for Profiling Other Levels File System Operations
//==================================================================
// Profiles Other Levels NewRandomAccessFile operations
#define LEVELDB_PROFILE_OTHER_LEVELS_FILE_OPEN() \
  LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                      other_levels_file_open_time_ns, other_levels_file_open_count)

// Profiles Other Levels Table::Open operations
#define LEVELDB_PROFILE_OTHER_LEVELS_TABLE_OPEN() \
  LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                      other_levels_table_open_time_ns, other_levels_table_open_count)

//==================================================================
//    Macros for Profiling Level 0 Data Block Access
//==================================================================
// Profiles searches within Level 0 SSTable's Index Block
#define LEVELDB_PROFILE_LEVEL0_INDEX_BLOCK_SEARCH() \
  LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                      level0_index_block_search_time_ns, level0_index_block_search_count)

// Profiles Level 0 Bloom Filter checks
#define LEVELDB_PROFILE_LEVEL0_BLOOM_FILTER_CHECK() \
  LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                      level0_bloom_filter_check_time_ns, level0_bloom_filter_check_count)

// Profiles Level 0 Data Block reads
#define LEVELDB_PROFILE_LEVEL0_DATA_BLOCK_READ() \
  LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                      level0_data_block_read_time_ns, level0_data_block_read_count)

//==================================================================
//    Macros for Profiling Other Levels Data Block Access
//==================================================================
// Profiles searches within Other Levels SSTable's Index Block
#define LEVELDB_PROFILE_OTHER_LEVELS_INDEX_BLOCK_SEARCH() \
  LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                      other_levels_index_block_search_time_ns, other_levels_index_block_search_count)

// Profiles Other Levels Bloom Filter checks
#define LEVELDB_PROFILE_OTHER_LEVELS_BLOOM_FILTER_CHECK() \
  LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                      other_levels_bloom_filter_check_time_ns, other_levels_bloom_filter_check_count)

// Profiles Other Levels Data Block reads
#define LEVELDB_PROFILE_OTHER_LEVELS_DATA_BLOCK_READ() \
  LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                      other_levels_data_block_read_time_ns, other_levels_data_block_read_count)

//==================================================================
//          Macros for Counting Level 0 Bloom Filter Results
//==================================================================
// Increments Level 0 Bloom filter 'positive' results
#define LEVELDB_PROFILE_LEVEL0_BLOOM_FILTER_POSITIVE() \
    do { if (leveldb::PerformanceProfileOptions::IsProfilingEnabled()) \
         leveldb::PerformanceProfiler::GlobalInstance().GetStats().level0_bloom_filter_positive_count.fetch_add(1); } while(0)

// Increments Level 0 Bloom filter 'negative' results
#define LEVELDB_PROFILE_LEVEL0_BLOOM_FILTER_NEGATIVE() \
    do { if (leveldb::PerformanceProfileOptions::IsProfilingEnabled()) \
         leveldb::PerformanceProfiler::GlobalInstance().GetStats().level0_bloom_filter_negative_count.fetch_add(1); } while(0)

//==================================================================
//      Macros for Counting Other Levels Bloom Filter Results
//==================================================================
// Increments Other Levels Bloom filter 'positive' results
#define LEVELDB_PROFILE_OTHER_LEVELS_BLOOM_FILTER_POSITIVE() \
    do { if (leveldb::PerformanceProfileOptions::IsProfilingEnabled()) \
         leveldb::PerformanceProfiler::GlobalInstance().GetStats().other_levels_bloom_filter_positive_count.fetch_add(1); } while(0)

// Increments Other Levels Bloom filter 'negative' results
#define LEVELDB_PROFILE_OTHER_LEVELS_BLOOM_FILTER_NEGATIVE() \
    do { if (leveldb::PerformanceProfileOptions::IsProfilingEnabled()) \
         leveldb::PerformanceProfiler::GlobalInstance().GetStats().other_levels_bloom_filter_negative_count.fetch_add(1); } while(0)



//==================================================================
//          Macros for Overall Profiling Control
//==================================================================
#define LEVELDB_PROFILE_TOTAL_GET() \
    LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                       total_get_time_ns, total_get_count)

#define LEVELDB_PROFILE_DUAL_GET() \
    PerformanceTimer timer(&leveldb::PerformanceProfiler::GlobalInstance(), \
                          &PerformanceStats::total_get_time_ns, &PerformanceStats::batch_get_time_ns, \
                          &PerformanceStats::total_get_count, &PerformanceStats::batch_get_count)


#define LEVELDB_PROFILE_CHECK_AUTO_REPORT() \
    do { if (LEVELDB_PROFILE_ENABLED()) leveldb::PerformanceProfiler::GlobalInstance().CheckAutoReport(); } while(0)



#define LEVELDB_PROFILE_TOTAL_PUT() \
    LEVELDB_PROFILE_TIMER_WITH_PROFILER(&leveldb::PerformanceProfiler::GlobalInstance(), \
                                       total_put_time_ns, total_put_count)

#define LEVELDB_PROFILE_DUAL_PUT() \
    PerformanceTimer timer(&leveldb::PerformanceProfiler::GlobalInstance(), \
      &PerformanceStats::total_put_time_ns, &PerformanceStats::batch_put_time_ns, \
      &PerformanceStats::total_put_count, &PerformanceStats::batch_put_count)

#endif // STORAGE_LEVELDB_INCLUDE_LEVELDB_PERFORMANCE_PROFILE_H_