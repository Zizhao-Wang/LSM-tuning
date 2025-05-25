// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/cache.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "port/port.h"
#include "port/thread_annotations.h"
#include "util/hash.h"
#include "util/mutexlock.h"

namespace leveldb {

Cache::~Cache() {}

namespace {

// LRU cache implementation
//
// Cache entries have an "in_cache" boolean indicating whether the cache has a
// reference on the entry.  The only ways that this can become false without the
// entry being passed to its "deleter" are via Erase(), via Insert() when
// an element with a duplicate key is inserted, or on destruction of the cache.
//
// The cache keeps two linked lists of items in the cache.  All items in the
// cache are in one list or the other, and never both.  Items still referenced
// by clients but erased from the cache are in neither list.  The lists are:
// - in-use:  contains the items currently referenced by clients, in no
//   particular order.  (This list is used for invariant checking.  If we
//   removed the check, elements that would otherwise be on this list could be
//   left as disconnected singleton lists.)
// - LRU:  contains the items not currently referenced by clients, in LRU order
// Elements are moved between these lists by the Ref() and Unref() methods,
// when they detect an element in the cache acquiring or losing its only
// external reference.

// An entry is a variable length heap-allocated structure.  Entries
// are kept in a circular doubly linked list ordered by access time.
struct LRUHandle {
  void* value;
  void (*deleter)(const Slice&, void* value);
  LRUHandle* next_hash;
  LRUHandle* next;
  LRUHandle* prev;
  size_t charge;  // TODO(opt): Only allow uint32_t?
  size_t key_length;
  bool in_cache;     // Whether entry is in the cache.
  uint32_t refs;     // References, including cache reference, if present.
  uint32_t hash;     // Hash of key(); used for fast sharding and comparisons
  char key_data[1];  // Beginning of key

  Slice key() const {
    // next is only equal to this if the LRU handle is the list head of an
    // empty list. List heads never have meaningful keys.
    assert(next != this);

    return Slice(key_data, key_length);
  }
};

// We provide our own simple hash table since it removes a whole bunch
// of porting hacks and is also faster than some of the built-in hash
// table implementations in some of the compiler/runtime combinations
// we have tested.  E.g., readrandom speeds up by ~5% over the g++
// 4.4.3's builtin hashtable.
class HandleTable {
 public:
  HandleTable() : length_(0), elems_(0), list_(nullptr) { Resize(); }
  ~HandleTable() { delete[] list_; }

  LRUHandle* Lookup(const Slice& key, uint32_t hash) {
    return *FindPointer(key, hash);
  }

  LRUHandle* Insert(LRUHandle* h) {
    LRUHandle** ptr = FindPointer(h->key(), h->hash);
    LRUHandle* old = *ptr;
    h->next_hash = (old == nullptr ? nullptr : old->next_hash);
    *ptr = h;
    if (old == nullptr) {
      ++elems_;
      if (elems_ > length_) {
        // Since each cache entry is fairly large, we aim for a small
        // average linked list length (<= 1).
        Resize();
      }
    }
    return old;
  }

  LRUHandle* Remove(const Slice& key, uint32_t hash) {
    LRUHandle** ptr = FindPointer(key, hash);
    LRUHandle* result = *ptr;
    if (result != nullptr) {
      *ptr = result->next_hash;
      --elems_;
    }
    return result;
  }

 private:
  // The table consists of an array of buckets where each bucket is
  // a linked list of cache entries that hash into the bucket.
  uint32_t length_;
  uint32_t elems_;
  LRUHandle** list_;

  // Return a pointer to slot that points to a cache entry that
  // matches key/hash.  If there is no such cache entry, return a
  // pointer to the trailing slot in the corresponding linked list.
  LRUHandle** FindPointer(const Slice& key, uint32_t hash) {
    LRUHandle** ptr = &list_[hash & (length_ - 1)];
    while (*ptr != nullptr && ((*ptr)->hash != hash || key != (*ptr)->key())) {
      ptr = &(*ptr)->next_hash;
    }
    return ptr;
  }

  void Resize() {
    uint32_t new_length = 4;
    while (new_length < elems_) {
      new_length *= 2;
    }
    LRUHandle** new_list = new LRUHandle*[new_length];
    memset(new_list, 0, sizeof(new_list[0]) * new_length);
    uint32_t count = 0;
    for (uint32_t i = 0; i < length_; i++) {
      LRUHandle* h = list_[i];
      while (h != nullptr) {
        LRUHandle* next = h->next_hash;
        uint32_t hash = h->hash;
        LRUHandle** ptr = &new_list[hash & (new_length - 1)];
        h->next_hash = *ptr;
        *ptr = h;
        h = next;
        count++;
      }
    }
    assert(elems_ == count);
    delete[] list_;
    list_ = new_list;
    length_ = new_length;
  }
};

// A single shard of sharded cache.
class LevelLRUCache {
 public:
  LevelLRUCache();
  ~LevelLRUCache();

  // Separate from constructor so caller can easily make an array of LevelLRUCache
  void SetCapacity(size_t capacity) { capacity_ = capacity; }

  // Like Cache methods, but with an extra "hash" parameter.
  Cache::Handle* Insert(const Slice& key, uint32_t hash, void* value,
                        size_t charge,
                        void (*deleter)(const Slice& key, void* value));
  Cache::Handle* Lookup(const Slice& key, uint32_t hash);
  void Release(Cache::Handle* handle);
  void Erase(const Slice& key, uint32_t hash);
  void Prune();


  size_t TotalCharge() const {
    MutexLock l(&mutex_);
    return usage_;
  }

// =======================================================================
  //                 Per-Shard Statistics Methods
  // =======================================================================
  // These methods provide thread-safe access to the performance
  // statistics (hits, misses, lookups) for this specific cache shard.
  // =======================================================================

  /**
   * @brief Get the number of cache hits for this shard.
   *
   * This operation is thread-safe.
   * @return size_t The number of hits.
   */
  size_t GetHits() const {
    MutexLock l(&mutex_);
    return hits_;
  }

  /**
   * @brief Get the number of cache misses for this shard.
   *
   * This operation is thread-safe.
   * @return size_t The number of misses.
   */
  size_t GetMisses() const {
    MutexLock l(&mutex_);
    return misses_;
  }

  /**
   * @brief Get the total number of lookup operations for this shard.
   *
   * This operation is thread-safe.
   * @return size_t The total number of lookups (hits + misses).
   */
  size_t GetLookups() const {
    MutexLock l(&mutex_);
    return lookups_;
  }

  /**
   * @brief Resets the statistics counters (hits, misses, lookups)
   * for this shard back to zero.
   *
   * This operation is thread-safe.
   */
  void ResetStats() {
    MutexLock l(&mutex_);
    hits_ = 0;
    misses_ = 0;
    lookups_ = 0;
  }

  // =======================================================================
  //               End of Per-Shard Statistics Methods
  // =======================================================================

  size_t EvictSomeLRU(size_t bytes_to_evict) {
    MutexLock l(&mutex_);
    size_t evicted_charge = 0;

    // Keep evicting from the tail of the LRU list as long as:
    // 1. We still need to evict more bytes.
    // 2. The LRU list is not empty.
    while (evicted_charge < bytes_to_evict && lru_.next != &lru_) {
      LRUHandle* e = lru_.next; // Get the oldest entry

      assert(e->refs == 1); // Entries in lru_ list must have refs == 1

      size_t current_charge = e->charge;

      // Remove from hash table and then finish erase (removes from list, updates usage, unrefs)
      bool erased = FinishErase(table_.Remove(e->key(), e->hash));

      if (erased) {
        evicted_charge += current_charge;
      } else {
        // This should ideally not happen if the item was properly in the LRU list.
        // If it does, break to avoid potential infinite loops or errors.
        assert(erased); // Will fail in debug, good for catching issues
        break;
      }
    }
    return evicted_charge;
  }

 private:
  void LRU_Remove(LRUHandle* e);
  void LRU_Append(LRUHandle* list, LRUHandle* e);
  void Ref(LRUHandle* e);
  void Unref(LRUHandle* e);
  bool FinishErase(LRUHandle* e) EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Initialized before use.
  size_t capacity_;

  // mutex_ protects the following state.
  mutable port::Mutex mutex_;
  size_t usage_ GUARDED_BY(mutex_);

  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  // Entries have refs==1 and in_cache==true.
  LRUHandle lru_ GUARDED_BY(mutex_);

  // Dummy head of in-use list.
  // Entries are in use by clients, and have refs >= 2 and in_cache==true.
  LRUHandle in_use_ GUARDED_BY(mutex_);

  HandleTable table_ GUARDED_BY(mutex_);

    // <--- 新增: 统计计数器 --->
  uint64_t hits_ GUARDED_BY(mutex_);
  uint64_t misses_ GUARDED_BY(mutex_);
  uint64_t lookups_ GUARDED_BY(mutex_);
};

LevelLRUCache::LevelLRUCache() : capacity_(0), usage_(0), hits_(0), misses_(0), lookups_(0) {
  // Make empty circular linked lists.
  lru_.next = &lru_;
  lru_.prev = &lru_;
  in_use_.next = &in_use_;
  in_use_.prev = &in_use_;
}

LevelLRUCache::~LevelLRUCache() {
  assert(in_use_.next == &in_use_);  // Error if caller has an unreleased handle
  for (LRUHandle* e = lru_.next; e != &lru_;) {
    LRUHandle* next = e->next;
    assert(e->in_cache);
    e->in_cache = false;
    assert(e->refs == 1);  // Invariant of lru_ list.
    Unref(e);
    e = next;
  }
}

void LevelLRUCache::Ref(LRUHandle* e) {
  if (e->refs == 1 && e->in_cache) {  // If on lru_ list, move to in_use_ list.
    LRU_Remove(e);
    LRU_Append(&in_use_, e);
  }
  e->refs++;
}

void LevelLRUCache::Unref(LRUHandle* e) {
  assert(e->refs > 0);
  e->refs--;
  if (e->refs == 0) {  // Deallocate.
    assert(!e->in_cache);
    (*e->deleter)(e->key(), e->value);
    free(e);
  } else if (e->in_cache && e->refs == 1) {
    // No longer in use; move to lru_ list.
    LRU_Remove(e);
    LRU_Append(&lru_, e);
  }
}

void LevelLRUCache::LRU_Remove(LRUHandle* e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
}

void LevelLRUCache::LRU_Append(LRUHandle* list, LRUHandle* e) {
  // Make "e" newest entry by inserting just before *list
  e->next = list;
  e->prev = list->prev;
  e->prev->next = e;
  e->next->prev = e;
}

Cache::Handle* LevelLRUCache::Lookup(const Slice& key, uint32_t hash) {
  MutexLock l(&mutex_);
  lookups_++;
  LRUHandle* e = table_.Lookup(key, hash);
  if (e != nullptr) {
    hits_++;
    Ref(e);
  }else{
    misses_++;
  }
  return reinterpret_cast<Cache::Handle*>(e);
}

void LevelLRUCache::Release(Cache::Handle* handle) {
  MutexLock l(&mutex_);
  Unref(reinterpret_cast<LRUHandle*>(handle));
}

Cache::Handle* LevelLRUCache::Insert(const Slice& key, uint32_t hash, void* value,
                                size_t charge,
                                void (*deleter)(const Slice& key,
                                                void* value)) {
  MutexLock l(&mutex_);

  LRUHandle* e =
      reinterpret_cast<LRUHandle*>(malloc(sizeof(LRUHandle) - 1 + key.size()));
  e->value = value;
  e->deleter = deleter;
  e->charge = charge;
  e->key_length = key.size();
  e->hash = hash;
  e->in_cache = false;
  e->refs = 1;  // for the returned handle.
  std::memcpy(e->key_data, key.data(), key.size());

  if (capacity_ > 0) {
    e->refs++;  // for the cache's reference.
    e->in_cache = true;
    LRU_Append(&in_use_, e);
    usage_ += charge;
    FinishErase(table_.Insert(e));
  } else {  // don't cache. (capacity_==0 is supported and turns off caching.)
    // next is read by key() in an assert, so it must be initialized
    e->next = nullptr;
  }
  while (usage_ > capacity_ && lru_.next != &lru_) {
    LRUHandle* old = lru_.next;
    assert(old->refs == 1);
    bool erased = FinishErase(table_.Remove(old->key(), old->hash));
    if (!erased) {  // to avoid unused variable when compiled NDEBUG
      assert(erased);
    }
  }

  return reinterpret_cast<Cache::Handle*>(e);
}

// If e != nullptr, finish removing *e from the cache; it has already been
// removed from the hash table.  Return whether e != nullptr.
bool LevelLRUCache::FinishErase(LRUHandle* e) {
  if (e != nullptr) {
    assert(e->in_cache);
    LRU_Remove(e);
    e->in_cache = false;
    usage_ -= e->charge;
    Unref(e);
  }
  return e != nullptr;
}

void LevelLRUCache::Erase(const Slice& key, uint32_t hash) {
  MutexLock l(&mutex_);
  FinishErase(table_.Remove(key, hash));
}

void LevelLRUCache::Prune() {
  MutexLock l(&mutex_);
  while (lru_.next != &lru_) {
    LRUHandle* e = lru_.next;
    assert(e->refs == 1);
    bool erased = FinishErase(table_.Remove(e->key(), e->hash));
    if (!erased) {  // to avoid unused variable when compiled NDEBUG
      assert(erased);
    }
  }
}

static const int kNumShardBits = 4;
static const int kNumShards = 1 << kNumShardBits;

class DynamicalShardedLRUCache : public Cache {
 private:
  LevelLRUCache shard_[kNumShards];
  port::Mutex id_mutex_;
  uint64_t last_id_;
  size_t overall_capacity_; // <--- 新增: 记录总容量


  static inline uint32_t HashSlice(const Slice& s) {
    return Hash(s.data(), s.size(), 0);
  }

  static uint32_t Shard(uint32_t hash) { return hash >> (32 - kNumShardBits); }

   // <--- 新增: 应用容量到分片的方法 --->
  void ApplyCapacity() {
    const size_t per_shard = (overall_capacity_ + (kNumShards - 1)) / kNumShards;
    for (int s = 0; s < kNumShards; s++) {
        shard_[s].SetCapacity(per_shard);
    }
  }

 public:

  explicit DynamicalShardedLRUCache(size_t capacity) : overall_capacity_(capacity),last_id_(0) {
    ApplyCapacity(); 
    // const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
    // for (int s = 0; s < kNumShards; s++) {
    //   shard_[s].SetCapacity(per_shard);
    // }
  }

  ~DynamicalShardedLRUCache() override {}

    // <--- 新增: SetCapacity 方法 --->
  void SetCapacity(size_t capacity) {
      MutexLock l(&id_mutex_); 
      overall_capacity_ = capacity;
      ApplyCapacity();
    }

  // <--- 新增: PruneToCapacity 方法 (可选但推荐) --->
  // 这个方法会主动剔除数据, 直到满足新的容量限制
  void PruneToCapacity() {
    MutexLock l(&id_mutex_); // 确保调整期间容量不变
    size_t current_usage = TotalCharge();
    if (current_usage <= overall_capacity_) {
        return; // 已经满足要求
    }

    size_t to_evict = current_usage - overall_capacity_;

    // 实现一个策略来从各个分片中剔除数据
    // 简单的策略: 从每个分片平均剔除, 或者优先从使用率高的分片剔除
    // 注意: 这里需要调用 LevelLRUCache::Prune() 或实现更精细的 Evict 逻辑
    // LevelLRUCache::Prune() 会清空整个 LRU 列表, 可能过于激进.
    // 更好的方法是修改 LevelLRUCache, 添加一个 EvictNBytes(bytes) 或 EvictNItems(N) 的方法.
    // 假设我们能修改 LevelLRUCache 添加 EvictLRUItem() (需要小心处理锁)
    for (int s = 0; s < kNumShards && to_evict > 0; s++) {
        size_t evicted = shard_[s].EvictSomeLRU(to_evict / (kNumShards - s)); // <--- 假设有这个方法
        to_evict -= evicted;
    }
  }

  // <--- 新增: 获取当前容量 --->
  size_t GetCapacity() const {
    // MutexLock l(&id_mutex_); // 根据锁策略决定是否加锁
    return overall_capacity_;
  }


  Handle* Insert(const Slice& key, void* value, size_t charge,
                 void (*deleter)(const Slice& key, void* value)) override {
    const uint32_t hash = HashSlice(key);
    return shard_[Shard(hash)].Insert(key, hash, value, charge, deleter);
  }
  Handle* Lookup(const Slice& key) override {
    const uint32_t hash = HashSlice(key);
    return shard_[Shard(hash)].Lookup(key, hash);
  }
  void Release(Handle* handle) override {
    LRUHandle* h = reinterpret_cast<LRUHandle*>(handle);
    shard_[Shard(h->hash)].Release(handle);
  }
  void Erase(const Slice& key) override {
    const uint32_t hash = HashSlice(key);
    shard_[Shard(hash)].Erase(key, hash);
  }
  void* Value(Handle* handle) override {
    return reinterpret_cast<LRUHandle*>(handle)->value;
  }
  uint64_t NewId() override {
    MutexLock l(&id_mutex_);
    return ++(last_id_);
  }
  void Prune() override {
    for (int s = 0; s < kNumShards; s++) {
      shard_[s].Prune();
    }
  }
  size_t TotalCharge() const override {
    size_t total = 0;
    for (int s = 0; s < kNumShards; s++) {
      total += shard_[s].TotalCharge();
    }
    return total;
  }

  // =======================================================================
  //                   Statistics Gathering Methods
  // =======================================================================
  // These methods provide access to cache performance statistics by
  // aggregating data from all shards.
  // =======================================================================

  /**
   * @brief Get the total number of cache hits across all shards.
   *
   * @return size_t The total number of hits.
   * @note This method iterates through shards without a global lock for
   * performance. As such, the returned value is an approximation
   * in highly concurrent scenarios, but generally sufficient for
   * monitoring and tuning.
   */
  size_t GetTotalHits() const {
    size_t total = 0;
    for (int s = 0; s < kNumShards; s++) {
      total += shard_[s].GetHits();
    }
    return total;
  }

  /**
   * @brief Get the total number of cache misses across all shards.
   *
   * @return size_t The total number of misses.
   * @note Similar to GetTotalHits, this offers an approximation due to
   * the lack of a global lock during aggregation.
   */
  size_t GetTotalMisses() const {
    size_t total = 0;
    for (int s = 0; s < kNumShards; s++) {
      total += shard_[s].GetMisses();
    }
    return total;
  }

  /**
   * @brief Get the total number of lookup operations (hits + misses)
   * across all shards.
   *
   * @return size_t The total number of lookups.
   * @note Provides an approximate value in concurrent environments.
   */
  size_t GetTotalLookups() const {
    size_t total = 0;
    for (int s = 0; s < kNumShards; s++) {
      total += shard_[s].GetLookups();
    }
    return total;
  }

  /**
   * @brief Calculate and return the overall cache hit rate.
   *
   * The hit rate is calculated as (Total Hits) / (Total Lookups).
   *
   * @return double The cache hit rate, ranging from 0.0 to 1.0.
   * Returns 0.0 if there have been no lookups.
   */
  double GetHitRate() const {
    size_t lookups = GetTotalLookups();
    if (lookups == 0) {
      return 0.0;  // No lookups yet, define hit rate as 0.0
    }
    return static_cast<double>(GetTotalHits()) / lookups;
  }

  /**
   * @brief Resets all statistics counters (hits, misses, lookups)
   * across all shards.
   *
   * Useful for measuring performance over specific intervals.
   */
  void ResetAllStats() {
    for (int s = 0; s < kNumShards; s++) {
      shard_[s].ResetStats();
    }
  }

  // =======================================================================
  //                 End of Statistics Gathering Methods
  // =======================================================================
};

}  // end anonymous namespace

Cache* NewLRUCache(size_t capacity) { return new DynamicalShardedLRUCache(capacity); }

}  // namespace leveldb
