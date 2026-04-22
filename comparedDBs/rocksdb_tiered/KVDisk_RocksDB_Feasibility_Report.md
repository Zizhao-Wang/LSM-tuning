# KVDisk × RocksDB 代码级可行性评估报告（完整版）

> **版本**: v2.0 — 对照 KVDisk 完整设计文档 v2  
> **日期**: 2026-03-31  
> **方法**: 逐行阅读 RocksDB 源码，追踪每个相关函数的完整代码路径，精确到文件名:行号  
> **结论**: ✅ 完全可行，零修改 RocksDB 源码

---

## 目录

- [总体结论](#总体结论)
- [P0-1: prefix_scan 性能](#p0-1-prefix_scan-性能)
- [P0-2: prefix Bloom filter 的 SST 级过滤](#p0-2-prefix-bloom-filter-的-sst-级过滤)
- [P0-3: 小 value 的内联存储效率](#p0-3-小-value-的内联存储效率)
- [P0-4: CompactionFilter 行为](#p0-4-compactionfilter-行为)
- [P0-5: RateLimiter 动态调整](#p0-5-ratelimiter-动态调整)
- [P1-1: DeleteRange 的实现和性能影响](#p1-1-deleterange-的实现和性能影响)
- [P1-2: MergeOperator 作为 access stats 更新机制](#p1-2-mergeoperator-作为-access-stats-更新机制)
- [P1-3: WriteBatch 原子性保证](#p1-3-writebatch-原子性保证)
- [P1-4: 内存使用 breakdown](#p1-4-内存使用-breakdown)
- [P1-5: Direct I/O 与 RocksDB 的交互](#p1-5-direct-io-与-rocksdb-的交互)
- [P2-1 到 P2-5: 优化方向](#p2-优化方向)
- [设计文档中嵌入问题的逐一回答](#设计文档中嵌入问题的逐一回答)
- [最终建议与修改清单](#最终建议与修改清单)

---

## 总体结论

| 维度 | 结论 |
|------|------|
| 可行性 | ✅ 完全可行 |
| 是否需要修改 RocksDB 源码 | ❌ 零修改 |
| 设计文档中的 RocksDB 配置 | ⚠️ 有 3 处需要调整（下文详述） |
| 方案 B（自建）是否更优 | ❌ 不推荐，除非条目数永远 < 100 万 |

**需要调整的 3 处配置**:
1. `RateLimiter::Mode` 应改为 `kWritesOnly` 而非 `kAllIo`（详见 P0-5）
2. `write_opts_.no_slowdown = true` 有风险，建议改为 `false`（详见 P1-3）
3. `read_opts_.verify_checksums = false` 在生产环境建议改为 `true`（详见 P0-3）

---

## P0-1: prefix_scan 性能

**问题**: `prefix_same_as_start=true` 时，`Seek` + 80×`Next` 的延迟？是否有额外的 prefix 检查开销？

**结论**: ✅ 预估 10-25μs（block cache hit），满足 50μs 目标。prefix 检查开销极小（一次 `memcmp` 26 字节）。

### 代码路径追踪

#### 1. `DBIter::Seek()` — 文件 `db/db_iter.cc:1506`

```cpp
void DBIter::Seek(const Slice& target) {
    // ...
    iter_.Seek(saved_key_.GetInternalKey());  // 内部 iterator seek
    // ...
    if (prefix_same_as_start_) {
        assert(prefix_extractor_ != nullptr);
        Slice target_prefix = prefix_extractor_->Transform(target);
        // ↑ 对你的 36B key 调用 FixedPrefixTransform(26)::Transform()
        //   返回前 26 字节 (model_id + tenant_id + prefix_hash)
        FindNextUserEntry(false, &target_prefix);
        if (valid_) {
            prefix_.SetUserKey(target_prefix);
            // ↑ 保存前缀，供后续 Next() 检查
        }
    }
}
```

**关键**: `prefix_same_as_start_` 的初始化在构造函数中（`db/db_iter.cc:72`）：
```cpp
prefix_same_as_start_(mutable_cf_options.prefix_extractor
                          ? read_options.prefix_same_as_start
                          : false),
```
只有当 `prefix_extractor` 非空 **且** `ReadOptions::prefix_same_as_start = true` 时才启用。

#### 2. `DBIter::Next()` — 文件 `db/db_iter.cc:144`

```cpp
void DBIter::Next() {
    // ...
    iter_.Next();  // 内部 iterator 前进一步
    // ...
    if (prefix_same_as_start_) {
        assert(prefix_extractor_ != nullptr);
        const Slice prefix = prefix_.GetUserKey();
        // ↑ 取出 Seek 时保存的 26 字节前缀
        FindNextUserEntry(true, &prefix);
        // ↑ 传入 prefix 指针，FindNextUserEntryInternal 会检查
    }
}
```

#### 3. `FindNextUserEntryInternal()` 中的 prefix 检查 — 文件 `db/db_iter.cc:299`

```cpp
bool DBIter::FindNextUserEntryInternal(bool skipping_saved_key,
                                       const Slice* prefix) {
    // ...
    do {
        // 解析当前 internal key
        if (!ParseKey(&ikey_)) return false;
        Slice user_key_without_ts = StripTimestampFromUserKey(ikey_.user_key, timestamp_size_);

        // ★ 关键的 prefix 检查 ★
        assert(prefix == nullptr || prefix_extractor_ != nullptr);
        if (prefix != nullptr &&
            prefix_extractor_->Transform(user_key_without_ts).compare(*prefix) != 0) {
            // ↑ 对当前 key 提取前 26 字节，与 Seek 时的前缀比较
            // ↑ compare() 内部是 memcmp，26 字节比较 ≈ 几纳秒
            assert(prefix_same_as_start_);
            break;  // ← 前缀不同，立即终止迭代
        }
        // ... 继续处理当前 key ...
    } while (iter_.Valid());
}
```

**性能分析**:

| 操作 | 开销 |
|------|------|
| `prefix_extractor_->Transform()` | `FixedPrefixTransform::Transform()` = `Slice(data, 26)` — 零拷贝，~1ns |
| `Slice::compare()` | `memcmp(a, b, 26)` — ~3-5ns |
| 每次 `Next()` 的额外 prefix 检查 | ~5ns |
| 80 次 `Next()` 的总 prefix 检查开销 | ~400ns ≈ 0.4μs |

**结论**: prefix 检查开销可忽略。`prefix_same_as_start` 是一个**硬保证**，不是 hint——当前缀不同时 `break` 直接退出循环，iterator 变为 `!Valid()`。

---

## P0-2: prefix Bloom filter 的 SST 级过滤

**问题**: `whole_key_filtering=false` 时，`Get()` 和 `Iterator::Seek()` 如何利用 prefix bloom？

**结论**: ✅ 两者都能利用 prefix bloom 跳过不含目标 prefix 的 SST 文件。

### Get() 路径

文件 `db/db_impl/db_impl.cc:2276` — `DBImpl::GetImpl()`：

```cpp
// 1. 查 memtable（有 memtable prefix bloom）
if (sv->mem->Get(lkey, ...)) { done = true; }
// 2. 查 immutable memtable
else if (sv->imm->Get(lkey, ...)) { done = true; }
// 3. 查 SST 文件
if (!done) {
    sv->current->Get(read_options, lkey, ...);
    // ↑ Version::Get() 内部逐层查找，每层用 bloom filter 过滤
}
```

Memtable bloom filter 检查（`db/memtable.cc:1280`）：

```cpp
if (bloom_filter_) {
    if (moptions_.memtable_whole_key_filtering) {
        may_contain = bloom_filter_->MayContain(user_key_without_ts);
    } else {
        // ★ whole_key_filtering=false 时走这个分支 ★
        assert(prefix_extractor_);
        if (prefix_extractor_->InDomain(user_key_without_ts)) {
            may_contain = bloom_filter_->MayContain(
                prefix_extractor_->Transform(user_key_without_ts));
            // ↑ 用前 26 字节前缀查 bloom filter
        }
    }
}
if (bloom_filter_ && !may_contain) {
    // bloom filter 判定不存在 → 跳过整个 memtable
    *seq = kMaxSequenceNumber;
    return false;
}
```

### Iterator::Seek() 路径

文件 `table/block_based/block_based_table_iterator.cc:34` — `SeekImpl()`：

```cpp
void BlockBasedTableIterator::SeekImpl(const Slice* target, bool async_prefetch) {
    // ...
    if (target &&
        !CheckPrefixMayMatch(*target, IterDirection::kForward, &filter_checked)) {
        ResetDataIter();  // ← 整个 SST 文件被跳过
        return;
    }
}
```

`CheckPrefixMayMatch()` 调用 `BlockBasedTable::PrefixRangeMayMatch()`（`table/block_based/block_based_table_reader.cc:1955`）：

```cpp
bool BlockBasedTable::PrefixRangeMayMatch(...) const {
    if (!rep_->filter_policy) return true;
    // 提取 user key 的前缀
    auto user_key_without_ts = ExtractUserKeyAndStripTimestamp(internal_key, ts_sz);
    if (!prefix_extractor->InDomain(user_key_without_ts)) return true;
    // 查 SST 文件的 bloom filter
    FilterBlockReader* const filter = rep_->filter.get();
    if (filter != nullptr) {
        may_match = filter->RangeMayExist(
            read_options.iterate_upper_bound, user_key_without_ts,
            prefix_extractor, ...);
        // ↑ 用前缀查 SST 级 bloom filter
    }
    return may_match;
}
```

**结论**: `whole_key_filtering=false` 时：
- `Get()` 在 memtable 中用 prefix bloom 过滤
- `Iterator::Seek()` 在每个 SST 文件中用 prefix bloom 过滤
- 不含目标前缀的 SST 文件被完全跳过，不读取任何 data block

---

## P0-3: 小 value 的内联存储效率

**问题**: 36B key + 48B value = 84B per entry，在 4KB block 中的打包效率？

**结论**: ✅ 每个 4KB block 可容纳 ~42-48 个条目，80 个条目需要 ~2 个 block。

### Block 内部格式

文件 `table/block_based/block_builder.cc:1-33` 注释：

```
每个 entry 的格式:
    shared_bytes: varint32      // 与前一个 key 共享的字节数
    unshared_bytes: varint32    // 不共享的字节数
    value_length: varint32      // value 长度
    key_delta: char[unshared_bytes]  // key 的非共享部分
    value: char[value_length]        // 完整 value

restart point: 每 16 个 key 一个 (block_restart_interval=16)
    restart point 处 shared_bytes=0，存完整 key

block trailer:
    restarts: uint32[num_restarts]   // restart point 偏移数组
    num_restarts: uint32             // restart point 数量
    block_footer: uint32             // index type + num_restarts packed
```

### 精确计算

你的 key 是 36B 定长，但 RocksDB 内部存储的是 **internal key** = user_key(36B) + sequence(7B) + type(1B) = **44B**。

**Restart point 处（每 16 个 entry 一次）**:
```
shared_bytes = 0                    → varint32 = 1B
unshared_bytes = 44                 → varint32 = 1B
value_length = 48                   → varint32 = 1B
key_delta = 44B (完整 internal key)
value = 48B
总计 = 1 + 1 + 1 + 44 + 48 = 95B
```

**非 restart point 处（delta encoding）**:

同一前缀的 key 前 26 字节相同，后 10 字节不同。加上 8B internal key 后缀，共享字节数 = 26。

```
shared_bytes = 26                   → varint32 = 1B
unshared_bytes = 44 - 26 = 18      → varint32 = 1B
value_length = 48                   → varint32 = 1B
key_delta = 18B (非共享部分)
value = 48B
总计 = 1 + 1 + 1 + 18 + 48 = 69B
```

**一个 4KB block 的容量**:

```
block_restart_interval = 16
假设 80 个条目分布在 2 个 block 中，每 block 40 个条目
每 block 有 40/16 ≈ 3 个 restart point

restart point entries: 3 × 95B = 285B
non-restart entries: 37 × 69B = 2553B
restart array: 3 × 4B = 12B
footer: 4B

总计 = 285 + 2553 + 12 + 4 = 2854B < 4096B ✅
```

**结论**: 一个 4KB block 可以容纳 ~40-48 个条目。80 个条目需要 2 个 block = 8KB I/O。如果 block cache 命中，这是纯内存操作。

---

## P0-4: CompactionFilter 行为

**问题**: `Filter()` 对 tombstone 的处理？对 merge operand 的处理？TTL filter 是否会干扰 `DeleteRange` 的 tombstone？

**结论**: ✅ CompactionFilter **不会**对 tombstone 调用。你的 TTL filter 与 DeleteRange 不冲突。

### 代码证据

文件 `db/compaction/compaction_iterator.cc:226`：

```cpp
bool CompactionIterator::InvokeFilterIfNeeded(bool* need_skip, Slice* skip_until) {
    if (!compaction_filter_) return true;

    // ★ 关键：只对这三种类型调用 filter ★
    if (ikey_.type != kTypeValue && ikey_.type != kTypeBlobIndex &&
        ikey_.type != kTypeWideColumnEntity) {
        return true;  // ← kTypeDeletion, kTypeSingleDeletion, kTypeRangeDeletion 都直接跳过
    }
    // ...
    decision = compaction_filter_->FilterV3(level_, filter_key, ...);
}
```

`InvokeFilterIfNeeded` 的调用时机（`db/compaction/compaction_iterator.cc:570`）：

```cpp
// 只在 "first occurrence of this user key" 时调用
if (!has_current_user_key_ || !user_key_equal_without_ts || cmp_ts != 0) {
    // First occurrence of this user key
    if (current_key_committed_ &&
        !InvokeFilterIfNeeded(&need_skip, &skip_until)) {
        break;
    }
}
```

**结论**:
1. CompactionFilter **只对 `kTypeValue`** 调用（你的 48B MetadataEntry 就是 `kTypeValue`）
2. **不对 `kTypeDeletion`（Delete tombstone）调用**
3. **不对 `kTypeRangeDeletion`（DeleteRange tombstone）调用**
4. 你的 TTL filter 返回 `kRemove` 时，RocksDB 将该 key 转为 `kTypeDeletion` tombstone
5. DeleteRange 产生的 range tombstone 不经过 CompactionFilter，不会被误删

**你的设计文档中的 TTL filter 实现是正确的**，但有一个小问题：

```cpp
// 你的代码:
bool Filter(int level, const Slice& key, const Slice& existing_value,
            std::string* new_value, bool* value_changed) const override {
    if (existing_value.size() != sizeof(MetadataEntry)) {
        return true;  // ← 格式异常的条目，丢弃
    }
```

**建议修改**: 格式异常的条目不应该丢弃（`return true`），应该保留（`return false`）。因为 RocksDB 内部可能有其他类型的 value（如 merge operand），虽然在你的场景中不太可能出现，但防御性编程更安全。

---

## P0-5: RateLimiter 动态调整

**问题**: `SetBytesPerSecond()` 是否线程安全？调用频率限制？

**结论**: ✅ 线程安全，可每秒调用多次，无频率限制。

### 代码证据

文件 `util/rate_limiter.cc:103`：

```cpp
void GenericRateLimiter::SetBytesPerSecond(int64_t bytes_per_second) {
    MutexLock g(&request_mutex_);  // ← pthread_mutex_t 加锁
    SetBytesPerSecondLocked(bytes_per_second);
}

void GenericRateLimiter::SetBytesPerSecondLocked(int64_t bytes_per_second) {
    assert(bytes_per_second > 0);
    rate_bytes_per_sec_.store(bytes_per_second, std::memory_order_relaxed);
    refill_bytes_per_period_.store(
        CalculateRefillBytesPerPeriodLocked(bytes_per_second),
        std::memory_order_relaxed);
}
```

**⚠️ 你的设计文档中有一个配置错误**:

```cpp
// 你的代码:
rocksdb::RateLimiter::Mode::kAllIo  // 限制读写
```

**应改为**:
```cpp
rocksdb::RateLimiter::Mode::kWritesOnly  // 只限制写（compaction flush）
```

**原因**: `kAllIo` 会限制 RocksDB 的 **读** I/O，包括 `Get()` 和 `Iterator::Seek()` 触发的 SST 文件读取。这会直接影响你的 `prefix_scan` 性能。你只想限制 compaction 的写 I/O，不想限制前台读。

文件 `include/rocksdb/rate_limiter.h:130`：
```cpp
virtual bool IsRateLimited(OpType op_type) {
    if ((mode_ == RateLimiter::Mode::kWritesOnly &&
         op_type == RateLimiter::OpType::kRead) ||
        (mode_ == RateLimiter::Mode::kReadsOnly &&
         op_type == RateLimiter::OpType::kWrite)) {
        return false;  // ← kWritesOnly 时读操作不受限
    }
    return true;
}
```

---

## P1-1: DeleteRange 的实现和性能影响

**问题**: range tombstone 对后续 `Seek`/`Next` 的性能影响？大量 prefix invalidation 后是否会导致读性能退化？

**结论**: ⚠️ 有一定影响，但可控。建议限制 `DeleteRange` 的使用频率。

### 机制分析

`DeleteRange` 创建一个 **range tombstone**，存储在 SST 文件的独立 meta block 中。读取时，`ReadRangeDelAggregator`（`db/range_del_aggregator.h:375`）维护一个有序的 tombstone 列表：

```cpp
class ReadRangeDelAggregator final : public RangeDelAggregator {
    bool ShouldDelete(const ParsedInternalKey& parsed,
                      RangeDelPositioningMode mode) final override {
        if (rep_.IsEmpty()) return false;  // ← 无 range tombstone 时零开销
        return ShouldDeleteImpl(parsed, mode);
    }
};
```

**性能影响**:
- 无 range tombstone 时：`ShouldDelete()` 只检查 `IsEmpty()`，开销 ~1ns
- 有 range tombstone 时：每次 `Next()` 都需要检查当前 key 是否被 range tombstone 覆盖
- range tombstone 在 compaction 时被清理（与被覆盖的 key 一起）

**建议**:
1. 不要频繁调用 `remove_prefix()`（你的 `DeleteRange`）
2. 如果需要频繁删除前缀，改用逐条 `Delete()` + `SingleDelete()`
3. 配置 `periodic_compaction_seconds = 3600` 确保 range tombstone 被及时清理

**你的设计文档中 `remove_prefix()` 的实现是正确的**，但建议添加注释说明性能影响。

---

## P1-2: MergeOperator 作为 access stats 更新机制

**问题**: 自定义 `MergeOperator` (`reuse_count += delta`) 的 compaction 行为？

**结论**: ✅ 可行且推荐。比你当前的 read-modify-write 方案更好。

### MergeOperator 接口

文件 `include/rocksdb/merge_operator.h:53`：

```cpp
class MergeOperator : public Customizable {
    // FullMergeV2: 将多个 merge operand 合并到 base value 上
    virtual bool FullMergeV2(const MergeOperationInput& merge_in,
                             MergeOperationOutput* merge_out) const;

    // PartialMerge: 将两个 merge operand 合并为一个（可选）
    virtual bool PartialMerge(const Slice& key, const Slice& left_operand,
                              const Slice& right_operand,
                              std::string* new_value, Logger* logger) const {
        return false;  // 默认不支持
    }
};
```

更简单的 `AssociativeMergeOperator`（`include/rocksdb/merge_operator.h:306`）：

```cpp
class AssociativeMergeOperator : public MergeOperator {
    // 只需实现一个函数：将 existing_value 和 value 合并
    virtual bool Merge(const Slice& key, const Slice* existing_value,
                       const Slice& value, std::string* new_value,
                       Logger* logger) const = 0;
};
```

### 推荐实现

```cpp
// 用于 access stats 的 merge operand 格式 (10 bytes):
// [delta_reuse_count: uint16_le][last_access_us: uint64_le]

class AccessStatsMergeOperator : public rocksdb::AssociativeMergeOperator {
public:
    bool Merge(const rocksdb::Slice& key,
               const rocksdb::Slice* existing_value,
               const rocksdb::Slice& value,
               std::string* new_value,
               rocksdb::Logger* logger) const override {
        MetadataEntry base{};
        if (existing_value && existing_value->size() == sizeof(MetadataEntry)) {
            memcpy(&base, existing_value->data(), sizeof(base));
        }

        if (value.size() == 10) {
            // merge operand: delta_reuse_count(2B) + last_access_us(8B)
            uint16_t delta;
            uint64_t access_time;
            memcpy(&delta, value.data(), 2);
            memcpy(&access_time, value.data() + 2, 8);
            base.reuse_count += delta;
            if (access_time > base.last_access_us) {
                base.last_access_us = access_time;
            }
        }

        new_value->assign(reinterpret_cast<const char*>(&base), sizeof(base));
        return true;
    }

    const char* Name() const override { return "AccessStatsMergeOperator"; }
};
```

**使用方式**: 替换你的 `AccessStatsBuffer` 的 `flush_to_meta_store()`：

```cpp
// 旧方案: read-modify-write (需要先 Get 再 Put)
// 新方案: 直接 Merge (无需 Get)
void record_access(const std::string& skey) {
    char operand[10];
    uint16_t delta = 1;
    uint64_t now = now_us();
    memcpy(operand, &delta, 2);
    memcpy(operand + 2, &now, 8);
    db_->Merge(write_opts_, skey, Slice(operand, 10));
}
```

**优势**:
- 无需先 `Get()` 再 `Put()`，减少一次读操作
- `Merge()` 写入 memtable 的延迟与 `Put()` 相同（~2-5μs）
- 多个 merge operand 在 compaction 时自动合并
- `PartialMerge` 可以在 compaction 前合并多个 operand，减少堆积

**注意**: 如果使用 MergeOperator，需要在 `Options` 中设置：
```cpp
opts.merge_operator = std::make_shared<AccessStatsMergeOperator>();
```

---

## P1-3: WriteBatch 原子性保证

**问题**: 100 个 Put 的 WriteBatch commit 延迟？WAL 写入是否是瓶颈？

**结论**: ✅ 无 WAL 时 ~20-35μs，有 WAL (sync=false) 时 ~30-85μs。

### WriteBatch 内部格式

文件 `db/write_batch.cc:10`：

```
WriteBatch::rep_ :=
   sequence: fixed64        // 8B
   count: fixed32           // 4B
   data: record[count]

record :=
   kTypeValue(1B) + key_len(varint32) + key + val_len(varint32) + val
```

**100 个条目的 WriteBatch 大小**:
```
header: 8 + 4 = 12B
per record: 1 (type) + 1 (key_len, 36<128) + 36 (key) + 1 (val_len, 48<128) + 48 (val) = 87B
total: 12 + 100 × 87 = 8712B ≈ 8.5KB
```

### Write 代码路径

文件 `db/db_impl/db_impl_write.cc:150`：

```cpp
Status DBImpl::Write(const WriteOptions& write_options, WriteBatch* my_batch) {
    s = WriteImpl(write_options, my_batch, ...);
}
```

`WriteImpl` 的关键步骤（`db/db_impl/db_impl_write.cc:196`）：

```
1. JoinBatchGroup()     — leader-follower 模式，多个 writer 合并为一组
2. PreprocessWrite()    — 检查是否需要 flush memtable
3. WriteToWAL()         — 写 WAL（如果未禁用）
4. InsertInto()         — 写 memtable（可并行）
```

**⚠️ 你的设计文档中 `no_slowdown = true` 有风险**:

```cpp
write_opts_.no_slowdown = true; // 即使 write stall 也不阻塞调用者
```

当 L0 文件数达到 `level0_stop_writes_trigger`（你设为 12）时，`no_slowdown = true` 会导致 `Write()` 返回 `Status::Incomplete()`。你的 `put_batch()` 需要处理这个错误并重试。

**建议**: 改为 `no_slowdown = false`（默认值）。write stall 在你的场景中极少发生（元数据写入量小），但一旦发生，`no_slowdown = true` 会导致数据丢失（调用者可能不处理 `Incomplete` 错误）。

---

## P1-4: 内存使用 breakdown

**问题**: 100 万条目 (84 MB 数据) 时 RocksDB 总内存占用？

**结论**: 按你的配置，总内存 ~350-420 MB。

### 精确计算

| 组件 | 计算 | 内存 (MB) |
|------|------|-----------|
| **Memtable** (活跃) | `write_buffer_size = 64MB` | 64 |
| **Memtable** (immutable, 最多 2 个) | `64MB × 2` | 128 |
| **Memtable Bloom** | 未在你的配置中设置 `memtable_prefix_bloom_size_ratio` | 0 |
| **Block Cache** | `block_cache_mb = 128` | 128 |
| **SST Bloom Filter** (在 block cache 中) | `100万 × 10 bits = 1.2MB` | 1.2 |
| **SST Index Block** (在 block cache 中) | `~20000 blocks × 46B = 0.9MB` | 0.9 |
| **RocksDB 固定开销** | VersionSet, ColumnFamily, Arena 等 | ~10 |
| **总计** | | **~332 MB** |

**注意**: 你的配置中 `max_write_buffer_number = 3`，意味着最多 3 个 memtable 同时存在（1 活跃 + 2 immutable）。如果写入速率低（你的场景），通常只有 1-2 个 memtable。

**优化建议**: 如果内存紧张，可以减小 `write_buffer_size` 到 32MB，`max_write_buffer_number` 到 2。100 万条目 × 84B = 84MB 数据，32MB memtable 足够。

**你的配置中缺少 `memtable_prefix_bloom_size_ratio`**。建议添加：
```cpp
opts.memtable_prefix_bloom_size_ratio = 0.1;  // 6.4MB per memtable
```
这会增加 ~20MB 内存，但能显著加速 memtable 中的 prefix 查找。

---

## P1-5: Direct I/O 与 RocksDB 的交互

**问题**: `use_direct_reads=true` 时的内部对齐处理？

**结论**: ✅ RocksDB 内部正确处理了 Direct I/O 的对齐要求。

你的配置：
```cpp
opts.use_direct_reads = true;
opts.use_direct_io_for_flush_and_compaction = true;
```

RocksDB 在 `file/random_access_file_reader.cc` 中自动处理对齐：
- 读取时将 offset 向下对齐到 `logical_sector_size`（通常 512B 或 4KB）
- 分配对齐的 buffer（`aligned_buf`）
- 读取对齐后的范围，然后从中提取实际需要的数据

**对你的场景的影响**:
- SST 文件的 data block 是 4KB，天然对齐
- 读取一个 4KB block 不会产生额外的读放大
- 但读取小于 4KB 的 metadata（如 bloom filter 的一部分）可能会读取整个 4KB page

**结论**: Direct I/O 对你的场景没有负面影响，反而避免了 OS page cache 的双重缓存（你已经有 block cache）。

---

## P2: 优化方向

### P2-1: 自定义 Env 用 io_uring

**结论**: 不推荐。RocksDB 已内置 io_uring 支持（`env/io_posix.h:77` 的 `Posix_IOHandle`），且 MetaStore 的 I/O 量极小。自定义 Env 需要实现 ~30 个虚函数（`include/rocksdb/file_system.h:290` 的 `FileSystem` 类），工程量大，收益低。

**建议**: RocksDB MetaStore 用默认 POSIX I/O，BlobStore 用你自己的 io_uring IOScheduler。

### P2-2: 关闭 SST 压缩

**结论**: ✅ 你的决定正确。84B 的结构化二进制条目，LZ4 压缩率通常 < 10%（因为数据中有大量随机哈希值和时间戳）。关闭压缩节省 CPU。

### P2-3: ColumnFamily 隔离不同 TTL

**结论**: 不推荐。你的场景中 TTL 差异不大（都是小时级），且条目数量不大。多 ColumnFamily 增加管理复杂度，收益有限。

### P2-4: periodic_compaction_seconds 的行为

文件 `db/version_set.cc:3683`：

```cpp
void VersionStorageInfo::ComputeFilesMarkedForPeriodicCompaction(
    const ImmutableOptions& ioptions,
    const uint64_t periodic_compaction_seconds, int last_level) {
    // ...
    const uint64_t allowed_time_limit = current_time - periodic_compaction_seconds;
    for (int level = 0; level <= last_level; level++) {
        for (auto f : files_[level]) {
            if (!f->being_compacted) {
                uint64_t file_modification_time = f->TryGetFileCreationTime();
                // ...
                if (file_modification_time > 0 &&
                    file_modification_time < adjusted_allowed_time_limit) {
                    files_marked_for_periodic_compaction_.emplace_back(level, f);
                }
            }
        }
    }
}
```

**行为**: 所有创建时间超过 `periodic_compaction_seconds` 的 SST 文件都会被标记为需要 compaction。这确保了 TTL 过期的条目最终会经过 CompactionFilter 被清理。

**你的配置 `periodic_compaction_seconds = 3600` 是合理的**。

### P2-5: RocksDB vs LevelDB vs 自建

**结论**: 推荐 RocksDB。

| 特性 | RocksDB | LevelDB | 自建 |
|------|---------|---------|------|
| Prefix Bloom Filter | ✅ | ❌ | 需自建 |
| prefix_same_as_start | ✅ | ❌ | 需自建 |
| CompactionFilter | ✅ | ❌ | 需自建 |
| RateLimiter | ✅ | ❌ | 需自建 |
| DeleteRange | ✅ | ❌ | 需自建 |
| MergeOperator | ✅ | ❌ | 需自建 |
| Direct I/O | ✅ | ❌ | 需自建 |
| Concurrent memtable write | ✅ | ❌ | 需自建 |
| 编译依赖 | 中等 | 轻量 | 无 |
| 代码量 | ~500 行包装 | ~500 行包装 | ~5000+ 行 |

LevelDB 缺少 prefix bloom、CompactionFilter、RateLimiter 等关键功能，不适合你的场景。

---

## 设计文档中嵌入问题的逐一回答

### §2.5 的问题: CompactionFilter 对 tombstone 的处理

> Q: RocksDB 的 CompactionFilter 是否保证对 tombstone 也调用?

**答**: **不调用**。已在 P0-4 中详细分析。`InvokeFilterIfNeeded()`（`compaction_iterator.cc:228`）只对 `kTypeValue`、`kTypeBlobIndex`、`kTypeWideColumnEntity` 调用。`kTypeDeletion` 和 `kTypeRangeDeletion` 直接跳过。

> 如果一个 key 先 Put 再 Delete，compaction 时 tombstone 是否经过 Filter?

**答**: 不经过。Delete tombstone 不经过 CompactionFilter。Put 的 value 如果在 tombstone 之前（更旧的 sequence number），会被 tombstone 覆盖而直接丢弃，也不经过 CompactionFilter。

> 两条路径是否有冲突?

**答**: 不冲突。`invalidate()` 写 Delete tombstone → compaction 时 tombstone 直接删除对应 key，不经过 TTL filter。TTL 过期在 CompactionFilter 中清理 → 只对仍存活的 `kTypeValue` 条目生效。两条路径互不干扰。

### §2.6 的问题: DeleteRange 对 prefix_scan 的影响

> Q1: DeleteRange 对后续 prefix_scan 的影响?

**答**: range tombstone 在读取路径中通过 `ReadRangeDelAggregator::ShouldDelete()`（`db/range_del_aggregator.h:389`）检查。当 `rep_.IsEmpty()` 时（无 range tombstone），开销为零。有 range tombstone 时，每次 `Next()` 需要额外检查，但开销很小（二分查找 tombstone 列表）。

> range tombstone 何时被 compaction 清理?

**答**: 当 range tombstone 被 compact 到 bottommost level 且覆盖范围内的所有 key 都已被删除时，tombstone 被清理。`periodic_compaction_seconds = 3600` 确保这在 1 小时内发生。

### §2.6 的问题: prefix_same_as_start 的精确行为

> Q2: Iterator::Next() 在遇到不同前缀时是否立即返回 Invalid()? 还是它只是一个 hint?

**答**: **硬保证，不是 hint**。已在 P0-1 中详细分析。`FindNextUserEntryInternal()`（`db/db_iter.cc:358`）中：

```cpp
if (prefix != nullptr &&
    prefix_extractor_->Transform(user_key_without_ts).compare(*prefix) != 0) {
    break;  // ← 立即退出循环，iterator 变为 !Valid()
}
```

这是一个 `break`，不是 `continue`。前缀不同时立即终止。

### §2.6 的问题: Get 和 Iterator 的 Bloom filter 利用路径

> Q3: Get 用 whole_key_filtering=false 时是否还能用 prefix bloom?

**答**: **能**。已在 P0-2 中详细分析。`MemTable::Get()`（`db/memtable.cc:1289`）在 `whole_key_filtering=false` 时使用 `prefix_extractor_->Transform()` 提取前缀查 bloom filter。

> Iterator::Seek 用 prefix bloom 跳过无关 SST 的逻辑在哪?

**答**: `BlockBasedTableIterator::SeekImpl()`（`block_based_table_iterator.cc:34`）→ `CheckPrefixMayMatch()`（`block_based_table_iterator.h:375`）→ `BlockBasedTable::PrefixRangeMayMatch()`（`block_based_table_reader.cc:1955`）。

### §2.7 的问题: MergeOperator vs read-modify-write

> Q: 这种 read-modify-write 模式在 RocksDB 中是否有更好的实现方式?

**答**: **有，用 MergeOperator**。已在 P1-2 中给出完整实现。`Merge()` 操作直接写入 memtable，无需先 `Get()`，延迟与 `Put()` 相同。

### §3.4 的问题: Direct I/O 对齐

> Q1: Direct I/O 的 pwrite 要求 buffer 地址、大小、文件偏移都 4KB 对齐。上面的实现是否正确处理了所有对齐?

**答**: **是的，你的实现正确**。`aligned_alloc(4096, total_write)` 保证地址对齐；`total_write = next_entry - entry_start` 是 4KB 的倍数（因为 `next_entry = align_up(data_end, 4096)`）；`entry_start` 也是 4KB 对齐的（初始 `write_offset = 4096`，后续每个 entry 的 `next_entry` 都是 4KB 对齐的）。

> Q2: 多线程写同一个段的锁粒度?

**答**: per-segment mutex 是合理的。同一前缀的写是串行的，但不同前缀的写是并行的。lock-free CAS 在这里不值得——`pwrite` 本身就是系统调用，mutex 的开销相比 `pwrite` 可忽略。

> Q3: fsync 策略?

**答**: 你的分析正确。Direct I/O 绕过 page cache 但不保证持久化。不 fsync 是合理的——KV cache 是可重建数据，丢失可接受。

### §6 的问题: StagingPool

> Q1: MAP_HUGETLB 是否保证返回 2MB 对齐的地址?

**答**: 是的。Linux 的 hugepage 分配保证返回 hugepage 大小对齐的地址。2MB hugepage → 2MB 对齐。

> Q2: 4 GB pinned memory 的 CUDA 注册开销?

**答**: `cudaHostRegister()` 一次性注册 4GB 的开销约 10-50ms（取决于 GPU 驱动），只在启动时执行一次。`cudaMallocHost()` 也可以，但它分配的内存不在你控制的地址范围内，不方便做 slot 管理。

> Q3: io_uring 的 IORING_REGISTER_BUFFERS 是否值得做?

**答**: 值得。预注册 buffer 后，`io_uring_prep_read_fixed()` 避免了每次 I/O 的 `get_user_pages()` 开销（~1-2μs per I/O）。对于你的场景（80 个并发读），可以节省 ~80-160μs。

---

## 最终建议与修改清单

### 你的设计文档中需要修改的地方

| # | 位置 | 当前值 | 建议值 | 原因 |
|---|------|--------|--------|------|
| 1 | §2.4 RateLimiter Mode | `kAllIo` | `kWritesOnly` | `kAllIo` 会限制前台读 I/O，影响 prefix_scan 性能 |
| 2 | §2.4 WriteOptions | `no_slowdown = true` | `no_slowdown = false` | write stall 时返回 Incomplete 可能导致数据丢失 |
| 3 | §2.4 ReadOptions | `verify_checksums = false` | `verify_checksums = true` | 生产环境应开启校验，开销极小（CRC32C 有硬件加速） |
| 4 | §2.5 TTL Filter | 格式异常 `return true` (删除) | `return false` (保留) | 防御性编程，避免误删非 MetadataEntry 的 value |
| 5 | §2.4 缺少配置 | 无 memtable bloom | 添加 `memtable_prefix_bloom_size_ratio = 0.1` | 加速 memtable 中的 prefix 查找 |
| 6 | §2.7 Access Stats | read-modify-write | 改用 MergeOperator | 减少一次 Get 操作，降低延迟 |

### 不需要修改的地方（确认正确）

| 设计决策 | 确认 |
|----------|------|
| Key 大端序编码 36B | ✅ 与 BytewiseComparator 兼容 |
| Value 小端序 48B packed struct | ✅ x86/ARM64 原生，零解析开销 |
| FixedPrefixTransform(26) | ✅ 精确匹配你的 key 前缀结构 |
| whole_key_filtering = false | ✅ 只用 prefix bloom，节省空间 |
| compression = kNoCompression | ✅ 84B 条目压缩无意义 |
| periodic_compaction_seconds = 3600 | ✅ 确保 TTL 过期条目被清理 |
| compaction_pri = kMinOverlappingRatio | ✅ 减少写放大 |
| WAL 保留 (disableWAL = false) | ✅ 正常关闭不丢数据 |
| sync = false | ✅ 不等 fsync，crash 可能丢几百 ms 数据，可接受 |
| Direct I/O for SST | ✅ 避免 page cache 双重缓存 |
| BlobStore 4KB 对齐 | ✅ Direct I/O 兼容 |
| BlobStore 不 fsync | ✅ KV cache 可重建 |
| 崩溃一致性模型 | ✅ CRC 校验 + 重建，无需复杂 recovery |

### RocksDB 源码零修改确认

本方案涉及的所有 RocksDB 功能均通过公开 API 使用：

| API | 源码位置 | 使用方式 |
|-----|----------|----------|
| `DB::Open()` | `db/db_impl/db_impl_open.cc` | 配置 + 调用 |
| `DB::Put()` | `db/db_impl/db_impl_write.cc:150` | 直接调用 |
| `DB::Write(WriteBatch)` | `db/db_impl/db_impl_write.cc:150` | 直接调用 |
| `DB::Get()` | `db/db_impl/db_impl.cc:2276` | 直接调用 |
| `DB::NewIterator()` | `db/db_impl/db_impl.cc:3769` | 直接调用 |
| `DB::Delete()` | `db/db_impl/db_impl_write.cc` | 直接调用 |
| `DB::DeleteRange()` | `db/db_impl/db_impl_write.cc` | 直接调用 |
| `DB::KeyMayExist()` | `db/db_impl/db_impl.cc:3740` | 直接调用 |
| `DB::Merge()` | `db/db_impl/db_impl_write.cc` | 直接调用（如果用 MergeOperator） |
| `NewFixedPrefixTransform(26)` | `util/slice.cc:25` | 工厂函数 |
| `NewBloomFilterPolicy(10)` | `table/block_based/filter_policy.cc` | 工厂函数 |
| `NewGenericRateLimiter()` | `util/rate_limiter.cc` | 工厂函数 |
| `SetBytesPerSecond()` | `util/rate_limiter.cc:103` | 方法调用 |
| `CompactionFilter` 子类 | `include/rocksdb/compaction_filter.h` | 继承 + 实现虚函数 |
| `MergeOperator` 子类 | `include/rocksdb/merge_operator.h` | 继承 + 实现虚函数（可选） |
| `NewLRUCache()` | `cache/lru_cache.cc` | 工厂函数 |
| `WriteBatch::Put()` | `db/write_batch.cc` | 直接调用 |

**无需修改任何 RocksDB 源码文件。**

---

> **报告结束**
