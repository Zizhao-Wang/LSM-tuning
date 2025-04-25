// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/builder.h"

#include "db/dbformat.h"
#include "db/filename.h"
#include "db/table_cache.h"
#include "db/version_edit.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/iterator.h"

namespace leveldb {

Status BuildTable(const std::string& dbname, Env* env, const Options& options,
                  TableCache* table_cache, Iterator* iter, FileMetaData* meta, double* key_freq_variance) {
  Status s;
  meta->file_size = 0;
  iter->SeekToFirst();

  uint64_t group_count = 0;
  double sum = 0.0, sum_sq = 0.0;
  uint64_t count = 0;
  bool first = true;
  Slice prev_key;
  Slice cur_key;

  std::string fname = TableFileName(dbname, meta->number);
  if (iter->Valid()) {

    WritableFile* file;
    s = env->NewWritableFile(fname, &file);
    if (!s.ok()) {
      return s;
    }

    TableBuilder* builder = new TableBuilder(options, file);
    meta->smallest.DecodeFrom(iter->key());
    Slice key;
    for (; iter->Valid(); iter->Next()) {

      key = iter->key();
      builder->Add(key, iter->value());

      cur_key = iter->key();
      if(first){
        prev_key = cur_key;
        count = 1;
        first = false;
      }else if (cur_key.compare(prev_key)==0) {
        ++count;
      } else {
        ++group_count;
        sum    += static_cast<double>(count);
        sum_sq += static_cast<double>(count) * count;
        prev_key = cur_key;
        count    = 1;
      }
    }
    if (!key.empty()) {
      meta->largest.DecodeFrom(key);
    }

    // 处理最后一个 key
    if (!first) {
      ++group_count;
      sum    += static_cast<double>(count);
      sum_sq += static_cast<double>(count) * count;
    }

    // 计算并返回方差：Var = E[X^2] - (E[X])^2
    if (group_count > 0) {
      double mean = sum / static_cast<double>(group_count);
      double var  = sum_sq / static_cast<double>(group_count) - mean * mean;
      if (key_freq_variance) *key_freq_variance = var;
    } else if (key_freq_variance) {
      *key_freq_variance = 0.0;
    }

    // Finish and check for builder errors
    s = builder->Finish();
    if (s.ok()) {
      meta->file_size = builder->FileSize();
      assert(meta->file_size > 0);
    }
    delete builder;

    // Finish and check for file errors
    if (s.ok()) {
      s = file->Sync();
    }
    if (s.ok()) {
      s = file->Close();
    }
    delete file;
    file = nullptr;

    if (s.ok()) {
      // Verify that the table is usable
      Iterator* it = table_cache->NewIterator(ReadOptions(), meta->number,
                                              meta->file_size);
      s = it->status();
      delete it;
    }
  }

  // Check for input iterator errors
  if (!iter->status().ok()) {
    s = iter->status();
  }

  if (s.ok() && meta->file_size > 0) {
    // Keep it
  } else {
    env->RemoveFile(fname);
  }
  return s;
}

}  // namespace leveldb
