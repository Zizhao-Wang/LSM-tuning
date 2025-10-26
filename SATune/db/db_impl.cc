// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/db_impl.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <cmath>
#include <sstream>
#include <iomanip>  // 包含 setprecision
#include <set>
#include <string>
#include <chrono>
#include <vector>
#include <inttypes.h>
#include <atomic>


#include "db/builder.h"
#include "db/db_iter.h"
#include "db/dbformat.h"
#include "db/filename.h"
#include "db/log_reader.h"
#include "db/log_writer.h"
#include "db/memtable.h"
#include "db/table_cache.h"
#include "db/version_set.h"
#include "db/write_batch_internal.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/status.h"
#include "leveldb/table.h"
#include "leveldb/table_builder.h"
#include "port/port.h"
#include "table/block.h"
#include "table/merger.h"
#include "table/two_level_iterator.h"
#include "util/coding.h"
#include "util/logging.h"
#include "util/mutexlock.h"
#include "leveldb/performance_profile.h"
#include "tuning_framework/auto_tuner.h"



namespace leveldb {

const int kNumNonTableCacheFiles = 10;
// #define zipf_hc_compaction_monitor

// Information kept for every waiting writer
struct DBImpl::Writer {
  explicit Writer(port::Mutex* mu)
      : batch(nullptr), sync(false), done(false), cv(mu) {}

  Status status;
  WriteBatch* batch;
  bool sync;
  bool done;
  port::CondVar cv;
};

struct DBImpl::CompactionState {
  // Files produced by compaction
  struct Output {
    uint64_t number;
    uint64_t file_size;
    InternalKey smallest, largest;
  };

  Output* current_output() { return &outputs[outputs.size() - 1]; }

  explicit CompactionState(Compaction* c)
      : compaction(c),
        smallest_snapshot(0),
        outfile(nullptr),
        builder(nullptr),
        total_bytes(0) {}

  Compaction* const compaction;

  // Sequence numbers < smallest_snapshot are not significant since we
  // will never have to service a snapshot below smallest_snapshot.
  // Therefore if we have seen a sequence number S <= smallest_snapshot,
  // we can drop all entries for the same key with sequence numbers < S.
  SequenceNumber smallest_snapshot;

  std::vector<Output> outputs;

  // State kept for output being generated
  WritableFile* outfile;
  TableBuilder* builder;

  uint64_t total_bytes;
};

// Fix user-supplied options to be reasonable
template <class T, class V>
static void ClipToRange(T* ptr, V minvalue, V maxvalue) {
  if (static_cast<V>(*ptr) > maxvalue) *ptr = maxvalue;
  if (static_cast<V>(*ptr) < minvalue) *ptr = minvalue;
}
Options SanitizeOptions(const std::string& dbname,
                        const InternalKeyComparator* icmp,
                        const InternalFilterPolicy* ipolicy,
                        const Options& src) {
  Options result = src;
  result.comparator = icmp;
  result.filter_policy = (src.filter_policy != nullptr) ? ipolicy : nullptr;
  ClipToRange(&result.max_open_files, 64 + kNumNonTableCacheFiles, 50000);
  ClipToRange(&result.write_buffer_size, 64 << 10, 1 << 30);
  ClipToRange(&result.max_file_size, 1 << 20, 1 << 30);
  ClipToRange(&result.block_size, 1 << 10, 4 << 20);

  if (result.info_log == nullptr) {
    // Open a log file in the same directory as the db
    src.env->CreateDir(dbname);  // In case it does not exist
    src.env->RenameFile(InfoLogFileName(dbname), OldInfoLogFileName(dbname));
    Status s = src.env->NewLogger(InfoLogFileName(dbname), &result.info_log);
    if (!s.ok()) {
      // No place suitable for logging
      result.info_log = nullptr;
    }
  }

  if (result.version_debug_log_ == nullptr) {
    src.env->RenameFile(VersionDebugLogFileName(dbname), OldVersionDebugLogFileName(dbname));
    Status s = src.env->NewLogger(VersionDebugLogFileName(dbname), &result.version_debug_log_);
    if (!s.ok()) {
      // No place suitable for logging
      result.version_debug_log_ = nullptr;
    }
  }
  
  if (result.level_compaction_log_ == nullptr) {
    Status s = src.env->NewLogger(src.level_compaction_log_filename, &result.level_compaction_log_);
    if (!s.ok()) {
      // No place suitable for logging
      result.level_compaction_log_ = nullptr;
    }
  }

  if (result.tuning_log_ == nullptr) {

    std::string log_dir = src.custom_tuning_log_dir.empty() ? dbname : src.custom_tuning_log_dir;

    src.env->RenameFile(TuningLogFileName(log_dir), OldTuningLogFileName(log_dir));
    Status s = src.env->NewLogger(TuningLogFileName(log_dir), &result.tuning_log_);

    if (!s.ok()) {
      // No place suitable for logging
      result.tuning_log_ = nullptr;
    }
  }

  if (result.block_cache == nullptr) {
    result.block_cache = NewLRUCache(8 << 20);
  }
  return result;
}

static int TableCacheSize(const Options& sanitized_options) {
  // Reserve ten files or so for other uses and give the rest to TableCache.
  return sanitized_options.max_open_files - kNumNonTableCacheFiles;
}

DBImpl::DBImpl(const Options& raw_options, const std::string& dbname)
    : env_(raw_options.env),
      internal_comparator_(raw_options.comparator),
      internal_filter_policy_(raw_options.filter_policy),
      options_(SanitizeOptions(dbname, &internal_comparator_,
                               &internal_filter_policy_, raw_options)),
      compaction_opts_atomic_(options_.compaction_opts),
      owns_info_log_(options_.info_log != raw_options.info_log),
      owns_cache_(options_.block_cache != raw_options.block_cache),
      compaction_count_(0),
      dbname_(dbname),
      hot_file_path(raw_options.hot_file_path),
      percentagesStr(raw_options.percentages),
      table_cache_(new TableCache(dbname_, options_, TableCacheSize(options_))),
      db_lock_(nullptr),
      shutting_down_(false),
      background_work_finished_signal_(&mutex_),
      flush_work_finished_signal_(&mutex_),
      imms_not_empty_cv_(&imms_mutex_),
      compaction_cv_(&metadata_mutex_),
      shutdown_cv_(&mutex_),
      mem_(nullptr),
      workload_monitor_(0.05, 1000, 5.0),
      performance_monitor_(&leveldb::PerformanceProfiler::GlobalInstance()),
      imm_(nullptr),
      current_stats_ptr_(nullptr),
      has_imm_(false),
      logfile_(nullptr),
      logfile_number_(0),
      c0_adaptation_enabled_(raw_options.is_start_c0_tuning),
      log_(nullptr),
      seed_(0),
      tmp_batch_(new WriteBatch),
      background_compaction_scheduled_(false),
      background_flush_scheduled_(false),
      manual_compaction_(nullptr),
      versions_(new VersionSet(dbname_, &options_, table_cache_,
                               &internal_comparator_, &compaction_opts_atomic_,&log_and_apply_stats_)) {}

DBImpl::~DBImpl() {

  // Wait for background work to finish.
  mutex_.Lock();
  shutting_down_.store(true, std::memory_order_release);

  {
    MutexLock l(&imms_mutex_);
    imms_not_empty_cv_.SignalAll(); 
  }

  {
    MutexLock l(&metadata_mutex_);
    compaction_cv_.SignalAll();      
  }

  while (running_background_threads_.load() > 0) {
    shutdown_cv_.Wait();
  }

  mutex_.Unlock();

  if (db_lock_ != nullptr) {
    env_->UnlockFile(db_lock_);
  }

  delete versions_;
  if (mem_ != nullptr) mem_->Unref();
  if (imm_ != nullptr) imm_->Unref();
  delete tmp_batch_;
  delete log_;
  delete logfile_;
  delete table_cache_;

  if (owns_info_log_) {
    delete options_.info_log;
  }

  if (options_.version_debug_log_ != nullptr) {
    delete options_.version_debug_log_;
  }
  
  if (options_.level_compaction_log_ != nullptr) {
    delete options_.level_compaction_log_;
  }


  if (owns_cache_) {
    delete options_.block_cache;
  }
}

Status DBImpl::NewDB() {
  VersionEdit new_db;
  new_db.SetComparatorName(user_comparator()->Name());
  new_db.SetLogNumber(0);
  new_db.SetNextFile(2);
  new_db.SetLastSequence(0);

  const std::string manifest = DescriptorFileName(dbname_, 1);
  WritableFile* file;
  Status s = env_->NewWritableFile(manifest, &file);
  if (!s.ok()) {
    return s;
  }
  {
    log::Writer log(file);
    std::string record;
    new_db.EncodeTo(&record);
    s = log.AddRecord(record);
    if (s.ok()) {
      s = file->Sync();
    }
    if (s.ok()) {
      s = file->Close();
    }
  }
  delete file;
  if (s.ok()) {
    // Make "CURRENT" file that points to the new manifest file.
    s = SetCurrentFile(env_, dbname_, 1);
  } else {
    env_->RemoveFile(manifest);
  }
  return s;
}

void DBImpl::MaybeIgnoreError(Status* s) const {
  if (s->ok() || options_.paranoid_checks) {
    // No change needed
  } else {
    Log(options_.info_log, "Ignoring error %s", s->ToString().c_str());
    *s = Status::OK();
  }
}

// This is a specialized version used by compactMemtable, designed to be called
// only by the flush thread (CompactMemTable). Its only job is to delete
// obsolete WAL log files. It MUST NOT delete any table files.
void DBImpl::RemoveObsoleteLogFiles() {

  metadata_mutex_.AssertHeld();

  if (!bg_error_.ok()) {
    // After a background error, we don't know whether a new version may
    // or may not have been committed, so we cannot safely garbage collect.
    return;
  }

  std::vector<std::string> filenames;
  env_->GetChildren(dbname_, &filenames);  // Ignoring errors on purpose
  uint64_t number;
  FileType type;
  std::vector<std::string> files_to_delete;

  for (std::string& filename : filenames) {
    if (ParseFileName(filename, &number, &type)) {
      bool keep = true;
      switch (type) {
        case kLogFile:
          // This is the only file type we are interested in deleting.
          // Keep the current log and the previous log (if any).
          keep = ((number >= versions_->LogNumber()) || (number == versions_->PrevLogNumber()));
          break;
        // For all other file types, we KEEP them. This function must not
        // touch Table files, Manifests, etc.
        default:
          keep = true;
          break;
      }

      if (!keep) {
        files_to_delete.push_back(std::move(filename));
        Log(options_.info_log, "[Flush Thread] Delete type=%d #%lld\n",static_cast<int>(type), static_cast<unsigned long long>(number));
      }
    }
  }

  // While deleting all files unblock other threads.
  metadata_mutex_.Unlock();
  for (const std::string& filename : files_to_delete) {
    env_->RemoveFile(dbname_ + "/" + filename);
  }
  metadata_mutex_.Lock();
}

// This is a specialized cleanup function for the compaction thread.
// Its only job is to delete the specific input SSTable files for a given
// compaction task. It gets the list of inputs directly from the Compaction object.
void DBImpl::RemoveCompactionInputFiles(const Compaction* c) {
  metadata_mutex_.AssertHeld();

  if (!bg_error_.ok()) {
    return; // Do not clean up if there was a background error.
  }

  // Collect the file numbers of all input files for this compaction.
  std::vector<uint64_t> level_files;
  for (size_t i = 0; i < c->num_input_files(0); i++) {
    level_files.push_back(c->input(0, i)->number);
  }

  std::vector<uint64_t> level_plus_1_files;
  for (size_t i = 0; i < c->num_input_files(1); i++) {
    level_plus_1_files.push_back(c->input(1, i)->number);
  }

  // Unlock the mutex while performing slow file I/O.
  metadata_mutex_.Unlock();

  const int level = c->level();
  for (size_t i = 0; i < level_files.size(); i++) {
    const uint64_t file_number = level_files[i];
    table_cache_->Evict(file_number);
    std::string filename = TableFileName(options_, dbname_, file_number, level);
    env_->RemoveFile(filename);
    Log(options_.info_log,"[Compaction Thread] Deleted input file #%llu from level %d\n",
      static_cast<unsigned long long>(file_number), level);
  }

  const int level_plus_1 = c->level() + 1;
  for (size_t i = 0; i < level_plus_1_files.size(); i++) {
    const uint64_t file_number = level_plus_1_files[i];
    table_cache_->Evict(file_number);
    std::string filename = TableFileName(options_, dbname_, file_number, level_plus_1);
    env_->RemoveFile(filename);
    Log(options_.info_log,"[Compaction Thread] Deleted input file #%llu from level %d\n",
      static_cast<unsigned long long>(file_number), level_plus_1);
  }

  metadata_mutex_.Lock();
}


void DBImpl::RemoveObsoleteFiles2() {
  metadata_mutex_.AssertHeld();

  if (!bg_error_.ok()) {
    // After a background error, we don't know whether a new version may
    // or may not have been committed, so we cannot safely garbage collect.
    return;
  }

  // Make a set of all of the live files
  std::set<uint64_t> live = pending_outputs_;
  versions_->AddLiveFiles(&live);

  std::vector<std::string> filenames;
  env_->GetChildren(dbname_, &filenames);  // Ignoring errors on purpose
  uint64_t number;
  FileType type;
  std::vector<std::string> files_to_delete;
  for (std::string& filename : filenames) {
    if (ParseFileName(filename, &number, &type)) {
      bool keep = true;
      switch (type) {
        case kLogFile:
          keep = ((number >= versions_->LogNumber()) ||
                  (number == versions_->PrevLogNumber()));
          break;
        case kDescriptorFile:
          // Keep my manifest file, and any newer incarnations'
          // (in case there is a race that allows other incarnations)
          keep = (number >= versions_->ManifestFileNumber());
          break;
        case kTableFile:
          keep = (live.find(number) != live.end());
          break;
        case kTempFile:
          // Any temp files that are currently being written to must
          // be recorded in pending_outputs_, which is inserted into "live"
          keep = (live.find(number) != live.end());
          break;
        case kCurrentFile:
        case kDBLockFile:
        case kInfoLogFile:
          keep = true;
          break;
      }

      if (!keep) {
        files_to_delete.push_back(std::move(filename));
        if (type == kTableFile) {
          table_cache_->Evict(number);
        }
        Log(options_.info_log, "Delete type=%d #%lld\n", static_cast<int>(type),
            static_cast<unsigned long long>(number));
      }
    }
  }

  // While deleting all files unblock other threads. All files being deleted
  // have unique names which will not collide with newly created files and
  // are therefore safe to delete while allowing other threads to proceed.
  metadata_mutex_.Unlock();
  for (const std::string& filename : files_to_delete) {
    env_->RemoveFile(dbname_ + "/" + filename);
  }
  metadata_mutex_.Lock();
}

Status DBImpl::Recover(VersionEdit* edit, bool* save_manifest) {
  mutex_.AssertHeld();

  // Ignore error from CreateDir since the creation of the DB is
  // committed only when the descriptor is created, and this directory
  // may already exist from a previous failed creation attempt.
  env_->CreateDir(dbname_);
  assert(db_lock_ == nullptr);
  Status s = env_->LockFile(LockFileName(dbname_), &db_lock_);
  if (!s.ok()) {
    return s;
  }

  if (!env_->FileExists(CurrentFileName(dbname_))) {
    if (options_.create_if_missing) {
      Log(options_.info_log, "Creating DB %s since it was missing.",
          dbname_.c_str());
      s = NewDB();
      if (!s.ok()) {
        return s;
      }
    } else {
      return Status::InvalidArgument(
          dbname_, "does not exist (create_if_missing is false)");
    }
  } else {
    if (options_.error_if_exists) {
      return Status::InvalidArgument(dbname_,
                                     "exists (error_if_exists is true)");
    }
  }

  s = versions_->Recover(save_manifest);
  if (!s.ok()) {
    return s;
  }
  SequenceNumber max_sequence(0);

  // Recover from all newer log files than the ones named in the
  // descriptor (new log files may have been added by the previous
  // incarnation without registering them in the descriptor).
  //
  // Note that PrevLogNumber() is no longer used, but we pay
  // attention to it in case we are recovering a database
  // produced by an older version of leveldb.
  const uint64_t min_log = versions_->LogNumber();
  const uint64_t prev_log = versions_->PrevLogNumber();
  std::vector<std::string> filenames;
  s = env_->GetChildren(dbname_, &filenames);
  if (!s.ok()) {
    return s;
  }
  std::set<uint64_t> expected;
  versions_->AddLiveFiles(&expected);
  uint64_t number;
  FileType type;
  std::vector<uint64_t> logs;
  for (size_t i = 0; i < filenames.size(); i++) {
    if (ParseFileName(filenames[i], &number, &type)) {
      expected.erase(number);
      if (type == kLogFile && ((number >= min_log) || (number == prev_log)))
        logs.push_back(number);
    }
  }
  if (!expected.empty()) {
    char buf[50];
    std::snprintf(buf, sizeof(buf), "%d missing files; e.g.",
                  static_cast<int>(expected.size()));
    return Status::Corruption(buf, TableFileName(dbname_, *(expected.begin())));
  }

  // Recover in the order in which the logs were generated
  std::sort(logs.begin(), logs.end());
  for (size_t i = 0; i < logs.size(); i++) {
    s = RecoverLogFile(logs[i], (i == logs.size() - 1), save_manifest, edit,
                       &max_sequence);
    if (!s.ok()) {
      return s;
    }

    // The previous incarnation may not have written any MANIFEST
    // records after allocating this log number.  So we manually
    // update the file number allocation counter in VersionSet.
    versions_->MarkFileNumberUsed(logs[i]);
  }

  if (versions_->LastSequence() < max_sequence) {
    versions_->SetLastSequence(max_sequence);
  }

  return Status::OK();
}

Status DBImpl::RecoverLogFile(uint64_t log_number, bool last_log,
                              bool* save_manifest, VersionEdit* edit,
                              SequenceNumber* max_sequence) {
  struct LogReporter : public log::Reader::Reporter {
    Env* env;
    Logger* info_log;
    const char* fname;
    Status* status;  // null if options_.paranoid_checks==false
    void Corruption(size_t bytes, const Status& s) override {
      Log(info_log, "%s%s: dropping %d bytes; %s",
          (this->status == nullptr ? "(ignoring error) " : ""), fname,
          static_cast<int>(bytes), s.ToString().c_str());
      if (this->status != nullptr && this->status->ok()) *this->status = s;
    }
  };

  mutex_.AssertHeld();

  // Open the log file
  std::string fname = LogFileName(dbname_, log_number);
  SequentialFile* file;
  bool direct_open_log_file = options_.use_direct_reads_for_recovery;
  Status status = env_->NewSequentialFile(fname, &file, direct_open_log_file);
  if (!status.ok()) {
    MaybeIgnoreError(&status);
    return status;
  }

  // Create the log reader.
  LogReporter reporter;
  reporter.env = env_;
  reporter.info_log = options_.info_log;
  reporter.fname = fname.c_str();
  reporter.status = (options_.paranoid_checks ? &status : nullptr);
  // We intentionally make log::Reader do checksumming even if
  // paranoid_checks==false so that corruptions cause entire commits
  // to be skipped instead of propagating bad information (like overly
  // large sequence numbers).
  log::Reader reader(file, &reporter, true /*checksum*/, 0 /*initial_offset*/);
  Log(options_.info_log, "Recovering log #%llu",
      (unsigned long long)log_number);

  // Read all the records and add to a memtable
  std::string scratch;
  Slice record;
  WriteBatch batch;
  int compactions = 0;
  MemTable* mem = nullptr;
  while (reader.ReadRecord(&record, &scratch) && status.ok()) {
    if (record.size() < 12) {
      reporter.Corruption(record.size(),
                          Status::Corruption("log record too small"));
      continue;
    }
    WriteBatchInternal::SetContents(&batch, record);

    if (mem == nullptr) {
      mem = new MemTable(internal_comparator_);
      mem->Ref();
    }
    status = WriteBatchInternal::InsertInto(&batch, mem);
    MaybeIgnoreError(&status);
    if (!status.ok()) {
      break;
    }
    const SequenceNumber last_seq = WriteBatchInternal::Sequence(&batch) +
                                    WriteBatchInternal::Count(&batch) - 1;
    if (last_seq > *max_sequence) {
      *max_sequence = last_seq;
    }

    if (mem->ApproximateMemoryUsage() > options_.write_buffer_size) {
      compactions++;
      *save_manifest = true;
      status = WriteLevel0Table(mem, edit, nullptr);
      mem->Unref();
      mem = nullptr;
      if (!status.ok()) {
        // Reflect errors immediately so that conditions like full
        // file-systems cause the DB::Open() to fail.
        break;
      }
    }
  }

  delete file;

  // See if we should keep reusing the last log file.
  if (status.ok() && options_.reuse_logs && last_log && compactions == 0) {
    assert(logfile_ == nullptr);
    assert(log_ == nullptr);
    assert(mem_ == nullptr);
    uint64_t lfile_size;
    if (env_->GetFileSize(fname, &lfile_size).ok() &&
        env_->NewAppendableFile(fname, &logfile_).ok()) {
      Log(options_.info_log, "Reusing old log %s \n", fname.c_str());
      log_ = new log::Writer(logfile_, lfile_size);
      logfile_number_ = log_number;
      if (mem != nullptr) {
        mem_ = mem;
        mem = nullptr;
      } else {
        // mem can be nullptr if lognum exists but was empty.
        mem_ = new MemTable(internal_comparator_);
        mem_->Ref();
      }
    }
  }

  if (mem != nullptr) {
    // mem did not get reused; compact it.
    if (status.ok()) {
      *save_manifest = true;
      status = WriteLevel0Table(mem, edit, nullptr);
    }
    mem->Unref();
  }

  return status;
}

Status DBImpl::WriteLevel0Table(MemTable* mem, VersionEdit* edit,
                                Version* base) {
  metadata_mutex_.AssertHeld();
  const uint64_t start_micros = env_->NowMicros();
  FileMetaData meta;
  meta.number = versions_->NewFileNumber();
  pending_outputs_.insert(meta.number);
  Iterator* iter = mem->NewIterator();
  Log(options_.info_log, "Level-0 table #%llu: started", (unsigned long long)meta.number);
  double table_variance=0.0;
  int64_t table_unique=0;
  int64_t table_total_keys=0;
  int64_t total_memtable_size=0;
  Status s;
  {
    metadata_mutex_.Unlock();
    
    bool should_sample = compaction_opts_atomic_.ShouldSampleFlushTime();
    uint64_t flush_start = 0;
    if (should_sample) {
      flush_start = env_->NowMicros();
    }

    // auto start = std::chrono::high_resolution_clock::now();
    if(options_.merge_versions_on_flush){
      s = BuildTableWithVarianceWiMerge(dbname_, env_, options_, table_cache_, iter, &meta, &table_variance, &table_unique, &table_total_keys, &total_memtable_size);
    }else{
      s = BuildTableWithVarianceWoMerge(dbname_, env_, options_, table_cache_, iter, &meta, &table_variance, &table_unique, &table_total_keys);
    }
    // s = BuildTable2(dbname_, env_, options_, table_cache_, iter, &meta, &table_variance, &table_unique, &table_total_keys, &total_memtable_size);
    // auto stop = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
    if (should_sample) {
      compaction_opts_atomic_.RecordFlushTime(env_->NowMicros() - flush_start);
    }
    // l0_time += duration.count();
    // fprintf (stderr,"BuildTable2 execution time: %.3fs \n", duration.count()/1e6);

    metadata_mutex_.Lock();
  }

  Log(options_.info_log, "Level-0 table #%llu: %lld bytes %s",(unsigned long long)meta.number, (unsigned long long)meta.file_size, s.ToString().c_str());
  delete iter;
  pending_outputs_.erase(meta.number);

  // Note that if file_size is zero, the file has been deleted and
  // should not be added to the manifest.

  int level = 0;
  if (s.ok() && meta.file_size > 0) {
    const Slice min_user_key = meta.smallest.user_key();
    const Slice max_user_key = meta.largest.user_key();
    // if (base != nullptr) {
    //   level = base->PickLevelForMemTableOutput(min_user_key, max_user_key);
    // }
    edit->AddFile(level, meta.number, meta.file_size, meta.smallest,
                  meta.largest);
  }


  if(level==0){
    if(current_stats_ptr_ == nullptr){
      current_stats_ptr_ = &l0variancestats;
    } 
    current_stats_ptr_->table_number++;
    current_stats_ptr_->variance = current_stats_ptr_->variance+table_variance;
    current_stats_ptr_->unique_keys += table_unique;
    current_stats_ptr_->total_keys += table_total_keys;
    current_stats_ptr_->average_kvs_in_each_file = current_stats_ptr_->average_kvs_in_each_file==0 ? table_unique : (current_stats_ptr_->average_kvs_in_each_file+table_unique)/2;
    // fprintf(stderr, "The %lu-th Table statistics in level %d:\n"
    //   " - Table variance: %.3f\n"
    //   " - Accumulated variance: %.3f\n"
    //   " - Average variance (current table & accumulated): %.3f\n"
    //   " - Average variance (all tables): %.3f\n"
    //   " - Unique keys this table: %lu, Total keys this table: %lu\n"
    //   " - Accumulated unique keys: %lu, Accumulated total keys: %lu\n",
    //   current_stats_ptr_->table_number, level,table_variance,current_stats_ptr_->variance,(table_variance + current_stats_ptr_->variance) / 2.0,
    //   current_stats_ptr_->variance / (double)current_stats_ptr_->table_number, table_unique, table_total_keys,
    //   current_stats_ptr_->unique_keys, current_stats_ptr_->total_keys);
  }
  
  CompactionStats stats;
  stats.micros = env_->NowMicros() - start_micros;
  stats.bytes_written = meta.file_size;
  stats_[level].Add(stats);

  flush_stats_.total_flush_count++;
  flush_stats_.total_bytes_flushed += meta.file_size;
  flush_stats_.actual_bytes_written += meta.file_size;
  flush_stats_.user_bytes_written += total_memtable_size;

  TableStats ts;
  ts.mu = static_cast<double>(table_total_keys) / table_unique;
  ts.var = table_variance;
  ts.n = table_unique; 
  flush_stats_.recent_tables_.push_back(ts);

  // newly added source codes
  // new_LeveldataStats new_stats;
  // new_stats.micros = env_->NowMicros() - start_micros;
  // new_stats.bytes_written = meta.file_size;
  // new_stats.user_bytes_written = meta.file_size;
  // level_stats_[level].Add(new_stats);

  return s;
}

void DBImpl::CompactMemTable() {
  mutex_.AssertHeld();
  assert(imm_ != nullptr);

  // Save the contents of the memtable as a new Table
  VersionEdit edit;
  Version* base = versions_->current();
  base->Ref();
  mem_version_ref_count.fetch_add(1);
  Status s = WriteLevel0Table(imm_, &edit, base);
  base->Unref();
  mem_version_unref_count.fetch_add(1);

  if (s.ok() && shutting_down_.load(std::memory_order_acquire)) {
    s = Status::IOError("Deleting DB during memtable compaction");
  }

  // Replace immutable memtable with the generated Table
  if (s.ok()) {
    edit.SetPrevLogNumber(0);
    edit.SetLogNumber(logfile_number_);  // Earlier logs no longer needed
    s = versions_->LogAndApply(&edit, &mutex_);
  }

  if (s.ok()) {
    // Commit to the new state
    imm_->Unref();
    imm_ = nullptr;
    has_imm_.store(false, std::memory_order_release);
    // RemoveObsoleteFiles();
  } else {
    RecordBackgroundError(s);
  }
  
  L0TuningSystemState current_tuning_state = l0_tuning_state_.load(std::memory_order_acquire);
  if(versions_->IsL0NeedsCompaction() && current_tuning_state == L0TuningSystemState::IDLE){
    // MaybeAdjustC0();
  }
}


void DBImpl::CompactMemTable(MemTable* mem_to_deal) {
  MutexLock l(&metadata_mutex_);
  assert(mem_to_deal != nullptr);
  // Save the contents of the memtable as a new Table
  VersionEdit edit;
  Version* base = versions_->current();
  base->Ref();
  mem_version_ref_count.fetch_add(1);
  Status s = WriteLevel0Table(mem_to_deal, &edit, base);
  base->Unref();
  mem_version_unref_count.fetch_add(1);

  if (s.ok() && shutting_down_.load(std::memory_order_acquire)) {
    s = Status::IOError("Deleting DB during memtable compaction");
  }

  // Replace immutable memtable with the generated Table
  if (s.ok()) {
    edit.SetPrevLogNumber(0);
    edit.SetLogNumber(logfile_number_);  // Earlier logs no longer needed
    s = versions_->LogAndApply(&edit, &metadata_mutex_, CallerView::kFlush);
  }

  if (s.ok()) {
    // Commit to the new state
    // imm_->Unref();
    // imm_ = nullptr;
    // has_imm_.store(false, std::memory_order_release);
    RemoveObsoleteLogFiles();
    num_level0_files_.store(versions_->NumLevelFiles(0), std::memory_order_relaxed);
  } else {
    RecordBackgroundError(s);
  }
  
  L0TuningSystemState current_tuning_state = l0_tuning_state_.load(std::memory_order_acquire);
  if(versions_->IsL0NeedsCompaction() && current_tuning_state == L0TuningSystemState::IDLE){
    // MaybeAdjustC0();
    {
      // bool needs          = versions_->NeedsCompaction();
      // int  l0_files       = versions_->NumLevelFiles(0);
      // double comp_score   = versions_->GetCurrentCompactionScore();
      // int    comp_trigger = compaction_opts_atomic_.level0_compaction_trigger.load(std::memory_order_relaxed);
      // fprintf(stderr,
      //   "[DBG CompactMemTable] NeedsCompaction=%d, L0_files=%d, compaction_score=%.3f, "
      //   "level0_compaction_trigger=%d\n",static_cast<int>(needs),l0_files,comp_score,comp_trigger);
    }
  }
}

void DBImpl::CompactRange(const Slice* begin, const Slice* end) {
  int max_level_with_files = 1;
  {
    MutexLock l(&mutex_);
    Version* base = versions_->current();
    for (int level = 1; level < config::kNumLevels; level++) {
      if (base->OverlapInLevel(level, begin, end)) {
        max_level_with_files = level;
      }
    }
  }
  TEST_CompactMemTable();  // TODO(sanjay): Skip if memtable does not overlap
  for (int level = 0; level < max_level_with_files; level++) {
    TEST_CompactRange(level, begin, end);
  }
}

void DBImpl::TEST_CompactRange(int level, const Slice* begin,
                               const Slice* end) {
  assert(level >= 0);
  assert(level + 1 < config::kNumLevels);

  InternalKey begin_storage, end_storage;

  ManualCompaction manual;
  manual.level = level;
  manual.done = false;
  if (begin == nullptr) {
    manual.begin = nullptr;
  } else {
    begin_storage = InternalKey(*begin, kMaxSequenceNumber, kValueTypeForSeek);
    manual.begin = &begin_storage;
  }
  if (end == nullptr) {
    manual.end = nullptr;
  } else {
    end_storage = InternalKey(*end, 0, static_cast<ValueType>(0));
    manual.end = &end_storage;
  }

  MutexLock l(&mutex_);
  while (!manual.done && !shutting_down_.load(std::memory_order_acquire) &&
         bg_error_.ok()) {
    if (manual_compaction_ == nullptr) {  // Idle
      manual_compaction_ = &manual;
      MaybeScheduleCompaction();
    } else {  // Running either my compaction or another compaction.
      background_work_finished_signal_.Wait();
    }
  }
  if (manual_compaction_ == &manual) {
    // Cancel my manual compaction since we aborted early for some reason.
    manual_compaction_ = nullptr;
  }
}

Status DBImpl::TEST_CompactMemTable() {
  // nullptr batch means just wait for earlier writes to be done
  Status s = Write(WriteOptions(), nullptr);
  if (s.ok()) {
    // Wait until the compaction completes
    MutexLock l(&mutex_);
    while (imm_ != nullptr && bg_error_.ok()) {
      background_work_finished_signal_.Wait();
    }
    if (imm_ != nullptr) {
      s = bg_error_;
    }
  }
  return s;
}

void DBImpl::MaybeScheduleFlush() {
  mutex_.AssertHeld();

  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  {
    MutexLock l(&imms_mutex_);
    if (!imms_queue_.empty()) {
      imms_not_empty_cv_.Signal();
    }
  }

  // background_flush_scheduled_ is used to ensure that we only schedule the background thread to start once.
  if (background_flush_scheduled_) {
    return;
  }
  
  // 检查是否有待处理的工作（即队列非空）
  bool has_work = false;
  {
    MutexLock l(&imms_mutex_);
    has_work = !imms_queue_.empty();
  }

  if (has_work) {
    fprintf(stderr, "Scheduling a new flush thread because none was scheduled before.\n");
    background_flush_scheduled_ = true;
    env_->ScheduleFlush(&DBImpl::BGWork_Flush, this);
  }
}

void DBImpl::BGWork_Flush(void* db) {
  reinterpret_cast<DBImpl*>(db)->FlushCall();
}


void DBImpl::FlushCall() {
  fprintf(stderr, "[Flush Thread]: Entered FlushCall.\n");
  while (!shutting_down_.load(std::memory_order_acquire)) {
    MemTable* mem_to_flush = nullptr;
    {
      MutexLock l(&imms_mutex_);
      // 使用 while 循环来防止虚假唤醒 (spurious wakeup)
      while (imms_queue_.empty() && !shutting_down_.load(std::memory_order_acquire)) {
        imms_not_empty_cv_.Wait();
      }

      if (shutting_down_.load(std::memory_order_acquire)) {
        return;
      }
      
      assert(!imms_queue_.empty());
      mem_to_flush = imms_queue_.front();
      imms_queue_.pop_front();
      imms_queue_size_.fetch_sub(1, std::memory_order_relaxed);
    }

    {
      MutexLock l(&mutex_);
      // fprintf(stderr,"Notify the waiting thread!\n");
      flush_work_finished_signal_.SignalAll(); // 唤醒所有在等待的用户线程
    }

    // 2. 如果成功获取到任务，就在不持有任何锁的情况下执行刷盘操作。
    bool schedule_compaction = false;
    if (mem_to_flush != nullptr ) {
      // running_background_threads_.fetch_add(1, std::memory_order_relaxed);
      // bg_compaction_level_.store(0, std::memory_order_relaxed); 
      CompactMemTable(mem_to_flush);
      // bg_compaction_level_.store(-1, std::memory_order_relaxed); 

      mem_to_flush->Unref();
      // running_background_threads_.fetch_sub(1, std::memory_order_relaxed);
      if (versions_->NeedsCompaction() && running_background_threads_.load()==0) {
        MaybeScheduleCompaction(); 
      } 
    }
  }

  {
    MutexLock l(&mutex_);
    shutdown_cv_.SignalAll(); // 只通知析构函数
  }
  fprintf(stderr, "[Flush Thread]: Exiting FlushCall function.\n");
}


void DBImpl::RecordBackgroundError(const Status& s) {
  mutex_.AssertHeld();
  if (bg_error_.ok()) {
    bg_error_ = s;
    background_work_finished_signal_.SignalAll();
  }
}

void DBImpl::MaybeScheduleCompaction(bool already_locked) {

  // metadata_mutex_.AssertHeld();
  if(already_locked){
    return ;
  }

  {
    MutexLock l(&metadata_mutex_);
    // fprintf(stderr, "[Flush Thread]: Signaling compaction thread that state has changed.\n");
    compaction_cv_.SignalAll(); 
  } 

  if (background_compaction_scheduled_ || shutting_down_.load(std::memory_order_acquire) || !bg_error_.ok()) {
    return;
  }

  if(versions_->NeedsCompaction()){
    fprintf(stderr, "[Flush Thread]: Scheduling a new PERMANENT compaction thread.\n");
    background_compaction_scheduled_ = true;
    env_->Schedule(&DBImpl::BGWork, this);
  }
}



void DBImpl::BGWork(void* db) {
  reinterpret_cast<DBImpl*>(db)->BackgroundCall();
}

/**
 * @brief Perform performance tuning logic around compaction.
 * 
 * This method encapsulates batch checking, L0 recording control,
 * pending tuning application, and version re-finalization.
 * 
 * @param compacted_level The compaction level being processed.
 * @param before_compaction Whether this call occurs before (true) or after (false) compaction.
 */
void DBImpl::HandlePerformanceTuning(int compacted_level, bool before_compaction) {
  performance_monitor_.CheckAndHandleBatch(this, compacted_level, true);

  if (performance_monitor_.GetTuningRecords().CheckAndPauseL0Recording()) {
    enable_l0_overflow_recording_.store(false, std::memory_order_relaxed);
  }

  if (performance_monitor_.GetTuningRecords().has_pending_changes) {
    performance_monitor_.ApplyTuningWithExemption(this);
  }

  versions_->RefinalizeCurrentVersion();

  fprintf(stderr,"[Compaction Thread]: Performance tuning %s compaction completed (level=%d, count=%lu)\n",
    before_compaction ? "before" : "after",compacted_level, compaction_count_);
}



void DBImpl::BackgroundCall() {

  while (!shutting_down_.load(std::memory_order_acquire)) {
    // 检查是否有工作可做。如果没有，就等待。
    // 使用 while 循环可以防止“虚假唤醒”，确保醒来后条件真的满足。
    metadata_mutex_.Lock();
    while (!versions_->NeedsCompaction() && !shutting_down_.load(std::memory_order_acquire) && bg_error_.ok()) {
      // fprintf(stderr, "[Compaction Thread]: No work to do, going to wait...\n");
      compaction_cv_.Wait(); // 等待 Flush 线程的“门铃”信号
      // fprintf(stderr, "[Compaction Thread]: Woke up, re-checking for work.\n");
    }

    // 如果被唤醒是因为关闭或出错，则退出循环
    if (shutting_down_.load(std::memory_order_acquire) || !bg_error_.ok()) {
      metadata_mutex_.Unlock();
      break;
    }

    /* =======================================================
    * [2] PREPARE COMPACTION CONTEXT
    * ======================================================= */
    int compacted_level = versions_->LevelNeedsCompaction();
    bg_compaction_level_.store(compacted_level, std::memory_order_relaxed);

    /* =======================================================
     * [3] PERFORMANCE TUNING LOGIC (PRE-COMPACTION)
     * Only execute before compaction if this is NOT the first one.
    * ======================================================= */
    if(compaction_count_ > 0){
      HandlePerformanceTuning(compacted_level, /*before_compaction=*/true);
    }
    // performance_monitor_.CheckAndHandleBatch(this,compacted_level, true);
    // if(performance_monitor_.GetTuningRecords().CheckAndPauseL0Recording()){
    //   enable_l0_overflow_recording_.store(false, std::memory_order_relaxed);
    // }
    // //apply the changes if have
    // if (performance_monitor_.GetTuningRecords().has_pending_changes) {
    //   performance_monitor_.ApplyTuningWithExemption(this);
    // } 
    // versions_->RefinalizeCurrentVersion();
    
    
    if (!versions_->NeedsCompaction()) {
      fprintf(stderr, "[Compaction Thread]: After tuning in this Batch, compaction no longer needed\n");
      metadata_mutex_.Unlock();
      continue;  
    }  
    metadata_mutex_.Unlock();
    // fprintf(stderr, "[Compaction Thread]: ---> Starting a major compaction.\n");
    running_background_threads_.fetch_add(1, std::memory_order_relaxed);

    {
      MutexLock l(&metadata_mutex_);
      BackgroundCompaction();
      compaction_count_++;

      if (compaction_count_ == 1) {
        HandlePerformanceTuning(compacted_level, /*before_compaction=*/false);
      }

      // Restore recording "overflows" after compaction completes
      if (!enable_l0_overflow_recording_.load(std::memory_order_relaxed)) {
        enable_l0_overflow_recording_.store(true, std::memory_order_relaxed);
      }
      performance_monitor_.UpdateLevelCreation(bg_compaction_level_);
    }
    
    // fprintf(stderr, "[Compaction Thread]: <--- Finished a major compaction.\n");
    running_background_threads_.fetch_sub(1, std::memory_order_relaxed);
    bg_compaction_level_.store(-1, std::memory_order_relaxed);

    // Wake up user threads that may be waiting
    {
      MutexLock l_main(&mutex_);
      background_work_finished_signal_.SignalAll();
    }
  }

  {
    MutexLock l(&mutex_);
    shutdown_cv_.SignalAll(); // 只通知析构函数
  }

}

void DBImpl::BackgroundCompaction() {
  metadata_mutex_.AssertHeld(); // 获取元数据锁

  Compaction* c;
  bool is_manual = (manual_compaction_ != nullptr);
  InternalKey manual_end;
  if (is_manual) {
    ManualCompaction* m = manual_compaction_;
    c = versions_->CompactRange(m->level, m->begin, m->end);
    m->done = (c == nullptr);
    if (c != nullptr) {
      manual_end = c->input(0, c->num_input_files(0) - 1)->largest;
    }
    Log(options_.info_log,"Manual compaction at level-%d from %s .. %s; will stop at %s\n",
        m->level, (m->begin ? m->begin->DebugString().c_str() : "(begin)"),
        (m->end ? m->end->DebugString().c_str() : "(end)"),
        (m->done ? "(end)" : manual_end.DebugString().c_str()));
    
    // newly added source codes
    level_stats_[m->level].number_manual_compaction++;

  } else {
    c = versions_->PickCompaction();
  }

  Status status;
  if (c == nullptr) {
    // Nothing to do
    fprintf(stderr, "[DBG in the background compaction job] c==nullptr, skip compaction\n");
  } else if (!is_manual && c->IsTrivialMove()) { 
    // Move file to next level
    assert(c->num_input_files(0) == 1);
    FileMetaData* f = c->input(0, 0);
    c->edit()->RemoveFile(c->level(), f->number);
    c->edit()->AddFile(c->level() + 1, f->number, f->file_size, f->smallest,
                       f->largest);
    status = versions_->LogAndApply(c->edit(), &metadata_mutex_, CallerView::kCompaction);
    if (!status.ok()) {
      RecordBackgroundError(status);
    }
    VersionSet::LevelSummaryStorage tmp;
    Log(options_.info_log, "Moved #%lld to level-%d %lld bytes %s: %s\n",
        static_cast<unsigned long long>(f->number), c->level() + 1,
        static_cast<unsigned long long>(f->file_size),
        status.ToString().c_str(), versions_->LevelSummary(&tmp));

    // newly added source codes
    level_stats_[c->level()+1].moved_directly_from_last_level_bytes = f->file_size;
    level_stats_[c->level()].moved_from_this_level_bytes = f->file_size;
    level_stats_[c->level()].number_TrivialMove++;
    
  } else {

    //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
    if(c->get_compaction_type() == 1){
      level_stats_[c->level()].number_size_compaction++;
    }
    else if(c->get_compaction_type()==2){
      level_stats_[c->level()].number_seek_compaction++;
    }
    //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~

    // int ctype = c->get_compaction_type();
    // fprintf(stderr,
    //   "[DBG BackgroundCompaction] DoCompactionWork: level=%d, files=%d, type=%d\n",
    //    c->level(), c->num_input_files(0), ctype);
    
    l0_tuning_state_.store(L0TuningSystemState::L0_COMPACTION_RUNNING, std::memory_order_release);
    CompactionState* compact = new CompactionState(c);
    status = DoCompactionWork(compact);
    if(c->level() == 0){
      performance_monitor_.set_first_l0_compaction();
    }
    versions_->MaybeUpdateNumLiveLevels(c->level());
    if (!status.ok()) {
      RecordBackgroundError(status);
    }
    CleanupCompaction(compact);
    c->ReleaseInputs();
    RemoveCompactionInputFiles(c);
    
  }
  delete c;

  num_level0_files_.store(versions_->NumLevelFiles(0), std::memory_order_relaxed);
  

  // fprintf(stderr,"[DBG BackgroundCompaction] Exit: status.ok=%d\n", status.ok());
  if (status.ok()) {
    // Done
  } else if (shutting_down_.load(std::memory_order_acquire)) {
    // Ignore compaction errors found during shutting down
  } else {
    Log(options_.info_log, "Compaction error: %s", status.ToString().c_str());
  }

  if (is_manual) {
    ManualCompaction* m = manual_compaction_;
    if (!status.ok()) {
      m->done = true;
    }
    if (!m->done) {
      // We only compacted part of the requested range.  Update *m
      // to the range that is left to be compacted.
      m->tmp_storage = manual_end;
      m->begin = &m->tmp_storage;
    }
    manual_compaction_ = nullptr;
  }
}

void DBImpl::CleanupCompaction(CompactionState* compact) {
  metadata_mutex_.AssertHeld();
  if (compact->builder != nullptr) {
    // May happen if we get a shutdown call in the middle of compaction
    compact->builder->Abandon();
    delete compact->builder;
  } else {
    assert(compact->outfile == nullptr);
  }
  delete compact->outfile;
  for (size_t i = 0; i < compact->outputs.size(); i++) {
    const CompactionState::Output& out = compact->outputs[i];
    pending_outputs_.erase(out.number);
  }
  delete compact;
}

Status DBImpl::OpenCompactionOutputFile(CompactionState* compact) {
  assert(compact != nullptr);
  assert(compact->builder == nullptr);
  uint64_t file_number;
  {
    metadata_mutex_.Lock();
    file_number = versions_->NewFileNumber();
    pending_outputs_.insert(file_number);
    CompactionState::Output out;
    out.number = file_number;
    out.smallest.Clear();
    out.largest.Clear();
    compact->outputs.push_back(out);
    metadata_mutex_.Unlock();
  }

  // Make the output file
  const int output_level = compact->compaction->level() + 1;
  std::string fname = TableFileName(options_, dbname_, file_number, output_level);

  bool using_direct_writeappend_file = options_.use_direct_writeappend_file;
  Log(options_.info_log,"[Compaction DBG] Attempting to create new direct?(%d) output file: %s", using_direct_writeappend_file, fname.c_str());
  Status s = env_->NewWritableFile(fname, &compact->outfile,using_direct_writeappend_file);
  if (s.ok()) {
    compact->builder = new TableBuilder(options_, compact->outfile);
  }else{
    Log(options_.info_log, "[Compaction DBG] FATAL: env_->NewWritableFile failed for file #%llu (%s). Status: %s",
      static_cast<unsigned long long>(file_number),fname.c_str(), s.ToString().c_str());
  }
  return s;
}

Status DBImpl::FinishCompactionOutputFile(CompactionState* compact, Iterator* input) {
  assert(compact != nullptr);
  assert(compact->outfile != nullptr);
  assert(compact->builder != nullptr);

  const uint64_t output_number = compact->current_output()->number;
  const int output_level = compact->compaction->level() + 1; // 我们已经知道输出 level
  assert(output_number != 0);

  // Check for iterator errors
  Status s = input->status();
  const uint64_t current_entries = compact->builder->NumEntries();
  if (s.ok()) {
    s = compact->builder->Finish();
  } else {
    compact->builder->Abandon();
  }
  const uint64_t current_bytes = compact->builder->FileSize();
  compact->current_output()->file_size = current_bytes;
  compact->total_bytes += current_bytes;
  delete compact->builder;
  compact->builder = nullptr;

  // Finish and check for file errors
  if (s.ok()) {
    s = compact->outfile->Sync();
  }
  if (s.ok()) {
    s = compact->outfile->Close();
  }
  delete compact->outfile;
  compact->outfile = nullptr;

  if (s.ok() && current_entries > 0) {
    // Verify that the table is usable
    Iterator* iter =
        table_cache_->NewIterator(ReadOptions(), output_number,current_bytes,output_level);
    s = iter->status();
    delete iter;
    if (s.ok()) {
      Log(options_.info_log, "Generated table #%llu@%d: %lld keys, %lld bytes",
          (unsigned long long)output_number, compact->compaction->level(),
          (unsigned long long)current_entries,
          (unsigned long long)current_bytes);
    }
  }
  return s;
}

Status DBImpl::InstallCompactionResults(CompactionState* compact) {
  metadata_mutex_.AssertHeld();
  Log(options_.info_log, "Compacted %d@%d + %d@%d files => %lld bytes",
      compact->compaction->num_input_files(0), compact->compaction->level(),
      compact->compaction->num_input_files(1), compact->compaction->level() + 1,
      static_cast<long long>(compact->total_bytes));

  // Add compaction outputs
  compact->compaction->AddInputDeletions(compact->compaction->edit());
  const int level = compact->compaction->level();
  for (size_t i = 0; i < compact->outputs.size(); i++) {
    const CompactionState::Output& out = compact->outputs[i];
    compact->compaction->edit()->AddFile(level + 1, out.number, out.file_size,
                                         out.smallest, out.largest);
  }
  return versions_->LogAndApply(compact->compaction->edit(), &metadata_mutex_, CallerView::kCompaction);
}

// In db/db_impl.cc

void DBImpl::RecordWriteStall(bool is_stop) {
  if (c0_adaptation_enabled_) {
    if (is_stop) {
      stop_events_.fetch_add(1, std::memory_order_relaxed);
    } else {
      fprintf(stderr,"Slow down once!\n");
      slowdown_events_.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

// In db/db_impl.cc


bool DBImpl::L0CheckAndHandleBatch(L0VarianceStats& baseline, L0VarianceStats& batch) {
  metadata_mutex_.AssertHeld(); // Ensure we are under lock

  // Define the similarity threshold (e.g., allow 20% relative difference)
  const double SKEW_SIMILARITY_THRESHOLD = 0.10; 

  // Ensure both have data to compare
  if (baseline.total_keys == 0 || batch.total_keys == 0 || baseline.table_number == 0 || batch.table_number == 0) {
    Log(options_.info_log, "[C0 Adapt Batch] Cannot compare, baseline or batch is empty.\n");
    // fprintf(stderr, "[C0 Adapt Batch] Cannot compare, baseline or batch is empty.\n");
    // If baseline is empty, maybe make the current batch the new baseline?
    // For now, let's return false, meaning "not similar enough to proceed".
    return false; 
  }

  // --- Calculate Skewness Indicator ---
  // We use (unique_keys / total_keys) as a proxy. Lower means more skewed.
  // REMEMBER: These unique_keys are OVERESTIMATES due to simple addition.
  // We rely on the *change* in this *overestimated* ratio as an indicator.
  double avg_var_baseline = baseline.variance / baseline.table_number;
  double avg_var_batch = batch.variance / batch.table_number;

  Log(options_.info_log, 
      "[C0 Adapt Batch] Comparing - Baseline (Files:%ld, AvgVar:%.3f, Var:%.3f) vs Batch (Files:%ld, AvgVar:%.3f, Var:%.3f)",
      baseline.table_number, avg_var_baseline, baseline.variance, 
      batch.table_number, avg_var_batch, batch.variance);
  fprintf(stderr, 
      "[C0 Adapt Batch] Comparing - Baseline (Files:%ld, AvgVar:%.3f, Var:%.3f) vs Batch (Files:%ld, AvgVar:%.3f, Var:%.3f)\n",
      baseline.table_number, avg_var_baseline, baseline.variance,
      batch.table_number, avg_var_batch, batch.variance);

  // --- Compare Skewness ---
  bool is_similar = false;
  // If baseline variance is very close to zero, batch must also be close to zero.
  if (avg_var_baseline < 1e-6) { 
    is_similar = (avg_var_batch < 1e-6); 
  } else {
    // Calculate relative difference and check against threshold.
    double relative_difference = std::abs(avg_var_batch - avg_var_baseline) / avg_var_baseline;
    is_similar = (relative_difference <= SKEW_SIMILARITY_THRESHOLD);
    Log(options_.info_log, "[C0 Adapt Batch] Relative Difference (AvgVar): %.2f%%", relative_difference * 100.0);
  }

  // --- Handle Result ---
  if (is_similar) {
    // fprintf(stderr, "[C0 Adapt Batch] Batches are SIMILAR. Workload seems stable. Resetting batch.\n");
    Log(options_.info_log, "[C0 Adapt Batch] Batches are SIMILAR. Workload seems stable. Resetting batch.\n");
    // Workload seems stable, so we "merge" the easy stats (total keys, table number)
    // We DO NOT merge unique_keys directly. 
    // We might update variance/average in a statistically sound way if needed,
    // but for now, let's just extend the baseline count.
    baseline.total_keys += batch.total_keys;
    baseline.unique_keys += batch.unique_keys;
    baseline.table_number += batch.table_number;
    baseline.variance += batch.variance;
    // We *could* update baseline.unique_keys with batch.unique_keys, *knowing* it's an overestimate,
    // or we could decide to *only* update the baseline during actual compaction.
    // Let's *not* merge unique_keys for now to avoid compounding the error.
    
    batch.Reset(); // Reset the batch for the next round.
    return true;

  } else {
    Log(options_.info_log, 
      "[C0 Adapt Batch WARN] Batches are DIFFERENT. Workload may have changed (Baseline_AvgVar:%.3f, Batch_AvgVar:%.3f).",
      avg_var_baseline, avg_var_batch);
    // fprintf(stderr, 
    //   "[C0 Adapt Batch WARN] Batches are DIFFERENT. Workload may have changed (Baseline_AvgVar:%.3f, Batch_AvgVar:%.3f).\n",
    //   avg_var_baseline, avg_var_batch);
    // Workload changed! This is a Red Flag.
    // What should we do?
    // Option 1: Reset the batch anyway and let the next L0 compaction handle it. (Simpler)
    // Option 2: Keep the batch data, reset the baseline, make the batch the new baseline.
    // Option 3: Trigger MaybeAdjustC0 immediately, forcing a C0 decrease?
    // Let's choose Option 1 for now, but log a strong warning.
    // We *must* react - maybe set a flag `c0_increase_plan_invalid = true;`
    
    batch.Reset(); // Resetting anyway to start fresh, but we know something changed.
    return false;
  }
}

void DBImpl::MaybeAdjustC0(const L0TuningStatsInCompaction* stats /* = nullptr */) { // Renamed or V2
  metadata_mutex_.AssertHeld(); // Ensure mutual exclusion

  // --- 1. Pre-checks ---
  if (!c0_adaptation_enabled_ ) {
    fprintf(stderr,"Tuning is not allowed!\n");
    return; // Disabled or not L0 compaction
  }

  uint64_t now = env_->NowMicros();
  // If we first call the MaybeAdjustC0, we should set the  default_totalkeys_in_C0
  if(compaction_opts_atomic_.level0_compaction_trigger.load()==4){
    l0variancestats.default_totalkeys_in_C0 = l0variancestats.total_keys;
  }

  if(current_stats_ptr_ == &batch_l0variancestats ){
    L0CheckAndHandleBatch(l0variancestats,batch_l0variancestats);
    fflush(stderr);
  }

  int current_C0 = compaction_opts_atomic_.level0_compaction_trigger.load();

  if (stats != nullptr && stats->total_keys_in > 0) {
    fprintf(stderr, "[C0 Adapt] Post-Compaction Tuning (U:%lu, T:%lu).",
      stats->unique_keys_in, stats->total_keys_in);
    Log(options_.info_log, "[C0 Adapt] Post-Compaction Tuning (U:%lu, T:%lu).",
      stats->unique_keys_in, stats->total_keys_in);
    
  }else{

    // 1. 检查是否满足增加 C0 的基本条件
    if (l0variancestats.unique_keys < l0variancestats.total_keys &&
      l0variancestats.default_totalkeys_in_C0 > l0variancestats.unique_keys && l0variancestats.average_kvs_in_each_file > 0) {

      // 2. 计算理论上还需要增加多少个文件
      int expected_file_to_tuning = static_cast<int>(
        (l0variancestats.default_totalkeys_in_C0 - l0variancestats.unique_keys) /l0variancestats.average_kvs_in_each_file);

      // 向上取整，确保至少为 1 (如果差值>0)
      if ((l0variancestats.default_totalkeys_in_C0 - l0variancestats.unique_keys) > 0 && expected_file_to_tuning == 0) {
        expected_file_to_tuning = 1;
      }

      // 3. 如果理论上需要增加 (expected > 0)
      if (expected_file_to_tuning > 0) {
        // --- Core Logic: Decide the actual increase amount ---
        // Strategy: Aim for 1/3 of the theoretical need,
        //           but ensure a minimum step and respect a dynamic maximum step.

        // 1. Calculate the base increase amount.
        //    We take 1/3 of the estimated need as a conservative starting point.
        //    std::max(1, ...) ensures that if we decide to increase, we increase by at least 1 (the "base increase").
        int increase_amount = std::max(1, expected_file_to_tuning / 3);

        // 2. & 3. Calculate the dynamic step cap (limited between 4 and 8).
        //    The cap starts as half of the current C0, making it dynamic (larger C0 allows larger steps).
        //    std::max(4, ...) sets a *minimum* cap of 4, preventing steps from being too small when C0 is low.
        //    std::min(8, ...) sets an *absolute maximum* cap of 8, preventing steps from being too large and risky.
        int dynamic_cap = std::max(4, current_C0 / 2);
        dynamic_cap = std::min(8, dynamic_cap);

        // 4. Apply the calculated cap to the increase amount.
        //    This ensures our final increase step respects the safety limits.
        increase_amount = std::min(dynamic_cap, increase_amount);
        int new_C0 = current_C0 + increase_amount;

        fprintf(stderr,
          "[C0 Adapt] Skew: U:%ld < T:%ld. Target T_def:%ld. Expect_add:%d. Increase_by:%d. C0: %d -> %d\n",
          l0variancestats.unique_keys, l0variancestats.total_keys,
          l0variancestats.default_totalkeys_in_C0, expected_file_to_tuning,
          (new_C0 > current_C0) ? (new_C0 - current_C0) : 0, current_C0, new_C0);
        Log(options_.info_log,
          "[C0 Adapt] Skew: U:%ld < T:%ld. Target T_def:%ld. Expect_add:%d. Increase_by:%d. C0: %d -> %d\n",
          l0variancestats.unique_keys, l0variancestats.total_keys,
          l0variancestats.default_totalkeys_in_C0, expected_file_to_tuning,
          (new_C0 > current_C0) ? (new_C0 - current_C0) : 0, current_C0, new_C0);

        // 5. 如果 C0 确实改变了，则应用并重新计算分数
        if (new_C0 > current_C0) {
          compaction_opts_atomic_.SetL0Triggers_Internal(new_C0);
          versions_->RecalculateCompactionScores(); 
          current_stats_ptr_ = &batch_l0variancestats;
        }
      } else {
        fprintf(stderr, "[C0 Adapt] Skew detected, but expected_file_to_tuning (%d) <= 0. Holding C0 at %d.\n",
          expected_file_to_tuning, current_C0);
      }
    } else {
      fprintf(stderr, "[C0 Adapt] Conditions not met (U:%ld, T:%ld, T_def:%ld). Holding C0 at %d.\n",
        l0variancestats.unique_keys, l0variancestats.total_keys, l0variancestats.default_totalkeys_in_C0, current_C0);
    }
  }
  fprintf(stderr,"\n\n");
  fflush(stderr);

  // int64_t slows = slowdown_events_.exchange(0, std::memory_order_relaxed);
  // int64_t stops = stop_events_.exchange(0, std::memory_order_relaxed);
  // bool stalled = (stops > 0 || slows > 0);
}


Status DBImpl::DoCompactionWork(CompactionState* compact) {
  metadata_mutex_.AssertHeld();
  const uint64_t start_micros = env_->NowMicros();
  int64_t imm_micros = 0;  // Micros spent doing imm_ compactions

  new_LeveldataStats new_compact_statistics;

  // compact->compaction->num_input_files(), 0代表要发生合并的level的文件的数量，1代表有overlap的level的文件的数量
  // compact->compaction->num_input_files(1) 示与当前级别有重叠的下一个级别（level + 1）中参与压缩的文件数量。
  Log(options_.info_log, "Compacting %d@%d + %d@%d files",
      compact->compaction->num_input_files(0), compact->compaction->level(),
      compact->compaction->num_input_files(1),
      compact->compaction->level() + 1);

  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
  // record the number of files that involved in every compaction!
  level_stats_[compact->compaction->level()].number_of_compactions++;
  if(compact->compaction->get_compaction_type() == 1){ // size compaction
    level_stats_[compact->compaction->level()].number_size_compaction_initiator_files += compact->compaction->num_input_files(0);
    level_stats_[compact->compaction->level()+1].number_seek_compaction_participant_files += compact->compaction->num_input_files(1);
  }
  else if(compact->compaction->get_compaction_type() == 2){ // seek compaction 
    level_stats_[compact->compaction->level()].number_seek_compaction_initiator_files += compact->compaction->num_input_files(0);
    level_stats_[compact->compaction->level()+1].number_seek_compaction_participant_files += compact->compaction->num_input_files(1);
  }
  // std::string input_files_l0;
  // for (size_t i = 0; i < compact->compaction->num_input_files(0); ++i) {
  //   input_files_l0 += std::to_string(compact->compaction->input(0, i)->number);
  //   if (i < compact->compaction->num_input_files(0) - 1) {
  //     input_files_l0 += ", ";
  //   }
  // }
  // Log(options_.info_log, "[Compaction Inputs] Level-%d files: [ %s ]", 
  //   compact->compaction->level(), input_files_l0.c_str());

  // 2. 打印 level-n+1 的输入文件号
  // compact->compaction->inputs_[1] 是 level-n+1 的输入文件元数据 vector
  // if (compact->compaction->num_input_files(1) > 0) {
  //   std::string input_files_l1;
  //   for (size_t i = 0; i < compact->compaction->num_input_files(1); ++i) {
  //     input_files_l1 += std::to_string(compact->compaction->input(1, i)->number);
  //     if (i < compact->compaction->num_input_files(1) - 1) {
  //       input_files_l1 += ", ";
  //     }
  //   }
  //   Log(options_.info_log, "[Compaction Inputs] Level-%d files: [ %s ]", 
  //     compact->compaction->level() + 1, input_files_l1.c_str());
  // }
  //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~

  // 这个 compact->compaction->level() 是指当前 compaction 的 level，也是就是哪个level需要被合并
  assert(versions_->NumLevelFiles(compact->compaction->level()) > 0);
  assert(compact->builder == nullptr);
  assert(compact->outfile == nullptr);
  if (snapshots_.empty()) {
    compact->smallest_snapshot = versions_->LastSequence();
  } else {
    compact->smallest_snapshot = snapshots_.oldest()->sequence_number();
  }

  // 制作一个迭代器
  Iterator* input = versions_->MakeInputIterator(compact->compaction);

  // Release mutex while we're actually doing the compaction work
  metadata_mutex_.Unlock();

  input->SeekToFirst();
  Status status;
  ParsedInternalKey ikey;
  std::string current_user_key;
  bool has_current_user_key = false;
  int64_t total_KVs_in_compaction=0;
  int64_t unique_KVs_in_compaction=0;
  int64_t unique_KVs_writes_after_compaction=0;
  SequenceNumber last_sequence_for_key = kMaxSequenceNumber;


  while (input->Valid() && !shutting_down_.load(std::memory_order_acquire)) {
    Slice key = input->key();
    total_KVs_in_compaction++;
    if (compact->compaction->ShouldStopBefore(key) &&
        compact->builder != nullptr) {
      status = FinishCompactionOutputFile(compact, input);
      if (!status.ok()) {
        Log(options_.info_log, "[Compaction DBG] Error in FinishCompactionOutputFile (inside loop) at level %d: %s",
          compact->compaction->level(), status.ToString().c_str());
        break;
      }
    }

    // Handle key/value, add to state, etc.
    bool drop = false;
    if (!ParseInternalKey(key, &ikey)) {
      // Do not hide error keys
      current_user_key.clear();
      has_current_user_key = false;
      last_sequence_for_key = kMaxSequenceNumber;
    } else {
      if (!has_current_user_key ||
          user_comparator()->Compare(ikey.user_key, Slice(current_user_key)) !=
              0) {
        // First occurrence of this user key
        unique_KVs_in_compaction++;
        current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
        has_current_user_key = true;
        last_sequence_for_key = kMaxSequenceNumber;
      }

      if (last_sequence_for_key <= compact->smallest_snapshot) {
        // Hidden by an newer entry for same user key
        drop = true;  // (A)
      } else if (ikey.type == kTypeDeletion &&
                 ikey.sequence <= compact->smallest_snapshot &&
                 compact->compaction->IsBaseLevelForKey(ikey.user_key)) {
        // For this user key:
        // (1) there is no data in higher levels
        // (2) data in lower levels will have larger sequence numbers
        // (3) data in layers that are being compacted here and have
        //     smaller sequence numbers will be dropped in the next
        //     few iterations of this loop (by rule (A) above).
        // Therefore this deletion marker is obsolete and can be dropped.
        drop = true;
      }

      last_sequence_for_key = ikey.sequence;
    }
    
#if 0
    Log(options_.info_log,
        "  Compact: %s, seq %d, type: %d %d, drop: %d, is_base: %d, "
        "%d smallest_snapshot: %d",
        ikey.user_key.ToString().c_str(),
        (int)ikey.sequence, ikey.type, kTypeValue, drop,
        compact->compaction->IsBaseLevelForKey(ikey.user_key),
        (int)last_sequence_for_key, (int)compact->smallest_snapshot);
#endif

    if (!drop) {
      // Open output file if necessary
      if (compact->builder == nullptr) {
        status = OpenCompactionOutputFile(compact);
        if (!status.ok()) {
          Log(options_.info_log, "[Compaction DBG] Error in OpenCompactionOutputFile at L%d->L%d merge: %s",
            compact->compaction->level(),compact->compaction->level()+1, status.ToString().c_str());
          break;
        }
      }
      if (compact->builder->NumEntries() == 0) {
        compact->current_output()->smallest.DecodeFrom(key);
      }
      compact->current_output()->largest.DecodeFrom(key);
      compact->builder->Add(key, input->value());
      unique_KVs_writes_after_compaction++;
      // Close output file if it is big enough
      if (compact->builder->FileSize() >=
          compact->compaction->MaxOutputFileSize()) {
        status = FinishCompactionOutputFile(compact, input);
        if (!status.ok()) {
          Log(options_.info_log, "[Compaction DBG] Error in FinishCompactionOutputFile (file size:%ld limit:%ld) at L%d->L%d merge: %s",
            compact->builder->FileSize(), compact->compaction->MaxOutputFileSize(), compact->compaction->level(),compact->compaction->level()+1, status.ToString().c_str());
          break;
        }
      }
    }

    input->Next();
  }

  if (status.ok() && shutting_down_.load(std::memory_order_acquire)) {
    Log(options_.info_log, "[Compaction DBG] Compaction aborted bacause shutdoenwn at level %d: %s",
      compact->compaction->level(), status.ToString().c_str());
    status = Status::IOError("Deleting DB during compaction");
  }
  if (status.ok() && compact->builder != nullptr) {
    status = FinishCompactionOutputFile(compact, input);
    if (!status.ok()) {
      Log(options_.info_log, "[Compaction DBG] Error in final FinishCompactionOutputFile at level %d: %s",
        compact->compaction->level(), status.ToString().c_str());
    }
  }
  if (status.ok()) {
    status = input->status();
  }
  delete input;
  input = nullptr;

  CompactionStats stats;
  stats.micros = env_->NowMicros() - start_micros - imm_micros;
  for (int which = 0; which < 2; which++) {
    for (int i = 0; i < compact->compaction->num_input_files(which); i++) {
      stats.bytes_read += compact->compaction->input(which, i)->file_size;
    }
  }

  for (size_t i = 0; i < compact->outputs.size(); i++) {
    stats.bytes_written += compact->outputs[i].file_size;
    flush_stats_.actual_bytes_written += compact->outputs[i].file_size;
  }

  metadata_mutex_.Lock();
  if(compact->compaction->level()==0){
    l0compaction_stats.total_keys_in = total_KVs_in_compaction;
    l0compaction_stats.unique_keys_in = unique_KVs_in_compaction;
    l0compaction_stats.total_keys_out = unique_KVs_writes_after_compaction;
    l0compaction_stats.num_l0_files = compact->compaction->num_input_files(0);
    l0compaction_stats.num_l1_files_in = compact->compaction->num_input_files(1);
    // l0compaction_stats.CalculateDerivedStats();
    // l0compaction_stats.Print();
  }


  stats_[compact->compaction->level() + 1].Add(stats);
  level_stats_[compact->compaction->level() + 1].bytes_read += stats.bytes_read;
  level_stats_[compact->compaction->level() + 1].bytes_written += stats.bytes_written;

  if (status.ok()) {
    status = InstallCompactionResults(compact);
  }
  if (!status.ok()) {
    Log(options_.info_log, "[Compaction DBG] Final status not OK before returning at level %d: %s",
      compact->compaction->level(), status.ToString().c_str());
    RecordBackgroundError(status);
  }
  VersionSet::LevelSummaryStorage tmp;
  Log(options_.info_log, "compacted to: %s", versions_->LevelSummary(&tmp));
  Log(options_.level_compaction_log_, 
    "[Compaction Summary] L%d->L%d | Read: %d files (%lld bytes, %.2f MB) | Write: %zu files (%lld bytes, %.2f MB) | Time: %lld micros",
    compact->compaction->level(),compact->compaction->level() + 1,compact->compaction->num_input_files(0) + compact->compaction->num_input_files(1),
    (long long)stats.bytes_read,(double)stats.bytes_read / (1024.0 * 1024.0),  
    compact->outputs.size(),(long long)stats.bytes_written,
    (double)stats.bytes_written / (1024.0 * 1024.0), (long long)stats.micros
  );
  return status;
}

namespace {

struct IterState {
  port::Mutex* const mu;
  Version* const version GUARDED_BY(mu);
  MemTable* const mem GUARDED_BY(mu);
  MemTable* const imm GUARDED_BY(mu);

  IterState(port::Mutex* mutex, MemTable* mem, MemTable* imm, Version* version)
      : mu(mutex), version(version), mem(mem), imm(imm) {}
};

static void CleanupIteratorState(void* arg1, void* arg2) {
  IterState* state = reinterpret_cast<IterState*>(arg1);
  state->mu->Lock();
  state->mem->Unref();
  if (state->imm != nullptr) state->imm->Unref();
  state->version->Unref();
  state->mu->Unlock();
  delete state;
}

}  // anonymous namespace

Iterator* DBImpl::NewInternalIterator(const ReadOptions& options,
                                      SequenceNumber* latest_snapshot,
                                      uint32_t* seed) {
  mutex_.Lock();
  *latest_snapshot = versions_->LastSequence();

  // Collect together all needed child iterators
  std::vector<Iterator*> list;
  list.push_back(mem_->NewIterator());
  mem_->Ref();
  if (imm_ != nullptr) {
    list.push_back(imm_->NewIterator());
    imm_->Ref();
  }
  versions_->current()->AddIterators(options, &list);
  Iterator* internal_iter =
      NewMergingIterator(&internal_comparator_, &list[0], list.size());
  versions_->current()->Ref();

  IterState* cleanup = new IterState(&mutex_, mem_, imm_, versions_->current());
  internal_iter->RegisterCleanup(CleanupIteratorState, cleanup, nullptr);

  *seed = ++seed_;
  mutex_.Unlock();
  return internal_iter;
}

Iterator* DBImpl::TEST_NewInternalIterator() {
  SequenceNumber ignored;
  uint32_t ignored_seed;
  return NewInternalIterator(ReadOptions(), &ignored, &ignored_seed);
}

int64_t DBImpl::TEST_MaxNextLevelOverlappingBytes() {
  MutexLock l(&mutex_);
  return versions_->MaxNextLevelOverlappingBytes();
}

Status DBImpl::Get(const ReadOptions& options, const Slice& key, std::string* value) {
  Status s;
  // MutexLock l(&mutex_);
  SequenceNumber snapshot;
  performance_monitor_.Addops();
  {    
    LEVELDB_PROFILE_DUAL_GET(); // 统计整个Get操作的时间
    {
      MutexLock l(&metadata_mutex_);
      if (options.snapshot != nullptr) {
        snapshot = static_cast<const SnapshotImpl*>(options.snapshot)->sequence_number();
      } else {
        snapshot = versions_->LastSequence();
      }
    }

    MemTable* mem = nullptr;
    {
      MutexLock l(&mutex_);
      mem = mem_;
      mem->Ref();
    }

    std::deque<MemTable*> imms_snapshot;
    {
      MutexLock l(&imms_mutex_);
      // 复制当前的 imms_queue_，并增加引用计数
      for (auto* imm : imms_queue_) {
        imm->Ref();
        imms_snapshot.push_back(imm);
      }
    }

    Version* current = nullptr;
    {
      MutexLock l(&metadata_mutex_);
      current = versions_->current();
      current->Ref();
      g_version_ref_count.fetch_add(1);
    }

    bool have_stat_update = false;
    Version::GetStats stats;
    bool found = false;

    // Unlock while reading from files and memtables
    {
      // First look in the memtable, then in the immutable memtable (if any).
      LookupKey lkey(key, snapshot);

      // 统计memtable查找时间
      {
        LEVELDB_PROFILE_MEMTABLE_LOOKUP();
        if (mem->Get(lkey, value, &s)) { // Done 
          found = true;
        }
      }

      if (!found ) {
        LEVELDB_PROFILE_IMMUTABLE_LOOKUP();
        for (auto it = imms_snapshot.rbegin(); it != imms_snapshot.rend(); ++it) {
          if ((*it)->Get(lkey, value, &s)) {
            found = true;
            break;
          }
        }
      }

      if (!found) {
        LEVELDB_PROFILE_FILE_READ();
        s = current->Get(options, lkey, value, &stats);
        have_stat_update = true;
      }
    }

    // releasing the locked resources
    {
      MutexLock l(&metadata_mutex_);
      current->Unref();
      g_version_unref_count.fetch_add(1);
    }
    
    mem->Unref();
    for (auto* imm : imms_snapshot) {
      imm->Unref();
    }
  }

  // AutoTuner::check_and_trigger_tuning(this, workload_monitor_);

  // performance_monitor_.CheckAndHandleBatch(this);
  return s;
}

Iterator* DBImpl::NewIterator(const ReadOptions& options) {
  SequenceNumber latest_snapshot;
  uint32_t seed;
  Iterator* iter = NewInternalIterator(options, &latest_snapshot, &seed);
  return NewDBIterator(this, user_comparator(), iter,
                       (options.snapshot != nullptr
                            ? static_cast<const SnapshotImpl*>(options.snapshot)
                                  ->sequence_number()
                            : latest_snapshot),
                       seed);
}

void DBImpl::RecordReadSample(Slice key) {
  MutexLock l(&mutex_);
  if (versions_->current()->RecordReadSample(key)) {
    MaybeScheduleCompaction();
  }
}

const Snapshot* DBImpl::GetSnapshot() {
  MutexLock l(&metadata_mutex_);
  return snapshots_.New(versions_->LastSequence());
}

void DBImpl::ReleaseSnapshot(const Snapshot* snapshot) {
  MutexLock l(&metadata_mutex_);
  snapshots_.Delete(static_cast<const SnapshotImpl*>(snapshot));
}

// Convenience methods
Status DBImpl::Put(const WriteOptions& o, const Slice& key, const Slice& val) {
  
  performance_monitor_.Addops();
  Status s = DB::Put(o, key, val);
  // workload_monitor_.record_write();
  // AutoTuner::check_and_trigger_tuning(this, workload_monitor_);

  // performance_monitor_.CheckAndHandleBatch(this);
  return s;
}

Status DBImpl::Delete(const WriteOptions& options, const Slice& key) {

  performance_monitor_.Addops();
  Status s = DB::Delete(options, key);
  // performance_monitor_.CheckAndHandleBatch(this);
  return s;
}

Status DBImpl::Write(const WriteOptions& options, WriteBatch* updates) {
  
  Status status;
  performance_monitor_.Addops();
  {
  LEVELDB_PROFILE_DUAL_PUT();
  Writer w(&mutex_);
  w.batch = updates; // 记录要写入的数据
  w.sync = options.sync; // 记录要写入的选项，只有是否同步一个选项
  w.done = false;    // 写入的状态，完成或者未完成，当前肯定是未完成
      
  MutexLock l(&mutex_);

  writers_.push_back(&w);
  while (!w.done && &w != writers_.front()) {
    // auto queue_wait_start = std::chrono::high_resolution_clock::now();
    w.cv.Wait();
    // auto queue_wait_end = std::chrono::high_resolution_clock::now();
    // total_queue_wait_micros_.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(queue_wait_end - queue_wait_start).count(), std::memory_order_relaxed);
  }
  if (w.done) {

    // performance_monitor_.CheckAndHandleBatch(this);
    return w.status;
  }

  // May temporarily unlock and wait.
  // auto makeroom_start = std::chrono::high_resolution_clock::now();
  status = MakeRoomForWrite(updates == nullptr);
  // auto makeroom_end = std::chrono::high_resolution_clock::now();
  // auto makeroom_duration = std::chrono::duration_cast<std::chrono::microseconds>(makeroom_end - makeroom_start).count();
  // total_makeroom_micros_.fetch_add(makeroom_duration, std::memory_order_relaxed);

  uint64_t last_sequence = versions_->LastSequence();
  Writer* last_writer = &w;
  if (status.ok() && updates != nullptr) {  // nullptr batch is for compactions
    WriteBatch* write_batch = BuildBatchGroup(&last_writer);
    WriteBatchInternal::SetSequence(write_batch, last_sequence + 1);
    last_sequence += WriteBatchInternal::Count(write_batch);

    // Add to log and apply to memtable.  We can release the lock
    // during this phase since &w is currently responsible for logging
    // and protects against concurrent loggers and concurrent writes
    // into mem_.
    {
      mutex_.Unlock();

      // auto wal_start = std::chrono::high_resolution_clock::now();
      status = log_->AddRecord(WriteBatchInternal::Contents(write_batch));
      // auto wal_end = std::chrono::high_resolution_clock::now();
      // total_wal_time_micros_.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(wal_end - wal_start).count(), std::memory_order_relaxed);

      bool sync_error = false;
      if (status.ok() && options.sync) {

        // auto sync_start = std::chrono::high_resolution_clock::now();
        status = logfile_->Sync();
        // auto sync_end = std::chrono::high_resolution_clock::now();
        // total_sync_time_micros_.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(sync_end - sync_start).count(), std::memory_order_relaxed);

        if (!status.ok()) {
          sync_error = true;
        }
      }
      if (status.ok()) {
        // auto mem_start = std::chrono::high_resolution_clock::now();
        status = WriteBatchInternal::InsertInto(write_batch, mem_);
        // auto mem_end = std::chrono::high_resolution_clock::now();
        // total_memtable_time_micros_.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(mem_end - mem_start).count(), std::memory_order_relaxed);
      }
      mutex_.Lock();
      if (sync_error) {
        // The state of the log file is indeterminate: the log record we
        // just added may or may not show up when the DB is re-opened.
        // So we force the DB into a mode where all future writes fail.
        RecordBackgroundError(status);
      }
    }
    if (write_batch == tmp_batch_) tmp_batch_->Clear();

    versions_->SetLastSequence(last_sequence);
  }

  while (true) {
    Writer* ready = writers_.front();
    writers_.pop_front();
    if (ready != &w) {
      ready->status = status;
      ready->done = true;
      ready->cv.Signal();
    }
    if (ready == last_writer) break;
  }

  // Notify new head of write queue
  if (!writers_.empty()) {
    writers_.front()->cv.Signal();
  }
  }

  // performance_monitor_.CheckAndHandleBatch(this);
  return status;
}

// REQUIRES: Writer list must be non-empty
// REQUIRES: First writer must have a non-null batch
WriteBatch* DBImpl::BuildBatchGroup(Writer** last_writer) {
  mutex_.AssertHeld();
  assert(!writers_.empty());
  Writer* first = writers_.front();
  WriteBatch* result = first->batch;
  assert(result != nullptr);

  size_t size = WriteBatchInternal::ByteSize(first->batch);

  // Allow the group to grow up to a maximum size, but if the
  // original write is small, limit the growth so we do not slow
  // down the small write too much.
  size_t max_size = 1 << 20;
  if (size <= (128 << 10)) {
    max_size = size + (128 << 10);
  }

  *last_writer = first;
  std::deque<Writer*>::iterator iter = writers_.begin();
  ++iter;  // Advance past "first"
  for (; iter != writers_.end(); ++iter) {
    Writer* w = *iter;
    if (w->sync && !first->sync) {
      // Do not include a sync write into a batch handled by a non-sync write.
      break;
    }

    if (w->batch != nullptr) {
      size += WriteBatchInternal::ByteSize(w->batch);
      if (size > max_size) {
        // Do not make batch too big
        break;
      }

      // Append to *result
      if (result == first->batch) {
        // Switch to temporary batch instead of disturbing caller's batch
        result = tmp_batch_;
        assert(WriteBatchInternal::Count(result) == 0);
        WriteBatchInternal::Append(result, first->batch);
      }
      WriteBatchInternal::Append(result, w->batch);
    }
    *last_writer = w;
  }
  return result;
}

// REQUIRES: mutex_ is held
// REQUIRES: this thread is currently at the front of the writer queue
Status DBImpl::MakeRoomForWrite(bool force) {
  mutex_.AssertHeld();
  assert(!writers_.empty());
  bool allow_delay = !force;
  Status s;
  while (true) {
    if (!bg_error_.ok()) {
      // Yield previous error
      s = bg_error_;
      break;
    } 

    bool needs_slowdown = false;
    bool needs_stop = false;
    {
      const int num_l0_files = num_level0_files_.load(std::memory_order_relaxed);
      int l0_trigger = compaction_opts_atomic_.level0_compaction_trigger.load();

      if (allow_delay && num_l0_files >= 
          compaction_opts_atomic_.level0_slowdown_writes_trigger.load(std::memory_order_relaxed)) {
        needs_slowdown = true;
      } else if (num_l0_files >= 
                 compaction_opts_atomic_.level0_stop_writes_trigger.load(std::memory_order_relaxed)) {
        needs_stop = true;
      }
      if(enable_l0_overflow_recording_.load(std::memory_order_relaxed)){
        L0_unknown_stall_stats_.RecordL0Exceed(num_l0_files, l0_trigger);
      }
    } 
    
    if (needs_slowdown) {
      // We are getting close to hitting a hard limit on the number of
      // L0 files.  Rather than delaying a single write by several
      // seconds when we hit the hard limit, start delaying each
      // individual write by 1ms to reduce latency variance.  Also,
      // this delay hands over some CPU to the compaction thread in
      // case it is sharing the same core as the writer.
      // RecordWriteStall(false);  
      mutex_.Unlock();
      env_->SleepForMicroseconds(1000);

      const int compaction_level = bg_compaction_level_.load(std::memory_order_relaxed);
      if (compaction_level >= 0 && compaction_level < config::kNumLevels) {
        level_stall_stats_[compaction_level].slowdown_micros.fetch_add(1000, std::memory_order_relaxed);
        level_stall_stats_[compaction_level].slowdown_count.fetch_add(1, std::memory_order_relaxed);
      } else {
        // This covers cases where level is -1 (no compaction running) or invalid.
        L0_unknown_stall_stats_.slowdown_micros.fetch_add(1000, std::memory_order_relaxed);
        L0_unknown_stall_stats_.slowdown_count.fetch_add(1, std::memory_order_relaxed);
        L0_unknown_stall_stats_.total_slow_or_stop_time.fetch_add(1000, std::memory_order_relaxed);
      }

      allow_delay = false;  // Do not delay a single write more than once
      mutex_.Lock();
    } else if (!force &&
               (mem_->ApproximateMemoryUsage() <= options_.write_buffer_size)) {
      // There is room in current memtable
      break;
    } else if (needs_stop) {
      // There are too many level-0 files.
      // RecordWriteStall(true);  
      const int compaction_level = bg_compaction_level_.load(std::memory_order_relaxed);

      Log(options_.info_log, "Too many L0 files; waiting...\n"); 
      auto wait_start = std::chrono::high_resolution_clock::now();
      background_work_finished_signal_.Wait();
      auto wait_end = std::chrono::high_resolution_clock::now();
      auto wait_duration = std::chrono::duration_cast<std::chrono::microseconds>(wait_end - wait_start).count();
      // Check if the compaction level is valid and use it as an array index.
      if (compaction_level >= 0 && compaction_level < config::kNumLevels) {
        level_stall_stats_[compaction_level].stop_micros.fetch_add(wait_duration, std::memory_order_relaxed);
        level_stall_stats_[compaction_level].stop_count.fetch_add(1, std::memory_order_relaxed);
      } else {
        L0_unknown_stall_stats_.stop_micros.fetch_add(wait_duration, std::memory_order_relaxed);
        L0_unknown_stall_stats_.stop_count.fetch_add(1, std::memory_order_relaxed);
        L0_unknown_stall_stats_.total_slow_or_stop_time.fetch_add(wait_duration, std::memory_order_relaxed);
      }
    } else {

      if (imms_queue_size_.load(std::memory_order_relaxed) >= options_.max_immutable_memtables) {
        Log(options_.info_log, "Too many immutable memtables; waiting...\n");
        // fprintf(stderr,"We are waiting! imms_queue_size=%ld\n", imms_queue_size_.load(std::memory_order_relaxed));
        auto wait_start = std::chrono::high_resolution_clock::now();
        flush_work_finished_signal_.Wait();
        auto wait_end = std::chrono::high_resolution_clock::now();
        auto wait_duration = std::chrono::duration_cast<std::chrono::microseconds>(wait_end - wait_start).count();
        L0_unknown_stall_stats_.total_slow_or_stop_time.fetch_add(wait_duration, std::memory_order_relaxed);
        // wait_queue_micros_.fetch_add(wait_duration, std::memory_order_relaxed);
        // fprintf(stderr,"Waiting finished! imms_queue_size=%ld\n", imms_queue_size_.load(std::memory_order_relaxed));
      }
      
      
      // Attempt to switch to a new memtable and trigger compaction of old
      assert(versions_->PrevLogNumber() == 0);
      metadata_mutex_.Lock();
      uint64_t new_log_number = versions_->NewFileNumber();
      metadata_mutex_.Unlock();
      WritableFile* lfile = nullptr;
      
      // --------------------------------------------------------------------------- 
      uint64_t logical_wal_size = logfile_ ? logfile_->GetFileSize() : 0;
      uint64_t physical_wal_size = 0;
      Status s_sz = env_->GetFileSize(LogFileName(dbname_, logfile_number_), &physical_wal_size);
      size_t mem_usage = mem_ ? mem_->ApproximateMemoryUsage() : 0;
      // fprintf(stderr,"[SwitchMemtable] logical_wal=%" PRIu64 " (%.2f MiB) / "
      //   "physical_wal=%" PRIu64 " (%.2f MiB) | "
      //   "mem_usage=%zu (%.2f MiB) | "
      //   "imm_queue=%zu\n",
      //   logical_wal_size,  logical_wal_size  / 1048576.0,
      //   physical_wal_size, physical_wal_size / 1048576.0,
      //   mem_usage,         mem_usage         / 1048576.0,
      //   imms_queue_size_.load(std::memory_order_relaxed));
      // --------------------------------------------------------------------------- 

      //We never use direct IO for sync logs.
      s = env_->NewWritableFile(LogFileName(dbname_, new_log_number), &lfile);
      if (!s.ok()) {
        // Avoid chewing through file number space in a tight loop.
        versions_->ReuseFileNumber(new_log_number);
        break;
      }

      delete log_;

      s = logfile_->Close();
      if (!s.ok()) {
        // We may have lost some data written to the previous log file.
        // Switch to the new log file anyway, but record as a background
        // error so we do not attempt any more writes.
        //
        // We could perhaps attempt to save the memtable corresponding
        // to log file and suppress the error if that works, but that
        // would add more complexity in a critical code path.
        RecordBackgroundError(s);
      }
      delete logfile_;

      logfile_ = lfile;
      logfile_number_ = new_log_number;
      log_ = new log::Writer(lfile);

      // imm_ = mem_;
      // has_imm_.store(true, std::memory_order_release);
      {
        MutexLock l(&imms_mutex_);
        imms_queue_.push_back(mem_);
        imms_queue_size_.fetch_add(1, std::memory_order_relaxed); 
        imms_not_empty_cv_.Signal(); // 唤醒 Flush 线程
      }

      mem_ = new MemTable(internal_comparator_);
      mem_->Ref();
      force = false;  // Do not force another compaction if have room

      // auto schedule_start = std::chrono::high_resolution_clock::now();
      MaybeScheduleFlush();
      // auto schedule_end = std::chrono::high_resolution_clock::now();
      // auto schedule_duration = std::chrono::duration_cast<std::chrono::microseconds>(schedule_end - schedule_start).count();
      // total_schedule_micros_.fetch_add(schedule_duration, std::memory_order_relaxed);

    }
  }
  return s;
}

bool DBImpl::GetProperty(const Slice& property, std::string* value) {
  
  value->clear();
  MutexLock l(&metadata_mutex_);
  Slice in = property;
  Slice prefix("leveldb.");
  if (!in.starts_with(prefix)) return false;
  in.remove_prefix(prefix.size());

  if (in.starts_with("num-files-at-level")) {
    in.remove_prefix(strlen("num-files-at-level"));
    uint64_t level;
    bool ok = ConsumeDecimalNumber(&in, &level) && in.empty();
    if (!ok || level >= config::kNumLevels) {
      return false;
    } else {
      char buf[100];
      std::snprintf(buf, sizeof(buf), "%d",
                    versions_->NumLevelFiles(static_cast<int>(level)));
      *value = buf;
      return true;
    }
  } else if (in == "stats") {
    double user_io = 0;
    double total_io = 0;
    char buf[300];
    std::snprintf(buf, sizeof(buf),
                  "                               Compactions\n"
                  "Level  Files Size(MB) Time(sec) Read(MB) Write(MB)\n"
                  "--------------------------------------------------\n");
    value->append(buf);
    // fprintf(stderr, "entering io_statistics1\n");
    // fflush(stdout);
    for (int level = 0; level < config::kNumLevels; level++) {
      int files = versions_->NumLevelFiles(level);
      if (stats_[level].micros > 0 || files > 0) {
        std::snprintf(buf, sizeof(buf), "%3d %8d %8.0f %9.0f %8.0f %9.0f\n",
                      level, files, versions_->NumLevelBytes(level) / 1048576.0,
                      stats_[level].micros / 1e6,
                      stats_[level].bytes_read / 1048576.0,
                      stats_[level].bytes_written / 1048576.0);
        value->append(buf);
      }
      total_io += stats_[level].bytes_written / 1048576.0;
      if(level == 0){
        user_io = stats_[level].bytes_written/ 1048576.0;
      }
    }
    // fprintf(stderr, "entering io_statistics\n");
    // fflush(stdout);
    snprintf(buf, sizeof(buf), "user_io:%.3fMB total_ios: %.3fMB WriteAmplification: %2.4f\n", user_io, total_io, total_io/ user_io);
    value->append(buf);
    return true;
  } else if (in == "sstables") {
    *value = versions_->current()->DebugString();
    return true;
  } else if (in == "approximate-memory-usage") {
    size_t total_usage = options_.block_cache->TotalCharge();
    if (mem_) {
      total_usage += mem_->ApproximateMemoryUsage();
    }
    if (imm_) {
      total_usage += imm_->ApproximateMemoryUsage();
    }
    char buf[50];
    std::snprintf(buf, sizeof(buf), "%llu",
                  static_cast<unsigned long long>(total_usage));
    value->append(buf);
    return true;
  }

  return false;
}


bool DBImpl::GetProperty_with_whole_lsm(const Slice& property, std::string* value) {
  
  value->clear();
  MutexLock l(&metadata_mutex_);
  Slice in = property;
  Slice prefix("leveldb.");
  if (!in.starts_with(prefix)) return false;
  in.remove_prefix(prefix.size());

  if (in.starts_with("num-files-at-level")) {
    in.remove_prefix(strlen("num-files-at-level"));
    uint64_t level;
    bool ok = ConsumeDecimalNumber(&in, &level) && in.empty();
    if (!ok || level >= config::kNumLevels) {
      return false;
    } else {
      char buf[100];
      std::snprintf(buf, sizeof(buf), "%d",
                    versions_->NumLevelFiles(static_cast<int>(level)));
      *value = buf;
      return true;
    }
  } else if (in == "statsandwal") {

    //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
    // I modified this part for print more details of a whole LSM
    value->append("--- Write Stalls per Level ---\n");
    char buf_level[300];
    long long total_stall_micros = 0;

    for (int i = 0; i < config::kNumLevels; ++i) {
      long long slow_micros = level_stall_stats_[i].slowdown_micros.load(std::memory_order_relaxed);
      long long slow_count = level_stall_stats_[i].slowdown_count.load(std::memory_order_relaxed);
      long long stop_micros = level_stall_stats_[i].stop_micros.load(std::memory_order_relaxed);
      long long stop_count = level_stall_stats_[i].stop_count.load(std::memory_order_relaxed);

      total_stall_micros += slow_micros + stop_micros;

      if (slow_count > 0 || stop_count > 0) {
        std::string details;
        if (slow_count > 0) {
          char part[100];
          std::snprintf(part, sizeof(part), "Slowdown(Cnt:%lld,Time:%.2fs) ", slow_count, slow_micros / 1e6);
          details.append(part);
        }
        if (stop_count > 0) {
          char part[100];
          std::snprintf(part, sizeof(part), "Stop(Cnt:%lld,Time:%.2fs)", stop_count, stop_micros / 1e6);
          details.append(part);
        }
        std::snprintf(buf_level, sizeof(buf_level), "  Level-%d Stalls: %s\n", i, details.c_str());
        value->append(buf_level);
      }
    }

    // Handle Unknown Stalls
    long long unknown_stop_micros = L0_unknown_stall_stats_.stop_micros.load(std::memory_order_relaxed);
    long long unknown_stop_count = L0_unknown_stall_stats_.stop_count.load(std::memory_order_relaxed);
    if (unknown_stop_count > 0) {
      total_stall_micros += unknown_stop_micros;
      std::snprintf(buf_level, sizeof(buf_level),"  Unknown Stalls: Stop(Cnt:%lld,Time:%.2fs)\n", unknown_stop_count, unknown_stop_micros / 1e6);
      value->append(buf_level);
    }

    // Print Grand Total
    std::snprintf(buf_level, sizeof(buf_level), "  Total Stalls:   %.2fs (%lld us)\n", total_stall_micros / 1e6, total_stall_micros);
    value->append(buf_level);
    value->append("--- Write Stalls per Level ---\n\n");

    value->append("--- LogAndApply Conflict Stats ---\n");
    char buf_stats[250];
    long long flush_conflicts = log_and_apply_stats_.flush_conflicts.load(std::memory_order_relaxed);
    long long compaction_conflicts = log_and_apply_stats_.compaction_conflicts.load(std::memory_order_relaxed);
    std::snprintf(buf_stats, sizeof(buf_stats),"  Total Conflicts: %lld (Flush: %lld, Compaction: %lld)\n",
      flush_conflicts + compaction_conflicts, flush_conflicts, compaction_conflicts);
    value->append(buf_stats);

    if (!log_and_apply_stats_.recent_conflicts.empty()) {
      value->append("  Recent Conflicts (Caller, Level, BaseVer -> NewVer):\n");
      for (const auto& log : log_and_apply_stats_.recent_conflicts) {
        const char* type_str = (log.type == CallerView::kFlush) ? "Flush" : "Compaction";
        std::snprintf(buf_stats, sizeof(buf_stats),"    - %s, %llu -> %llu changes\n",
          type_str, static_cast<unsigned long long>(log.base_version_id),
          static_cast<unsigned long long>(log.new_version_id));
        value->append(buf_stats);
      }
    }
    value->append("--- LogAndApply Conflict Stats ---\n\n");

    // char schedule_buf[52];
    // long long total_sched_micros = total_schedule_micros_.load(std::memory_order_relaxed);
    // double total_sched_seconds = total_sched_micros / 1000000.0;
    // std::snprintf(schedule_buf, sizeof(schedule_buf),"MaybeScheduleFlush-Time: %.2f seconds (%lld us)\n", total_sched_seconds, total_sched_micros);
    // value->append(schedule_buf);

    char buf2[50]; 
    long long wal_micros = total_wal_time_micros_.load(std::memory_order_relaxed);
    std::snprintf(buf2, sizeof(buf2), "WAL-Write-Time: %.2f seconds (%lld us) ", wal_micros / 1e6, wal_micros);
    value->append(buf2);
    long long sync_micros = total_sync_time_micros_.load(std::memory_order_relaxed);
    std::snprintf(buf2, sizeof(buf2), "WAL-Sync-Time: %.2f seconds (%lld us)\n", sync_micros / 1e6, sync_micros);
    value->append(buf2);
    long long mem_micros = total_memtable_time_micros_.load(std::memory_order_relaxed);
    std::snprintf(buf2, sizeof(buf2), "Memtable-Write-Time: %.2f seconds (%lld us) ", mem_micros / 1e6, mem_micros);
    value->append(buf2);
    long long queue_micros = total_queue_wait_micros_.load(std::memory_order_relaxed);
    std::snprintf(buf2, sizeof(buf2), "Writer-Queue-Wait-Time: %.2f seconds (%lld us)\n", queue_micros / 1e6, queue_micros);
    value->append(buf2);
    // long long mr_micros = total_makeroom_micros_.load(std::memory_order_relaxed);
    // std::snprintf(buf2, sizeof(buf2), "Total-MakeRoomForWrite-Time: %.2f seconds (%lld us)\n", mr_micros / 1e6, mr_micros);
    // value->append(buf2);

    double total_io = 0;
    double user_io = 0;
    char buf[250];
    std::snprintf(buf, sizeof(buf),
                  "                               Compactions\n"
                  "Level Files Size(M) Time(s) ReadH(M) ReadC(M) WriteH(M) WriteC(M) m_comp si_comp ifile pfile se_comp ifiles pfiles comps triv_move t_last_b t_next_b\n"
                  "--------------------------------------------------\n");
    value->append(buf);
    for (int level = 0; level < config::kNumLevels; level++) {
      int files = versions_->NumLevelFiles(level);
      if (stats_[level].micros > 0 || files > 0) {
        std::snprintf(buf, sizeof(buf), "%5d %5d %7.0f %7.0f %8.0f %8.0f %9.0f %9.0f %6d %7d %5d %5d %7d %5d %6d %5d %9d %8f %8f\n",
                      level, files, versions_->NumLevelBytes(level) / 1048576.0,
                      stats_[level].micros / 1e6,stats_[level].bytes_read / 1048576.0,stats_[level].bytes_read / 1048576.0,            
                      stats_[level].bytes_written / 1048576.0, stats_[level].bytes_written / 1048576.0,                      
                      level_stats_[level].number_manual_compaction,level_stats_[level].number_size_compaction,
                      level_stats_[level].number_size_compaction_initiator_files,level_stats_[level].number_size_compaction_participant_files,
                      level_stats_[level].number_seek_compaction,level_stats_[level].number_seek_compaction_initiator_files,
                      level_stats_[level].number_seek_compaction_participant_files,
                      level_stats_[level].number_of_compactions,level_stats_[level].number_TrivialMove,
                      level_stats_[level].moved_directly_from_last_level_bytes / 1048576.0,
                      level_stats_[level].moved_from_this_level_bytes / 1048576.0);
        value->append(buf);   
      }
    }
    total_io = flush_stats_.actual_bytes_written/1048576.0;
    user_io = flush_stats_.user_bytes_written/1048576.0;
    snprintf(buf, sizeof(buf), "user_io:%.3fMB (L0 writes after deduplicate:%.3fMB)  total_ios: %.3fMB WriteAmplification: %2.4f",
         user_io, flush_stats_.total_bytes_flushed/1048756.0, total_io, total_io/ user_io);
    value->append(buf);
    return true;
    //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~

  } else if (in == "stats") {
    //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
    // I modified this part for print more details of a whole LSM
    double total_io = 0;
    double user_io = 0;
    char buf[250];
    std::snprintf(buf, sizeof(buf),
                  "                               Compactions\n"
                  "Level Files Size(M) Time(s) ReadH(M) ReadC(M) WriteH(M) WriteC(M) m_comp si_comp ifile pfile se_comp ifiles pfiles comps triv_move t_last_b t_next_b\n"
                  "--------------------------------------------------\n");
    value->append(buf);
    for (int level = 0; level < config::kNumLevels; level++) {
      int files = versions_->NumLevelFiles(level);
      if (stats_[level].micros > 0 || files > 0) {
        std::snprintf(buf, sizeof(buf), "%5d %5d %7.0f %7.0f %8.0f %8.0f %9.0f %9.0f %6d %7d %5d %5d %7d %5d %6d %5d %9d %8f %8f\n",
                      level, files, versions_->NumLevelBytes(level) / 1048576.0,
                      stats_[level].micros / 1e6,stats_[level].bytes_read / 1048576.0,stats_[level].bytes_read / 1048576.0,            
                      stats_[level].bytes_written / 1048576.0, stats_[level].bytes_written / 1048576.0,                      
                      level_stats_[level].number_manual_compaction,level_stats_[level].number_size_compaction,
                      level_stats_[level].number_size_compaction_initiator_files,level_stats_[level].number_size_compaction_participant_files,
                      level_stats_[level].number_seek_compaction,level_stats_[level].number_seek_compaction_initiator_files,
                      level_stats_[level].number_seek_compaction_participant_files,
                      level_stats_[level].number_of_compactions,level_stats_[level].number_TrivialMove,
                      level_stats_[level].moved_directly_from_last_level_bytes / 1048576.0,
                      level_stats_[level].moved_from_this_level_bytes / 1048576.0);
        value->append(buf);   
      }
    }
    total_io = flush_stats_.actual_bytes_written/1048576.0;
    user_io = flush_stats_.user_bytes_written/1048576.0;
    snprintf(buf, sizeof(buf), "user_io:%.3fMB (L0 writes after deduplicate:%.3fMB)  total_ios: %.3fMB WriteAmplification: %2.4f",
         user_io, flush_stats_.total_bytes_flushed/1048756.0, total_io, total_io/ user_io);
    value->append(buf);     
    return true;
    //  ~~~~~~~ WZZ's comments for his adding source codes ~~~~~~~
  } else if (in == "sstables") {
    *value = versions_->current()->DebugString();
    return true;
  } else if (in == "approximate-memory-usage") {
    size_t total_usage = options_.block_cache->TotalCharge();
    if (mem_) {
      total_usage += mem_->ApproximateMemoryUsage();
    }
    if (imm_) {
      total_usage += imm_->ApproximateMemoryUsage();
    }
    char buf[50];
    std::snprintf(buf, sizeof(buf), "%llu",
                  static_cast<unsigned long long>(total_usage));
    value->append(buf);
    return true;
  }

  return false;
}

void DBImpl::GetApproximateSizes(const Range* range, int n, uint64_t* sizes) {
  // TODO(opt): better implementation
  MutexLock l(&mutex_);
  Version* v = versions_->current();
  v->Ref();
  approximate_version_ref_count.fetch_add(1);

  for (int i = 0; i < n; i++) {
    // Convert user_key into a corresponding internal key.
    InternalKey k1(range[i].start, kMaxSequenceNumber, kValueTypeForSeek);
    InternalKey k2(range[i].limit, kMaxSequenceNumber, kValueTypeForSeek);
    uint64_t start = versions_->ApproximateOffsetOf(v, k1);
    uint64_t limit = versions_->ApproximateOffsetOf(v, k2);
    sizes[i] = (limit >= start ? limit - start : 0);
  }

  v->Unref();
  approximate_version_unref_count.fetch_add(1);
}

// Default implementations of convenience methods that subclasses of DB
// can call if they wish
Status DB::Put(const WriteOptions& opt, const Slice& key, const Slice& value) {
  LEVELDB_PROFILE_DUAL_PUT();
  WriteBatch batch;
  batch.Put(key, value);
  return Write(opt, &batch);
}

Status DB::Delete(const WriteOptions& opt, const Slice& key) {
  WriteBatch batch;
  batch.Delete(key);
  return Write(opt, &batch);
}

DB::~DB() = default;

Status DB::Open(const Options& options, const std::string& dbname, DB** dbptr) {

  #ifdef USE_O_DIRECT
    fprintf(stderr, "Macro USE_O_DIRECT is defined.\n");
  #else
    fprintf(stderr, "Macro USE_O_DIRECT is NOT defined.\n");
  #endif
  *dbptr = nullptr;
  std::fprintf(stderr, "entering db_impl.cc:%s\n", dbname.c_str());
  fflush(stderr);
  DBImpl* impl = new DBImpl(options, dbname);
  impl->metadata_mutex_.Lock();
  VersionEdit edit;
  // Recover handles create_if_missing, error_if_exists
  bool save_manifest = false;
  Status s = impl->Recover(&edit, &save_manifest);
  if (s.ok() && impl->mem_ == nullptr) {
    // Create new log and a corresponding memtable.
    uint64_t new_log_number = impl->versions_->NewFileNumber();
    WritableFile* lfile;
    s = options.env->NewWritableFile(LogFileName(dbname, new_log_number),&lfile);
    if (s.ok()) {
      edit.SetLogNumber(new_log_number);
      impl->logfile_ = lfile;
      impl->logfile_number_ = new_log_number;
      impl->log_ = new log::Writer(lfile);
      impl->mem_ = new MemTable(impl->internal_comparator_);
      impl->mem_->Ref();
    }
  }
  if (s.ok() && save_manifest) {
    edit.SetPrevLogNumber(0);  // No older logs needed after recovery.
    edit.SetLogNumber(impl->logfile_number_);
    s = impl->versions_->LogAndApply(&edit, &impl->metadata_mutex_);
  }
  if (s.ok()) {
    impl->RemoveObsoleteFiles2();
    impl->num_level0_files_.store(impl->versions_->NumLevelFiles(0), std::memory_order_relaxed);
    impl->MaybeScheduleCompaction(true);
  }
  impl->metadata_mutex_.Unlock();
  if (s.ok()) {
    assert(impl->mem_ != nullptr);
    *dbptr = impl;
  } else {
    delete impl;
  }

  return s;
}

Snapshot::~Snapshot() = default;

Status DestroyDB(const std::string& dbname, const Options& options) {
  Env* env = options.env;
  std::vector<std::string> filenames;
  Status result = env->GetChildren(dbname, &filenames);
  if (!result.ok()) {
    // Ignore error in case directory does not exist
    return Status::OK();
  }

  FileLock* lock;
  const std::string lockname = LockFileName(dbname);
  result = env->LockFile(lockname, &lock);
  if (result.ok()) {
    uint64_t number;
    FileType type;
    for (size_t i = 0; i < filenames.size(); i++) {
      if (ParseFileName(filenames[i], &number, &type) &&
          type != kDBLockFile) {  // Lock file will be deleted at end
        Status del = env->RemoveFile(dbname + "/" + filenames[i]);
        if (result.ok() && !del.ok()) {
          result = del;
        }
      }
    }
    env->UnlockFile(lock);  // Ignore error since state is already gone
    env->RemoveFile(lockname);
    env->RemoveDir(dbname);  // Ignore error in case dir contains other files
  }
  return result;
}

}  // namespace leveldb
