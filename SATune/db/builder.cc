// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#include <map>
#include <cmath>

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
                  TableCache* table_cache, Iterator* iter, FileMetaData* meta) {
  Status s;
  meta->file_size = 0;
  iter->SeekToFirst();

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
    }
    if (!key.empty()) {
      meta->largest.DecodeFrom(key);
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


Status BuildTableWithVariance(const std::string& dbname, Env* env, const Options& options,
              TableCache* table_cache, Iterator* iter, FileMetaData* meta,double* variance_output) {
  Status s;
  meta->file_size = 0;
  iter->SeekToFirst();

  std::string fname = TableFileName(dbname, meta->number);
  std::map<std::string, int> key_frequencies; // 用于存储 Key 的频率
  int total_keys = 0; // 总 Key 的数量 (包括重复)

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

      // --- 计算频率 ---
      key_frequencies[key.ToString()]++; // 使用 ToString() 确保 key 被复制
      total_keys++;
      // --- 结束计算 ---
    }
    if (!key.empty()) {
      meta->largest.DecodeFrom(key);
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

    // --- 计算方差 ---
  *variance_output = 0.0;
  if (s.ok() && !key_frequencies.empty()) {
    double sum_freq = 0.0;
    double sum_freq_sq = 0.0;
    int unique_keys = key_frequencies.size();

    for (const auto& pair : key_frequencies) {
      sum_freq += pair.second; // pair.second 是频率
      sum_freq_sq += std::pow(pair.second, 2);
    }

    // 注意：这里的 total_keys 应该等于 sum_freq
    // assert(total_keys == static_cast<int>(sum_freq)); 

    double mean = sum_freq / unique_keys;
    double mean_sq = sum_freq_sq / unique_keys;

    *variance_output = mean_sq - std::pow(mean, 2); // 方差 = E[X^2] - (E[X])^2
  }

  if (s.ok() && meta->file_size > 0) {
    // Keep it
  } else {
    env->RemoveFile(fname);
  }
  return s;
}

Status BuildTableWithDiscardCount(const std::string& dbname, Env* env,const Options& options,
      TableCache* table_cache, Iterator* iter,FileMetaData* meta,int* discarded_count_output) { // 输出参数：可抛弃数量
  Status s;
  meta->file_size = 0;
  iter->SeekToFirst();

  std::string fname = TableFileName(dbname, meta->number);
  int total_keys = 0;
  int unique_keys = 1;
  std::string last_user_key_storage; // 用 string 来安全存储上一个唯一 Key 的数据
  last_user_key_storage.assign(iter->key().data(), iter->key().size()-8);

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

      // --- 计算唯一 Key 和总 Key (优化版) ---
      total_keys++;

      // 1. 提取 User Key (不创建 string)
      //    (再次提醒：确认 key.size() - 8 是正确的 InternalKey 格式)
      Slice current_user_key(key.data(), key.size() - 8);
      if (Slice(last_user_key_storage).compare(current_user_key) != 0) {
          unique_keys++;
          // 3. 只有当 Key 变化时，才更新 string 存储 (创建/复制 string)
          last_user_key_storage.assign(current_user_key.data(), current_user_key.size());
      }
      // --- 结束计算 ---
    }
    // 处理最后一个 key
    if (!key.empty()) {
      meta->largest.DecodeFrom(key); // 仍然用完整的 key
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

  // --- 计算抛弃数量 ---
  *discarded_count_output = total_keys - unique_keys;
  // --- 结束计算 ---

  if (s.ok() && meta->file_size > 0) {
    // Keep it
  } else {
    env->RemoveFile(fname);
  }
  return s;
}

}  // namespace leveldb
