#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <atomic>
#include <cinttypes>
#include <iomanip> 
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <chrono>
#include <random>
#include <unordered_map>
#include <gflags/gflags.h> 
#include <optional>
#include <unistd.h>

#include "rocksdb/cache.h"
#include "rocksdb/convenience.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/memtablerep.h"
#include "rocksdb/options.h"
#include "rocksdb/perf_context.h"
#include "rocksdb/persistent_cache.h"
#include "rocksdb/rate_limiter.h"
#include "rocksdb/secondary_cache.h"
#include "rocksdb/slice.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/stats_history.h"
#include "rocksdb/table.h"
#include "rocksdb/utilities/backup_engine.h"
#include "rocksdb/utilities/object_registry.h"
#include "rocksdb/utilities/optimistic_transaction_db.h"
#include "rocksdb/utilities/options_type.h"
#include "rocksdb/utilities/options_util.h"
#include "rocksdb/utilities/replayer.h"
#include "rocksdb/utilities/sim_cache.h"
#include "rocksdb/utilities/transaction.h"
#include "rocksdb/utilities/transaction_db.h"
#include "rocksdb/write_batch.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/write_batch.h"
#include "rocksdb/slice.h"
#include "rocksdb/statistics.h"
#include "rocksdb/table.h"
#include "rocksdb/table_properties.h" // 新增头文件以提供完整的表定义
#include "rocksdb/filter_policy.h"
#include "rocksdb/perf_context.h"
#include "rocksdb/iostats_context.h"
#include "random.h"
#include "port_posix.h"
#include "histogram.h"
#include "persistent_cache_tier.h"

using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;
using GFLAGS_NAMESPACE::SetVersionString;

DEFINE_string(benchmarks,"mixgraph", "");

DEFINE_int64(num, 1000000, "Number of key/values to place in database");

DEFINE_int64(workload_num, 1000000, "Number of key/values to place in database");

DEFINE_int64(numdistinct, 1000,
             "Number of distinct keys to use. Used in RandomWithVerify to "
             "read/write on fewer keys so that gets are more likely to find the"
             " key and puts are more likely to update the same key");

DEFINE_int64(merge_keys, -1,
             "Number of distinct keys to use for MergeRandom and "
             "ReadRandomMergeRandom. "
             "If negative, there will be FLAGS_num keys.");
DEFINE_int32(num_column_families, 1, "Number of Column Families to use.");


DEFINE_int32(
    num_hot_column_families, 0,
    "Number of Hot Column Families. If more than 0, only write to this "
    "number of column families. After finishing all the writes to them, "
    "create new set of column families and insert to them. Only used "
    "when num_column_families > 1.");

DEFINE_string(column_family_distribution, "",
              "Comma-separated list of percentages, where the ith element "
              "indicates the probability of an op using the ith column family. "
              "The number of elements must be `num_hot_column_families` if "
              "specified; otherwise, it must be `num_column_families`. The "
              "sum of elements must be 100. E.g., if `num_column_families=4`, "
              "and `num_hot_column_families=0`, a valid list could be "
              "\"10,20,30,40\".");

DEFINE_int64(reads, -1,
             "Number of read operations to do.  "
             "If negative, do FLAGS_num reads.");

DEFINE_int64(deletes, -1,
             "Number of delete operations to do.  "
             "If negative, do FLAGS_num deletions.");

DEFINE_int32(bloom_locality, 0, "Control bloom filter probes locality");

// 首先添加新的 gflags
DEFINE_double(read_ratio, 0.5, "Read ratio (0.0 to 1.0)");
DEFINE_double(write_ratio, 0.5, "Write ratio (0.0 to 1.0)");
DEFINE_bool(verify_reads, false, "Verify read values against expected values");

DEFINE_int64(seed, 0,
             "Seed base for random number generators. "
             "When 0 it is derived from the current time.");

DEFINE_int32(threads, 1, "Number of concurrent threads to run.");

DEFINE_int32(duration, 0,
             "Time in seconds for the random-ops tests to run."
             " When 0 then num & reads determine the test duration");

DEFINE_string(value_size_distribution_type, "fixed",
              "Value size distribution type: fixed, uniform, normal");


DEFINE_string(data_file_path, "", "Value size distribution type: fixed, uniform, normal");

DEFINE_string(YCSB_data_file, "", "Use the db with the following name.");

DEFINE_int64(ycsb_num, 10000000, "Number of key/values to place in database");

DEFINE_string(Read_data_file, "", "Value size distribution type: fixed, uniform, normal");

DEFINE_string(mem_log_file,"", "mem usage path");
              

DEFINE_int32(value_size, 128, "Size of each value in fixed distribution");

DEFINE_int32(value_size_min, 100, "Min size of random value");

DEFINE_int32(value_size_max, 102400, "Max size of random value");

DEFINE_int32(seek_nexts, 0,
             "How many times to call Next() after Seek() in "
             "fillseekseq, seekrandom, seekrandomwhilewriting and "
             "seekrandomwhilemerging");

DEFINE_bool(reverse_iterator, false,
            "When true use Prev rather than Next for iterators that do "
            "Seek and then Next");

DEFINE_bool(auto_prefix_mode, false, "Set auto_prefix_mode for seek benchmark");

DEFINE_int64(max_scan_distance, 0,
             "Used to define iterate_upper_bound (or iterate_lower_bound "
             "if FLAGS_reverse_iterator is set to true) when value is nonzero");

DEFINE_bool(use_uint64_comparator, false, "use Uint64 user comparator");

DEFINE_int64(batch_size, 1, "Batch size");

static bool ValidateKeySize(const char* /*flagname*/, int32_t /*value*/) {
  return true;
}

static bool ValidateUint32Range(const char* flagname, uint64_t value) {
  if (value > std::numeric_limits<uint32_t>::max()) {
    fprintf(stderr, "Invalid value for --%s: %lu, overflow\n", flagname,
            (unsigned long)value);
    return false;
  }
  return true;
}

DEFINE_int32(key_size, 16, "size of each key");

DEFINE_int32(value_size_, 128, "size of each value");

DEFINE_int32(user_timestamp_size, 0,
             "number of bytes in a user-defined timestamp");

DEFINE_int32(num_multi_db, 0,
             "Number of DBs used in the benchmark. 0 means single DB.");

DEFINE_double(compression_ratio, 0.5,
              "Arrange to generate values that shrink to this fraction of "
              "their original size after compression");

DEFINE_double(
    overwrite_probability, 0.0,
    "Used in 'filluniquerandom' benchmark: for each write operation, "
    "we give a probability to perform an overwrite instead. The key used for "
    "the overwrite is randomly chosen from the last 'overwrite_window_size' "
    "keys previously inserted into the DB. "
    "Valid overwrite_probability values: [0.0, 1.0].");

DEFINE_uint32(overwrite_window_size, 1,
              "Used in 'filluniquerandom' benchmark. For each write operation,"
              " when the overwrite_probability flag is set by the user, the "
              "key used to perform an overwrite is randomly chosen from the "
              "last 'overwrite_window_size' keys previously inserted into DB. "
              "Warning: large values can affect throughput. "
              "Valid overwrite_window_size values: [1, kMaxUint32].");

DEFINE_uint64(
    disposable_entries_delete_delay, 0,
    "Minimum delay in microseconds for the series of Deletes "
    "to be issued. When 0 the insertion of the last disposable entry is "
    "immediately followed by the issuance of the Deletes. "
    "(only compatible with fillanddeleteuniquerandom benchmark).");

DEFINE_uint64(disposable_entries_batch_size, 0,
              "Number of consecutively inserted disposable KV entries "
              "that will be deleted after 'delete_delay' microseconds. "
              "A series of Deletes is always issued once all the "
              "disposable KV entries it targets have been inserted "
              "into the DB. When 0 no deletes are issued and a "
              "regular 'filluniquerandom' benchmark occurs. "
              "(only compatible with fillanddeleteuniquerandom benchmark)");

DEFINE_int32(disposable_entries_value_size, 64,
             "Size of the values (in bytes) of the entries targeted by "
             "selective deletes. "
             "(only compatible with fillanddeleteuniquerandom benchmark)");

DEFINE_uint64(
    persistent_entries_batch_size, 0,
    "Number of KV entries being inserted right before the deletes "
    "targeting the disposable KV entries are issued. These "
    "persistent keys are not targeted by the deletes, and will always "
    "remain valid in the DB. (only compatible with "
    "--benchmarks='fillanddeleteuniquerandom' "
    "and used when--disposable_entries_batch_size is > 0).");

DEFINE_int32(persistent_entries_value_size, 64,
             "Size of the values (in bytes) of the entries not targeted by "
             "deletes. (only compatible with "
             "--benchmarks='fillanddeleteuniquerandom' "
             "and used when--disposable_entries_batch_size is > 0).");

DEFINE_double(read_random_exp_range, 0.0,
              "Read random's key will be generated using distribution of "
              "num * exp(-r) where r is uniform number from 0 to this value. "
              "The larger the number is, the more skewed the reads are. "
              "Only used in readrandom and multireadrandom benchmarks.");

DEFINE_bool(histogram, false, "Print histogram of operation timings");

DEFINE_bool(confidence_interval_only, false,
            "Print 95% confidence interval upper and lower bounds only for "
            "aggregate stats.");

DEFINE_bool(enable_numa, false,
            "Make operations aware of NUMA architecture and bind memory "
            "and cpus corresponding to nodes together. In NUMA, memory "
            "in same node as CPUs are closer when compared to memory in "
            "other nodes. Reads can be faster when the process is bound to "
            "CPU and memory of same node. Use \"$numactl --hardware\" command "
            "to see NUMA memory architecture.");

DEFINE_int64(db_write_buffer_size,
             rocksdb::Options().db_write_buffer_size,
             "Number of bytes to buffer in all memtables before compacting");

DEFINE_bool(cost_write_buffer_to_cache, false,
            "The usage of memtable is costed to the block cache");

DEFINE_int64(arena_block_size, rocksdb::Options().arena_block_size,
             "The size, in bytes, of one block in arena memory allocation.");

DEFINE_int64(write_buffer_size, rocksdb::Options().write_buffer_size,
             "Number of bytes to buffer in memtable before compacting");

DEFINE_int32(max_write_buffer_number,
             rocksdb::Options().max_write_buffer_number,
             "The number of in-memory memtables. Each memtable is of size"
             " write_buffer_size bytes.");

DEFINE_int32(min_write_buffer_number_to_merge,
             rocksdb::Options().min_write_buffer_number_to_merge,
             "The minimum number of write buffers that will be merged together"
             "before writing to storage. This is cheap because it is an"
             "in-memory merge. If this feature is not enabled, then all these"
             "write buffers are flushed to L0 as separate files and this "
             "increases read amplification because a get request has to check"
             " in all of these files. Also, an in-memory merge may result in"
             " writing less data to storage if there are duplicate records "
             " in each of these individual write buffers.");

DEFINE_int32(max_write_buffer_number_to_maintain,
             rocksdb::Options().max_write_buffer_number_to_maintain,
             "The total maximum number of write buffers to maintain in memory "
             "including copies of buffers that have already been flushed. "
             "Unlike max_write_buffer_number, this parameter does not affect "
             "flushing. This controls the minimum amount of write history "
             "that will be available in memory for conflict checking when "
             "Transactions are used. If this value is too low, some "
             "transactions may fail at commit time due to not being able to "
             "determine whether there were any write conflicts. Setting this "
             "value to 0 will cause write buffers to be freed immediately "
             "after they are flushed.  If this value is set to -1, "
             "'max_write_buffer_number' will be used.");

DEFINE_int64(max_write_buffer_size_to_maintain,
             rocksdb::Options().max_write_buffer_size_to_maintain,
             "The total maximum size of write buffers to maintain in memory "
             "including copies of buffers that have already been flushed. "
             "Unlike max_write_buffer_number, this parameter does not affect "
             "flushing. This controls the minimum amount of write history "
             "that will be available in memory for conflict checking when "
             "Transactions are used. If this value is too low, some "
             "transactions may fail at commit time due to not being able to "
             "determine whether there were any write conflicts. Setting this "
             "value to 0 will cause write buffers to be freed immediately "
             "after they are flushed.  If this value is set to -1, "
             "'max_write_buffer_number' will be used.");

DEFINE_int32(max_background_jobs,
             rocksdb::Options().max_background_jobs,
             "The maximum number of concurrent background jobs that can occur "
             "in parallel.");

DEFINE_int32(num_bottom_pri_threads, 0,
             "The number of threads in the bottom-priority thread pool (used "
             "by universal compaction only).");

DEFINE_int32(num_high_pri_threads, 0,
             "The maximum number of concurrent background compactions"
             " that can occur in parallel.");

DEFINE_int32(num_low_pri_threads, 0,
             "The maximum number of concurrent background compactions"
             " that can occur in parallel.");

DEFINE_int32(max_background_compactions,
             rocksdb::Options().max_background_compactions,
             "The maximum number of concurrent background compactions"
             " that can occur in parallel.");

DEFINE_uint64(subcompactions, 1,
              "For CompactRange, set max_subcompactions for each compaction "
              "job in this CompactRange, for auto compactions, this is "
              "Maximum number of subcompactions to divide L0-L1 compactions "
              "into.");
static const bool FLAGS_subcompactions_dummy __attribute__((__unused__)) =
    RegisterFlagValidator(&FLAGS_subcompactions, &ValidateUint32Range);

DEFINE_int32(max_background_flushes,
             rocksdb::Options().max_background_flushes,
             "The maximum number of concurrent background flushes"
             " that can occur in parallel.");

static rocksdb::CompactionStyle FLAGS_compaction_style_e;
DEFINE_int32(compaction_style,
             (int32_t)rocksdb::Options().compaction_style,
             "style of compaction: level-based, universal and fifo");

static rocksdb::CompactionPri FLAGS_compaction_pri_e;
DEFINE_int32(compaction_pri,
             (int32_t)rocksdb::Options().compaction_pri,
             "priority of files to compaction: by size or by data age");

DEFINE_int32(universal_size_ratio, 0,
             "Percentage flexibility while comparing file size "
             "(for universal compaction only).");

DEFINE_int32(universal_min_merge_width, 0,
             "The minimum number of files in a single compaction run "
             "(for universal compaction only).");

DEFINE_int32(universal_max_merge_width, 0,
             "The max number of files to compact in universal style "
             "compaction");

DEFINE_int32(universal_max_size_amplification_percent, 0,
             "The max size amplification for universal style compaction");

DEFINE_int32(universal_compression_size_percent, -1,
             "The percentage of the database to compress for universal "
             "compaction. -1 means compress everything.");

DEFINE_int32(universal_max_read_amp, -1,
             "The limit on the number of sorted runs");

DEFINE_bool(universal_allow_trivial_move, false,
            "Allow trivial move in universal compaction.");

DEFINE_bool(universal_incremental, false,
            "Enable incremental compactions in universal compaction.");

DEFINE_int32(
    universal_stop_style,
    (int32_t)rocksdb::CompactionOptionsUniversal().stop_style,
    "Universal compaction stop style.");

DEFINE_int64(cache_size, 32 << 20,  // 32MB
             "Number of bytes to use as a cache of uncompressed data");

DEFINE_int32(cache_numshardbits, -1,
             "Number of shards for the block cache"
             " is 2 ** cache_numshardbits. Negative means use default settings."
             " This is applied only if FLAGS_cache_size is non-negative.");

DEFINE_double(cache_high_pri_pool_ratio, 0.0,
              "Ratio of block cache reserve for high pri blocks. "
              "If > 0.0, we also enable "
              "cache_index_and_filter_blocks_with_high_priority.");

DEFINE_double(cache_low_pri_pool_ratio, 0.0,
              "Ratio of block cache reserve for low pri blocks.");

DEFINE_string(cache_type, "lru_cache", "Type of block cache.");

DEFINE_bool(use_compressed_secondary_cache, false,
            "Use the CompressedSecondaryCache as the secondary cache.");

DEFINE_int64(compressed_secondary_cache_size, 32 << 20,  // 32MB
             "Number of bytes to use as a cache of data");

DEFINE_int32(compressed_secondary_cache_numshardbits, 6,
             "Number of shards for the block cache"
             " is 2 ** compressed_secondary_cache_numshardbits."
             " Negative means use default settings."
             " This is applied only if FLAGS_cache_size is non-negative.");

DEFINE_double(compressed_secondary_cache_high_pri_pool_ratio, 0.0,
              "Ratio of block cache reserve for high pri blocks. "
              "If > 0.0, we also enable "
              "cache_index_and_filter_blocks_with_high_priority.");

DEFINE_double(compressed_secondary_cache_low_pri_pool_ratio, 0.0,
              "Ratio of block cache reserve for low pri blocks.");

DEFINE_string(compressed_secondary_cache_compression_type, "lz4",
              "The compression algorithm to use for large "
              "values stored in CompressedSecondaryCache.");
static enum rocksdb::CompressionType
    FLAGS_compressed_secondary_cache_compression_type_e =
        rocksdb::kLZ4Compression;

DEFINE_int32(compressed_secondary_cache_compression_level,
             rocksdb::CompressionOptions().level,
             "Compression level. The meaning of this value is library-"
             "dependent. If unset, we try to use the default for the library "
             "specified in `--compressed_secondary_cache_compression_type`");

DEFINE_uint32(
    compressed_secondary_cache_compress_format_version, 2,
    "compress_format_version can have two values: "
    "compress_format_version == 1 -- decompressed size is not included"
    " in the block header."
    "compress_format_version == 2 -- decompressed size is included"
    " in the block header in varint32 format.");

DEFINE_bool(use_tiered_cache, false,
            "If use_compressed_secondary_cache is true and "
            "use_tiered_volatile_cache is true, then allocate a tiered cache "
            "that distributes cache reservations proportionally over both "
            "the caches.");

DEFINE_string(
    tiered_adm_policy, "auto",
    "Admission policy to use for the secondary cache(s) in the tiered cache. "
    "Allowed values are auto, placeholder, allow_cache_hits, and three_queue.");

DEFINE_int64(simcache_size, -1,
             "Number of bytes to use as a simcache of "
             "uncompressed data. Nagative value disables simcache.");

DEFINE_bool(cache_index_and_filter_blocks, false,
            "Cache index/filter blocks in block cache.");

DEFINE_bool(use_cache_jemalloc_no_dump_allocator, false,
            "Use JemallocNodumpAllocator for block/blob cache.");

DEFINE_bool(use_cache_memkind_kmem_allocator, false,
            "Use memkind kmem allocator for block/blob cache.");

DEFINE_bool(partition_index_and_filters, false,
            "Partition index and filter blocks.");

DEFINE_bool(partition_index, false, "Partition index blocks");

DEFINE_bool(index_with_first_key, false, "Include first key in the index");

DEFINE_bool(
    optimize_filters_for_memory,
    rocksdb::BlockBasedTableOptions().optimize_filters_for_memory,
    "Minimize memory footprint of filters");

DEFINE_int64(
    index_shortening_mode, 2,
    "mode to shorten index: 0 for no shortening; 1 for only shortening "
    "separaters; 2 for shortening shortening and successor");

DEFINE_int64(metadata_block_size,
             rocksdb::BlockBasedTableOptions().metadata_block_size,
             "Max partition size when partitioning index/filters");

// The default reduces the overhead of reading time with flash. With HDD, which
// offers much less throughput, however, this number better to be set to 1.
DEFINE_int32(ops_between_duration_checks, 1000,
             "Check duration limit every x ops");

DEFINE_bool(pin_l0_filter_and_index_blocks_in_cache, false,
            "Pin index/filter blocks of L0 files in block cache.");

DEFINE_bool(
    pin_top_level_index_and_filter, false,
    "Pin top-level index of partitioned index/filter blocks in block cache.");

DEFINE_int32(block_size,
             static_cast<int32_t>(
                 rocksdb::BlockBasedTableOptions().block_size),
             "Number of bytes in a block.");

DEFINE_int32(format_version,
             static_cast<int32_t>(
                 rocksdb::BlockBasedTableOptions().format_version),
             "Format version of SST files.");

DEFINE_int32(block_restart_interval,
             rocksdb::BlockBasedTableOptions().block_restart_interval,
             "Number of keys between restart points "
             "for delta encoding of keys in data block.");

DEFINE_int32(
    index_block_restart_interval,
    rocksdb::BlockBasedTableOptions().index_block_restart_interval,
    "Number of keys between restart points "
    "for delta encoding of keys in index block.");

DEFINE_int32(read_amp_bytes_per_bit,
             rocksdb::BlockBasedTableOptions().read_amp_bytes_per_bit,
             "Number of bytes per bit to be used in block read-amp bitmap");

DEFINE_bool(
    enable_index_compression,
    rocksdb::BlockBasedTableOptions().enable_index_compression,
    "Compress the index block");

DEFINE_bool(block_align,
            rocksdb::BlockBasedTableOptions().block_align,
            "Align data blocks on page size");

DEFINE_int64(prepopulate_block_cache, 0,
             "Pre-populate hot/warm blocks in block cache. 0 to disable and 1 "
             "to insert during flush");

DEFINE_bool(use_data_block_hash_index, false,
            "if use kDataBlockBinaryAndHash "
            "instead of kDataBlockBinarySearch. "
            "This is valid if only we use BlockTable");

DEFINE_double(data_block_hash_table_util_ratio, 0.75,
              "util ratio for data block hash index table. "
              "This is only valid if use_data_block_hash_index is "
              "set to true");

DEFINE_int64(compressed_cache_size, -1,
             "Number of bytes to use as a cache of compressed data.");

DEFINE_int64(row_cache_size, 0,
             "Number of bytes to use as a cache of individual rows"
             " (0 = disabled).");

DEFINE_int32(open_files, rocksdb::Options().max_open_files,
             "Maximum number of files to keep open at the same time"
             " (use default if == 0)");

DEFINE_int32(file_opening_threads,
             rocksdb::Options().max_file_opening_threads,
             "If open_files is set to -1, this option set the number of "
             "threads that will be used to open files during DB::Open()");

DEFINE_uint64(compaction_readahead_size,
              rocksdb::Options().compaction_readahead_size,
              "Compaction readahead size");

DEFINE_int32(log_readahead_size, 0, "WAL and manifest readahead size");

DEFINE_int32(random_access_max_buffer_size, 1024 * 1024,
             "Maximum windows randomaccess buffer size");

DEFINE_int32(writable_file_max_buffer_size, 1024 * 1024,
             "Maximum write buffer for Writable File");

DEFINE_int32(bloom_bits, -1,
             "Bloom filter bits per key. Negative means use default."
             "Zero disables.");

DEFINE_bool(use_ribbon_filter, false, "Use Ribbon instead of Bloom filter");

DEFINE_double(memtable_bloom_size_ratio, 0,
              "Ratio of memtable size used for bloom filter. 0 means no bloom "
              "filter.");
DEFINE_bool(memtable_whole_key_filtering, false,
            "Try to use whole key bloom filter in memtables.");
DEFINE_bool(memtable_use_huge_page, false,
            "Try to use huge page in memtables.");

DEFINE_bool(whole_key_filtering,
            rocksdb::BlockBasedTableOptions().whole_key_filtering,
            "Use whole keys (in addition to prefixes) in SST bloom filter.");

DEFINE_bool(use_existing_db, false,
            "If true, do not destroy the existing database.  If you set this "
            "flag and also specify a benchmark that wants a fresh database, "
            "that benchmark will fail.");

DEFINE_bool(use_existing_keys, false,
            "If true, uses existing keys in the DB, "
            "rather than generating new ones. This involves some startup "
            "latency to load all keys into memory. It is supported for the "
            "same read/overwrite benchmarks as `-use_existing_db=true`, which "
            "must also be set for this flag to be enabled. When this flag is "
            "set, the value for `-num` will be ignored.");

DEFINE_bool(show_table_properties, false,
            "If true, then per-level table"
            " properties will be printed on every stats-interval when"
            " stats_interval is set and stats_per_interval is on.");

DEFINE_string(db, "", "Use the db with the following name.");

DEFINE_bool(progress_reports, true,
            "If true, db_bench will report number of finished operations.");

// Read cache flags

DEFINE_string(read_cache_path, "",
              "If not empty string, a read cache will be used in this path");

DEFINE_int64(read_cache_size, 4LL * 1024 * 1024 * 1024,
             "Maximum size of the read cache");

DEFINE_bool(read_cache_direct_write, true,
            "Whether to use Direct IO for writing to the read cache");

DEFINE_bool(read_cache_direct_read, true,
            "Whether to use Direct IO for reading from read cache");

DEFINE_bool(use_keep_filter, false, "Whether to use a noop compaction filter");

static bool ValidateCacheNumshardbits(const char* flagname, int32_t value) {
  if (value >= 20) {
    fprintf(stderr, "Invalid value for --%s: %d, must be < 20\n", flagname,
            value);
    return false;
  }
  return true;
}

DEFINE_bool(verify_checksum, true,
            "Verify checksum for every block read from storage");

DEFINE_int32(checksum_type,
             rocksdb::BlockBasedTableOptions().checksum,
             "ChecksumType as an int");

DEFINE_bool(statistics, false, "Database statistics");
DEFINE_int32(stats_level, rocksdb::StatsLevel::kExceptDetailedTimers,
             "stats level for statistics");
DEFINE_string(statistics_string, "", "Serialized statistics string");
static class std::shared_ptr<rocksdb::Statistics> dbstats;

DEFINE_int64(writes, -1,
             "Number of write operations to do. If negative, do --num reads.");

DEFINE_bool(finish_after_writes, false,
            "Write thread terminates after all writes are finished");

DEFINE_bool(sync, false, "Sync all writes to disk");

DEFINE_bool(use_fsync, false, "If true, issue fsync instead of fdatasync");

DEFINE_bool(disable_wal, false, "If true, do not write WAL for write.");

DEFINE_bool(manual_wal_flush, false,
            "If true, buffer WAL until buffer is full or a manual FlushWAL().");

DEFINE_string(wal_compression, "none",
              "Algorithm to use for WAL compression. none to disable.");
static enum rocksdb::CompressionType FLAGS_wal_compression_e =
    rocksdb::kNoCompression;

DEFINE_string(wal_dir, "", "If not empty, use the given dir for WAL");

DEFINE_string(truth_db, "/dev/shm/truth_db/dbbench",
              "Truth key/values used when using verify");

DEFINE_int32(num_levels, 7, "The total number of levels");

DEFINE_int64(target_file_size_base,
             rocksdb::Options().target_file_size_base,
             "Target file size at level-1");

DEFINE_int32(target_file_size_multiplier,
             rocksdb::Options().target_file_size_multiplier,
             "A multiplier to compute target level-N file size (N >= 2)");

DEFINE_uint64(max_bytes_for_level_base,
              rocksdb::Options().max_bytes_for_level_base,
              "Max bytes for level-1");

DEFINE_bool(level_compaction_dynamic_level_bytes, false,
            "Whether level size base is dynamic");

DEFINE_double(max_bytes_for_level_multiplier, 10,
              "A multiplier to compute max bytes for level-N (N >= 2)");

static std::vector<int> FLAGS_max_bytes_for_level_multiplier_additional_v;
DEFINE_string(max_bytes_for_level_multiplier_additional, "",
              "A vector that specifies additional fanout per level");

DEFINE_int32(level0_stop_writes_trigger,
             rocksdb::Options().level0_stop_writes_trigger,
             "Number of files in level-0 that will trigger put stop.");

DEFINE_int32(level0_slowdown_writes_trigger,
             rocksdb::Options().level0_slowdown_writes_trigger,
             "Number of files in level-0 that will slow down writes.");

DEFINE_int32(level0_file_num_compaction_trigger,
             rocksdb::Options().level0_file_num_compaction_trigger,
             "Number of files in level-0 when compactions start.");

DEFINE_uint64(periodic_compaction_seconds,
              rocksdb::Options().periodic_compaction_seconds,
              "Files older than this will be picked up for compaction and"
              " rewritten to the same level");

DEFINE_uint64(ttl_seconds, rocksdb::Options().ttl, "Set options.ttl");

static bool ValidateInt32Percent(const char* flagname, int32_t value) {
  if (value <= 0 || value >= 100) {
    fprintf(stderr, "Invalid value for --%s: %d, 0< pct <100 \n", flagname,
            value);
    return false;
  }
  return true;
}

DEFINE_int32(readwritepercent, 90,
             "Ratio of reads to reads/writes (expressed as percentage) for "
             "the ReadRandomWriteRandom workload. The default value 90 means "
             "90% operations out of all reads and writes operations are "
             "reads. In other words, 9 gets for every 1 put.");

DEFINE_int32(mergereadpercent, 70,
             "Ratio of merges to merges&reads (expressed as percentage) for "
             "the ReadRandomMergeRandom workload. The default value 70 means "
             "70% out of all read and merge operations are merges. In other "
             "words, 7 merges for every 3 gets.");

DEFINE_int32(deletepercent, 2,
             "Percentage of deletes out of reads/writes/deletes (used in "
             "RandomWithVerify only). RandomWithVerify "
             "calculates writepercent as (100 - FLAGS_readwritepercent - "
             "deletepercent), so deletepercent must be smaller than (100 - "
             "FLAGS_readwritepercent)");

DEFINE_bool(optimize_filters_for_hits,
            rocksdb::Options().optimize_filters_for_hits,
            "Optimizes bloom filters for workloads for most lookups return "
            "a value. For now this doesn't create bloom filters for the max "
            "level of the LSM to reduce metadata that should fit in RAM. ");

DEFINE_bool(paranoid_checks, rocksdb::Options().paranoid_checks,
            "RocksDB will aggressively check consistency of the data.");

DEFINE_bool(force_consistency_checks,
            rocksdb::Options().force_consistency_checks,
            "Runs consistency checks on the LSM every time a change is "
            "applied.");

DEFINE_uint64(delete_obsolete_files_period_micros, 0,
              "Ignored. Left here for backward compatibility");

DEFINE_int64(writes_before_delete_range, 0,
             "Number of writes before DeleteRange is called regularly.");

DEFINE_int64(writes_per_range_tombstone, 0,
             "Number of writes between range tombstones");

DEFINE_int64(range_tombstone_width, 100, "Number of keys in tombstone's range");

DEFINE_int64(max_num_range_tombstones, 0,
             "Maximum number of range tombstones to insert.");

DEFINE_bool(expand_range_tombstones, false,
            "Expand range tombstone into sequential regular tombstones.");

// Transactions Options
DEFINE_bool(optimistic_transaction_db, false,
            "Open a OptimisticTransactionDB instance. "
            "Required for randomtransaction benchmark.");

DEFINE_bool(transaction_db, false,
            "Open a TransactionDB instance. "
            "Required for randomtransaction benchmark.");

DEFINE_uint64(transaction_sets, 2,
              "Number of keys each transaction will "
              "modify (use in RandomTransaction only).  Max: 9999");

DEFINE_bool(transaction_set_snapshot, false,
            "Setting to true will have each transaction call SetSnapshot()"
            " upon creation.");

DEFINE_int32(transaction_sleep, 0,
             "Max microseconds to sleep in between "
             "reading and writing a value (used in RandomTransaction only). ");

DEFINE_uint64(transaction_lock_timeout, 100,
              "If using a transaction_db, specifies the lock wait timeout in"
              " milliseconds before failing a transaction waiting on a lock");
DEFINE_string(
    options_file, "",
    "The path to a RocksDB options file.  If specified, then db_bench will "
    "run with the RocksDB options in the default column family of the "
    "specified options file. "
    "Note that with this setting, db_bench will ONLY accept the following "
    "RocksDB options related command-line arguments, all other arguments "
    "that are related to RocksDB options will be ignored:\n"
    "\t--use_existing_db\n"
    "\t--use_existing_keys\n"
    "\t--statistics\n"
    "\t--row_cache_size\n"
    "\t--row_cache_numshardbits\n"
    "\t--enable_io_prio\n"
    "\t--dump_malloc_stats\n"
    "\t--num_multi_db\n");

// FIFO Compaction Options
DEFINE_uint64(fifo_compaction_max_table_files_size_mb, 0,
              "The limit of total table file sizes to trigger FIFO compaction");

DEFINE_bool(fifo_compaction_allow_compaction, true,
            "Allow compaction in FIFO compaction.");

DEFINE_uint64(fifo_compaction_ttl, 0, "TTL for the SST Files in seconds.");

DEFINE_uint64(fifo_age_for_warm, 0, "age_for_warm for FIFO compaction.");

// Stacked BlobDB Options
DEFINE_bool(use_blob_db, false, "[Stacked BlobDB] Open a BlobDB instance.");

DEFINE_string(
    blob_db_compression_type, "snappy",
    "[Stacked BlobDB] Algorithm to use to compress blobs in blob files.");
static enum rocksdb::CompressionType
    FLAGS_blob_db_compression_type_e = rocksdb::kSnappyCompression;


// Integrated BlobDB options
DEFINE_bool(
    enable_blob_files,
    rocksdb::AdvancedColumnFamilyOptions().enable_blob_files,
    "[Integrated BlobDB] Enable writing large values to separate blob files.");

DEFINE_uint64(min_blob_size,
              rocksdb::AdvancedColumnFamilyOptions().min_blob_size,
              "[Integrated BlobDB] The size of the smallest value to be stored "
              "separately in a blob file.");

DEFINE_uint64(blob_file_size,
              rocksdb::AdvancedColumnFamilyOptions().blob_file_size,
              "[Integrated BlobDB] The size limit for blob files.");

DEFINE_string(blob_compression_type, "none",
              "[Integrated BlobDB] The compression algorithm to use for large "
              "values stored in blob files.");

DEFINE_bool(enable_blob_garbage_collection,
            rocksdb::AdvancedColumnFamilyOptions()
                .enable_blob_garbage_collection,
            "[Integrated BlobDB] Enable blob garbage collection.");

DEFINE_double(blob_garbage_collection_age_cutoff,
              rocksdb::AdvancedColumnFamilyOptions()
                  .blob_garbage_collection_age_cutoff,
              "[Integrated BlobDB] The cutoff in terms of blob file age for "
              "garbage collection.");

DEFINE_double(blob_garbage_collection_force_threshold,
              rocksdb::AdvancedColumnFamilyOptions()
                  .blob_garbage_collection_force_threshold,
              "[Integrated BlobDB] The threshold for the ratio of garbage in "
              "the oldest blob files for forcing garbage collection.");

DEFINE_uint64(blob_compaction_readahead_size,
              rocksdb::AdvancedColumnFamilyOptions()
                  .blob_compaction_readahead_size,
              "[Integrated BlobDB] Compaction readahead for blob files.");

DEFINE_int32(
    blob_file_starting_level,
    rocksdb::AdvancedColumnFamilyOptions().blob_file_starting_level,
    "[Integrated BlobDB] The starting level for blob files.");

DEFINE_bool(use_blob_cache, false, "[Integrated BlobDB] Enable blob cache.");

DEFINE_bool(
    use_shared_block_and_blob_cache, true,
    "[Integrated BlobDB] Use a shared backing cache for both block "
    "cache and blob cache. It only takes effect if use_blob_cache is enabled.");

DEFINE_uint64(
    blob_cache_size, 8 << 20,
    "[Integrated BlobDB] Number of bytes to use as a cache of blobs. It only "
    "takes effect if the block and blob caches are different "
    "(use_shared_block_and_blob_cache = false).");

DEFINE_int32(blob_cache_numshardbits, 6,
             "[Integrated BlobDB] Number of shards for the blob cache is 2 ** "
             "blob_cache_numshardbits. Negative means use default settings. "
             "It only takes effect if blob_cache_size is greater than 0, and "
             "the block and blob caches are different "
             "(use_shared_block_and_blob_cache = false).");

DEFINE_int32(prepopulate_blob_cache, 0,
             "[Integrated BlobDB] Pre-populate hot/warm blobs in blob cache. 0 "
             "to disable and 1 to insert during flush.");


// Secondary DB instance Options
DEFINE_bool(use_secondary_db, false,
            "Open a RocksDB secondary instance. A primary instance can be "
            "running in another db_bench process.");

DEFINE_string(secondary_path, "",
              "Path to a directory used by the secondary instance to store "
              "private files, e.g. info log.");

DEFINE_int32(secondary_update_interval, 5,
             "Secondary instance attempts to catch up with the primary every "
             "secondary_update_interval seconds.");

DEFINE_bool(open_as_follower, false,
            "Open a RocksDB DB as a follower. The leader instance can be "
            "running in another db_bench process.");

DEFINE_string(leader_path, "", "Path to the directory of the leader DB");

DEFINE_bool(report_bg_io_stats, false,
            "Measure times spents on I/Os while in compactions. ");

DEFINE_bool(use_stderr_info_logger, false,
            "Write info logs to stderr instead of to LOG file. ");


DEFINE_string(trace_file, "", "Trace workload to a file. ");

DEFINE_double(trace_replay_fast_forward, 1.0,
              "Fast forward trace replay, must > 0.0.");
DEFINE_int32(block_cache_trace_sampling_frequency, 1,
             "Block cache trace sampling frequency, termed s. It uses spatial "
             "downsampling and samples accesses to one out of s blocks.");
DEFINE_int64(
    block_cache_trace_max_trace_file_size_in_bytes,
    uint64_t{64} * 1024 * 1024 * 1024,
    "The maximum block cache trace file size in bytes. Block cache accesses "
    "will not be logged if the trace file size exceeds this threshold. Default "
    "is 64 GB.");
DEFINE_string(block_cache_trace_file, "", "Block cache trace file path.");
DEFINE_int32(trace_replay_threads, 1,
             "The number of threads to replay, must >=1.");

DEFINE_bool(io_uring_enabled, true,
            "If true, enable the use of IO uring if the platform supports it");
extern "C" bool RocksDbIOUringEnable() { return FLAGS_io_uring_enabled; }

DEFINE_bool(adaptive_readahead, false,
            "carry forward internal auto readahead size from one file to next "
            "file at each level during iteration");

DEFINE_bool(rate_limit_user_ops, false,
            "When true use Env::IO_USER priority level to charge internal rate "
            "limiter for reads associated with user operations.");

DEFINE_bool(file_checksum, false,
            "When true use FileChecksumGenCrc32cFactory for "
            "file_checksum_gen_factory.");

DEFINE_bool(rate_limit_auto_wal_flush, false,
            "When true use Env::IO_USER priority level to charge internal rate "
            "limiter for automatic WAL flush (`Options::manual_wal_flush` == "
            "false) after the user write operation.");

DEFINE_bool(async_io, false,
            "When set true, RocksDB does asynchronous reads for internal auto "
            "readahead prefetching.");

DEFINE_bool(optimize_multiget_for_io, true,
            "When set true, RocksDB does asynchronous reads for SST files in "
            "multiple levels for MultiGet.");

DEFINE_bool(charge_compression_dictionary_building_buffer, false,
            "Setting for "
            "CacheEntryRoleOptions::charged of "
            "CacheEntryRole::kCompressionDictionaryBuildingBuffer");

DEFINE_bool(charge_filter_construction, false,
            "Setting for "
            "CacheEntryRoleOptions::charged of "
            "CacheEntryRole::kFilterConstruction");

DEFINE_bool(charge_table_reader, false,
            "Setting for "
            "CacheEntryRoleOptions::charged of "
            "CacheEntryRole::kBlockBasedTableReader");

DEFINE_bool(charge_file_metadata, false,
            "Setting for "
            "CacheEntryRoleOptions::charged of "
            "CacheEntryRole::kFileMetadata");

DEFINE_bool(charge_blob_cache, false,
            "Setting for "
            "CacheEntryRoleOptions::charged of "
            "CacheEntryRole::kBlobCache");

DEFINE_uint64(backup_rate_limit, 0ull,
              "If non-zero, db_bench will rate limit reads and writes for DB "
              "backup. This "
              "is the global rate in ops/second.");

DEFINE_uint64(restore_rate_limit, 0ull,
              "If non-zero, db_bench will rate limit reads and writes for DB "
              "restore. This "
              "is the global rate in ops/second.");

DEFINE_string(backup_dir, "",
              "If not empty string, use the given dir for backup.");

DEFINE_string(restore_dir, "",
              "If not empty string, use the given dir for restore.");

DEFINE_uint64(
    initial_auto_readahead_size,
    rocksdb::BlockBasedTableOptions().initial_auto_readahead_size,
    "RocksDB does auto-readahead for iterators on noticing more than two reads "
    "for a table file if user doesn't provide readahead_size. The readahead "
    "size starts at initial_auto_readahead_size");

DEFINE_uint64(
    max_auto_readahead_size,
    rocksdb::BlockBasedTableOptions().max_auto_readahead_size,
    "Rocksdb implicit readahead starts at "
    "BlockBasedTableOptions.initial_auto_readahead_size and doubles on every "
    "additional read upto max_auto_readahead_size");

DEFINE_uint64(
    num_file_reads_for_auto_readahead,
    rocksdb::BlockBasedTableOptions()
        .num_file_reads_for_auto_readahead,
    "Rocksdb implicit readahead is enabled if reads are sequential and "
    "num_file_reads_for_auto_readahead indicates after how many sequential "
    "reads into that file internal auto prefetching should be start.");

DEFINE_bool(
    auto_readahead_size, false,
    "When set true, RocksDB does auto tuning of readahead size during Scans");

static enum rocksdb::CompressionType StringToCompressionType(
    const char* ctype) {
  assert(ctype);

  if (!strcasecmp(ctype, "none")) {
    return rocksdb::kNoCompression;
  } else if (!strcasecmp(ctype, "snappy")) {
    return rocksdb::kSnappyCompression;
  } else if (!strcasecmp(ctype, "zlib")) {
    return rocksdb::kZlibCompression;
  } else if (!strcasecmp(ctype, "bzip2")) {
    return rocksdb::kBZip2Compression;
  } else if (!strcasecmp(ctype, "lz4")) {
    return rocksdb::kLZ4Compression;
  } else if (!strcasecmp(ctype, "lz4hc")) {
    return rocksdb::kLZ4HCCompression;
  } else if (!strcasecmp(ctype, "xpress")) {
    return rocksdb::kXpressCompression;
  } else if (!strcasecmp(ctype, "zstd")) {
    return rocksdb::kZSTD;
  } else {
    fprintf(stderr, "Cannot parse compression type '%s'\n", ctype);
    exit(1);
  }
}

static enum rocksdb::TieredAdmissionPolicy StringToAdmissionPolicy(
    const char* policy) {
  assert(policy);

  if (!strcasecmp(policy, "auto")) {
    return rocksdb::kAdmPolicyAuto;
  } else if (!strcasecmp(policy, "placeholder")) {
    return rocksdb::kAdmPolicyPlaceholder;
  } else if (!strcasecmp(policy, "allow_cache_hits")) {
    return rocksdb::kAdmPolicyAllowCacheHits;
  } else if (!strcasecmp(policy, "three_queue")) {
    return rocksdb::kAdmPolicyThreeQueue;
  } else {
    fprintf(stderr, "Cannot parse admission policy %s\n", policy);
    exit(1);
  }
}

static std::string ColumnFamilyName(size_t i) {
  if (i == 0) {
    return rocksdb::kDefaultColumnFamilyName;
  } else {
    char name[100];
    snprintf(name, sizeof(name), "column_family_name_%06zu", i);
    return std::string(name);
  }
}

DEFINE_string(compression_type, "snappy",
              "Algorithm to use to compress the database");
static enum rocksdb::CompressionType FLAGS_compression_type_e =
    rocksdb::kNoCompression;

DEFINE_int64(sample_for_compression, 0, "Sample every N block for compression");

DEFINE_int32(compression_level, rocksdb::CompressionOptions().level,
             "Compression level. The meaning of this value is library-"
             "dependent. If unset, we try to use the default for the library "
             "specified in `--compression_type`");

DEFINE_int32(compression_max_dict_bytes,
             rocksdb::CompressionOptions().max_dict_bytes,
             "Maximum size of dictionary used to prime the compression "
             "library.");

DEFINE_int32(compression_zstd_max_train_bytes,
             rocksdb::CompressionOptions().zstd_max_train_bytes,
             "Maximum size of training data passed to zstd's dictionary "
             "trainer.");

DEFINE_int32(min_level_to_compress, -1,
             "If non-negative, compression starts"
             " from this level. Levels with number < min_level_to_compress are"
             " not compressed. Otherwise, apply compression_type to "
             "all levels.");

DEFINE_int32(compression_parallel_threads, 1,
             "Number of threads for parallel compression.");

DEFINE_uint64(compression_max_dict_buffer_bytes,
              rocksdb::CompressionOptions().max_dict_buffer_bytes,
              "Maximum bytes to buffer to collect samples for dictionary.");

DEFINE_bool(compression_use_zstd_dict_trainer,
            rocksdb::CompressionOptions().use_zstd_dict_trainer,
            "If true, use ZSTD_TrainDictionary() to create dictionary, else"
            "use ZSTD_FinalizeDictionary() to create dictionary");

static bool ValidateTableCacheNumshardbits(const char* flagname,
                                           int32_t value) {
  if (0 >= value || value >= 20) {
    fprintf(stderr, "Invalid value for --%s: %d, must be  0 < val < 20\n",
            flagname, value);
    return false;
  }
  return true;
}
DEFINE_int32(table_cache_numshardbits, 4, "");

DEFINE_string(env_uri, "",
              "URI for registry Env lookup. Mutually exclusive with --fs_uri");
DEFINE_string(fs_uri, "",
              "URI for registry Filesystem lookup. Mutually exclusive"
              " with --env_uri."
              " Creates a default environment with the specified filesystem.");
DEFINE_string(simulate_hybrid_fs_file, "",
              "File for Store Metadata for Simulate hybrid FS. Empty means "
              "disable the feature. Now, if it is set, last_level_temperature "
              "is set to kWarm.");
DEFINE_int32(simulate_hybrid_hdd_multipliers, 1,
             "In simulate_hybrid_fs_file or simulate_hdd mode, how many HDDs "
             "are simulated.");
DEFINE_bool(simulate_hdd, false, "Simulate read/write latency on HDD.");

DEFINE_int64(
    preclude_last_level_data_seconds, 0,
    "Preclude the latest data from the last level. (Used for tiered storage)");

DEFINE_int64(preserve_internal_time_seconds, 0,
             "Preserve the internal time information which stores with SST.");

static std::shared_ptr<rocksdb::Env> env_guard;


DEFINE_int64(stats_interval, 0,
             "Stats are reported every N operations when this is greater than "
             "zero. When 0 the interval grows over time.");

DEFINE_int64(stats_interval_seconds, 0,
             "Report stats every N seconds. This overrides stats_interval when"
             " both are > 0.");

DEFINE_int32(stats_per_interval, 0,
             "Reports additional stats per interval when this is greater than "
             "0.");

DEFINE_uint64(slow_usecs, 1000000,
              "A message is printed for operations that take at least this "
              "many microseconds.");

DEFINE_int64(report_interval_seconds, 0,
             "If greater than zero, it will write simple stats in CSV format "
             "to --report_file every N seconds");

DEFINE_string(report_file, "report.csv",
              "Filename where some simple stats are reported to (if "
              "--report_interval_seconds is bigger than 0)");

DEFINE_int32(thread_status_per_interval, 0,
             "Takes and report a snapshot of the current status of each thread"
             " when this is greater than 0.");

DEFINE_int32(perf_level, rocksdb::PerfLevel::kDisable,
             "Level of perf collection");

DEFINE_uint64(soft_pending_compaction_bytes_limit, 64ull * 1024 * 1024 * 1024,
              "Slowdown writes if pending compaction bytes exceed this number");

DEFINE_uint64(hard_pending_compaction_bytes_limit, 128ull * 1024 * 1024 * 1024,
              "Stop writes if pending compaction bytes exceed this number");

DEFINE_uint64(delayed_write_rate, 8388608u,
              "Limited bytes allowed to DB when soft_rate_limit or "
              "level0_slowdown_writes_trigger triggers");

DEFINE_bool(enable_pipelined_write, true,
            "Allow WAL and memtable writes to be pipelined");

DEFINE_bool(
    unordered_write, false,
    "Enable the unordered write feature, which provides higher throughput but "
    "relaxes the guarantees around atomic reads and immutable snapshots");

DEFINE_bool(allow_concurrent_memtable_write, true,
            "Allow multi-writers to update mem tables in parallel.");

DEFINE_double(experimental_mempurge_threshold, 0.0,
              "Maximum useful payload ratio estimate that triggers a mempurge "
              "(memtable garbage collection).");

DEFINE_bool(inplace_update_support,
            rocksdb::Options().inplace_update_support,
            "Support in-place memtable update for smaller or same-size values");

DEFINE_uint64(inplace_update_num_locks,
              rocksdb::Options().inplace_update_num_locks,
              "Number of RW locks to protect in-place memtable updates");

DEFINE_bool(enable_write_thread_adaptive_yield, true,
            "Use a yielding spin loop for brief writer thread waits.");

DEFINE_uint64(
    write_thread_max_yield_usec, 100,
    "Maximum microseconds for enable_write_thread_adaptive_yield operation.");

DEFINE_uint64(write_thread_slow_yield_usec, 3,
              "The threshold at which a slow yield is considered a signal that "
              "other processes or threads want the core.");

DEFINE_uint64(rate_limiter_bytes_per_sec, 0, "Set options.rate_limiter value.");

DEFINE_int64(rate_limiter_refill_period_us, 100 * 1000,
             "Set refill period on rate limiter.");

DEFINE_bool(rate_limiter_auto_tuned, false,
            "Enable dynamic adjustment of rate limit according to demand for "
            "background I/O");

DEFINE_int64(rate_limiter_single_burst_bytes, 0,
             "Set single burst bytes on background I/O rate limiter.");

DEFINE_bool(sine_write_rate, false, "Use a sine wave write_rate_limit");

DEFINE_uint64(
    sine_write_rate_interval_milliseconds, 10000,
    "Interval of which the sine wave write_rate_limit is recalculated");

DEFINE_double(sine_a, 1, "A in f(x) = A sin(bx + c) + d");

DEFINE_double(sine_b, 1, "B in f(x) = A sin(bx + c) + d");

DEFINE_double(sine_c, 0, "C in f(x) = A sin(bx + c) + d");

DEFINE_double(sine_d, 1, "D in f(x) = A sin(bx + c) + d");

DEFINE_bool(rate_limit_bg_reads, false,
            "Use options.rate_limiter on compaction reads");

DEFINE_uint64(
    benchmark_write_rate_limit, 0,
    "If non-zero, db_bench will rate-limit the writes going into RocksDB. This "
    "is the global rate in bytes/second.");

// the parameters of mix_graph
DEFINE_double(keyrange_dist_a, 0.0,
              "The parameter 'a' of prefix average access distribution "
              "f(x)=a*exp(b*x)+c*exp(d*x)");
DEFINE_double(keyrange_dist_b, 0.0,
              "The parameter 'b' of prefix average access distribution "
              "f(x)=a*exp(b*x)+c*exp(d*x)");
DEFINE_double(keyrange_dist_c, 0.0,
              "The parameter 'c' of prefix average access distribution"
              "f(x)=a*exp(b*x)+c*exp(d*x)");
DEFINE_double(keyrange_dist_d, 0.0,
              "The parameter 'd' of prefix average access distribution"
              "f(x)=a*exp(b*x)+c*exp(d*x)");
DEFINE_int64(keyrange_num, 1,
             "The number of key ranges that are in the same prefix "
             "group, each prefix range will have its key access distribution");
DEFINE_double(key_dist_a, 0.0,
              "The parameter 'a' of key access distribution model f(x)=a*x^b");
DEFINE_double(key_dist_b, 0.0,
              "The parameter 'b' of key access distribution model f(x)=a*x^b");

              
DEFINE_double(value_theta, 0.0,
              "The parameter 'theta' of Generized Pareto Distribution "
              "f(x)=(1/sigma)*(1+k*(x-theta)/sigma)^-(1/k+1)");
// Use reasonable defaults based on the mixgraph paper
DEFINE_double(value_k, 0.2615,
              "The parameter 'k' of Generized Pareto Distribution "
              "f(x)=(1/sigma)*(1+k*(x-theta)/sigma)^-(1/k+1)");
// Use reasonable defaults based on the mixgraph paper
DEFINE_double(value_sigma, 25.45,
              "The parameter 'theta' of Generized Pareto Distribution "
              "f(x)=(1/sigma)*(1+k*(x-theta)/sigma)^-(1/k+1)");


DEFINE_double(iter_theta, 0.0,
              "The parameter 'theta' of Generized Pareto Distribution "
              "f(x)=(1/sigma)*(1+k*(x-theta)/sigma)^-(1/k+1)");
// Use reasonable defaults based on the mixgraph paper
DEFINE_double(iter_k, 2.517,
              "The parameter 'k' of Generized Pareto Distribution "
              "f(x)=(1/sigma)*(1+k*(x-theta)/sigma)^-(1/k+1)");
// Use reasonable defaults based on the mixgraph paper
DEFINE_double(iter_sigma, 14.236,
              "The parameter 'sigma' of Generized Pareto Distribution "
              "f(x)=(1/sigma)*(1+k*(x-theta)/sigma)^-(1/k+1)");


DEFINE_double(mix_get_ratio, 1.0,
              "The ratio of Get queries of mix_graph workload");
DEFINE_double(mix_put_ratio, 0.0,
              "The ratio of Put queries of mix_graph workload");
DEFINE_double(mix_seek_ratio, 0.0,
              "The ratio of Seek queries of mix_graph workload");
DEFINE_int64(mix_max_scan_len, 10000, "The max scan length of Iterator");
DEFINE_int64(mix_max_value_size, 1024, "The max value size of this workload");
DEFINE_double(
    sine_mix_rate_noise, 0.0,
    "Add the noise ratio to the sine rate, it is between 0.0 and 1.0");
DEFINE_bool(sine_mix_rate, false,
            "Enable the sine QPS control on the mix workload");
DEFINE_uint64(
    sine_mix_rate_interval_milliseconds, 10000,
    "Interval of which the sine wave read_rate_limit is recalculated");
DEFINE_int64(mix_accesses, -1,
             "The total query accesses of mix_graph workload");

DEFINE_uint64(
    benchmark_read_rate_limit, 0,
    "If non-zero, db_bench will rate-limit the reads from RocksDB. This "
    "is the global rate in ops/second.");

DEFINE_uint64(max_compaction_bytes,
              rocksdb::Options().max_compaction_bytes,
              "Max bytes allowed in one compaction");

DEFINE_bool(readonly, false, "Run read only benchmarks.");

DEFINE_bool(print_malloc_stats, false,
            "Print malloc stats to stdout after benchmarks finish.");

DEFINE_bool(disable_auto_compactions, false, "Do not auto trigger compactions");

DEFINE_uint64(wal_ttl_seconds, 0, "Set the TTL for the WAL Files in seconds.");
DEFINE_uint64(wal_size_limit_MB, 0,
              "Set the size limit for the WAL Files in MB.");
DEFINE_uint64(max_total_wal_size, 0, "Set total max WAL size");

DEFINE_bool(mmap_read, rocksdb::Options().allow_mmap_reads,
            "Allow reads to occur via mmap-ing files");

DEFINE_bool(mmap_write, rocksdb::Options().allow_mmap_writes,
            "Allow writes to occur via mmap-ing files");

DEFINE_bool(use_direct_reads, rocksdb::Options().use_direct_reads,
            "Use O_DIRECT for reading data");

DEFINE_bool(use_direct_io_for_flush_and_compaction,
            rocksdb::Options().use_direct_io_for_flush_and_compaction,
            "Use O_DIRECT for background flush and compaction writes");

DEFINE_bool(advise_random_on_open,
            rocksdb::Options().advise_random_on_open,
            "Advise random access on table file open");

DEFINE_bool(use_tailing_iterator, false,
            "Use tailing iterator to access a series of keys instead of get");

DEFINE_bool(use_adaptive_mutex, rocksdb::Options().use_adaptive_mutex,
            "Use adaptive mutex");

DEFINE_uint64(bytes_per_sync, rocksdb::Options().bytes_per_sync,
              "Allows OS to incrementally sync SST files to disk while they are"
              " being written, in the background. Issue one request for every"
              " bytes_per_sync written. 0 turns it off.");

DEFINE_uint64(wal_bytes_per_sync,
              rocksdb::Options().wal_bytes_per_sync,
              "Allows OS to incrementally sync WAL files to disk while they are"
              " being written, in the background. Issue one request for every"
              " wal_bytes_per_sync written. 0 turns it off.");

DEFINE_bool(use_single_deletes, true,
            "Use single deletes (used in RandomReplaceKeys only).");

DEFINE_double(stddev, 2000.0,
              "Standard deviation of normal distribution used for picking keys"
              " (used in RandomReplaceKeys only).");

DEFINE_int32(key_id_range, 100000,
             "Range of possible value of key id (used in TimeSeries only).");

DEFINE_string(expire_style, "none",
              "Style to remove expired time entries. Can be one of the options "
              "below: none (do not expired data), compaction_filter (use a "
              "compaction filter to remove expired data), delete (seek IDs and "
              "remove expired data) (used in TimeSeries only).");

DEFINE_uint64(
    time_range, 100000,
    "Range of timestamp that store in the database (used in TimeSeries"
    " only).");

DEFINE_int32(num_deletion_threads, 1,
             "Number of threads to do deletion (used in TimeSeries and delete "
             "expire_style only).");

DEFINE_int32(max_successive_merges, 0,
             "Maximum number of successive merge operations on a key in the "
             "memtable");

DEFINE_bool(strict_max_successive_merges, false,
            "Whether to issue filesystem reads to keep within "
            "`max_successive_merges` limit");

static bool ValidatePrefixSize(const char* flagname, int32_t value) {
  if (value < 0 || value >= 2000000000) {
    fprintf(stderr, "Invalid value for --%s: %d. 0<= PrefixSize <=2000000000\n",
            flagname, value);
    return false;
  }
  return true;
}

DEFINE_int32(prefix_size, 0,
             "control the prefix size for HashSkipList and plain table");
DEFINE_int64(keys_per_prefix, 0,
             "control average number of keys generated per prefix, 0 means no "
             "special handling of the prefix, i.e. use the prefix comes with "
             "the generated random number.");
DEFINE_bool(total_order_seek, false,
            "Enable total order seek regardless of index format.");
DEFINE_bool(prefix_same_as_start, false,
            "Enforce iterator to return keys with prefix same as seek key.");
DEFINE_bool(
    seek_missing_prefix, false,
    "Iterator seek to keys with non-exist prefixes. Require prefix_size > 8");

DEFINE_int32(memtable_insert_with_hint_prefix_size, 0,
             "If non-zero, enable "
             "memtable insert with hint with the given prefix size.");
DEFINE_bool(enable_io_prio, false,
            "Lower the background flush/compaction threads' IO priority");
DEFINE_bool(enable_cpu_prio, false,
            "Lower the background flush/compaction threads' CPU priority");
DEFINE_bool(identity_as_first_hash, false,
            "the first hash function of cuckoo table becomes an identity "
            "function. This is only valid when key is 8 bytes");
DEFINE_bool(dump_malloc_stats, true, "Dump malloc stats in LOG ");
DEFINE_uint64(stats_dump_period_sec,
              rocksdb::Options().stats_dump_period_sec,
              "Gap between printing stats to log in seconds");
DEFINE_uint64(stats_persist_period_sec,
              rocksdb::Options().stats_persist_period_sec,
              "Gap between persisting stats in seconds");
DEFINE_bool(persist_stats_to_disk,
            rocksdb::Options().persist_stats_to_disk,
            "whether to persist stats to disk");
DEFINE_uint64(stats_history_buffer_size,
              rocksdb::Options().stats_history_buffer_size,
              "Max number of stats snapshots to keep in memory");
DEFINE_bool(avoid_flush_during_recovery,
            rocksdb::Options().avoid_flush_during_recovery,
            "If true, avoids flushing the recovered WAL data where possible.");
DEFINE_int64(multiread_stride, 0,
             "Stride length for the keys in a MultiGet batch");
DEFINE_bool(multiread_batched, false, "Use the new MultiGet API");

DEFINE_string(memtablerep, "skip_list", "");
DEFINE_int64(hash_bucket_count, 1024 * 1024, "hash bucket count");
DEFINE_bool(use_plain_table, false,
            "if use plain table instead of block-based table format");
DEFINE_bool(use_cuckoo_table, false, "if use cuckoo table format");
DEFINE_double(cuckoo_hash_ratio, 0.9, "Hash ratio for Cuckoo SST table.");
DEFINE_bool(use_hash_search, false,
            "if use kHashSearch instead of kBinarySearch. "
            "This is valid if only we use BlockTable");
DEFINE_string(merge_operator, "",
              "The merge operator to use with the database."
              "If a new merge operator is specified, be sure to use fresh"
              " database The possible merge operators are defined in"
              " utilities/merge_operators.h");
DEFINE_int32(skip_list_lookahead, 0,
             "Used with skip_list memtablerep; try linear search first for "
             "this many steps from the previous position");
DEFINE_bool(report_file_operations, false,
            "if report number of file operations");
DEFINE_bool(report_open_timing, false, "if report open timing");
DEFINE_int32(readahead_size, 0, "Iterator readahead size");

DEFINE_bool(read_with_latest_user_timestamp, true,
            "If true, always use the current latest timestamp for read. If "
            "false, choose a random timestamp from the past.");

DEFINE_string(secondary_cache_uri, "",
              "Full URI for creating a custom secondary cache object");
static class std::shared_ptr<rocksdb::SecondaryCache> secondary_cache;

static const bool FLAGS_prefix_size_dummy __attribute__((__unused__)) =
    RegisterFlagValidator(&FLAGS_prefix_size, &ValidatePrefixSize);

static const bool FLAGS_key_size_dummy __attribute__((__unused__)) =
    RegisterFlagValidator(&FLAGS_key_size, &ValidateKeySize);

static const bool FLAGS_cache_numshardbits_dummy __attribute__((__unused__)) =
    RegisterFlagValidator(&FLAGS_cache_numshardbits,
                          &ValidateCacheNumshardbits);

static const bool FLAGS_readwritepercent_dummy __attribute__((__unused__)) =
    RegisterFlagValidator(&FLAGS_readwritepercent, &ValidateInt32Percent);

DEFINE_int32(disable_seek_compaction, false,
             "Not used, left here for backwards compatibility");

DEFINE_bool(allow_data_in_errors,
            rocksdb::Options().allow_data_in_errors,
            "If true, allow logging data, e.g. key, value in LOG files.");

static const bool FLAGS_deletepercent_dummy __attribute__((__unused__)) =
    RegisterFlagValidator(&FLAGS_deletepercent, &ValidateInt32Percent);
static const bool FLAGS_table_cache_numshardbits_dummy
    __attribute__((__unused__)) = RegisterFlagValidator(
        &FLAGS_table_cache_numshardbits, &ValidateTableCacheNumshardbits);

DEFINE_uint32(write_batch_protection_bytes_per_key, 0,
              "Size of per-key-value checksum in each write batch. Currently "
              "only value 0 and 8 are supported.");

DEFINE_uint32(
    memtable_protection_bytes_per_key, 0,
    "Enable memtable per key-value checksum protection. "
    "Each entry in memtable will be suffixed by a per key-value checksum. "
    "This options determines the size of such checksums. "
    "Supported values: 0, 1, 2, 4, 8.");

DEFINE_uint32(block_protection_bytes_per_key, 0,
              "Enable block per key-value checksum protection. "
              "Supported values: 0, 1, 2, 4, 8.");

DEFINE_bool(build_info, false,
            "Print the build info via GetRocksBuildInfoAsString");

DEFINE_bool(track_and_verify_wals_in_manifest, false,
            "If true, enable WAL tracking in the MANIFEST");

static std::optional<int64_t> seed_base;

static unsigned int value_size = 128;
static rocksdb::Env* FLAGS_env = rocksdb::Env::Default();
enum DistributionType : unsigned char { kFixed = 0, kUniform, kNormal };
static enum DistributionType FLAGS_value_size_distribution_type_e = kFixed;

static enum DistributionType StringToDistributionType(const char* ctype) {
  assert(ctype);

  if (!strcasecmp(ctype, "fixed")) {
    return kFixed;
  } else if (!strcasecmp(ctype, "uniform")) {
    return kUniform;
  } else if (!strcasecmp(ctype, "normal")) {
    return kNormal;
  }

  fprintf(stdout, "Cannot parse distribution type '%s'\n", ctype);
  exit(1);
}

class BaseDistribution {
 public:
  BaseDistribution(unsigned int _min, unsigned int _max)
      : min_value_size_(_min), max_value_size_(_max) {}
  virtual ~BaseDistribution() = default;

  unsigned int Generate() {
    auto val = Get();
    if (NeedTruncate()) {
      val = std::max(min_value_size_, val);
      val = std::min(max_value_size_, val);
    }
    return val;
  }

 private:
  virtual unsigned int Get() = 0;
  virtual bool NeedTruncate() { return true; }
  unsigned int min_value_size_;
  unsigned int max_value_size_;
};

class FixedDistribution : public BaseDistribution {
 public:
  FixedDistribution(unsigned int size)
      : BaseDistribution(size, size), size_(size) {}

 private:
  unsigned int Get() override { return size_; }
  bool NeedTruncate() override { return false; }
  unsigned int size_;
};

class NormalDistribution : public BaseDistribution,
                           public std::normal_distribution<double> {
 public:
  NormalDistribution(unsigned int _min, unsigned int _max)
      : BaseDistribution(_min, _max),
        // 99.7% values within the range [min, max].
        std::normal_distribution<double>(
            (double)(_min + _max) / 2.0 /*mean*/,
            (double)(_max - _min) / 6.0 /*stddev*/),
        gen_(rd_()) {}

 private:
  unsigned int Get() override {
    return static_cast<unsigned int>((*this)(gen_));
  }
  std::random_device rd_;
  std::mt19937 gen_;
};

class UniformDistribution : public BaseDistribution,
                            public std::uniform_int_distribution<unsigned int> {
 public:
  UniformDistribution(unsigned int _min, unsigned int _max)
      : BaseDistribution(_min, _max),
        std::uniform_int_distribution<unsigned int>(_min, _max),
        gen_(rd_()) {}

 private:
  unsigned int Get() override { return (*this)(gen_); }
  bool NeedTruncate() override { return false; }
  std::random_device rd_;
  std::mt19937 gen_;
};

// Helper for quickly generating random data.
class RandomGenerator {
 private:
  std::string data_;
  unsigned int pos_;
  std::unique_ptr<BaseDistribution> dist_;

 public:
  RandomGenerator() {
    auto max_value_size = FLAGS_value_size_max;
    switch (FLAGS_value_size_distribution_type_e) {
      case kUniform:
        dist_.reset(new UniformDistribution(FLAGS_value_size_min,
                                            FLAGS_value_size_max));
        break;
      case kNormal:
        dist_.reset(
            new NormalDistribution(FLAGS_value_size_min, FLAGS_value_size_max));
        break;
      case kFixed:
      default:
        dist_.reset(new FixedDistribution(value_size));
        max_value_size = value_size;
    }
    // We use a limited amount of data over and over again and ensure
    // that it is larger than the compression window (32KB), and also
    // large enough to serve all typical value sizes we want to write.
    Random rnd(301);
    std::string piece;
    while (data_.size() < (unsigned)std::max(1048576, max_value_size)) {
      // Add a short fragment that is as compressible as specified
      // by FLAGS_compression_ratio.
      CompressibleString(&rnd, FLAGS_compression_ratio, 100, &piece);
      data_.append(piece);
    }
    pos_ = 0;
  }

  rocksdb::Slice CompressibleString(Random* rnd, double compressed_fraction,
                                int len, std::string* dst) {
    int raw = static_cast<int>(len * compressed_fraction);
    if (raw < 1) raw = 1;
    std::string raw_data = rnd->RandomBinaryString(raw);

    // Duplicate the random data until we have filled "len" bytes
    dst->clear();
    while (dst->size() < (unsigned int)len) {
      dst->append(raw_data);
    }
    dst->resize(len);
    return rocksdb::Slice(*dst);
  }

  rocksdb::Slice Generate(unsigned int len) {
    assert(len <= data_.size());
    if (pos_ + len > data_.size()) {
      pos_ = 0;
    }
    pos_ += len;
    return rocksdb::Slice(data_.data() + pos_ - len, len);
  }

  rocksdb::Slice Generate() {
    auto len = dist_->Generate();
    return Generate(len);
  }
};

static void AppendWithSpace(std::string* str, rocksdb::Slice msg) {
  if (msg.empty()) {
    return;
  }
  if (!str->empty()) {
    str->push_back(' ');
  }
  str->append(msg.data(), msg.size());
}

struct DBWithColumnFamilies {
  std::vector<rocksdb::ColumnFamilyHandle*> cfh;
  rocksdb::DB* db;
  rocksdb::OptimisticTransactionDB* opt_txn_db;
  std::atomic<size_t> num_created;  // Need to be updated after all the
                                    // new entries in cfh are set.
  size_t num_hot;  // Number of column families to be queried at each moment.
                   // After each CreateNewCf(), another num_hot number of new
                   // Column families will be created and used to be queried.
  port::Mutex create_cf_mutex;  // Only one thread can execute CreateNewCf()
  std::vector<int> cfh_idx_to_prob;  // ith index holds probability of operating
                                     // on cfh[i].

  DBWithColumnFamilies()
      : db(nullptr)
        ,
        opt_txn_db(nullptr)
  {
    cfh.clear();
    num_created = 0;
    num_hot = 0;
  }

  DBWithColumnFamilies(const DBWithColumnFamilies& other)
      : cfh(other.cfh),
        db(other.db),
        opt_txn_db(other.opt_txn_db),
        num_created(other.num_created.load()),
        num_hot(other.num_hot),
        cfh_idx_to_prob(other.cfh_idx_to_prob) {
  }

  void DeleteDBs() {
    std::for_each(cfh.begin(), cfh.end(),
                  [](rocksdb::ColumnFamilyHandle* cfhi) { delete cfhi; });
    cfh.clear();
    if (opt_txn_db) {
      delete opt_txn_db;
      opt_txn_db = nullptr;
    } else {
      delete db;
      db = nullptr;
    }
  }

  rocksdb::ColumnFamilyHandle* GetCfh(int64_t rand_num) {
    assert(num_hot > 0);
    size_t rand_offset = 0;
    if (!cfh_idx_to_prob.empty()) {
      assert(cfh_idx_to_prob.size() == num_hot);
      int sum = 0;
      while (sum + cfh_idx_to_prob[rand_offset] < rand_num % 100) {
        sum += cfh_idx_to_prob[rand_offset];
        ++rand_offset;
      }
      assert(rand_offset < cfh_idx_to_prob.size());
    } else {
      rand_offset = rand_num % num_hot;
    }
    return cfh[num_created.load(std::memory_order_acquire) - num_hot +
               rand_offset];
  }

  // stage: assume CF from 0 to stage * num_hot has be created. Need to create
  //        stage * num_hot + 1 to stage * (num_hot + 1).
  void CreateNewCf(rocksdb::ColumnFamilyOptions options, int64_t stage) {
    MutexLock l(&create_cf_mutex);
    if ((stage + 1) * num_hot <= num_created) {
      // Already created.
      return;
    }
    auto new_num_created = num_created + num_hot;
    assert(new_num_created <= cfh.size());
    for (size_t i = num_created; i < new_num_created; i++) {
      rocksdb::Status s =
          db->CreateColumnFamily(options, ColumnFamilyName(i), &(cfh[i]));
      if (!s.ok()) {
        fprintf(stderr, "create column family error: %s\n",
                s.ToString().c_str());
        abort();
      }
    }
    num_created.store(new_num_created, std::memory_order_release);
  }
};

// A class that reports stats to CSV file.
class ReporterAgent {
 public:
  ReporterAgent(rocksdb::Env* env, const std::string& fname,
                uint64_t report_interval_secs)
      : env_(env),
        total_ops_done_(0),
        last_report_(0),
        report_interval_secs_(report_interval_secs),
        stop_(false) {
    auto s = env_->NewWritableFile(fname, &report_file_, rocksdb::EnvOptions());
    if (s.ok()) {
      s = report_file_->Append(Header() + "\n");
    }
    if (s.ok()) {
      s = report_file_->Flush();
    }
    if (!s.ok()) {
      fprintf(stderr, "Can't open %s: %s\n", fname.c_str(),
              s.ToString().c_str());
      abort();
    }

    reporting_thread_ = port::Thread([&]() { SleepAndReport(); });
  }

  ~ReporterAgent() {
    {
      std::unique_lock<std::mutex> lk(mutex_);
      stop_ = true;
      stop_cv_.notify_all();
    }
    reporting_thread_.join();
  }

  // thread safe
  void ReportFinishedOps(int64_t num_ops) {
    total_ops_done_.fetch_add(num_ops);
  }

 private:
  std::string Header() const { return "secs_elapsed,interval_qps"; }
  void SleepAndReport() {
    auto* clock = env_->GetSystemClock().get();
    auto time_started = clock->NowMicros();
    while (true) {
      {
        std::unique_lock<std::mutex> lk(mutex_);
        if (stop_ ||
            stop_cv_.wait_for(lk, std::chrono::seconds(report_interval_secs_),
                              [&]() { return stop_; })) {
          // stopping
          break;
        }
        // else -> timeout, which means time for a report!
      }
      auto total_ops_done_snapshot = total_ops_done_.load();
      // round the seconds elapsed
      auto secs_elapsed =
          (clock->NowMicros() - time_started + kMicrosInSecond / 2) /
          kMicrosInSecond;
      std::string report =
          std::to_string(secs_elapsed) + "," +
          std::to_string(total_ops_done_snapshot - last_report_) + "\n";
      auto s = report_file_->Append(report);
      if (s.ok()) {
        s = report_file_->Flush();
      }
      if (!s.ok()) {
        fprintf(stderr,
                "Can't write to report file (%s), stopping the reporting\n",
                s.ToString().c_str());
        break;
      }
      last_report_ = total_ops_done_snapshot;
    }
  }

  rocksdb::Env* env_;
  std::unique_ptr<rocksdb::WritableFile> report_file_;
  std::atomic<int64_t> total_ops_done_;
  int64_t last_report_;
  const uint64_t report_interval_secs_;
  port::Thread reporting_thread_;
  std::mutex mutex_;
  // will notify on stop
  std::condition_variable stop_cv_;
  bool stop_;
};

enum OperationType : unsigned char {
  kRead = 0,
  kWrite,
  kDelete,
  kSeek,
  kMerge,
  kUpdate,
  kCompress,
  kUncompress,
  kCrc,
  kHash,
  kOthers
};

static std::unordered_map<OperationType, std::string, std::hash<unsigned char>>
    OperationTypeString = {{kRead, "read"},         {kWrite, "write"},
                           {kDelete, "delete"},     {kSeek, "seek"},
                           {kMerge, "merge"},       {kUpdate, "update"},
                           {kCompress, "compress"}, {kCompress, "uncompress"},
                           {kCrc, "crc"},           {kHash, "hash"},
                           {kOthers, "op"}};

static rocksdb::Status CreateMemTableRepFactory(
    const rocksdb::ConfigOptions& config_options,
    std::shared_ptr<rocksdb::MemTableRepFactory>* factory) {
  rocksdb::Status s;
  if (!strcasecmp(FLAGS_memtablerep.c_str(), rocksdb::SkipListFactory::kNickName())) {
    factory->reset(new rocksdb::SkipListFactory(FLAGS_skip_list_lookahead));
  } else if (!strcasecmp(FLAGS_memtablerep.c_str(), "prefix_hash")) {
    factory->reset(rocksdb::NewHashSkipListRepFactory(FLAGS_hash_bucket_count));
  } else if (!strcasecmp(FLAGS_memtablerep.c_str(),
                         rocksdb::VectorRepFactory::kNickName())) {
    factory->reset(new rocksdb::VectorRepFactory());
  } else if (!strcasecmp(FLAGS_memtablerep.c_str(), "hash_linkedlist")) {
    factory->reset(rocksdb::NewHashLinkListRepFactory(FLAGS_hash_bucket_count));
  } else {
    std::unique_ptr<rocksdb::MemTableRepFactory> unique;
    s = rocksdb::MemTableRepFactory::CreateFromString(config_options, FLAGS_memtablerep,
                                             &unique);
    if (s.ok()) {
      factory->reset(unique.release());
    }
  }
  return s;
}

class CombinedStats;

class Stats {
 private:
  rocksdb::SystemClock* clock_;
  int id_;
  uint64_t start_ = 0;
  uint64_t sine_interval_;
  uint64_t finish_;
  double seconds_;
  uint64_t done_;
  uint64_t last_report_done_;
  uint64_t next_report_;
  uint64_t bytes_;
  int call_ref;
  uint64_t last_op_finish_;
  uint64_t last_report_finish_;
  std::unordered_map<OperationType, std::shared_ptr<HistogramImpl>,
                     std::hash<unsigned char>>
      hist_;
  std::string message_;
  bool exclude_from_merge_;
  ReporterAgent* reporter_agent_;  // does not own
  friend class CombinedStats;

 public:
  Stats() : clock_(FLAGS_env->GetSystemClock().get()) { Start(-1); }

  void SetReporterAgent(ReporterAgent* reporter_agent) {
    reporter_agent_ = reporter_agent;
  }

  void Start(int id) {
    id_ = id;
    next_report_ = FLAGS_stats_interval ? FLAGS_stats_interval : 100;
    last_op_finish_ = start_;
    hist_.clear();
    done_ = 0;
    last_report_done_ = 0;
    bytes_ = 0;
    seconds_ = 0;
    call_ref = 0;
    start_ = clock_->NowMicros();
    sine_interval_ = clock_->NowMicros();
    finish_ = start_;
    last_report_finish_ = start_;
    message_.clear();
    // When set, stats from this thread won't be merged with others.
    exclude_from_merge_ = false;
  }

  void Merge(const Stats& other) {
    if (other.exclude_from_merge_) {
      return;
    }

    for (auto it = other.hist_.begin(); it != other.hist_.end(); ++it) {
      auto this_it = hist_.find(it->first);
      if (this_it != hist_.end()) {
        this_it->second->Merge(*(other.hist_.at(it->first)));
      } else {
        hist_.insert({it->first, it->second});
      }
    }

    done_ += other.done_;
    bytes_ += other.bytes_;
    seconds_ += other.seconds_;
    if (other.start_ < start_) {
      start_ = other.start_;
    }
    if (other.finish_ > finish_) {
      finish_ = other.finish_;
    }

    // Just keep the messages from one thread.
    if (message_.empty()) {
      message_ = other.message_;
    }
  }

  void Stop() {
    finish_ = clock_->NowMicros();
    seconds_ = (finish_ - start_) * 1e-6;
  }

  void AddMessage(rocksdb::Slice msg) { AppendWithSpace(&message_, msg); }

  void SetId(int id) { id_ = id; }
  void SetExcludeFromMerge() { exclude_from_merge_ = true; }

  void PrintThreadStatus() {
    std::vector<rocksdb::ThreadStatus> thread_list;
    FLAGS_env->GetThreadList(&thread_list);

    fprintf(stderr, "\n%18s %10s %12s %20s %13s %45s %12s %s\n", "ThreadID",
            "ThreadType", "cfName", "Operation", "ElapsedTime", "Stage",
            "State", "OperationProperties");

    int64_t current_time = 0;
    clock_->GetCurrentTime(&current_time).PermitUncheckedError();
    for (auto ts : thread_list) {
      fprintf(stderr, "%18" PRIu64 " %10s %12s %20s %13s %45s %12s",
              ts.thread_id,
              rocksdb::ThreadStatus::GetThreadTypeName(ts.thread_type).c_str(),
              ts.cf_name.c_str(),
              rocksdb::ThreadStatus::GetOperationName(ts.operation_type).c_str(),
              rocksdb::ThreadStatus::MicrosToString(ts.op_elapsed_micros).c_str(),
              rocksdb::ThreadStatus::GetOperationStageName(ts.operation_stage).c_str(),
              rocksdb::ThreadStatus::GetStateName(ts.state_type).c_str());

      auto op_properties = rocksdb::ThreadStatus::InterpretOperationProperties(
          ts.operation_type, ts.op_properties);
      for (const auto& op_prop : op_properties) {
        fprintf(stderr, " %s %" PRIu64 " |", op_prop.first.c_str(),
                op_prop.second);
      }
      fprintf(stderr, "\n");
    }
  }

  void ResetSineInterval() { sine_interval_ = clock_->NowMicros(); }

  uint64_t GetSineInterval() { return sine_interval_; }

  uint64_t GetStart() { return start_; }

  void ResetLastOpTime() {
    // Set to now to avoid latency from calls to SleepForMicroseconds.
    last_op_finish_ = clock_->NowMicros();
  }

  std::string getCurrentTime() {
    char timeStr[100];
    time_t now = time(NULL);
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(timeStr);
  } 

  std::string format_memory(unsigned long bytes) {
    const double GIGABYTE = 1024.0 * 1024.0 * 1024.0;
    const double MEGABYTE = 1024.0 * 1024.0;
    std::ostringstream oss;

    if (bytes >= GIGABYTE) {
        oss << std::fixed << std::setprecision(2) << (bytes / GIGABYTE) << " GB";
    } else {
        oss << std::fixed << std::setprecision(2) << (bytes / MEGABYTE) << " MB";
    }

    return oss.str();
  }
    
  void print_mem_usage() {

    std::ofstream log_file;
    if (call_ref == 0) {
      // 如果call_ref为0，清空文件并从头开始写
      log_file.open(FLAGS_mem_log_file, std::ios::trunc);
    } else {
      // 如果call_ref不为0，以追加模式打开文件
      log_file.open(FLAGS_mem_log_file, std::ios::app);
    }

    if (!log_file.is_open()) {
      fprintf(stderr, "Failed to open the mem_log file: %s.\n",FLAGS_mem_log_file.c_str());  
      return ;
    }
    
    std::ifstream statm("/proc/self/statm");
    if (!statm) {
      fprintf(stderr, "Failed to open /proc/self/statm.\n");
      return;
    }
    unsigned long size, resident, shr_size;
    if (!(statm >> size >> resident >> shr_size)) {
      fprintf(stderr, "Failed to read memory usage from /proc/self/statm.\n");
      return;
    }
    long page_size = sysconf(_SC_PAGESIZE); // 页面大小，字节

    // 使用ostream的插入操作符来写入信息
    log_file << getCurrentTime() << " ... thread " << id_ << ": (" << (done_ - last_report_done_) << "," << done_ << ") ops have been finished!\n";
    log_file << "virtual memory used: " << format_memory(size * page_size) << ", "
             << "Resident set size: " << format_memory(resident * page_size) << ", "
             << "Shared Memory Usage: " << format_memory(shr_size * page_size) << " \n\n";    
    
    call_ref++;
  }

  void printf_mem(rocksdb::DB* db){
    std::string block_cache_usage_str;
    if (db->GetProperty("rocksdb.block-cache-usage", &block_cache_usage_str)) {
      fprintf(stderr, "rocksdb.block-cache-usage COUNT : %s\n", block_cache_usage_str.c_str());
    } else {
      fprintf(stderr, "Failed to get rocksdb.block-cache-usage\n");
    }

    std::string block_cache_pinned_usage_str;
    if (db->GetProperty("rocksdb.block-cache-pinned-usage", &block_cache_pinned_usage_str)) {
      fprintf(stderr, "rocksdb.block-cache-pinned-usage COUNT : %s\n", block_cache_pinned_usage_str.c_str());
    } else {
      fprintf(stderr, "Failed to get rocksdb.block-cache-pinned-usage\n");
    }

    // --- 获取 TableReader 内存使用量估值 ---
    std::string estimate_table_readers_mem_str;
    if (db->GetProperty("rocksdb.estimate-table-readers-mem", &estimate_table_readers_mem_str)) {
      fprintf(stderr, "rocksdb.estimate-table-readers-mem COUNT : %s\n", estimate_table_readers_mem_str.c_str());
    } else {
      fprintf(stderr, "Failed to get rocksdb.estimate-table-readers-mem\n");
    }
  }


  void FinishedOps(DBWithColumnFamilies* db_with_cfh, rocksdb::DB* db, int64_t num_ops,
                   enum OperationType op_type = kOthers) {
    if (reporter_agent_) {
      reporter_agent_->ReportFinishedOps(num_ops);
    }
    if (FLAGS_histogram) {
      uint64_t now = clock_->NowMicros();
      uint64_t micros = now - last_op_finish_;

      if (hist_.find(op_type) == hist_.end()) {
        auto hist_temp = std::make_shared<HistogramImpl>();
        hist_.insert({op_type, std::move(hist_temp)});
      }
      hist_[op_type]->Add(micros);

      if (micros >= FLAGS_slow_usecs && !FLAGS_stats_interval) {
        fprintf(stderr, "long op: %" PRIu64 " micros%30s\r", micros, "");
        fflush(stderr);
      }
      last_op_finish_ = now;
    }

    done_ += num_ops;
    if (done_ >= next_report_ && FLAGS_progress_reports) {
      if (!FLAGS_stats_interval) {
        if (next_report_ < 1000) {
          next_report_ += 100;
        } else if (next_report_ < 5000) {
          next_report_ += 500;
        } else if (next_report_ < 10000) {
          next_report_ += 1000;
        } else if (next_report_ < 50000) {
          next_report_ += 5000;
        } else if (next_report_ < 100000) {
          next_report_ += 10000;
        } else if (next_report_ < 500000) {
          next_report_ += 50000;
        } else {
          next_report_ += 100000;
        }
        fprintf(stderr, "... finished %" PRIu64 " ops%30s\r", done_, "");
      } else {
        uint64_t now = clock_->NowMicros();
        int64_t usecs_since_last = now - last_report_finish_;

        // Determine whether to print status where interval is either
        // each N operations or each N seconds.

        if (FLAGS_stats_interval_seconds &&
            usecs_since_last < (FLAGS_stats_interval_seconds * 1000000)) {
          // Don't check again for this many operations.
          next_report_ += FLAGS_stats_interval;

        } else {
          fprintf(stderr,
                  "%s ... thread %d: (%" PRIu64 ",%" PRIu64
                  ") ops and "
                  "(%.1f,%.1f) ops/second in (%.6f,%.6f) seconds\n",
                  clock_->TimeToString(now / 1000000).c_str(), id_,
                  done_ - last_report_done_, done_,
                  (done_ - last_report_done_) / (usecs_since_last / 1000000.0),
                  done_ / ((now - start_) / 1000000.0),
                  (now - last_report_finish_) / 1000000.0,
                  (now - start_) / 1000000.0);
          print_mem_usage();

          std::string block_cache_usage_str;
          if (db->GetProperty("rocksdb.block-cache-usage", &block_cache_usage_str)) {
            fprintf(stderr, "rocksdb.block-cache-usage COUNT : %s\n", block_cache_usage_str.c_str());
          } else {
            fprintf(stderr, "Failed to get rocksdb.block-cache-usage\n");
          }

          std::string block_cache_pinned_usage_str;
          if (db->GetProperty("rocksdb.block-cache-pinned-usage", &block_cache_pinned_usage_str)) {
            fprintf(stderr, "rocksdb.block-cache-pinned-usage COUNT : %s\n", block_cache_pinned_usage_str.c_str());
          } else {
            fprintf(stderr, "Failed to get rocksdb.block-cache-pinned-usage\n");
          }

          // --- 获取 TableReader 内存使用量估值 ---
          std::string estimate_table_readers_mem_str;
          if (db->GetProperty("rocksdb.estimate-table-readers-mem", &estimate_table_readers_mem_str)) {
            fprintf(stderr, "rocksdb.estimate-table-readers-mem COUNT : %s\n", estimate_table_readers_mem_str.c_str());
          } else {
            fprintf(stderr, "Failed to get rocksdb.estimate-table-readers-mem\n");
          }

          if (id_ == 0 && FLAGS_stats_per_interval) {
            std::string stats;

            if (db_with_cfh && db_with_cfh->num_created.load()) {
              for (size_t i = 0; i < db_with_cfh->num_created.load(); ++i) {
                if (db->GetProperty(db_with_cfh->cfh[i], "rocksdb.cfstats",
                                    &stats)) {
                  fprintf(stderr, "%s\n", stats.c_str());
                }
                if (FLAGS_show_table_properties) {
                  for (int level = 0; level < FLAGS_num_levels; ++level) {
                    if (db->GetProperty(
                            db_with_cfh->cfh[i],
                            "rocksdb.aggregated-table-properties-at-level" +
                                std::to_string(level),
                            &stats)) {
                      if (stats.find("# entries=0") == std::string::npos) {
                        fprintf(stderr, "Level[%d]: %s\n", level,
                                stats.c_str());
                      }
                    }
                  }
                }
              }
            } else if (db) {
              if (db->GetProperty("rocksdb.stats", &stats)) {
                fprintf(stderr, "%s", stats.c_str());
              }
              if (db->GetProperty("rocksdb.num-running-compactions", &stats)) {
                fprintf(stderr, "num-running-compactions: %s\n", stats.c_str());
              }
              if (db->GetProperty("rocksdb.num-running-flushes", &stats)) {
                fprintf(stderr, "num-running-flushes: %s\n\n", stats.c_str());
              }
              if (FLAGS_show_table_properties) {
                for (int level = 0; level < FLAGS_num_levels; ++level) {
                  if (db->GetProperty(
                          "rocksdb.aggregated-table-properties-at-level" +
                              std::to_string(level),
                          &stats)) {
                    if (stats.find("# entries=0") == std::string::npos) {
                      fprintf(stderr, "Level[%d]: %s\n", level, stats.c_str());
                    }
                  }
                }
              }
            }
          }

          next_report_ += FLAGS_stats_interval;
          last_report_finish_ = now;
          last_report_done_ = done_;
        }
      }
      if (id_ == 0 && FLAGS_thread_status_per_interval) {
        PrintThreadStatus();
      }
      fflush(stderr);
    }
  }

  void AddBytes(int64_t n) { bytes_ += n; }

  void Report(const rocksdb::Slice& name) {
    // Pretend at least one op was done in case we are running a benchmark
    // that does not call FinishedOps().
    if (done_ < 1) {
      done_ = 1;
    }

    std::string extra;
    double elapsed = (finish_ - start_) * 1e-6;
    if (bytes_ > 0) {
      // Rate is computed on actual elapsed time, not the sum of per-thread
      // elapsed times.
      char rate[100];
      snprintf(rate, sizeof(rate), "%6.1f MB/s",
               (bytes_ / 1048576.0) / elapsed);
      extra = rate;
    }
    AppendWithSpace(&extra, message_);
    double throughput = (double)done_ / elapsed;

    fprintf(stdout,
            "%-12s : %11.3f micros/op %ld ops/sec %.3f seconds %" PRIu64
            " operations;%s%s\n",
            name.ToString().c_str(), seconds_ * 1e6 / done_, (long)throughput,
            elapsed, done_, (extra.empty() ? "" : " "), extra.c_str());

    if (FLAGS_histogram) {
      for (auto it = hist_.begin(); it != hist_.end(); ++it) {
        fprintf(stdout, "Microseconds per %s:\n%s\n",
                OperationTypeString[it->first].c_str(),
                it->second->ToString().c_str());
      }
    }

    fflush(stdout);
  }
};

class CombinedStats {
 public:
  void AddStats(const Stats& stat) {
    uint64_t total_ops = stat.done_;
    uint64_t total_bytes_ = stat.bytes_;
    double elapsed;

    if (total_ops < 1) {
      total_ops = 1;
    }

    elapsed = (stat.finish_ - stat.start_) * 1e-6;
    throughput_ops_.emplace_back(total_ops / elapsed);

    if (total_bytes_ > 0) {
      double mbs = (total_bytes_ / 1048576.0);
      throughput_mbs_.emplace_back(mbs / elapsed);
    }
  }

  void Report(const std::string& bench_name) {
    if (throughput_ops_.size() < 2) {
      // skip if there are not enough samples
      return;
    }

    const char* name = bench_name.c_str();
    int num_runs = static_cast<int>(throughput_ops_.size());

    if (throughput_mbs_.size() == throughput_ops_.size()) {
      fprintf(stdout,
              "%s [AVG %d runs] : %d (\xC2\xB1 %d) ops/sec; %6.1f (\xC2\xB1 "
              "%.1f) MB/sec\n",
              name, num_runs, static_cast<int>(CalcAvg(throughput_ops_)),
              static_cast<int>(CalcConfidence95(throughput_ops_)),
              CalcAvg(throughput_mbs_), CalcConfidence95(throughput_mbs_));
    } else {
      fprintf(stdout, "%s [AVG %d runs] : %d (\xC2\xB1 %d) ops/sec\n", name,
              num_runs, static_cast<int>(CalcAvg(throughput_ops_)),
              static_cast<int>(CalcConfidence95(throughput_ops_)));
    }
  }

  void ReportWithConfidenceIntervals(const std::string& bench_name) {
    if (throughput_ops_.size() < 2) {
      // skip if there are not enough samples
      return;
    }

    const char* name = bench_name.c_str();
    int num_runs = static_cast<int>(throughput_ops_.size());

    int ops_avg = static_cast<int>(CalcAvg(throughput_ops_));
    int ops_confidence_95 = static_cast<int>(CalcConfidence95(throughput_ops_));

    if (throughput_mbs_.size() == throughput_ops_.size()) {
      double mbs_avg = CalcAvg(throughput_mbs_);
      double mbs_confidence_95 = CalcConfidence95(throughput_mbs_);
      fprintf(stdout,
              "%s [CI95 %d runs] : (%d, %d) ops/sec; (%.1f, %.1f) MB/sec\n",
              name, num_runs, ops_avg - ops_confidence_95,
              ops_avg + ops_confidence_95, mbs_avg - mbs_confidence_95,
              mbs_avg + mbs_confidence_95);
    } else {
      fprintf(stdout, "%s [CI95 %d runs] : (%d, %d) ops/sec\n", name, num_runs,
              ops_avg - ops_confidence_95, ops_avg + ops_confidence_95);
    }
  }

  void ReportFinal(const std::string& bench_name) {
    if (throughput_ops_.size() < 2) {
      // skip if there are not enough samples
      return;
    }

    const char* name = bench_name.c_str();
    int num_runs = static_cast<int>(throughput_ops_.size());

    if (throughput_mbs_.size() == throughput_ops_.size()) {
      // \xC2\xB1 is +/- character in UTF-8
      fprintf(stdout,
              "%s [AVG    %d runs] : %d (\xC2\xB1 %d) ops/sec; %6.1f (\xC2\xB1 "
              "%.1f) MB/sec\n"
              "%s [MEDIAN %d runs] : %d ops/sec; %6.1f MB/sec\n",
              name, num_runs, static_cast<int>(CalcAvg(throughput_ops_)),
              static_cast<int>(CalcConfidence95(throughput_ops_)),
              CalcAvg(throughput_mbs_), CalcConfidence95(throughput_mbs_), name,
              num_runs, static_cast<int>(CalcMedian(throughput_ops_)),
              CalcMedian(throughput_mbs_));
    } else {
      fprintf(stdout,
              "%s [AVG    %d runs] : %d (\xC2\xB1 %d) ops/sec\n"
              "%s [MEDIAN %d runs] : %d ops/sec\n",
              name, num_runs, static_cast<int>(CalcAvg(throughput_ops_)),
              static_cast<int>(CalcConfidence95(throughput_ops_)), name,
              num_runs, static_cast<int>(CalcMedian(throughput_ops_)));
    }
  }

 private:
  double CalcAvg(std::vector<double>& data) {
    double avg = 0;
    for (double x : data) {
      avg += x;
    }
    avg = avg / data.size();
    return avg;
  }

  // Calculates 95% CI assuming a normal distribution of samples.
  // Samples are not from a normal distribution, but it still
  // provides useful approximation.
  double CalcConfidence95(std::vector<double>& data) {
    assert(data.size() > 1);
    double avg = CalcAvg(data);
    double std_error = CalcStdDev(data, avg) / std::sqrt(data.size());

    // Z score for the 97.5 percentile
    // see https://en.wikipedia.org/wiki/1.96
    return 1.959964 * std_error;
  }

  double CalcMedian(std::vector<double>& data) {
    assert(data.size() > 0);
    std::sort(data.begin(), data.end());

    size_t mid = data.size() / 2;
    if (data.size() % 2 == 1) {
      // Odd number of entries
      return data[mid];
    } else {
      // Even number of entries
      return (data[mid] + data[mid - 1]) / 2;
    }
  }

  double CalcStdDev(std::vector<double>& data, double average) {
    assert(data.size() > 1);
    double squared_sum = 0.0;
    for (double x : data) {
      squared_sum += std::pow(x - average, 2);
    }

    // using samples count - 1 following Bessel's correction
    // see https://en.wikipedia.org/wiki/Bessel%27s_correction
    return std::sqrt(squared_sum / (data.size() - 1));
  }

  std::vector<double> throughput_ops_;
  std::vector<double> throughput_mbs_;
};

class TimestampEmulator {
 private:
  std::atomic<uint64_t> timestamp_;

 public:
  TimestampEmulator() : timestamp_(0) {}
  uint64_t Get() const { return timestamp_.load(); }
  void Inc() { timestamp_++; }
  rocksdb::Slice Allocate(char* scratch) {
    // TODO: support larger timestamp sizes
    assert(FLAGS_user_timestamp_size == 8);
    assert(scratch);
    uint64_t ts = timestamp_.fetch_add(1);
    EncodeFixed64(scratch, ts);
    return rocksdb::Slice(scratch, FLAGS_user_timestamp_size);
  }
  rocksdb::Slice GetTimestampForRead(Random64& rand, char* scratch) {
    assert(FLAGS_user_timestamp_size == 8);
    assert(scratch);
    if (FLAGS_read_with_latest_user_timestamp) {
      return Allocate(scratch);
    }
    // Choose a random timestamp from the past.
    uint64_t ts = rand.Next() % Get();
    EncodeFixed64(scratch, ts);
    return rocksdb::Slice(scratch, FLAGS_user_timestamp_size);
  }
};

// State shared by all concurrent executions of the same benchmark.
struct SharedState {
  port::Mutex mu;
  port::CondVar cv;
  int total;
  int perf_level;
  std::shared_ptr<rocksdb::RateLimiter> write_rate_limiter;
  std::shared_ptr<rocksdb::RateLimiter> read_rate_limiter;

  // Each thread goes through the following states:
  //    (1) initializing
  //    (2) waiting for others to be initialized
  //    (3) running
  //    (4) done

  long num_initialized;
  long num_done;
  bool start;

  SharedState() : cv(&mu), perf_level(FLAGS_perf_level) {}
};



// Per-thread state for concurrent executions of the same benchmark.
struct ThreadState {
  int tid;        // 0..n-1 when running in n threads
  Random64 rand;  // Has different seeds for different threads
  Stats stats;
  SharedState* shared;

  explicit ThreadState(int index, int my_seed)
      : tid(index), rand(*seed_base + my_seed) {}
};

class Duration {
 public:
  Duration(uint64_t max_seconds, int64_t max_ops, int64_t ops_per_stage = 0) {
    max_seconds_ = max_seconds;
    max_ops_ = max_ops;
    ops_per_stage_ = (ops_per_stage > 0) ? ops_per_stage : max_ops;
    ops_ = 0;
    start_at_ = FLAGS_env->NowMicros();
  }

  int64_t GetStage() { return std::min(ops_, max_ops_ - 1) / ops_per_stage_; }

  bool Done(int64_t increment) {
    if (increment <= 0) {
      increment = 1;  // avoid Done(0) and infinite loops
    }
    ops_ += increment;

    if (max_seconds_) {
      // Recheck every appx 1000 ops (exact iff increment is factor of 1000)
      auto granularity = FLAGS_ops_between_duration_checks;
      if ((ops_ / granularity) != ((ops_ - increment) / granularity)) {
        uint64_t now = FLAGS_env->NowMicros();
        return ((now - start_at_) / 1000000) >= max_seconds_;
      } else {
        return false;
      }
    } else {
      return ops_ > max_ops_;
    }
  }

 private:
  uint64_t max_seconds_;
  int64_t max_ops_;
  int64_t ops_per_stage_;
  int64_t ops_;
  uint64_t start_at_;
};



class Benchmark {
 private:
 std::shared_ptr<rocksdb::Cache> cache_;
  DBWithColumnFamilies db_;
  std::vector<DBWithColumnFamilies> multi_dbs_;
  int64_t num_;
  int key_size_;
  int user_timestamp_size_;
  int prefix_size_;
  int total_thread_count_;
  int64_t keys_per_prefix_;
  int64_t entries_per_batch_;
  int64_t writes_before_delete_range_;
  int64_t writes_per_range_tombstone_;
  int64_t range_tombstone_width_;
  int64_t max_num_range_tombstones_;
  rocksdb::ReadOptions read_options_;
  rocksdb::WriteOptions write_options_;
  rocksdb::Options open_options_;  // keep options around to properly destroy db later
  rocksdb::TraceOptions trace_options_;
  rocksdb::TraceOptions block_cache_trace_options_;
  int64_t reads_;
  int64_t deletes_;
  double read_random_exp_range_;
  int64_t writes_;
  int64_t readwrites_;
  int64_t merge_keys_;
  bool report_file_operations_;
  bool use_blob_db_;    // Stacked BlobDB
  bool read_operands_;  // read via GetMergeOperands()
  std::vector<std::string> keys_;

  std::unique_ptr<TimestampEmulator> mock_app_clock_;

  bool SanityCheck() {
    if (FLAGS_compression_ratio > 1) {
      fprintf(stderr, "compression_ratio should be between 0 and 1\n");
      return false;
    }
    return true;
  }

// Current the following isn't equivalent to OS_LINUX.
#if defined(__linux)
  static rocksdb::Slice TrimSpace(rocksdb::Slice s) {
    unsigned int start = 0;
    while (start < s.size() && isspace(s[start])) {
      start++;
    }
    unsigned int limit = static_cast<unsigned int>(s.size());
    while (limit > start && isspace(s[limit - 1])) {
      limit--;
    }
    return rocksdb::Slice(s.data() + start, limit - start);
  }
#endif

  static bool KeyExpired(const TimestampEmulator* timestamp_emulator,
                         const rocksdb::Slice& key) {
    const char* pos = key.data();
    pos += 8;
    uint64_t timestamp = 0;
    
    int bytes_to_fill = 8;
    for (int i = 0; i < bytes_to_fill; ++i) {
      timestamp |= (static_cast<uint64_t>(static_cast<unsigned char>(pos[i]))
                      << ((bytes_to_fill - i - 1) << 3));
    }
    
    return timestamp_emulator->Get() - timestamp > FLAGS_time_range;
  }

  static std::shared_ptr<rocksdb::MemoryAllocator> GetCacheAllocator() {
    std::shared_ptr<rocksdb::MemoryAllocator> allocator;

    if (FLAGS_use_cache_jemalloc_no_dump_allocator) {
      rocksdb::JemallocAllocatorOptions jemalloc_options;
      if (!NewJemallocNodumpAllocator(jemalloc_options, &allocator).ok()) {
        fprintf(stderr, "JemallocNodumpAllocator not supported.\n");
        exit(1);
      }
    } else if (FLAGS_use_cache_memkind_kmem_allocator) {

#ifdef MEMKIND
      allocator = std::make_shared<MemkindKmemAllocator>();
#else
      fprintf(stderr, "Memkind library is not linked with the binary.\n");
      exit(1);
#endif

    }

    return allocator;
  }


  static int32_t GetCacheHashSeed() {
    // For a fixed Cache seed, need a non-negative int32
    return static_cast<int32_t>(*seed_base) & 0x7fffffff;
  }

  static std::shared_ptr<rocksdb::Cache> NewCache(int64_t capacity) {
    rocksdb::CompressedSecondaryCacheOptions secondary_cache_opts;
    bool use_tiered_cache = false;
    if (capacity <= 0) {
      return nullptr;
    }


    if (FLAGS_cache_type == "lru_cache") {
      rocksdb::LRUCacheOptions opts(
          static_cast<size_t>(capacity), FLAGS_cache_numshardbits,
          false /*strict_capacity_limit*/, FLAGS_cache_high_pri_pool_ratio,
          GetCacheAllocator(), kDefaultToAdaptiveMutex,
          rocksdb::kDefaultCacheMetadataChargePolicy, FLAGS_cache_low_pri_pool_ratio);
      opts.hash_seed = GetCacheHashSeed();

      if (use_tiered_cache) {
        rocksdb::TieredCacheOptions tiered_opts;
        opts.capacity += secondary_cache_opts.capacity;
        tiered_opts.cache_type = rocksdb::PrimaryCacheType::kCacheTypeLRU;
        tiered_opts.cache_opts = &opts;
        tiered_opts.comp_cache_opts = secondary_cache_opts;
        return NewTieredCache(tiered_opts);
      } else {
        return opts.MakeSharedCache();
      }
    } else {
      fprintf(stderr, "Cache type not supported.");
      exit(1);
    }
  }

 public:
  Benchmark()
      : cache_(NewCache(FLAGS_cache_size)),
        num_(FLAGS_num),
        key_size_(FLAGS_key_size),
        user_timestamp_size_(FLAGS_user_timestamp_size),
        prefix_size_(FLAGS_prefix_size),
        total_thread_count_(0),
        keys_per_prefix_(FLAGS_keys_per_prefix),
        entries_per_batch_(1),
        reads_(FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads),
        read_random_exp_range_(0.0),
        writes_(FLAGS_writes < 0 ? FLAGS_num : FLAGS_writes),
        readwrites_(
            (FLAGS_writes < 0 && FLAGS_reads < 0)
                ? FLAGS_num
                : ((FLAGS_writes > FLAGS_reads) ? FLAGS_writes : FLAGS_reads)),
        merge_keys_(FLAGS_merge_keys < 0 ? FLAGS_num : FLAGS_merge_keys),
        report_file_operations_(FLAGS_report_file_operations),
        use_blob_db_(FLAGS_use_blob_db),  // Stacked BlobDB
        read_operands_(false) {

    if (FLAGS_prefix_size > FLAGS_key_size) {
      fprintf(stderr, "prefix size is larger than key size");
      exit(1);
    }

    std::vector<std::string> files;
    FLAGS_env->GetChildren(FLAGS_db, &files);
    for (size_t i = 0; i < files.size(); i++) {
      if (rocksdb::Slice(files[i]).starts_with("heap-")) {
        FLAGS_env->DeleteFile(FLAGS_db + "/" + files[i]);
      }
    }
    if (!FLAGS_use_existing_db) {
      rocksdb::Options options;
      options.env = FLAGS_env;
      if (!FLAGS_wal_dir.empty()) {
        options.wal_dir = FLAGS_wal_dir;
      }
      DestroyDB(FLAGS_db, options);
      if (!FLAGS_wal_dir.empty()) {
        FLAGS_env->DeleteDir(FLAGS_wal_dir);
      }

      if (FLAGS_num_multi_db > 1) {
        FLAGS_env->CreateDir(FLAGS_db);
        if (!FLAGS_wal_dir.empty()) {
          FLAGS_env->CreateDir(FLAGS_wal_dir);
        }
      }
    }

    if (user_timestamp_size_ > 0) {
      mock_app_clock_.reset(new TimestampEmulator());
    }
  }

  void DeleteDBs() {
    db_.DeleteDBs();
    for (const DBWithColumnFamilies& dbwcf : multi_dbs_) {
      delete dbwcf.db;
    }
  }

  ~Benchmark() {
    DeleteDBs();
    if (cache_.get() != nullptr) {
      // Clear cache reference first
      open_options_.write_buffer_manager.reset();
      // this will leak, but we're shutting down so nobody cares
      cache_->DisownData();
    }
  }

  rocksdb::Slice AllocateKey(std::unique_ptr<const char[]>* key_guard) {
    char* data = new char[key_size_];
    const char* const_data = data;
    key_guard->reset(const_data);
    return rocksdb::Slice(key_guard->get(), key_size_);
  }

  // Generate key according to the given specification and random number.
  // The resulting key will have the following format:
  //   - If keys_per_prefix_ is positive, extra trailing bytes are either cut
  //     off or padded with '0'.
  //     The prefix value is derived from key value.
  //     ----------------------------
  //     | prefix 00000 | key 00000 |
  //     ----------------------------
  //
  //   - If keys_per_prefix_ is 0, the key is simply a binary representation of
  //     random number followed by trailing '0's
  //     ----------------------------
  //     |        key 00000         |
  //     ----------------------------
  void GenerateKeyFromInt(uint64_t v, int64_t num_keys, rocksdb::Slice* key) {
    if (!keys_.empty()) {
      assert(FLAGS_use_existing_keys);
      assert(keys_.size() == static_cast<size_t>(num_keys));
      assert(v < static_cast<uint64_t>(num_keys));
      *key = keys_[v];
      return;
    }
    char* start = const_cast<char*>(key->data());
    char* pos = start;
    if (keys_per_prefix_ > 0) {
      int64_t num_prefix = num_keys / keys_per_prefix_;
      int64_t prefix = v % num_prefix;
      int bytes_to_fill = std::min(prefix_size_, 8);
      
      for (int i = 0; i < bytes_to_fill; ++i) {
        pos[i] = (prefix >> ((bytes_to_fill - i - 1) << 3)) & 0xFF;
      }
       

      if (prefix_size_ > 8) {
        // fill the rest with 0s
        memset(pos + 8, '0', prefix_size_ - 8);
      }
      pos += prefix_size_;
    }

    int bytes_to_fill = std::min(key_size_ - static_cast<int>(pos - start), 8);
    
      for (int i = 0; i < bytes_to_fill; ++i) {
        pos[i] = (v >> ((bytes_to_fill - i - 1) << 3)) & 0xFF;
      }
    
    pos += bytes_to_fill;
    if (key_size_ > pos - start) {
      memset(pos, '0', key_size_ - (pos - start));
    }
  }

  void GenerateKeyFromIntForSeek(uint64_t v, int64_t num_keys, rocksdb::Slice* key) {
    GenerateKeyFromInt(v, num_keys, key);
    if (FLAGS_seek_missing_prefix) {
      assert(prefix_size_ > 8);
      char* key_ptr = const_cast<char*>(key->data());
      // This rely on GenerateKeyFromInt filling paddings with '0's.
      // Putting a '1' will create a non-existing prefix.
      key_ptr[8] = '1';
    }
  }

  std::string GetPathForMultiple(std::string base_name, size_t id) {
    if (!base_name.empty()) {
#ifndef OS_WIN
      if (base_name.back() != '/') {
        base_name += '/';
      }
#else
      if (base_name.back() != '\\') {
        base_name += '\\';
      }
#endif
    }
    return base_name + std::to_string(id);
  }

  void ErrorExit() {
    DeleteDBs();
    exit(1);
  }

  void Run() {
    if (!SanityCheck()) {
      ErrorExit();
    }
    Open(&open_options_);
    
    std::stringstream benchmark_stream(FLAGS_benchmarks);
    std::string name;
    while (std::getline(benchmark_stream, name, ',')) {
      // Sanitize parameters
      num_ = FLAGS_num;
      reads_ = (FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads);
      writes_ = (FLAGS_writes < 0 ? FLAGS_num : FLAGS_writes);
      deletes_ = (FLAGS_deletes < 0 ? FLAGS_num : FLAGS_deletes);
      value_size = FLAGS_value_size;
      key_size_ = FLAGS_key_size;
      entries_per_batch_ = FLAGS_batch_size;
      writes_before_delete_range_ = FLAGS_writes_before_delete_range;
      writes_per_range_tombstone_ = FLAGS_writes_per_range_tombstone;
      range_tombstone_width_ = FLAGS_range_tombstone_width;
      max_num_range_tombstones_ = FLAGS_max_num_range_tombstones;
      write_options_ = rocksdb::WriteOptions();
      read_random_exp_range_ = FLAGS_read_random_exp_range;
      if (FLAGS_sync) {
        write_options_.sync = true;
      }
      write_options_.disableWAL = FLAGS_disable_wal;
      write_options_.rate_limiter_priority =
          FLAGS_rate_limit_auto_wal_flush ? rocksdb::Env::IO_USER : rocksdb::Env::IO_TOTAL;
      read_options_ = rocksdb::ReadOptions(FLAGS_verify_checksum, true);
      read_options_.total_order_seek = FLAGS_total_order_seek;
      read_options_.prefix_same_as_start = FLAGS_prefix_same_as_start;
      read_options_.rate_limiter_priority =
          FLAGS_rate_limit_user_ops ? rocksdb::Env::IO_USER : rocksdb::Env::IO_TOTAL;
      read_options_.tailing = FLAGS_use_tailing_iterator;
      read_options_.readahead_size = FLAGS_readahead_size;
      read_options_.adaptive_readahead = FLAGS_adaptive_readahead;
      read_options_.async_io = FLAGS_async_io;
      read_options_.optimize_multiget_for_io = FLAGS_optimize_multiget_for_io;
      read_options_.auto_readahead_size = FLAGS_auto_readahead_size;

      void (Benchmark::*method)(ThreadState*) = nullptr;
      void (Benchmark::*post_process_method)() = nullptr;

      bool fresh_db = false;
      int num_threads = FLAGS_threads;

      int num_repeat = 1;
      int num_warmup = 0;
      if (!name.empty() && *name.rbegin() == ']') {
        auto it = name.find('[');
        if (it == std::string::npos) {
          fprintf(stderr, "unknown benchmark arguments '%s'\n", name.c_str());
          ErrorExit();
        }
        std::string args = name.substr(it + 1);
        args.resize(args.size() - 1);
        name.resize(it);

        std::string bench_arg;
        std::stringstream args_stream(args);
        while (std::getline(args_stream, bench_arg, '-')) {
          if (bench_arg.empty()) {
            continue;
          }
          if (bench_arg[0] == 'X') {
            // Repeat the benchmark n times
            std::string num_str = bench_arg.substr(1);
            num_repeat = std::stoi(num_str);
          } else if (bench_arg[0] == 'W') {
            // Warm up the benchmark for n times
            std::string num_str = bench_arg.substr(1);
            num_warmup = std::stoi(num_str);
          }
        }
      }

      // Both fillseqdeterministic and filluniquerandomdeterministic
      // fill the levels except the max level with UNIQUE_RANDOM
      // and fill the max level with fillseq and filluniquerandom, respectively
      if (name == "mixgraph") {
        method = &Benchmark::MixGraph;
      } else if (!name.empty()) {  // No error message for empty name
        fprintf(stderr, "unknown benchmark '%s'\n", name.c_str());
        ErrorExit();
      }

      if (fresh_db) {
        if (FLAGS_use_existing_db) {
          fprintf(stdout, "%-12s : skipped (--use_existing_db is true)\n",
                  name.c_str());
          method = nullptr;
        } else {
          if (db_.db != nullptr) {
            db_.DeleteDBs();
            DestroyDB(FLAGS_db, open_options_);
          }
          rocksdb::Options options = open_options_;
          for (size_t i = 0; i < multi_dbs_.size(); i++) {
            delete multi_dbs_[i].db;
            if (!open_options_.wal_dir.empty()) {
              options.wal_dir = GetPathForMultiple(open_options_.wal_dir, i);
            }
            DestroyDB(GetPathForMultiple(FLAGS_db, i), options);
          }
          multi_dbs_.clear();
        }
        Open(&open_options_);  // use open_options for the last accessed
      }

      if (method != nullptr) {
        fprintf(stdout, "DB path: [%s]\n", FLAGS_db.c_str());

        if (name == "backup") {
          std::cout << "Backup path: [" << FLAGS_backup_dir << "]" << std::endl;
        } else if (name == "restore") {
          std::cout << "Backup path: [" << FLAGS_backup_dir << "]" << std::endl;
          std::cout << "Restore path: [" << FLAGS_restore_dir << "]"
                    << std::endl;
        }
        // A trace_file option can be provided both for trace and replay
        // operations. But db_bench does not support tracing and replaying at
        // the same time, for now. So, start tracing only when it is not a
        // replay.
        if (FLAGS_trace_file != "" && name != "replay") {
          std::unique_ptr<rocksdb::TraceWriter> trace_writer;
          rocksdb::Status s = NewFileTraceWriter(FLAGS_env, rocksdb::EnvOptions(),
                                        FLAGS_trace_file, &trace_writer);
          if (!s.ok()) {
            fprintf(stderr, "Encountered an error starting a trace, %s\n",
                    s.ToString().c_str());
            ErrorExit();
          }
          s = db_.db->StartTrace(trace_options_, std::move(trace_writer));
          if (!s.ok()) {
            fprintf(stderr, "Encountered an error starting a trace, %s\n",
                    s.ToString().c_str());
            ErrorExit();
          }
          fprintf(stdout, "Tracing the workload to: [%s]\n",
                  FLAGS_trace_file.c_str());
        }
        // Start block cache tracing.
        if (!FLAGS_block_cache_trace_file.empty()) {
          // Sanity checks.
          if (FLAGS_block_cache_trace_sampling_frequency <= 0) {
            fprintf(stderr,
                    "Block cache trace sampling frequency must be higher than "
                    "0.\n");
            ErrorExit();
          }
          if (FLAGS_block_cache_trace_max_trace_file_size_in_bytes <= 0) {
            fprintf(stderr,
                    "The maximum file size for block cache tracing must be "
                    "higher than 0.\n");
            ErrorExit();
          }
          block_cache_trace_options_.max_trace_file_size =
              FLAGS_block_cache_trace_max_trace_file_size_in_bytes;
          block_cache_trace_options_.sampling_frequency =
              FLAGS_block_cache_trace_sampling_frequency;
          std::unique_ptr<rocksdb::TraceWriter> block_cache_trace_writer;
          rocksdb::Status s = NewFileTraceWriter(FLAGS_env, rocksdb::EnvOptions(),
                                        FLAGS_block_cache_trace_file,
                                        &block_cache_trace_writer);
          if (!s.ok()) {
            fprintf(stderr,
                    "Encountered an error when creating trace writer, %s\n",
                    s.ToString().c_str());
            ErrorExit();
          }
          s = db_.db->StartBlockCacheTrace(block_cache_trace_options_,
                                           std::move(block_cache_trace_writer));
          if (!s.ok()) {
            fprintf(
                stderr,
                "Encountered an error when starting block cache tracing, %s\n",
                s.ToString().c_str());
            ErrorExit();
          }
          fprintf(stdout, "Tracing block cache accesses to: [%s]\n",
                  FLAGS_block_cache_trace_file.c_str());
        }

        if (num_warmup > 0) {
          printf("Warming up benchmark by running %d times\n", num_warmup);
        }

        for (int i = 0; i < num_warmup; i++) {
          RunBenchmark(num_threads, name, method);
        }

        if (num_repeat > 1) {
          printf("Running benchmark for %d times\n", num_repeat);
        }

        CombinedStats combined_stats;
        for (int i = 0; i < num_repeat; i++) {
          Stats stats = RunBenchmark(num_threads, name, method);
          combined_stats.AddStats(stats);
          if (FLAGS_confidence_interval_only) {
            combined_stats.ReportWithConfidenceIntervals(name);
          } else {
            combined_stats.Report(name);
          }
        }
        if (num_repeat > 1) {
          combined_stats.ReportFinal(name);
        }
      }
      if (post_process_method != nullptr) {
        (this->*post_process_method)();
      }
    }

    if (secondary_update_thread_) {
      secondary_update_stopped_.store(1, std::memory_order_relaxed);
      secondary_update_thread_->join();
      secondary_update_thread_.reset();
    }

    if (name != "replay" && FLAGS_trace_file != "") {
      rocksdb::Status s = db_.db->EndTrace();
      if (!s.ok()) {
        fprintf(stderr, "Encountered an error ending the trace, %s\n",
                s.ToString().c_str());
      }
    }
    if (!FLAGS_block_cache_trace_file.empty()) {
      rocksdb::Status s = db_.db->EndBlockCacheTrace();
      if (!s.ok()) {
        fprintf(stderr,
                "Encountered an error ending the block cache tracing, %s\n",
                s.ToString().c_str());
      }
    }

    if (FLAGS_statistics) {
      fprintf(stdout, "STATISTICS:\n%s\n", dbstats->ToString().c_str());
    }

    if (FLAGS_use_secondary_db) {
      fprintf(stdout, "Secondary instance updated  %" PRIu64 " times.\n",
              secondary_db_updates_);
    }
  }

 private:
  std::shared_ptr<TimestampEmulator> timestamp_emulator_;
  std::unique_ptr<port::Thread> secondary_update_thread_;
  std::atomic<int> secondary_update_stopped_{0};
  uint64_t secondary_db_updates_ = 0;
  struct ThreadArg {
    Benchmark* bm;
    SharedState* shared;
    ThreadState* thread;
    void (Benchmark::*method)(ThreadState*);
  };

  static void ThreadBody(void* v) {
    ThreadArg* arg = static_cast<ThreadArg*>(v);
    SharedState* shared = arg->shared;
    ThreadState* thread = arg->thread;
    {
      MutexLock l(&shared->mu);
      shared->num_initialized++;
      if (shared->num_initialized >= shared->total) {
        shared->cv.SignalAll();
      }
      while (!shared->start) {
        shared->cv.Wait();
      }
    }

    // rocksdb::SetPerfLevel(static_cast<rocksdb::PerfLevel>(shared->perf_level));
    // perf_context.EnablePerLevelPerfContext();
    thread->stats.Start(thread->tid);

    (arg->bm->*(arg->method))(thread);

    if (FLAGS_perf_level > rocksdb::PerfLevel::kDisable) {
      thread->stats.AddMessage(std::string("PERF_CONTEXT:\n") +
                               rocksdb::get_perf_context()->ToString());
    }
    thread->stats.Stop();

    {
      MutexLock l(&shared->mu);
      shared->num_done++;
      if (shared->num_done >= shared->total) {
        shared->cv.SignalAll();
      }
    }
  }

  Stats RunBenchmark(int n, rocksdb::Slice name,
                     void (Benchmark::*method)(ThreadState*)) {
    SharedState shared;
    shared.total = n;
    shared.num_initialized = 0;
    shared.num_done = 0;
    shared.start = false;
    if (FLAGS_benchmark_write_rate_limit > 0) {
      shared.write_rate_limiter.reset(
          rocksdb::NewGenericRateLimiter(FLAGS_benchmark_write_rate_limit));
    }
    if (FLAGS_benchmark_read_rate_limit > 0) {
      shared.read_rate_limiter.reset(rocksdb::NewGenericRateLimiter(
          FLAGS_benchmark_read_rate_limit, 100000 /* refill_period_us */,
          10 /* fairness */, rocksdb::RateLimiter::Mode::kReadsOnly));
    }

    std::unique_ptr<ReporterAgent> reporter_agent;
    if (FLAGS_report_interval_seconds > 0) {
      reporter_agent.reset(new ReporterAgent(FLAGS_env, FLAGS_report_file,
                                             FLAGS_report_interval_seconds));
    }

    ThreadArg* arg = new ThreadArg[n];

    for (int i = 0; i < n; i++) {
#ifdef NUMA
      if (FLAGS_enable_numa) {
        // Performs a local allocation of memory to threads in numa node.
        int n_nodes = numa_num_task_nodes();  // Number of nodes in NUMA.
        numa_exit_on_error = 1;
        int numa_node = i % n_nodes;
        bitmask* nodes = numa_allocate_nodemask();
        numa_bitmask_clearall(nodes);
        numa_bitmask_setbit(nodes, numa_node);
        // numa_bind() call binds the process to the node and these
        // properties are passed on to the thread that is created in
        // StartThread method called later in the loop.
        numa_bind(nodes);
        numa_set_strict(1);
        numa_free_nodemask(nodes);
      }
#endif
      arg[i].bm = this;
      arg[i].method = method;
      arg[i].shared = &shared;
      total_thread_count_++;
      arg[i].thread = new ThreadState(i, total_thread_count_);
      arg[i].thread->stats.SetReporterAgent(reporter_agent.get());
      arg[i].thread->shared = &shared;
      FLAGS_env->StartThread(ThreadBody, &arg[i]);
    }

    shared.mu.Lock();
    while (shared.num_initialized < n) {
      shared.cv.Wait();
    }

    shared.start = true;
    shared.cv.SignalAll();
    while (shared.num_done < n) {
      shared.cv.Wait();
    }
    shared.mu.Unlock();

    // Stats for some threads can be excluded.
    Stats merge_stats;
    for (int i = 0; i < n; i++) {
      merge_stats.Merge(arg[i].thread->stats);
    }
    merge_stats.Report(name);

    for (int i = 0; i < n; i++) {
      delete arg[i].thread;
    }
    delete[] arg;

    return merge_stats;
  }

  template <OperationType kOpType, typename FnType, typename... Args>
  static inline void ChecksumBenchmark(FnType fn, ThreadState* thread,
                                       Args... args) {
    const int size = FLAGS_block_size;  // use --block_size option for db_bench
    std::string labels = "(" + std::to_string(FLAGS_block_size) + " per op)";
    const char* label = labels.c_str();

    std::string data(size, 'x');
    uint64_t bytes = 0;
    uint32_t val = 0;
    while (bytes < 5000U * uint64_t{1048576}) {  // ~5GB
      val += static_cast<uint32_t>(fn(data.data(), size, args...));
      thread->stats.FinishedOps(nullptr, nullptr, 1, kOpType);
      bytes += size;
    }
    // Print so result is not dead
    fprintf(stderr, "... val=0x%x\r", static_cast<unsigned int>(val));

    thread->stats.AddBytes(bytes);
    thread->stats.AddMessage(label);
  }


  void InitializeOptionsFromFlags(rocksdb::Options* opts) {
    printf("Initializing RocksDB Options from command-line flags\n");
    rocksdb::Options& options = *opts;
    rocksdb::ConfigOptions config_options(options);
    config_options.ignore_unsupported_options = false;

    assert(db_.db == nullptr);

    options.env = FLAGS_env;
    options.wal_dir = FLAGS_wal_dir;
    options.dump_malloc_stats = FLAGS_dump_malloc_stats;
    options.stats_dump_period_sec =
        static_cast<unsigned int>(FLAGS_stats_dump_period_sec);
    options.stats_persist_period_sec =
        static_cast<unsigned int>(FLAGS_stats_persist_period_sec);
    options.persist_stats_to_disk = FLAGS_persist_stats_to_disk;
    options.stats_history_buffer_size =
        static_cast<size_t>(FLAGS_stats_history_buffer_size);
    options.avoid_flush_during_recovery = FLAGS_avoid_flush_during_recovery;

    options.compression_opts.level = FLAGS_compression_level;
    options.compression_opts.max_dict_bytes = FLAGS_compression_max_dict_bytes;
    options.compression_opts.zstd_max_train_bytes =
        FLAGS_compression_zstd_max_train_bytes;
    options.compression_opts.parallel_threads =
        FLAGS_compression_parallel_threads;
    options.compression_opts.max_dict_buffer_bytes =
        FLAGS_compression_max_dict_buffer_bytes;
    options.compression_opts.use_zstd_dict_trainer =
        FLAGS_compression_use_zstd_dict_trainer;

    options.max_open_files = FLAGS_open_files;
    options.arena_block_size = FLAGS_arena_block_size;
    options.write_buffer_size = FLAGS_write_buffer_size;
    options.max_write_buffer_number = FLAGS_max_write_buffer_number;
    options.min_write_buffer_number_to_merge =
        FLAGS_min_write_buffer_number_to_merge;
    options.max_write_buffer_number_to_maintain =
        FLAGS_max_write_buffer_number_to_maintain;
    options.max_write_buffer_size_to_maintain =
        FLAGS_max_write_buffer_size_to_maintain;
    options.max_background_jobs = FLAGS_max_background_jobs;
    options.max_background_compactions = FLAGS_max_background_compactions;
    options.max_subcompactions = static_cast<uint32_t>(FLAGS_subcompactions);
    options.max_background_flushes = FLAGS_max_background_flushes;
    options.compaction_style = FLAGS_compaction_style_e;
    options.compaction_pri = FLAGS_compaction_pri_e;
    options.allow_mmap_reads = FLAGS_mmap_read;
    options.allow_mmap_writes = FLAGS_mmap_write;
    options.use_direct_reads = FLAGS_use_direct_reads;
    options.use_direct_io_for_flush_and_compaction =
        FLAGS_use_direct_io_for_flush_and_compaction;
    options.manual_wal_flush = FLAGS_manual_wal_flush;
    options.wal_compression = FLAGS_wal_compression_e;
    options.ttl = FLAGS_fifo_compaction_ttl;
    options.compaction_options_fifo = rocksdb::CompactionOptionsFIFO(
        FLAGS_fifo_compaction_max_table_files_size_mb * 1024 * 1024,
        FLAGS_fifo_compaction_allow_compaction);
    options.compaction_options_fifo.age_for_warm = FLAGS_fifo_age_for_warm;
    options.memtable_huge_page_size = FLAGS_memtable_use_huge_page ? 2048 : 0;
    options.memtable_prefix_bloom_size_ratio = FLAGS_memtable_bloom_size_ratio;
    options.memtable_whole_key_filtering = FLAGS_memtable_whole_key_filtering;
    options.bloom_locality = FLAGS_bloom_locality;
    options.max_file_opening_threads = FLAGS_file_opening_threads;
    options.compaction_readahead_size = FLAGS_compaction_readahead_size;
    options.log_readahead_size = FLAGS_log_readahead_size;
    options.random_access_max_buffer_size = FLAGS_random_access_max_buffer_size;
    options.writable_file_max_buffer_size = FLAGS_writable_file_max_buffer_size;
    options.use_fsync = FLAGS_use_fsync;
    options.num_levels = FLAGS_num_levels;
    options.target_file_size_base = FLAGS_target_file_size_base;
    options.target_file_size_multiplier = FLAGS_target_file_size_multiplier;
    options.max_bytes_for_level_base = FLAGS_max_bytes_for_level_base;
    options.level_compaction_dynamic_level_bytes =
        FLAGS_level_compaction_dynamic_level_bytes;
    options.max_bytes_for_level_multiplier =
        FLAGS_max_bytes_for_level_multiplier;
    rocksdb::Status s =
        CreateMemTableRepFactory(config_options, &options.memtable_factory);

    if (!s.ok()) {
      fprintf(stderr, "Could not create memtable factory: %s\n",
              s.ToString().c_str());
      exit(1);
    } else if ((FLAGS_prefix_size == 0) &&
               (options.memtable_factory->IsInstanceOf("prefix_hash") ||
                options.memtable_factory->IsInstanceOf("hash_linkedlist"))) {
      fprintf(stderr,
              "prefix_size should be non-zero if PrefixHash or "
              "HashLinkedList memtablerep is used\n");
      exit(1);
    }

    
      rocksdb::BlockBasedTableOptions block_based_options;
      block_based_options.checksum =
          static_cast<rocksdb::ChecksumType>(FLAGS_checksum_type);
      if (FLAGS_use_hash_search) {
        if (FLAGS_prefix_size == 0) {
          fprintf(stderr,
                  "prefix_size not assigned when enable use_hash_search \n");
          exit(1);
        }
        block_based_options.index_type = rocksdb::BlockBasedTableOptions::kHashSearch;
      } else {
        block_based_options.index_type = rocksdb::BlockBasedTableOptions::kBinarySearch;
      }
      if (FLAGS_partition_index_and_filters || FLAGS_partition_index) {
        if (FLAGS_index_with_first_key) {
          fprintf(stderr,
                  "--index_with_first_key is not compatible with"
                  " partition index.");
        }
        if (FLAGS_use_hash_search) {
          fprintf(stderr,
                  "use_hash_search is incompatible with "
                  "partition index and is ignored");
        }
        block_based_options.index_type =
            rocksdb::BlockBasedTableOptions::kTwoLevelIndexSearch;
        block_based_options.metadata_block_size = FLAGS_metadata_block_size;
        if (FLAGS_partition_index_and_filters) {
          block_based_options.partition_filters = true;
        }
      } else if (FLAGS_index_with_first_key) {
        block_based_options.index_type =
            rocksdb::BlockBasedTableOptions::kBinarySearchWithFirstKey;
      }
      rocksdb::BlockBasedTableOptions::IndexShorteningMode index_shortening =
          block_based_options.index_shortening;
      switch (FLAGS_index_shortening_mode) {
        case 0:
          index_shortening =
              rocksdb::BlockBasedTableOptions::IndexShorteningMode::kNoShortening;
          break;
        case 1:
          index_shortening =
              rocksdb::BlockBasedTableOptions::IndexShorteningMode::kShortenSeparators;
          break;
        case 2:
          index_shortening = rocksdb::BlockBasedTableOptions::IndexShorteningMode::
              kShortenSeparatorsAndSuccessor;
          break;
        default:
          fprintf(stderr, "Unknown key shortening mode\n");
      }
      block_based_options.optimize_filters_for_memory =
          FLAGS_optimize_filters_for_memory;
      block_based_options.index_shortening = index_shortening;
      if (cache_ == nullptr) {
        block_based_options.no_block_cache = true;
      }
      block_based_options.cache_index_and_filter_blocks =
          FLAGS_cache_index_and_filter_blocks;
      block_based_options.pin_l0_filter_and_index_blocks_in_cache =
          FLAGS_pin_l0_filter_and_index_blocks_in_cache;
      block_based_options.pin_top_level_index_and_filter =
          FLAGS_pin_top_level_index_and_filter;
      if (FLAGS_cache_high_pri_pool_ratio > 1e-6) {  // > 0.0 + eps
        block_based_options.cache_index_and_filter_blocks_with_high_priority =
            true;
      }
      if (FLAGS_cache_high_pri_pool_ratio + FLAGS_cache_low_pri_pool_ratio >
          1.0) {
        fprintf(stderr,
                "Sum of high_pri_pool_ratio and low_pri_pool_ratio "
                "cannot exceed 1.0.\n");
      }
      block_based_options.block_cache = cache_;
      block_based_options.cache_usage_options.options_overrides.insert(
          {rocksdb::CacheEntryRole::kCompressionDictionaryBuildingBuffer,
           {/*.charged = */ FLAGS_charge_compression_dictionary_building_buffer
                ? rocksdb::CacheEntryRoleOptions::Decision::kEnabled
                : rocksdb::CacheEntryRoleOptions::Decision::kDisabled}});
      block_based_options.cache_usage_options.options_overrides.insert(
          {rocksdb::CacheEntryRole::kFilterConstruction,
           {/*.charged = */ FLAGS_charge_filter_construction
                ? rocksdb::CacheEntryRoleOptions::Decision::kEnabled
                : rocksdb::CacheEntryRoleOptions::Decision::kDisabled}});
      block_based_options.cache_usage_options.options_overrides.insert(
          {rocksdb::CacheEntryRole::kBlockBasedTableReader,
           {/*.charged = */ FLAGS_charge_table_reader
                ? rocksdb::CacheEntryRoleOptions::Decision::kEnabled
                : rocksdb::CacheEntryRoleOptions::Decision::kDisabled}});
      block_based_options.cache_usage_options.options_overrides.insert(
          {rocksdb::CacheEntryRole::kFileMetadata,
           {/*.charged = */ FLAGS_charge_file_metadata
                ? rocksdb::CacheEntryRoleOptions::Decision::kEnabled
                : rocksdb::CacheEntryRoleOptions::Decision::kDisabled}});
      block_based_options.cache_usage_options.options_overrides.insert(
          {rocksdb::CacheEntryRole::kBlobCache,
           {/*.charged = */ FLAGS_charge_blob_cache
                ? rocksdb::CacheEntryRoleOptions::Decision::kEnabled
                : rocksdb::CacheEntryRoleOptions::Decision::kDisabled}});
      block_based_options.block_size = FLAGS_block_size;
      block_based_options.block_restart_interval = FLAGS_block_restart_interval;
      block_based_options.index_block_restart_interval =
          FLAGS_index_block_restart_interval;
      block_based_options.format_version =
          static_cast<uint32_t>(FLAGS_format_version);
      block_based_options.read_amp_bytes_per_bit = FLAGS_read_amp_bytes_per_bit;
      block_based_options.enable_index_compression =
          FLAGS_enable_index_compression;
      block_based_options.block_align = FLAGS_block_align;
      block_based_options.whole_key_filtering = FLAGS_whole_key_filtering;
      block_based_options.max_auto_readahead_size =
          FLAGS_max_auto_readahead_size;
      block_based_options.initial_auto_readahead_size =
          FLAGS_initial_auto_readahead_size;
      block_based_options.num_file_reads_for_auto_readahead =
          FLAGS_num_file_reads_for_auto_readahead;
      rocksdb::BlockBasedTableOptions::PrepopulateBlockCache prepopulate_block_cache =
          block_based_options.prepopulate_block_cache;
      switch (FLAGS_prepopulate_block_cache) {
        case 0:
          prepopulate_block_cache =
              rocksdb::BlockBasedTableOptions::PrepopulateBlockCache::kDisable;
          break;
        case 1:
          prepopulate_block_cache =
              rocksdb::BlockBasedTableOptions::PrepopulateBlockCache::kFlushOnly;
          break;
        default:
          fprintf(stderr, "Unknown prepopulate block cache mode\n");
      }
      block_based_options.prepopulate_block_cache = prepopulate_block_cache;
      if (FLAGS_use_data_block_hash_index) {
        block_based_options.data_block_index_type =
            rocksdb::BlockBasedTableOptions::kDataBlockBinaryAndHash;
      } else {
        block_based_options.data_block_index_type =
            rocksdb::BlockBasedTableOptions::kDataBlockBinarySearch;
      }
      block_based_options.data_block_hash_table_util_ratio =
          FLAGS_data_block_hash_table_util_ratio;

      options.table_factory.reset(
          NewBlockBasedTableFactory(block_based_options));
    

    if (FLAGS_max_bytes_for_level_multiplier_additional_v.size() > 0) {
      if (FLAGS_max_bytes_for_level_multiplier_additional_v.size() !=
          static_cast<unsigned int>(FLAGS_num_levels)) {
        fprintf(stderr, "Insufficient number of fanouts specified %d\n",
                static_cast<int>(
                    FLAGS_max_bytes_for_level_multiplier_additional_v.size()));
        exit(1);
      }
      options.max_bytes_for_level_multiplier_additional =
          FLAGS_max_bytes_for_level_multiplier_additional_v;
    }
    options.level0_stop_writes_trigger = FLAGS_level0_stop_writes_trigger;
    options.level0_file_num_compaction_trigger =
        FLAGS_level0_file_num_compaction_trigger;
    options.level0_slowdown_writes_trigger =
        FLAGS_level0_slowdown_writes_trigger;
    options.compression = FLAGS_compression_type_e;
    options.preclude_last_level_data_seconds =
        FLAGS_preclude_last_level_data_seconds;
    options.preserve_internal_time_seconds =
        FLAGS_preserve_internal_time_seconds;
    options.sample_for_compression = FLAGS_sample_for_compression;
    options.WAL_ttl_seconds = FLAGS_wal_ttl_seconds;
    options.WAL_size_limit_MB = FLAGS_wal_size_limit_MB;
    options.max_total_wal_size = FLAGS_max_total_wal_size;

    if (FLAGS_min_level_to_compress >= 0) {
      assert(FLAGS_min_level_to_compress <= FLAGS_num_levels);
      options.compression_per_level.resize(FLAGS_num_levels);
      for (int i = 0; i < FLAGS_min_level_to_compress; i++) {
        options.compression_per_level[i] = rocksdb::kNoCompression;
      }
      for (int i = FLAGS_min_level_to_compress; i < FLAGS_num_levels; i++) {
        options.compression_per_level[i] = FLAGS_compression_type_e;
      }
    }
    options.soft_pending_compaction_bytes_limit =
        FLAGS_soft_pending_compaction_bytes_limit;
    options.hard_pending_compaction_bytes_limit =
        FLAGS_hard_pending_compaction_bytes_limit;
    options.delayed_write_rate = FLAGS_delayed_write_rate;
    options.allow_concurrent_memtable_write =
        FLAGS_allow_concurrent_memtable_write;
    options.experimental_mempurge_threshold =
        FLAGS_experimental_mempurge_threshold;
    options.inplace_update_support = FLAGS_inplace_update_support;
    options.inplace_update_num_locks = FLAGS_inplace_update_num_locks;
    options.enable_write_thread_adaptive_yield =
        FLAGS_enable_write_thread_adaptive_yield;
    options.enable_pipelined_write = FLAGS_enable_pipelined_write;
    options.unordered_write = FLAGS_unordered_write;
    options.write_thread_max_yield_usec = FLAGS_write_thread_max_yield_usec;
    options.write_thread_slow_yield_usec = FLAGS_write_thread_slow_yield_usec;
    options.table_cache_numshardbits = FLAGS_table_cache_numshardbits;
    options.max_compaction_bytes = FLAGS_max_compaction_bytes;
    options.disable_auto_compactions = FLAGS_disable_auto_compactions;
    options.optimize_filters_for_hits = FLAGS_optimize_filters_for_hits;
    options.paranoid_checks = FLAGS_paranoid_checks;
    options.force_consistency_checks = FLAGS_force_consistency_checks;
    options.periodic_compaction_seconds = FLAGS_periodic_compaction_seconds;
    options.ttl = FLAGS_ttl_seconds;
    // fill storage options
    options.advise_random_on_open = FLAGS_advise_random_on_open;
    options.use_adaptive_mutex = FLAGS_use_adaptive_mutex;
    options.bytes_per_sync = FLAGS_bytes_per_sync;
    options.wal_bytes_per_sync = FLAGS_wal_bytes_per_sync;

    options.max_successive_merges = FLAGS_max_successive_merges;
    options.report_bg_io_stats = FLAGS_report_bg_io_stats;

    // set universal style compaction configurations, if applicable
    if (FLAGS_universal_size_ratio != 0) {
      options.compaction_options_universal.size_ratio =
          FLAGS_universal_size_ratio;
    }
    if (FLAGS_universal_min_merge_width != 0) {
      options.compaction_options_universal.min_merge_width =
          FLAGS_universal_min_merge_width;
    }
    if (FLAGS_universal_max_merge_width != 0) {
      options.compaction_options_universal.max_merge_width =
          FLAGS_universal_max_merge_width;
    }
    if (FLAGS_universal_max_size_amplification_percent != 0) {
      options.compaction_options_universal.max_size_amplification_percent =
          FLAGS_universal_max_size_amplification_percent;
    }
    if (FLAGS_universal_compression_size_percent != -1) {
      options.compaction_options_universal.compression_size_percent =
          FLAGS_universal_compression_size_percent;
    }
    options.compaction_options_universal.allow_trivial_move =
        FLAGS_universal_allow_trivial_move;
    options.compaction_options_universal.incremental =
        FLAGS_universal_incremental;
    options.compaction_options_universal.stop_style =
        static_cast<rocksdb::CompactionStopStyle>(FLAGS_universal_stop_style);
    if (FLAGS_thread_status_per_interval > 0) {
      options.enable_thread_tracking = true;
    }

    options.allow_data_in_errors = FLAGS_allow_data_in_errors;
    options.track_and_verify_wals_in_manifest =
        FLAGS_track_and_verify_wals_in_manifest;


    if (FLAGS_readonly && FLAGS_transaction_db) {
      fprintf(stderr, "Cannot use readonly flag with transaction_db\n");
      exit(1);
    }
    if (FLAGS_use_secondary_db &&
        (FLAGS_transaction_db || FLAGS_optimistic_transaction_db)) {
      fprintf(stderr, "Cannot use use_secondary_db flag with transaction_db\n");
      exit(1);
    }
    options.memtable_protection_bytes_per_key =
        FLAGS_memtable_protection_bytes_per_key;
    options.block_protection_bytes_per_key =
        FLAGS_block_protection_bytes_per_key;
  }

  void InitializeOptionsGeneral(rocksdb::Options* opts) {
    // Be careful about what is set here to avoid accidentally overwriting
    // settings already configured by OPTIONS file. Only configure settings that
    // are needed for the benchmark to run, settings for shared objects that
    // were not configured already, settings that require dynamically invoking
    // APIs, and settings for the benchmark itself.
    rocksdb::Options& options = *opts;

    // Always set these since they are harmless when not needed and prevent
    // a guaranteed failure when they are needed.
    options.create_missing_column_families = true;
    options.create_if_missing = true;

    if (options.statistics == nullptr) {
      options.statistics = dbstats;
    }

    auto table_options =
        options.table_factory->GetOptions<rocksdb::BlockBasedTableOptions>();
    if (table_options != nullptr) {
      if (FLAGS_cache_size > 0) {
        // This violates this function's rules on when to set options. But we
        // have to do it because the case of unconfigured block cache in OPTIONS
        // file is indistinguishable (it is sanitized to 32MB by this point, not
        // nullptr), and our regression tests assume this will be the shared
        // block cache, even with OPTIONS file provided.
        table_options->block_cache = cache_;
      }
      if (table_options->filter_policy == nullptr) {
        if (FLAGS_bloom_bits < 0) {
          table_options->filter_policy = rocksdb::BlockBasedTableOptions().filter_policy;
        } else if (FLAGS_bloom_bits == 0) {
          table_options->filter_policy.reset();
        } else {
          table_options->filter_policy.reset(
              FLAGS_use_ribbon_filter ? rocksdb::NewRibbonFilterPolicy(FLAGS_bloom_bits)
                                      : rocksdb::NewBloomFilterPolicy(FLAGS_bloom_bits));
        }
      }
    }

    if (options.row_cache == nullptr) {
      if (FLAGS_row_cache_size) {
        if (FLAGS_cache_numshardbits >= 1) {
          options.row_cache =
              rocksdb::NewLRUCache(FLAGS_row_cache_size, FLAGS_cache_numshardbits);
        } else {
          options.row_cache = rocksdb::NewLRUCache(FLAGS_row_cache_size);
        }
      }
    }

    if (options.env == rocksdb::Env::Default()) {
      options.env = FLAGS_env;
    }
    if (FLAGS_enable_io_prio) {
      options.env->LowerThreadPoolIOPriority(rocksdb::Env::LOW);
      options.env->LowerThreadPoolIOPriority(rocksdb::Env::HIGH);
    }
    if (FLAGS_enable_cpu_prio) {
      options.env->LowerThreadPoolCPUPriority(rocksdb::Env::LOW);
      options.env->LowerThreadPoolCPUPriority(rocksdb::Env::HIGH);
    }

    if (FLAGS_sine_write_rate) {
      FLAGS_benchmark_write_rate_limit = static_cast<uint64_t>(SineRate(0));
    }

    if (options.rate_limiter == nullptr) {
      if (FLAGS_rate_limiter_bytes_per_sec > 0) {
        options.rate_limiter.reset(rocksdb::NewGenericRateLimiter(
            FLAGS_rate_limiter_bytes_per_sec,
            FLAGS_rate_limiter_refill_period_us, 10 /* fairness */,
            // TODO: replace this with a more general FLAG for deciding
            // RateLimiter::Mode as now we also rate-limit foreground reads e.g,
            // Get()/MultiGet()
            FLAGS_rate_limit_bg_reads ? rocksdb::RateLimiter::Mode::kReadsOnly
                                      : rocksdb::RateLimiter::Mode::kWritesOnly,
            FLAGS_rate_limiter_auto_tuned));
      }
    }

    if (options.file_checksum_gen_factory == nullptr) {
    }

    options.level_capacities = {11,12,11,6};
    options.run_numbers = {1,1,1,1};
    
    OpenDb(options, FLAGS_db, &db_);
  

    if (FLAGS_use_existing_keys) {
      // Only work on single database
      assert(db_.db != nullptr);
      rocksdb::ReadOptions read_opts;  // before read_options_ initialized
      read_opts.total_order_seek = true;
      rocksdb::Iterator* iter = db_.db->NewIterator(read_opts);
      for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
        keys_.emplace_back(iter->key().ToString());
      }
      delete iter;
      FLAGS_num = keys_.size();
    }
  }

  void Open(rocksdb::Options* opts) {
    if (true) {
      InitializeOptionsFromFlags(opts);
    }

    InitializeOptionsGeneral(opts);
  }

   void OpenDb(rocksdb::Options options, const std::string& db_name,
              DBWithColumnFamilies* db) {
    uint64_t open_start = FLAGS_report_open_timing ? FLAGS_env->NowNanos() : 0;
    rocksdb::Status s;
    
    s = rocksdb::DB::Open(options, db_name, &db->db);
    
    if (FLAGS_report_open_timing) {
      std::cout << "OpenDb:     "
                << (FLAGS_env->NowNanos() - open_start) / 1000000.0
                << " milliseconds\n";
    }
    if (!s.ok()) {
      fprintf(stderr, "open error: %s\n", s.ToString().c_str());
      exit(1);
    }
  }

  enum WriteMode { RANDOM, SEQUENTIAL, UNIQUE_RANDOM };

  class KeyGenerator {
   public:
    KeyGenerator(Random64* rand, WriteMode mode, uint64_t num,
                 uint64_t /*num_per_set*/ = 64 * 1024)
        : rand_(rand), mode_(mode), num_(num), next_(0) {
      if (mode_ == UNIQUE_RANDOM) {
        // NOTE: if memory consumption of this approach becomes a concern,
        // we can either break it into pieces and only random shuffle a section
        // each time. Alternatively, use a bit map implementation
        // (https://reviews.facebook.net/differential/diff/54627/)
        values_.resize(num_);
        for (uint64_t i = 0; i < num_; ++i) {
          values_[i] = i;
        }
        RandomShuffle(values_.begin(), values_.end(),
                      static_cast<uint32_t>(*seed_base));
      }
    }

    uint64_t Next() {
      switch (mode_) {
        case SEQUENTIAL:
          return next_++;
        case RANDOM:
          return rand_->Next() % num_;
        case UNIQUE_RANDOM:
          assert(next_ < num_);
          return values_[next_++];
      }
      assert(false);
      return std::numeric_limits<uint64_t>::max();
    }

    // Only available for UNIQUE_RANDOM mode.
    uint64_t Fetch(uint64_t index) {
      assert(mode_ == UNIQUE_RANDOM);
      assert(index < values_.size());
      return values_[index];
    }

   private:
    Random64* rand_;
    WriteMode mode_;
    const uint64_t num_;
    uint64_t next_;
    std::vector<uint64_t> values_;
  };

  rocksdb::DB* SelectDB(ThreadState* thread) { return SelectDBWithCfh(thread)->db; }

  DBWithColumnFamilies* SelectDBWithCfh(ThreadState* thread) {
    return SelectDBWithCfh(thread->rand.Next());
  }

  DBWithColumnFamilies* SelectDBWithCfh(uint64_t rand_int) {
    if (db_.db != nullptr) {
      return &db_;
    } else {
      return &multi_dbs_[rand_int % multi_dbs_.size()];
    }
  }

  double SineRate(double x) {
    return FLAGS_sine_a * sin((FLAGS_sine_b * x) + FLAGS_sine_c) + FLAGS_sine_d;
  }


  int64_t GetRandomKey(Random64* rand) {
    uint64_t rand_int = rand->Next();
    int64_t key_rand;
    if (read_random_exp_range_ == 0) {
      key_rand = rand_int % FLAGS_num;
    } else {
      const uint64_t kBigInt = static_cast<uint64_t>(1U) << 62;
      long double order = -static_cast<long double>(rand_int % kBigInt) /
                          static_cast<long double>(kBigInt) *
                          read_random_exp_range_;
      long double exp_ran = std::exp(order);
      uint64_t rand_num =
          static_cast<int64_t>(exp_ran * static_cast<long double>(FLAGS_num));
      // Map to a different number to avoid locality.
      const uint64_t kBigPrime = 0x5bd1e995;
      // Overflow is like %(2^64). Will have little impact of results.
      key_rand = static_cast<int64_t>((rand_num * kBigPrime) % FLAGS_num);
    }
    return key_rand;
  }





  // The inverse function of Pareto distribution
  int64_t ParetoCdfInversion(double u, double theta, double k, double sigma) {
    double ret;
    if (k == 0.0) {
      ret = theta - sigma * std::log(u);
    } else {
      ret = theta + sigma * (std::pow(u, -1 * k) - 1) / k;
    }
    return static_cast<int64_t>(ceil(ret));
  }
  
  // The inverse function of power distribution (y=ax^b)
  int64_t PowerCdfInversion(double u, double a, double b) {
    double ret;
    ret = std::pow((u / a), (1 / b));
    return static_cast<int64_t>(ceil(ret));
  }

  // Add the noice to the QPS
  double AddNoise(double origin, double noise_ratio) {
    if (noise_ratio < 0.0 || noise_ratio > 1.0) {
      return origin;
    }
    int band_int = static_cast<int>(FLAGS_sine_a);
    double delta = (rand() % band_int - band_int / 2) * noise_ratio;
    if (origin + delta < 0) {
      return origin;
    } else {
      return (origin + delta);
    }
  }

  // Decide the ratio of different query types
  // 0 Get, 1 Put, 2 Seek, 3 SeekForPrev, 4 Delete, 5 SingleDelete, 6 merge
  class QueryDecider {
   public:
    std::vector<int> type_;
    std::vector<double> ratio_;
    int range_;

    QueryDecider() = default;
    ~QueryDecider() = default;

    rocksdb::Status Initiate(std::vector<double> ratio_input) {
      int range_max = 1000;
      double sum = 0.0;
      for (auto& ratio : ratio_input) {
        sum += ratio;
      }
      range_ = 0;
      for (auto& ratio : ratio_input) {
        range_ += static_cast<int>(ceil(range_max * (ratio / sum)));
        type_.push_back(range_);
        ratio_.push_back(ratio / sum);
      }
      return rocksdb::Status::OK();
    }

    int GetType(int64_t rand_num) {
      if (rand_num < 0) {
        rand_num = rand_num * (-1);
      }
      assert(range_ != 0);
      int pos = static_cast<int>(rand_num % range_);
      for (int i = 0; i < static_cast<int>(type_.size()); i++) {
        if (pos < type_[i]) {
          return i;
        }
      }
      return 0;
    }
  };

  // KeyrangeUnit is the struct of a keyrange. It is used in a keyrange vector
  // to transfer a random value to one keyrange based on the hotness.
  struct KeyrangeUnit {
    int64_t keyrange_start;
    int64_t keyrange_access;
    int64_t keyrange_keys;
  };

  // From our observations, the prefix hotness (key-range hotness) follows
  // the two-term-exponential distribution: f(x) = a*exp(b*x) + c*exp(d*x).
  // However, we cannot directly use the inverse function to decide a
  // key-range from a random distribution. To achieve it, we create a list of
  // KeyrangeUnit, each KeyrangeUnit occupies a range of integers whose size is
  // decided based on the hotness of the key-range. When a random value is
  // generated based on uniform distribution, we map it to the KeyrangeUnit Vec
  // and one KeyrangeUnit is selected. The probability of a  KeyrangeUnit being
  // selected is the same as the hotness of this KeyrangeUnit. After that, the
  // key can be randomly allocated to the key-range of this KeyrangeUnit, or we
  // can based on the power distribution (y=ax^b) to generate the offset of
  // the key in the selected key-range. In this way, we generate the keyID
  // based on the hotness of the prefix and also the key hotness distribution.
  class GenerateTwoTermExpKeys {
   public:
    // Avoid uninitialized warning-as-error in some compilers
    int64_t keyrange_rand_max_ = 0;
    int64_t keyrange_size_ = 0;
    int64_t keyrange_num_ = 0;
    std::vector<KeyrangeUnit> keyrange_set_;

    // Initiate the KeyrangeUnit vector and calculate the size of each
    // KeyrangeUnit.
    rocksdb::Status InitiateExpDistribution(int64_t total_keys, double prefix_a,
                                   double prefix_b, double prefix_c,
                                   double prefix_d) {
      int64_t amplify = 0;
      int64_t keyrange_start = 0;
      if (FLAGS_keyrange_num <= 0) {
        keyrange_num_ = 1;
      } else {
        keyrange_num_ = FLAGS_keyrange_num;
      }
      keyrange_size_ = total_keys / keyrange_num_;

      // Calculate the key-range shares size based on the input parameters
      for (int64_t pfx = keyrange_num_; pfx >= 1; pfx--) {
        // Step 1. Calculate the probability that this key range will be
        // accessed in a query. It is based on the two-term expoential
        // distribution
        double keyrange_p = prefix_a * std::exp(prefix_b * pfx) +
                            prefix_c * std::exp(prefix_d * pfx);
        if (keyrange_p < std::pow(10.0, -16.0)) {
          keyrange_p = 0.0;
        }
        // Step 2. Calculate the amplify
        // In order to allocate a query to a key-range based on the random
        // number generated for this query, we need to extend the probability
        // of each key range from [0,1] to [0, amplify]. Amplify is calculated
        // by 1/(smallest key-range probability). In this way, we ensure that
        // all key-ranges are assigned with an Integer that  >=0
        if (amplify == 0 && keyrange_p > 0) {
          amplify = static_cast<int64_t>(std::floor(1 / keyrange_p)) + 1;
        }

        // Step 3. For each key-range, we calculate its position in the
        // [0, amplify] range, including the start, the size (keyrange_access)
        KeyrangeUnit p_unit;
        p_unit.keyrange_start = keyrange_start;
        if (0.0 >= keyrange_p) {
          p_unit.keyrange_access = 0;
        } else {
          p_unit.keyrange_access =
              static_cast<int64_t>(std::floor(amplify * keyrange_p));
        }
        p_unit.keyrange_keys = keyrange_size_;
        keyrange_set_.push_back(p_unit);
        keyrange_start += p_unit.keyrange_access;
      }
      keyrange_rand_max_ = keyrange_start;

      // Step 4. Shuffle the key-ranges randomly
      // Since the access probability is calculated from small to large,
      // If we do not re-allocate them, hot key-ranges are always at the end
      // and cold key-ranges are at the begin of the key space. Therefore, the
      // key-ranges are shuffled and the rand seed is only decide by the
      // key-range hotness distribution. With the same distribution parameters
      // the shuffle results are the same.
      Random64 rand_loca(keyrange_rand_max_);
      for (int64_t i = 0; i < FLAGS_keyrange_num; i++) {
        int64_t pos = rand_loca.Next() % FLAGS_keyrange_num;
        assert(i >= 0 && i < static_cast<int64_t>(keyrange_set_.size()) &&
               pos >= 0 && pos < static_cast<int64_t>(keyrange_set_.size()));
        std::swap(keyrange_set_[i], keyrange_set_[pos]);
      }

      // Step 5. Recalculate the prefix start postion after shuffling
      int64_t offset = 0;
      for (auto& p_unit : keyrange_set_) {
        p_unit.keyrange_start = offset;
        offset += p_unit.keyrange_access;
      }

      return rocksdb::Status::OK();
    }

    // Generate the Key ID according to the input ini_rand and key distribution
    int64_t DistGetKeyID(int64_t ini_rand, double key_dist_a,
                         double key_dist_b) {
      int64_t keyrange_rand = ini_rand % keyrange_rand_max_;

      // Calculate and select one key-range that contains the new key
      int64_t start = 0, end = static_cast<int64_t>(keyrange_set_.size());
      while (start + 1 < end) {
        int64_t mid = start + (end - start) / 2;
        assert(mid >= 0 && mid < static_cast<int64_t>(keyrange_set_.size()));
        if (keyrange_rand < keyrange_set_[mid].keyrange_start) {
          end = mid;
        } else {
          start = mid;
        }
      }
      int64_t keyrange_id = start;

      // Select one key in the key-range and compose the keyID
      int64_t key_offset = 0, key_seed;
      if (key_dist_a == 0.0 || key_dist_b == 0.0) {
        key_offset = ini_rand % keyrange_size_;
      } else {
        double u =
            static_cast<double>(ini_rand % keyrange_size_) / keyrange_size_;
        key_seed = static_cast<int64_t>(
            ceil(std::pow((u / key_dist_a), (1 / key_dist_b))));
        Random64 rand_key(key_seed);
        key_offset = rand_key.Next() % keyrange_size_;
      }
      return keyrange_size_ * keyrange_id + key_offset;
    }
  };

  // The social graph workload mixed with Get, Put, Iterator queries.
  // The value size and iterator length follow Pareto distribution.
  // The overall key access follow power distribution. If user models the
  // workload based on different key-ranges (or different prefixes), user
  // can use two-term-exponential distribution to fit the workload. User
  // needs to decide the ratio between Get, Put, Iterator queries before
  // starting the benchmark.
  void MixGraph(ThreadState* thread) {
    int64_t gets = 0;
    int64_t puts = 0;
    int64_t get_found = 0;
    int64_t seek = 0;
    int64_t seek_found = 0;
    int64_t bytes = 0;
    double total_scan_length = 0;
    double total_val_size = 0;
    const int64_t default_value_max = 1 * 1024 * 1024;
    int64_t value_max = default_value_max;
    int64_t scan_len_max = FLAGS_mix_max_scan_len;
    double write_rate = 1000000.0;
    double read_rate = 1000000.0;
    bool use_prefix_modeling = false;
    bool use_random_modeling = false;
    GenerateTwoTermExpKeys gen_exp;
    std::vector<double> ratio{FLAGS_mix_get_ratio, FLAGS_mix_put_ratio,
                              FLAGS_mix_seek_ratio};
    char value_buffer[default_value_max];
    QueryDecider query;
    RandomGenerator gen;
    rocksdb::Status s;
    if (value_max > FLAGS_mix_max_value_size) {
      value_max = FLAGS_mix_max_value_size;
    }

    std::unique_ptr<const char[]> key_guard;
    rocksdb::Slice key = AllocateKey(&key_guard);
    rocksdb::PinnableSlice pinnable_val;
    query.Initiate(ratio);

    // the limit of qps initiation
    if (FLAGS_sine_mix_rate) {
      thread->shared->read_rate_limiter.reset(
          rocksdb::NewGenericRateLimiter(static_cast<int64_t>(read_rate)));
      thread->shared->write_rate_limiter.reset(
          rocksdb::NewGenericRateLimiter(static_cast<int64_t>(write_rate)));
    }

    // Decide if user wants to use prefix based key generation
    if (FLAGS_keyrange_dist_a != 0.0 || FLAGS_keyrange_dist_b != 0.0 ||
        FLAGS_keyrange_dist_c != 0.0 || FLAGS_keyrange_dist_d != 0.0) {
      use_prefix_modeling = true;
      gen_exp.InitiateExpDistribution(
          FLAGS_num, FLAGS_keyrange_dist_a, FLAGS_keyrange_dist_b,
          FLAGS_keyrange_dist_c, FLAGS_keyrange_dist_d);
    }
    if (FLAGS_key_dist_a == 0 || FLAGS_key_dist_b == 0) {
      use_random_modeling = true;
    }

    Duration duration(FLAGS_duration, reads_);
    while (!duration.Done(1)) {
      DBWithColumnFamilies* db_with_cfh = SelectDBWithCfh(thread);
      int64_t ini_rand, rand_v, key_rand, key_seed;
      ini_rand = GetRandomKey(&thread->rand);
      rand_v = ini_rand % FLAGS_num;
      double u = static_cast<double>(rand_v) / FLAGS_num;

      // Generate the keyID based on the key hotness and prefix hotness
      if (use_random_modeling) {
        key_rand = ini_rand;
      } else if (use_prefix_modeling) {
        key_rand =
            gen_exp.DistGetKeyID(ini_rand, FLAGS_key_dist_a, FLAGS_key_dist_b);
      } else {
        key_seed = PowerCdfInversion(u, FLAGS_key_dist_a, FLAGS_key_dist_b);
        Random64 rand(key_seed);
        key_rand = static_cast<int64_t>(rand.Next()) % FLAGS_num;
      }
      GenerateKeyFromInt(key_rand, FLAGS_num, &key);
      int query_type = query.GetType(rand_v);

      // change the qps
      uint64_t now = FLAGS_env->NowMicros();
      uint64_t usecs_since_last;
      if (now > thread->stats.GetSineInterval()) {
        usecs_since_last = now - thread->stats.GetSineInterval();
      } else {
        usecs_since_last = 0;
      }

      if (FLAGS_sine_mix_rate &&
          usecs_since_last >
              (FLAGS_sine_mix_rate_interval_milliseconds * uint64_t{1000})) {
        double usecs_since_start =
            static_cast<double>(now - thread->stats.GetStart());
        thread->stats.ResetSineInterval();
        double mix_rate_with_noise = AddNoise(
            SineRate(usecs_since_start / 1000000.0), FLAGS_sine_mix_rate_noise);
        read_rate = mix_rate_with_noise * (query.ratio_[0] + query.ratio_[2]);
        write_rate = mix_rate_with_noise * query.ratio_[1];

        if (read_rate > 0) {
          thread->shared->read_rate_limiter->SetBytesPerSecond(
              static_cast<int64_t>(read_rate));
        }
        if (write_rate > 0) {
          thread->shared->write_rate_limiter->SetBytesPerSecond(
              static_cast<int64_t>(write_rate));
        }
      }
      // Start the query
      if (query_type == 0) {
        // the Get query
        gets++;
        if (FLAGS_num_column_families > 1) {
          s = db_with_cfh->db->Get(read_options_, db_with_cfh->GetCfh(key_rand),
                                   key, &pinnable_val);
        } else {
          pinnable_val.Reset();
          s = db_with_cfh->db->Get(read_options_,
                                   db_with_cfh->db->DefaultColumnFamily(), key,
                                   &pinnable_val);
        }

        if (s.ok()) {
          get_found++;
          bytes += key.size() + pinnable_val.size();
        } else if (!s.IsNotFound()) {
          fprintf(stderr, "Get returned an error: %s\n", s.ToString().c_str());
          abort();
        }

        if (thread->shared->read_rate_limiter && (gets + seek) % 100 == 0) {
          thread->shared->read_rate_limiter->Request(100, rocksdb::Env::IO_HIGH,
                                                     nullptr /*stats*/);
        }
        thread->stats.FinishedOps(db_with_cfh, db_with_cfh->db, 1, kRead);
      } else if (query_type == 1) {
        // the Put query
        puts++;
        int64_t val_size = ParetoCdfInversion(u, FLAGS_value_theta,
                                              FLAGS_value_k, FLAGS_value_sigma);
        if (val_size < 10) {
          val_size = 10;
        } else if (val_size > value_max) {
          val_size = val_size % value_max;
        }
        total_val_size += val_size;

        s = db_with_cfh->db->Put(
            write_options_, key,
            gen.Generate(static_cast<unsigned int>(val_size)));
        if (!s.ok()) {
          fprintf(stderr, "put error: %s\n", s.ToString().c_str());
          ErrorExit();
        }

        if (thread->shared->write_rate_limiter && puts % 100 == 0) {
          thread->shared->write_rate_limiter->Request(100, rocksdb::Env::IO_HIGH,
                                                      nullptr /*stats*/);
        }
        thread->stats.FinishedOps(db_with_cfh, db_with_cfh->db, 1, kWrite);
      } else if (query_type == 2) {
        // Seek query
        if (db_with_cfh->db != nullptr) {
          rocksdb::Iterator* single_iter = nullptr;
          single_iter = db_with_cfh->db->NewIterator(read_options_);
          if (single_iter != nullptr) {
            single_iter->Seek(key);
            seek++;
            if (single_iter->Valid() && single_iter->key().compare(key) == 0) {
              seek_found++;
            }
            int64_t scan_length =
                ParetoCdfInversion(u, FLAGS_iter_theta, FLAGS_iter_k,
                                   FLAGS_iter_sigma) %
                scan_len_max;
            for (int64_t j = 0; j < scan_length && single_iter->Valid(); j++) {
              rocksdb::Slice value = single_iter->value();
              memcpy(value_buffer, value.data(),
                     std::min(value.size(), sizeof(value_buffer)));
              bytes += single_iter->key().size() + single_iter->value().size();
              single_iter->Next();
              assert(single_iter->status().ok());
              total_scan_length++;
            }
          }
          delete single_iter;
        }
        thread->stats.FinishedOps(db_with_cfh, db_with_cfh->db, 1, kSeek);
      }
    }
    char msg[256];
    snprintf(msg, sizeof(msg),
             "( Gets:%" PRIu64 " Puts:%" PRIu64 " Seek:%" PRIu64
             ", reads %" PRIu64 " in %" PRIu64
             " found, "
             "avg size: %.1f value, %.1f scan)\n",
             gets, puts, seek, get_found + seek_found, gets + seek,
             total_val_size / puts, total_scan_length / seek);

    thread->stats.AddBytes(bytes);
    thread->stats.AddMessage(msg);
  }

};

int main(int argc, char** argv) {
  ParseCommandLineFlags(&argc, &argv, true);
  Benchmark benchmark;
  benchmark.Run();
  return 0;
}

