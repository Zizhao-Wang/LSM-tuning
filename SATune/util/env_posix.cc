// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#ifndef __Fuchsia__
#include <sys/resource.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "leveldb/status.h"
#include "port/port.h"
#include "port/thread_annotations.h"
#include "util/env_posix_test_helper.h"
#include "util/posix_logger.h"

namespace leveldb {

namespace {

// Set by EnvPosixTestHelper::SetReadOnlyMMapLimit() and MaxOpenFiles().
int g_open_read_only_file_limit = -1;

// Up to 1000 mmap regions for 64-bit binaries; none for 32-bit.
constexpr const int kDefaultMmapLimit = (sizeof(void*) >= 8) ? 1000 : 0;

// Can be set using EnvPosixTestHelper::SetReadOnlyMMapLimit().
int g_mmap_limit = kDefaultMmapLimit;

// Common flags defined for all posix open operations
#if defined(HAVE_O_CLOEXEC)
constexpr const int kOpenBaseFlags = O_CLOEXEC;
#else
constexpr const int kOpenBaseFlags = 0;
#endif  // defined(HAVE_O_CLOEXEC)

constexpr const size_t kWritableFileBufferSize = 65536;

Status PosixError(const std::string& context, int error_number) {
  if (error_number == ENOENT) {
    return Status::NotFound(context, std::strerror(error_number));
  } else {
    return Status::IOError(context, std::strerror(error_number));
  }
}

// Helper class to limit resource usage to avoid exhaustion.
// Currently used to limit read-only file descriptors and mmap file usage
// so that we do not run out of file descriptors or virtual memory, or run into
// kernel performance problems for very large databases.
class Limiter {
 public:
  // Limit maximum number of resources to |max_acquires|.
  Limiter(int max_acquires)
      :
#if !defined(NDEBUG)
        max_acquires_(max_acquires),
#endif  // !defined(NDEBUG)
        acquires_allowed_(max_acquires) {
    assert(max_acquires >= 0);
  }

  Limiter(const Limiter&) = delete;
  Limiter operator=(const Limiter&) = delete;

  // If another resource is available, acquire it and return true.
  // Else return false.
  bool Acquire() {
    int old_acquires_allowed =
        acquires_allowed_.fetch_sub(1, std::memory_order_relaxed);

    if (old_acquires_allowed > 0) return true;

    int pre_increment_acquires_allowed =
        acquires_allowed_.fetch_add(1, std::memory_order_relaxed);

    // Silence compiler warnings about unused arguments when NDEBUG is defined.
    (void)pre_increment_acquires_allowed;
    // If the check below fails, Release() was called more times than acquire.
    assert(pre_increment_acquires_allowed < max_acquires_);

    return false;
  }

  // Release a resource acquired by a previous call to Acquire() that returned
  // true.
  void Release() {
    int old_acquires_allowed =
        acquires_allowed_.fetch_add(1, std::memory_order_relaxed);

    // Silence compiler warnings about unused arguments when NDEBUG is defined.
    (void)old_acquires_allowed;
    // If the check below fails, Release() was called more times than acquire.
    assert(old_acquires_allowed < max_acquires_);
  }

 private:
#if !defined(NDEBUG)
  // Catches an excessive number of Release() calls.
  const int max_acquires_;
#endif  // !defined(NDEBUG)

  // The number of available resources.
  //
  // This is a counter and is not tied to the invariants of any other class, so
  // it can be operated on safely using std::memory_order_relaxed.
  std::atomic<int> acquires_allowed_;
};

// Implements sequential read access in a file using read().
//
// Instances of this class are thread-friendly but not thread-safe, as required
// by the SequentialFile API.
class PosixSequentialFile final : public SequentialFile {
 public:
  PosixSequentialFile(std::string filename, int fd)
      : fd_(fd), filename_(std::move(filename)) {}
  ~PosixSequentialFile() override { close(fd_); }

  Status Read(size_t n, Slice* result, char* scratch) override {
    Status status;
    while (true) {
      ::ssize_t read_size = ::read(fd_, scratch, n);
      if (read_size < 0) {  // Read error.
        if (errno == EINTR) {
          continue;  // Retry
        }
        status = PosixError(filename_, errno);
        break;
      }
      *result = Slice(scratch, read_size);
      break;
    }
    return status;
  }

  Status Skip(uint64_t n) override {
    if (::lseek(fd_, n, SEEK_CUR) == static_cast<off_t>(-1)) {
      return PosixError(filename_, errno);
    }
    return Status::OK();
  }

 private:
  const int fd_;
  const std::string filename_;
};

class PosixDirectIOSequentialFile final : public SequentialFile {
 public:

  PosixDirectIOSequentialFile(std::string filename, int fd)
    : fd_(fd), filename_(std::move(filename)), buffer_offset_(0), buffer_size_(0), file_offset_(0) {
    // Allocate memory that is aligned to the block size.
    alignment_ = GetPageSize();
    aligned_buffer_size_ = AlignUp(kDefaultReadSize, alignment_);
    
    // 分配对齐的内存缓冲区
    if (posix_memalign(reinterpret_cast<void**>(&aligned_buffer_), alignment_, aligned_buffer_size_) != 0) {
      aligned_buffer_ = nullptr;
    }
  }


  ~PosixDirectIOSequentialFile() override {
    if (aligned_buffer_) {
      free(aligned_buffer_);
    }
    close(fd_);
  }

  // 磁盘文件 (File on Disk):
  // | ... | Block 0 (0-4095) | Block 1 (4096-8191) | Block 2 (8192-...) |
  // +-----+------------------+---------------------+--------------------+
  //                          ^
  //                          |
  //            aligned_file_offset_ = 4096 (物理读取起点)
  //                          |
  //                          +-------------------------------------------------+
  //                                                                            |
  //                                             pread()                      V
  // 内存中的仓库 (aligned_buffer_ in Memory):
  // +--------------------------------------------------------------------------+
  // |      Data from disk starting at 4096...                                  |
  // +--------------------------------------------------------------------------+
  // ^
  // |
  // aligned_buffer_ (仓库起点)

  Status Read(size_t n, Slice* result, char* scratch) override {
    if (!aligned_buffer_) {
      return Status::IOError("Failed to allocate aligned buffer for Direct I/O");
    }

    size_t bytes_copied = 0;
    
    while (bytes_copied < n) {
      // 检查是否需要重新填充缓冲区
      if (buffer_offset_ >= buffer_size_) {
        Status fill_status = FillBuffer();
        if (!fill_status.ok()) {
          if (bytes_copied > 0) {
            break; // 返回已读取的数据
          }
          return fill_status;
        }
        if (buffer_size_ == 0) {
          break; // EOF
        }
      }

      // 从内部缓冲区复制数据到用户缓冲区
      size_t bytes_available = buffer_size_ - buffer_offset_;
      size_t bytes_to_copy = std::min(n - bytes_copied, bytes_available);
      
      memcpy(scratch + bytes_copied, effective_buffer_ + buffer_offset_, bytes_to_copy);
      buffer_offset_ += bytes_to_copy;
      bytes_copied += bytes_to_copy;
      file_offset_ += bytes_to_copy;
    }

    *result = Slice(scratch, bytes_copied);
    return Status::OK();
  }

  Status Skip(uint64_t n) override {
    file_offset_ += n;
    
    // 检查跳跃是否超出当前缓冲区
    uint64_t buffer_start = aligned_file_offset_;
    uint64_t buffer_end = buffer_start + buffer_size_;
    
    if (file_offset_ < buffer_start || file_offset_ >= buffer_end) {
      // 跳跃超出当前缓冲区范围，标记需要重新加载
      buffer_offset_ = buffer_size_;
    } else {
      // 跳跃在当前缓冲区内，调整偏移
      buffer_offset_ = file_offset_ - buffer_start;
    }
    
    return Status::OK();
  }

 private:
  const int fd_;
  const std::string filename_;

  // Direct I/O 相关成员
  char* aligned_buffer_ = nullptr;
  char* effective_buffer_ = nullptr;  // 指向有效数据的起始位置
  size_t alignment_ = 0;
  size_t aligned_buffer_size_ = 0;
  
  // 缓冲区管理
  size_t buffer_offset_ = 0;      // 当前读取位置在缓冲区中的偏移
  size_t buffer_size_ = 0;        // 缓冲区中有效数据的大小
  uint64_t file_offset_ = 0;      // 当前文件读取位置
  uint64_t aligned_file_offset_ = 0; // 对齐后的文件读取位置
  static constexpr size_t kDefaultReadSize = 64 * 1024; // 64KB

  Status FillBuffer() {

    aligned_file_offset_ = AlignDown(file_offset_, alignment_);
    while (true) {
      ::ssize_t bytes_read = ::pread(fd_, aligned_buffer_, aligned_buffer_size_, aligned_file_offset_);
      if (bytes_read < 0) {
        if (errno == EINTR) {
          continue;
        }
        return PosixError(filename_, errno);
      }

      if (bytes_read == 0) {
        // EOF
        buffer_size_ = 0;
        buffer_offset_ = 0;
        effective_buffer_ = aligned_buffer_;
        return Status::OK();
      }

      // 计算有效数据的起始位置和大小
      size_t data_offset = file_offset_ - aligned_file_offset_;
      effective_buffer_ = aligned_buffer_ + data_offset;
      buffer_size_ = std::max(0L, bytes_read - static_cast<ssize_t>(data_offset));
      buffer_offset_ = 0;
      
      break;
    }

    return Status::OK();
  }

  static size_t GetPageSize() {
    return sysconf(_SC_PAGESIZE);
  }

  static size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
  }

  static uint64_t AlignDown(uint64_t value, size_t alignment) {
    return value & ~(alignment - 1);
  }
};


// Implements random read access in a file using pread().
//
// Instances of this class are thread-safe, as required by the RandomAccessFile
// API. Instances are immutable and Read() only calls thread-safe library
// functions.
class PosixRandomAccessFile final : public RandomAccessFile {
 public:
  // The new instance takes ownership of |fd|. |fd_limiter| must outlive this
  // instance, and will be used to determine if .
  PosixRandomAccessFile(std::string filename, int fd, Limiter* fd_limiter)
      : has_permanent_fd_(fd_limiter->Acquire()),
        fd_(has_permanent_fd_ ? fd : -1),
        fd_limiter_(fd_limiter),
        filename_(std::move(filename)) {
    if (!has_permanent_fd_) {
      assert(fd_ == -1);
      ::close(fd);  // The file will be opened on every read.
    }
  }

  ~PosixRandomAccessFile() override {
    if (has_permanent_fd_) {
      assert(fd_ != -1);
      ::close(fd_);
      fd_limiter_->Release();
    }
  }

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    int fd = fd_;
    if (!has_permanent_fd_) {
      fd = ::open(filename_.c_str(), O_RDONLY | kOpenBaseFlags);
      if (fd < 0) {
        return PosixError(filename_, errno);
      }
    }

    assert(fd != -1);

    Status status;
    ssize_t read_size;
 
    read_size = ::pread(fd, scratch, n, static_cast<off_t>(offset));
    *result = Slice(scratch, read_size);  

    if (read_size < 0) {
      // An error: return a non-ok status.
      status = PosixError(filename_, errno);
    }
    if (!has_permanent_fd_) {
      // Close the temporary file descriptor opened earlier.
      assert(fd != fd_);
      ::close(fd);
    }
    return status;
  }

 private:
  const bool has_permanent_fd_;  // If false, the file is opened on every read.
  const int fd_;                 // -1 if has_permanent_fd_ is false.
  Limiter* const fd_limiter_;
  const std::string filename_;
};

class PosixDirectIORandomAccessFile final : public RandomAccessFile {
 public:
  PosixDirectIORandomAccessFile(std::string filename, int fd, Limiter* fd_limiter)
  : has_permanent_fd_(fd_limiter->Acquire()),fd_(has_permanent_fd_ ? fd : -1),
    fd_limiter_(fd_limiter),filename_(std::move(filename)) {
    // 初始化 Direct I/O 相关参数
    alignment_ = GetPageSize();
    aligned_buffer_size_ = AlignUp(kDefaultReadSize, alignment_);
    
    if (!has_permanent_fd_) {
      assert(fd_ == -1);
      ::close(fd);  // 每次读取时重新打开文件
    }
  }

  ~PosixDirectIORandomAccessFile() override {
    if (has_permanent_fd_) {
      assert(fd_ != -1);
      ::close(fd_);
      fd_limiter_->Release();
    }
  }

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    int fd = fd_;
    bool need_close = false;
    
    // 如果没有永久文件描述符，需要临时打开文件
    if (!has_permanent_fd_) {
      fd = ::open(filename_.c_str(), O_RDONLY | O_DIRECT | kOpenBaseFlags);
      if (fd < 0) {
        return PosixError(filename_, errno);
      }
      need_close = true;
    }

    assert(fd != -1);

    Status status = DirectRead(fd, offset, n, result, scratch);

    if (need_close) {
      assert(fd != fd_);
      ::close(fd);
    }
    
    return status;
  }

 private:
  const bool has_permanent_fd_;
  const int fd_;
  Limiter* const fd_limiter_;
  const std::string filename_;
  
  // Direct I/O 相关参数
  mutable size_t alignment_ = 0;
  mutable size_t aligned_buffer_size_ = 0;
  
  static constexpr size_t kDefaultReadSize = 64 * 1024; // 64KB

  // Direct I/O 读取实现
  Status DirectRead(int fd, uint64_t offset, size_t n, Slice* result, char* scratch) const {
    char* aligned_buffer = nullptr;
    if (posix_memalign(reinterpret_cast<void**>(&aligned_buffer), 
                      alignment_, aligned_buffer_size_) != 0) {
      return Status::IOError("Failed to allocate aligned buffer for Direct I/O");
    }
    
    // 使用 RAII 管理内存
    std::unique_ptr<char, decltype(&free)> buffer_guard(aligned_buffer, &free);

    // 计算对齐的读取参数
    uint64_t aligned_offset = AlignDown(offset, alignment_);
    size_t prefix_length = offset - aligned_offset;
    size_t aligned_read_size = AlignUp(prefix_length + n, alignment_);
    
    // 确保不超过缓冲区大小
    aligned_read_size = std::min(aligned_read_size, aligned_buffer_size_);

    Status status;
    ssize_t bytes_read = 0;
    
    while (true) {
      bytes_read = ::pread(fd, aligned_buffer, aligned_read_size, 
                          static_cast<off_t>(aligned_offset));
      if (bytes_read < 0) {
        if (errno == EINTR) {
          continue;
        }
        status = PosixError(filename_, errno);
        break;
      }
      break;
    }

    if (!status.ok()) {
      return status;
    }

    // 计算实际可用的数据
    if (bytes_read <= static_cast<ssize_t>(prefix_length)) {
      // 读取的数据不足以覆盖请求的偏移
      *result = Slice(scratch, 0);
      return Status::OK();
    }

    size_t available_data = bytes_read - prefix_length;
    size_t copy_size = std::min(n, available_data);
    
    // 从对齐缓冲区复制数据到用户缓冲区
    memcpy(scratch, aligned_buffer + prefix_length, copy_size);
    *result = Slice(scratch, copy_size);

    return Status::OK();
  }

  static size_t GetPageSize() {
    return sysconf(_SC_PAGESIZE);
  }

  static size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
  }

  static uint64_t AlignDown(uint64_t value, size_t alignment) {
    return value & ~(alignment - 1);
  }
};


// Implements random read access in a file using mmap().
//
// Instances of this class are thread-safe, as required by the RandomAccessFile
// API. Instances are immutable and Read() only calls thread-safe library
// functions.
class PosixMmapReadableFile final : public RandomAccessFile {
 public:
  // mmap_base[0, length-1] points to the memory-mapped contents of the file. It
  // must be the result of a successful call to mmap(). This instances takes
  // over the ownership of the region.
  //
  // |mmap_limiter| must outlive this instance. The caller must have already
  // acquired the right to use one mmap region, which will be released when this
  // instance is destroyed.
  PosixMmapReadableFile(std::string filename, char* mmap_base, size_t length,
                        Limiter* mmap_limiter)
      : mmap_base_(mmap_base),
        length_(length),
        mmap_limiter_(mmap_limiter),
        filename_(std::move(filename)) {}

  ~PosixMmapReadableFile() override {
    ::munmap(static_cast<void*>(mmap_base_), length_);
    mmap_limiter_->Release();
  }

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    if (offset + n > length_) {
      *result = Slice();
      return PosixError(filename_, EINVAL);
    }

    *result = Slice(mmap_base_ + offset, n);
    return Status::OK();
  }

 private:
  char* const mmap_base_;
  const size_t length_;
  Limiter* const mmap_limiter_;
  const std::string filename_;
};

class PosixWritableFile final : public WritableFile {
 public:
  PosixWritableFile(std::string filename, int fd)
      : pos_(0),
        fd_(fd),
        is_manifest_(IsManifest(filename)),
        filename_(std::move(filename)),
        dirname_(Dirname(filename_)),
        file_offset_(0)  {}

  ~PosixWritableFile() override {
    if (fd_ >= 0) {
      // Ignoring any potential errors
      Close();
    }
  }

  Status Append(const Slice& data) override {
    size_t write_size = data.size();
    const char* write_data = data.data();

    // Fit as much as possible into buffer.
    size_t copy_size = std::min(write_size, kWritableFileBufferSize - pos_);
    std::memcpy(buf_ + pos_, write_data, copy_size);
    write_data += copy_size;
    write_size -= copy_size;
    pos_ += copy_size;
    if (write_size == 0) {
      return Status::OK();
    }

    // Can't fit in buffer, so need to do at least one write.
    Status status = FlushBuffer();
    if (!status.ok()) {
      return status;
    }

    // Small writes go to buffer, large writes are written directly.
    if (write_size < kWritableFileBufferSize) {
      std::memcpy(buf_, write_data, write_size);
      pos_ = write_size;
      return Status::OK();
    }
    return WriteUnbuffered(write_data, write_size);
  }

  Status Close() override {
    Status status = FlushBuffer();
    const int close_result = ::close(fd_);
    if (close_result < 0 && status.ok()) {
      status = PosixError(filename_, errno);
    }
    fd_ = -1;
    return status;
  }

  Status Flush() override { return FlushBuffer(); }

  Status Sync() override {
    // Ensure new files referred to by the manifest are in the filesystem.
    //
    // This needs to happen before the manifest file is flushed to disk, to
    // avoid crashing in a state where the manifest refers to files that are not
    // yet on disk.
    Status status = SyncDirIfManifest();
    if (!status.ok()) {
      return status;
    }

    status = FlushBuffer();
    if (!status.ok()) {
      return status;
    }

    return SyncFd(fd_, filename_);
  }

  uint64_t GetFileSize() const override {
    return file_offset_ + pos_;
  }

 private:
  Status FlushBuffer() {
    Status status = WriteUnbuffered(buf_, pos_);
    pos_ = 0;

    if (status.ok()) {
      file_offset_ += pos_;  
    }
    return status;
  }

  Status WriteUnbuffered(const char* data, size_t size) {
    size_t original_size = size;
    while (size > 0) {
      ssize_t write_result = ::write(fd_, data, size);
      if (write_result < 0) {
        if (errno == EINTR) {
          continue;  // Retry
        }
        return PosixError(filename_, errno);
      }
      data += write_result;
      size -= write_result;
    }
    file_offset_ += original_size;
    return Status::OK();
  }

  Status SyncDirIfManifest() {
    Status status;
    if (!is_manifest_) {
      return status;
    }

    int fd = ::open(dirname_.c_str(), O_RDONLY | kOpenBaseFlags);
    if (fd < 0) {
      status = PosixError(dirname_, errno);
    } else {
      status = SyncFd(fd, dirname_);
      ::close(fd);
    }
    return status;
  }

  // Ensures that all the caches associated with the given file descriptor's
  // data are flushed all the way to durable media, and can withstand power
  // failures.
  //
  // The path argument is only used to populate the description string in the
  // returned Status if an error occurs.
  static Status SyncFd(int fd, const std::string& fd_path) {
#if HAVE_FULLFSYNC
    // On macOS and iOS, fsync() doesn't guarantee durability past power
    // failures. fcntl(F_FULLFSYNC) is required for that purpose. Some
    // filesystems don't support fcntl(F_FULLFSYNC), and require a fallback to
    // fsync().
    if (::fcntl(fd, F_FULLFSYNC) == 0) {
      return Status::OK();
    }
#endif  // HAVE_FULLFSYNC

#if HAVE_FDATASYNC
    bool sync_success = ::fdatasync(fd) == 0;
#else
    bool sync_success = ::fsync(fd) == 0;
#endif  // HAVE_FDATASYNC

    if (sync_success) {
      return Status::OK();
    }
    return PosixError(fd_path, errno);
  }

  // Returns the directory name in a path pointing to a file.
  //
  // Returns "." if the path does not contain any directory separator.
  static std::string Dirname(const std::string& filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
      return std::string(".");
    }
    // The filename component should not contain a path separator. If it does,
    // the splitting was done incorrectly.
    assert(filename.find('/', separator_pos + 1) == std::string::npos);

    return filename.substr(0, separator_pos);
  }

  // Extracts the file name from a path pointing to a file.
  //
  // The returned Slice points to |filename|'s data buffer, so it is only valid
  // while |filename| is alive and unchanged.
  static Slice Basename(const std::string& filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
      return Slice(filename);
    }
    // The filename component should not contain a path separator. If it does,
    // the splitting was done incorrectly.
    assert(filename.find('/', separator_pos + 1) == std::string::npos);

    return Slice(filename.data() + separator_pos + 1,
                 filename.length() - separator_pos - 1);
  }

  // True if the given file is a manifest file.
  static bool IsManifest(const std::string& filename) {
    return Basename(filename).starts_with("MANIFEST");
  }

  // buf_[0, pos_ - 1] contains data to be written to fd_.
  char buf_[kWritableFileBufferSize];
  size_t pos_;
  int fd_;
  uint64_t file_offset_;

  const bool is_manifest_;  // True if the file's name starts with MANIFEST.
  const std::string filename_;
  const std::string dirname_;  // The directory of filename_.
};


class PosixDirectIOWritableFile final : public WritableFile {
 public:
  PosixDirectIOWritableFile(std::string filename, int fd)
      : pos_(0),
        fd_(fd),
        is_manifest_(IsManifest(filename)),
        filename_(std::move(filename)),
        dirname_(Dirname(filename_)),
        file_offset_(0) {
    
    // 初始化 Direct I/O 相关参数
    alignment_ = GetPageSize();
    aligned_buffer_size_ = AlignUp(kWritableFileBufferSize, alignment_);
    
    // 分配对齐的缓冲区
    if (posix_memalign(reinterpret_cast<void**>(&aligned_buf_), 
                      alignment_, aligned_buffer_size_) != 0) {
      aligned_buf_ = nullptr;
    }
  }

  ~PosixDirectIOWritableFile() override {
    if (fd_ >= 0) {
      // Ignoring any potential errors
      Close();
    }
    if (aligned_buf_) {
      free(aligned_buf_);
    }
  }

  Status Append(const Slice& data) override {
    if (!aligned_buf_) {
      return Status::IOError("Failed to allocate aligned buffer for Direct I/O");
    }

    size_t write_size = data.size();
    const char* write_data = data.data();

    // 尽可能放入缓冲区
    size_t copy_size = std::min(write_size, aligned_buffer_size_ - pos_);
    std::memcpy(aligned_buf_ + pos_, write_data, copy_size);
    write_data += copy_size;
    write_size -= copy_size;
    pos_ += copy_size;

    if (write_size == 0) {
      return Status::OK();
    }

    // 缓冲区满了，需要先刷新
    Status status = FlushBuffer();
    if (!status.ok()) {
      return status;
    }

    // 小的写入放入缓冲区，大的写入直接写
    if (write_size < aligned_buffer_size_) {
      std::memcpy(aligned_buf_, write_data, write_size);
      pos_ = write_size;
      return Status::OK();
    }
    
    return WriteUnbufferedAligned(write_data, write_size);
  }

  Status Close() override {
    Status status = FlushBuffer();
    const int close_result = ::close(fd_);
    if (close_result < 0 && status.ok()) {
      status = PosixError(filename_, errno);
    }
    fd_ = -1;
    return status;
  }

  Status Flush() override { 
    return FlushBuffer(); 
  }

  Status Sync() override {
    // 确保 manifest 引用的新文件已在文件系统中
    Status status = SyncDirIfManifest();
    if (!status.ok()) {
      return status;
    }

    status = FlushBuffer();
    if (!status.ok()) {
      return status;
    }

    return SyncFd(fd_, filename_);
  }

  uint64_t GetFileSize() const override {
    return file_offset_ + pos_;
  }

 private:
  // Direct I/O 相关成员变量
  char* aligned_buf_ = nullptr;
  size_t alignment_ = 0;
  size_t aligned_buffer_size_ = 0;
  uint64_t file_offset_ = 0;  // 当前文件写入位置
  
  // 原有成员变量
  size_t pos_;  // 缓冲区中的当前位置
  int fd_;
  const bool is_manifest_;
  const std::string filename_;
  const std::string dirname_;

  Status FlushBuffer() {
    if (pos_ == 0) {
      return Status::OK();
    }

    Status status = WriteBufferAligned();
    pos_ = 0;
    return status;
  }

  // 写入对齐的缓冲区数据
  Status WriteBufferAligned() {
    if (pos_ == 0) {
      return Status::OK();
    }

    // 对于 Direct I/O，需要写入对齐的大小
    size_t aligned_write_size = AlignUp(pos_, alignment_);
    
    // 如果需要，用零填充到对齐边界
    if (aligned_write_size > pos_) {
      memset(aligned_buf_ + pos_, 0, aligned_write_size - pos_);
    }

    Status status = WriteUnbuffered(aligned_buf_, aligned_write_size);
    if (status.ok()) {
      file_offset_ += pos_; // 只增加实际数据的大小
    }
    return status;
  }

  // 写入大块对齐数据
  Status WriteUnbufferedAligned(const char* data, size_t size) {
    // 对于大的写入，我们需要处理对齐
    size_t remaining = size;
    const char* current_data = data;

    while (remaining > 0) {
      size_t chunk_size = std::min(remaining, aligned_buffer_size_);
      
      // 复制数据到对齐缓冲区
      memcpy(aligned_buf_, current_data, chunk_size);
      
      // 如果不是对齐大小，用零填充
      size_t aligned_chunk_size = AlignUp(chunk_size, alignment_);
      if (aligned_chunk_size > chunk_size) {
        memset(aligned_buf_ + chunk_size, 0, aligned_chunk_size - chunk_size);
      }

      Status status = WriteUnbuffered(aligned_buf_, aligned_chunk_size);
      if (!status.ok()) {
        return status;
      }

      file_offset_ += chunk_size; // 只增加实际数据的大小
      current_data += chunk_size;
      remaining -= chunk_size;
    }

    return Status::OK();
  }

  Status WriteUnbuffered(const char* data, size_t size) {
    while (size > 0) {
      ssize_t write_result = ::write(fd_, data, size);
      if (write_result < 0) {
        if (errno == EINTR) {
          continue;  // Retry
        }
        return PosixError(filename_, errno);
      }
      data += write_result;
      size -= write_result;
    }
    return Status::OK();
  }

  Status SyncDirIfManifest() {
    Status status;
    if (!is_manifest_) {
      return status;
    }

    int fd = ::open(dirname_.c_str(), O_RDONLY | kOpenBaseFlags);
    if (fd < 0) {
      status = PosixError(dirname_, errno);
    } else {
      status = SyncFd(fd, dirname_);
      ::close(fd);
    }
    return status;
  }

  static Status SyncFd(int fd, const std::string& fd_path) {
#if HAVE_FULLFSYNC
    if (::fcntl(fd, F_FULLFSYNC) == 0) {
      return Status::OK();
    }
#endif  // HAVE_FULLFSYNC

#if HAVE_FDATASYNC
    bool sync_success = ::fdatasync(fd) == 0;
#else
    bool sync_success = ::fsync(fd) == 0;
#endif  // HAVE_FDATASYNC

    if (sync_success) {
      return Status::OK();
    }
    return PosixError(fd_path, errno);
  }

  static std::string Dirname(const std::string& filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
      return std::string(".");
    }
    assert(filename.find('/', separator_pos + 1) == std::string::npos);
    return filename.substr(0, separator_pos);
  }

  static Slice Basename(const std::string& filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
      return Slice(filename);
    }
    assert(filename.find('/', separator_pos + 1) == std::string::npos);
    return Slice(filename.data() + separator_pos + 1,
                 filename.length() - separator_pos - 1);
  }

  static bool IsManifest(const std::string& filename) {
    return Basename(filename).starts_with("MANIFEST");
  }

  static size_t GetPageSize() {
    return sysconf(_SC_PAGESIZE);
  }

  static size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
  }
};

int LockOrUnlock(int fd, bool lock) {
  errno = 0;
  struct ::flock file_lock_info;
  std::memset(&file_lock_info, 0, sizeof(file_lock_info));
  file_lock_info.l_type = (lock ? F_WRLCK : F_UNLCK);
  file_lock_info.l_whence = SEEK_SET;
  file_lock_info.l_start = 0;
  file_lock_info.l_len = 0;  // Lock/unlock entire file.
  return ::fcntl(fd, F_SETLK, &file_lock_info);
}

// Instances are thread-safe because they are immutable.
class PosixFileLock : public FileLock {
 public:
  PosixFileLock(int fd, std::string filename)
      : fd_(fd), filename_(std::move(filename)) {}

  int fd() const { return fd_; }
  const std::string& filename() const { return filename_; }

 private:
  const int fd_;
  const std::string filename_;
};

// Tracks the files locked by PosixEnv::LockFile().
//
// We maintain a separate set instead of relying on fcntl(F_SETLK) because
// fcntl(F_SETLK) does not provide any protection against multiple uses from the
// same process.
//
// Instances are thread-safe because all member data is guarded by a mutex.
class PosixLockTable {
 public:
  bool Insert(const std::string& fname) LOCKS_EXCLUDED(mu_) {
    mu_.Lock();
    bool succeeded = locked_files_.insert(fname).second;
    mu_.Unlock();
    return succeeded;
  }
  void Remove(const std::string& fname) LOCKS_EXCLUDED(mu_) {
    mu_.Lock();
    locked_files_.erase(fname);
    mu_.Unlock();
  }

 private:
  port::Mutex mu_;
  std::set<std::string> locked_files_ GUARDED_BY(mu_);
};

class PosixEnv : public Env {
 public:
  PosixEnv();
  ~PosixEnv() override {
    static const char msg[] =
        "PosixEnv singleton destroyed. Unsupported behavior!\n";
    std::fwrite(msg, 1, sizeof(msg), stderr);
    std::abort();
  }

  Status NewSequentialFile(const std::string& filename,
                           SequentialFile** result, bool user_direct_io) override {
    int flags = O_RDONLY | kOpenBaseFlags;
    if (user_direct_io) { 
      flags |= O_DIRECT;
    }

    int fd = ::open(filename.c_str(), flags);
    if (fd < 0) {
      *result = nullptr;
      return PosixError(filename, errno);
    }

    if(!user_direct_io){
      *result = new PosixSequentialFile(filename, fd);
    }else{
      *result = new PosixDirectIOSequentialFile(filename, fd);
    }

    return Status::OK();
  }

  Status NewRandomAccessFile(const std::string& filename,
                             RandomAccessFile** result, bool user_direct_io) override {
    *result = nullptr;
    int flags = O_RDONLY | kOpenBaseFlags;
    if (user_direct_io) { 
      flags |= O_DIRECT;
    }

    int fd = ::open(filename.c_str(), flags);
    if (fd < 0) {
      return PosixError(filename, errno);
    }

    if (user_direct_io) {
      *result = new PosixDirectIORandomAccessFile(filename, fd, &fd_limiter_);
      return Status::OK();
    }

    if (!mmap_limiter_.Acquire()) {
      *result = new PosixRandomAccessFile(filename, fd, &fd_limiter_);
      return Status::OK();
    }

    uint64_t file_size;
    Status status = GetFileSize(filename, &file_size);
    if (status.ok()) {
      void* mmap_base =
          ::mmap(/*addr=*/nullptr, file_size, PROT_READ, MAP_SHARED, fd, 0);
      if (mmap_base != MAP_FAILED) {
        *result = new PosixMmapReadableFile(filename,
                                            reinterpret_cast<char*>(mmap_base),
                                            file_size, &mmap_limiter_);
      } else {
        status = PosixError(filename, errno);
      }
    }
    ::close(fd);
    if (!status.ok()) {
      mmap_limiter_.Release();
    }
    return status;
  }



  Status NewWritableFile(const std::string& filename,
    WritableFile** result, bool user_direct_io = false) override {
                       
    int flags = O_TRUNC | O_WRONLY | O_CREAT | kOpenBaseFlags;
    if (user_direct_io) {
      flags |= O_DIRECT;
    }

    int fd = ::open(filename.c_str(), flags, 0644);
    if (fd < 0) {
      *result = nullptr;
      return PosixError(filename, errno);
    }

    if (user_direct_io) {
      *result = new PosixDirectIOWritableFile(filename, fd);
    } else {
      *result = new PosixWritableFile(filename, fd);
    }
    
    return Status::OK();
  }

  Status NewAppendableFile(const std::string& filename,
      WritableFile** result, bool user_direct_io = false) override {
    
    int flags = O_TRUNC | O_WRONLY | O_CREAT | kOpenBaseFlags;
    if (user_direct_io) {
      flags |= O_DIRECT;
    }

    int fd = ::open(filename.c_str(), flags, 0644);
    if (fd < 0) {
      *result = nullptr;
      return PosixError(filename, errno);
    }

    if (user_direct_io) {
      *result = new PosixDirectIOWritableFile(filename, fd);
    } else {
      *result = new PosixWritableFile(filename, fd);
    }
    
    return Status::OK();
  }

  bool FileExists(const std::string& filename) override {
    return ::access(filename.c_str(), F_OK) == 0;
  }

  Status GetChildren(const std::string& directory_path,
                     std::vector<std::string>* result) override {
    result->clear();
    ::DIR* dir = ::opendir(directory_path.c_str());
    if (dir == nullptr) {
      return PosixError(directory_path, errno);
    }
    struct ::dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
      result->emplace_back(entry->d_name);
    }
    ::closedir(dir);
    return Status::OK();
  }

  Status RemoveFile(const std::string& filename) override {
    if (::unlink(filename.c_str()) != 0) {
      return PosixError(filename, errno);
    }
    return Status::OK();
  }

  Status CreateDir(const std::string& dirname) override {
    if (::mkdir(dirname.c_str(), 0755) != 0) {
      return PosixError(dirname, errno);
    }
    return Status::OK();
  }

  Status RemoveDir(const std::string& dirname) override {
    if (::rmdir(dirname.c_str()) != 0) {
      return PosixError(dirname, errno);
    }
    return Status::OK();
  }

  Status GetFileSize(const std::string& filename, uint64_t* size) override {
    struct ::stat file_stat;
    if (::stat(filename.c_str(), &file_stat) != 0) {
      *size = 0;
      return PosixError(filename, errno);
    }
    *size = file_stat.st_size;
    return Status::OK();
  }

  Status RenameFile(const std::string& from, const std::string& to) override {
    if (std::rename(from.c_str(), to.c_str()) != 0) {
      return PosixError(from, errno);
    }
    return Status::OK();
  }

  Status LockFile(const std::string& filename, FileLock** lock) override {
    *lock = nullptr;

    int fd = ::open(filename.c_str(), O_RDWR | O_CREAT | kOpenBaseFlags, 0644);
    if (fd < 0) {
      return PosixError(filename, errno);
    }

    if (!locks_.Insert(filename)) {
      ::close(fd);
      return Status::IOError("lock " + filename, "already held by process");
    }

    if (LockOrUnlock(fd, true) == -1) {
      int lock_errno = errno;
      ::close(fd);
      locks_.Remove(filename);
      return PosixError("lock " + filename, lock_errno);
    }

    *lock = new PosixFileLock(fd, filename);
    return Status::OK();
  }

  Status UnlockFile(FileLock* lock) override {
    PosixFileLock* posix_file_lock = static_cast<PosixFileLock*>(lock);
    if (LockOrUnlock(posix_file_lock->fd(), false) == -1) {
      return PosixError("unlock " + posix_file_lock->filename(), errno);
    }
    locks_.Remove(posix_file_lock->filename());
    ::close(posix_file_lock->fd());
    delete posix_file_lock;
    return Status::OK();
  }

  void Schedule(void (*background_work_function)(void* background_work_arg),
                void* background_work_arg) override;

  void ScheduleFlush(void (*background_work_function)(void* background_work_arg),
                     void* background_work_arg) override ;

  void StartThread(void (*thread_main)(void* thread_main_arg),
                   void* thread_main_arg) override {
    std::thread new_thread(thread_main, thread_main_arg);
    new_thread.detach();
  }

  Status GetTestDirectory(std::string* result) override {
    const char* env = std::getenv("TEST_TMPDIR");
    if (env && env[0] != '\0') {
      *result = env;
    } else {
      char buf[100];
      std::snprintf(buf, sizeof(buf), "/tmp/leveldbtest-%d",
                    static_cast<int>(::geteuid()));
      *result = buf;
    }

    // The CreateDir status is ignored because the directory may already exist.
    CreateDir(*result);

    return Status::OK();
  }

  Status NewLogger(const std::string& filename, Logger** result) override {
    int fd = ::open(filename.c_str(),
                    O_APPEND | O_WRONLY | O_CREAT | kOpenBaseFlags, 0644);
    if (fd < 0) {
      *result = nullptr;
      return PosixError(filename, errno);
    }

    std::FILE* fp = ::fdopen(fd, "w");
    if (fp == nullptr) {
      ::close(fd);
      *result = nullptr;
      return PosixError(filename, errno);
    } else {
      *result = new PosixLogger(fp);
      return Status::OK();
    }
  }

  uint64_t NowMicros() override {
    static constexpr uint64_t kUsecondsPerSecond = 1000000;
    struct ::timeval tv;
    ::gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * kUsecondsPerSecond + tv.tv_usec;
  }

  void SleepForMicroseconds(int micros) override {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
  }

 private:
  void BackgroundThreadMain();

  void FlushThreadMain();

  static void BackgroundThreadEntryPoint(PosixEnv* env) {
    env->BackgroundThreadMain();
  }

  static void FlushThreadEntryPoint(PosixEnv* env) {
    env->FlushThreadMain();
  }

  // Stores the work item data in a Schedule() call.
  //
  // Instances are constructed on the thread calling Schedule() and used on the
  // background thread.
  //
  // This structure is thread-safe because it is immutable.
  struct BackgroundWorkItem {
    explicit BackgroundWorkItem(void (*function)(void* arg), void* arg)
        : function(function), arg(arg) {}

    void (*const function)(void*);
    void* const arg;
  };

  port::Mutex background_work_mutex_;
  port::CondVar background_work_cv_ GUARDED_BY(background_work_mutex_);
  bool started_background_thread_ GUARDED_BY(background_work_mutex_);

  std::queue<BackgroundWorkItem> background_work_queue_
      GUARDED_BY(background_work_mutex_);


  port::Mutex flush_mutex_;
  port::CondVar flush_cv_  GUARDED_BY(flush_mutex_);
  std::queue<BackgroundWorkItem> flush_queue_ GUARDED_BY(flush_mutex_);
  bool started_flush_thread_ GUARDED_BY(flush_mutex_);

  PosixLockTable locks_;  // Thread-safe.
  Limiter mmap_limiter_;  // Thread-safe.
  Limiter fd_limiter_;    // Thread-safe.
  bool use_direct_io_;
};

// Return the maximum number of concurrent mmaps.
int MaxMmaps() { return g_mmap_limit; }

// Return the maximum number of read-only files to keep open.
int MaxOpenFiles() {
  if (g_open_read_only_file_limit >= 0) {
    return g_open_read_only_file_limit;
  }
#ifdef __Fuchsia__
  // Fuchsia doesn't implement getrlimit.
  g_open_read_only_file_limit = 50;
#else
  struct ::rlimit rlim;
  if (::getrlimit(RLIMIT_NOFILE, &rlim)) {
    // getrlimit failed, fallback to hard-coded default.
    g_open_read_only_file_limit = 50;
  } else if (rlim.rlim_cur == RLIM_INFINITY) {
    g_open_read_only_file_limit = std::numeric_limits<int>::max();
  } else {
    // Allow use of 20% of available file descriptors for read-only files.
    g_open_read_only_file_limit = rlim.rlim_cur / 5;
  }
#endif
  return g_open_read_only_file_limit;
}

}  // namespace

PosixEnv::PosixEnv()
    : background_work_cv_(&background_work_mutex_),
      started_background_thread_(false),
      mmap_limiter_(MaxMmaps()),
      fd_limiter_(MaxOpenFiles()),
      flush_cv_(&flush_mutex_),
      started_flush_thread_(false) {}

void PosixEnv::ScheduleFlush(
    void (*background_work_function)(void* background_work_arg),
    void* background_work_arg) {
  
  flush_mutex_.Lock();

  // Start the background thread, if we haven't done so already.
  if (!started_flush_thread_) {
    started_flush_thread_ = true;
    std::thread flush_thread(PosixEnv::FlushThreadEntryPoint, this);
    fprintf(stderr,"start a flush thread!\n");
    flush_thread.detach();
  }

  // If the queue is empty, the background thread may be waiting for work.
  if (flush_queue_.empty()) {
    flush_cv_.Signal();
  }

  flush_queue_.emplace(background_work_function, background_work_arg);
  flush_mutex_.Unlock();
}


void PosixEnv::Schedule(
    void (*background_work_function)(void* background_work_arg),
    void* background_work_arg) {
  background_work_mutex_.Lock();

  // Start the background thread, if we haven't done so already.
  if (!started_background_thread_) {
    started_background_thread_ = true;
    std::thread background_thread(PosixEnv::BackgroundThreadEntryPoint, this);
    // fprintf(stderr,"start a Background thread!\n");
    background_thread.detach();
  }

  // If the queue is empty, the background thread may be waiting for work.
  if (background_work_queue_.empty()) {
    background_work_cv_.Signal();
  }

  background_work_queue_.emplace(background_work_function, background_work_arg);
  background_work_mutex_.Unlock();
}

void PosixEnv::BackgroundThreadMain() {
  while (true) {
    background_work_mutex_.Lock();

    // Wait until there is work to be done.
    while (background_work_queue_.empty()) {
      background_work_cv_.Wait();
    }

    assert(!background_work_queue_.empty());
    auto background_work_function = background_work_queue_.front().function;
    void* background_work_arg = background_work_queue_.front().arg;
    background_work_queue_.pop();

    background_work_mutex_.Unlock();
    background_work_function(background_work_arg);
  }
}

void PosixEnv::FlushThreadMain() {
  while (true) {
    flush_mutex_.Lock();

    // Wait until there is work to be done.
    while (flush_queue_.empty()) {
      flush_cv_.Wait();
    }

    assert(!flush_queue_.empty());
    auto background_work_function = flush_queue_.front().function;
    void* background_work_arg = flush_queue_.front().arg;
    flush_queue_.pop();

    flush_mutex_.Unlock();
    background_work_function(background_work_arg);
  }
}

namespace {

// Wraps an Env instance whose destructor is never created.
//
// Intended usage:
//   using PlatformSingletonEnv = SingletonEnv<PlatformEnv>;
//   void ConfigurePosixEnv(int param) {
//     PlatformSingletonEnv::AssertEnvNotInitialized();
//     // set global configuration flags.
//   }
//   Env* Env::Default() {
//     static PlatformSingletonEnv default_env;
//     return default_env.env();
//   }
template <typename EnvType>
class SingletonEnv {
 public:
  SingletonEnv() {
#if !defined(NDEBUG)
    env_initialized_.store(true, std::memory_order_relaxed);
#endif  // !defined(NDEBUG)
    static_assert(sizeof(env_storage_) >= sizeof(EnvType),
                  "env_storage_ will not fit the Env");
    static_assert(alignof(decltype(env_storage_)) >= alignof(EnvType),
                  "env_storage_ does not meet the Env's alignment needs");
    new (&env_storage_) EnvType();
  }
  ~SingletonEnv() = default;

  SingletonEnv(const SingletonEnv&) = delete;
  SingletonEnv& operator=(const SingletonEnv&) = delete;

  Env* env() { return reinterpret_cast<Env*>(&env_storage_); }

  static void AssertEnvNotInitialized() {
#if !defined(NDEBUG)
    assert(!env_initialized_.load(std::memory_order_relaxed));
#endif  // !defined(NDEBUG)
  }

 private:
  typename std::aligned_storage<sizeof(EnvType), alignof(EnvType)>::type
      env_storage_;
#if !defined(NDEBUG)
  static std::atomic<bool> env_initialized_;
#endif  // !defined(NDEBUG)
};

#if !defined(NDEBUG)
template <typename EnvType>
std::atomic<bool> SingletonEnv<EnvType>::env_initialized_;
#endif  // !defined(NDEBUG)

using PosixDefaultEnv = SingletonEnv<PosixEnv>;

}  // namespace

void EnvPosixTestHelper::SetReadOnlyFDLimit(int limit) {
  PosixDefaultEnv::AssertEnvNotInitialized();
  g_open_read_only_file_limit = limit;
}

void EnvPosixTestHelper::SetReadOnlyMMapLimit(int limit) {
  PosixDefaultEnv::AssertEnvNotInitialized();
  g_mmap_limit = limit;
}

Env* Env::Default() {
  static PosixDefaultEnv env_container;
  return env_container.env();
}

}  // namespace leveldb
