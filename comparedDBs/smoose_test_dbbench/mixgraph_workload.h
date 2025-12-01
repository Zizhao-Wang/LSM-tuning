#ifndef MIXGRAPH_WORKLOAD_H_
#define MIXGRAPH_WORKLOAD_H_

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

class Cleanable {
 public:
  Cleanable();
  // No copy constructor and copy assignment allowed.
  Cleanable(Cleanable&) = delete;
  Cleanable& operator=(Cleanable&) = delete;

  // Executes all the registered cleanups
  ~Cleanable();

  // Move constructor and move assignment is allowed.
  Cleanable(Cleanable&&) noexcept;
  Cleanable& operator=(Cleanable&&) noexcept;

  // Clients are allowed to register function/arg1/arg2 triples that
  // will be invoked when this iterator is destroyed.
  //
  // Note that unlike all of the preceding methods, this method is
  // not abstract and therefore clients should not override it.
  using CleanupFunction = void (*)(void* arg1, void* arg2);

  // Add another Cleanup to the list
  void RegisterCleanup(CleanupFunction function, void* arg1, void* arg2);

  // Move the cleanups owned by this Cleanable to another Cleanable, adding to
  // any existing cleanups it has
  void DelegateCleanupsTo(Cleanable* other);

  // DoCleanup and also resets the pointers for reuse
  inline void Reset() {
    DoCleanup();
    cleanup_.function = nullptr;
    cleanup_.next = nullptr;
  }

  inline bool HasCleanups() { return cleanup_.function != nullptr; }

 protected:
  struct Cleanup {
    CleanupFunction function;
    void* arg1;
    void* arg2;
    Cleanup* next;
  };
  Cleanup cleanup_;
  // It also becomes the owner of c
  void RegisterCleanup(Cleanup* c);

 private:
  // Performs all the cleanups. It does not reset the pointers. Making it
  // private
  // to prevent misuse
  inline void DoCleanup() {
    if (cleanup_.function != nullptr) {
      (*cleanup_.function)(cleanup_.arg1, cleanup_.arg2);
      for (Cleanup* c = cleanup_.next; c != nullptr;) {
        (*c->function)(c->arg1, c->arg2);
        Cleanup* next = c->next;
        delete c;
        c = next;
      }
    }
  }
};


/**
 * A Slice that can be pinned with some cleanup tasks, which will be run upon
 * ::Reset() or object destruction, whichever is invoked first. This can be used
 * to avoid memcpy by having the PinnableSlice object referring to the data
 * that is locked in the memory and release them after the data is consumed.
 */
class PinnableSlice : public rocksdb::Slice, public Cleanable {
 public:
  PinnableSlice() { buf_ = &self_space_; }
  explicit PinnableSlice(std::string* buf) { buf_ = buf; }

  PinnableSlice(PinnableSlice&& other);
  PinnableSlice& operator=(PinnableSlice&& other);

  // No copy constructor and copy assignment allowed.
  PinnableSlice(PinnableSlice&) = delete;
  PinnableSlice& operator=(PinnableSlice&) = delete;

  inline void PinSlice(const Slice& s, CleanupFunction f, void* arg1,
                       void* arg2) {
    assert(!pinned_);
    pinned_ = true;
    data_ = s.data();
    size_ = s.size();
    RegisterCleanup(f, arg1, arg2);
    assert(pinned_);
  }

  inline void PinSlice(const Slice& s, Cleanable* cleanable) {
    assert(!pinned_);
    pinned_ = true;
    data_ = s.data();
    size_ = s.size();
    if (cleanable != nullptr) {
      cleanable->DelegateCleanupsTo(this);
    }
    assert(pinned_);
  }

  inline void PinSelf(const Slice& slice) {
    assert(!pinned_);
    buf_->assign(slice.data(), slice.size());
    data_ = buf_->data();
    size_ = buf_->size();
    assert(!pinned_);
  }

  inline void PinSelf() {
    assert(!pinned_);
    data_ = buf_->data();
    size_ = buf_->size();
    assert(!pinned_);
  }

  void remove_suffix(size_t n) {
    assert(n <= size());
    if (pinned_) {
      size_ -= n;
    } else {
      buf_->erase(size() - n, n);
      PinSelf();
    }
  }

  void remove_prefix(size_t n) {
    assert(n <= size());
    if (pinned_) {
      data_ += n;
      size_ -= n;
    } else {
      buf_->erase(0, n);
      PinSelf();
    }
  }

  void Reset() {
    Cleanable::Reset();
    pinned_ = false;
    size_ = 0;
  }

  inline std::string* GetSelf() { return buf_; }

  inline bool IsPinned() const { return pinned_; }

 private:
  friend class PinnableSlice4Test;
  std::string self_space_;
  std::string* buf_;
  bool pinned_ = false;
};


  int64_t PowerCdfInversion(double u, double a, double b) {
    double ret;
    ret = std::pow((u / a), (1 / b));
    return static_cast<int64_t>(ceil(ret));
  }


  
#endif // MIXGRAPH_WORKLOAD_H_