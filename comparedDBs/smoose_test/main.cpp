#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <random>

#include <gflags/gflags.h> 


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
#include "mixgraph_workload.h"


DEFINE_string(data_file_path, "", "Path to the CSV data file.");
DEFINE_int64(num, 1000000, "Number of key-value pairs to write.");
DEFINE_int32(key_size, 20, "Size of the key in bytes.");
DEFINE_int32(value_size, 100, "Size of the value in bytes.");
DEFINE_int32(entries_per_batch, 1000, "Number of entries per write batch.");
DEFINE_string(db, "", "Use the db with the following name.");
DEFINE_int64(stats_interval_ops, 100000, "Interval in keys for printing statistics. Set to 0 to disable.");
DEFINE_int32(bits_per_key, 10, "Bits per key for Bloom filter."); // 新增 bits_per_key 参数
DEFINE_bool(sync, false, "Sync all writes to disk");
// 在 main 函数中添加调用选项
// 可以通过添加一个新的 flag 来选择运行哪个函数
DEFINE_string(workload, "fill", "Workload type: 'fill' or 'mixed'");

// 首先添加新的 gflags
DEFINE_double(read_ratio, 0.5, "Read ratio (0.0 to 1.0)");
DEFINE_double(write_ratio, 0.5, "Write ratio (0.0 to 1.0)");
DEFINE_bool(verify_reads, false, "Verify read values against expected values");

DEFINE_int32(perf_level, 1, "Performance monitoring level (1-5). 1=disabled, 5=full detail");

DEFINE_int64(block_cache_mb, 128, "Block cache size in MB (e.g., 8, 64, 128)");
DEFINE_int64(table_cache_size, 1000, "Table cache size (number of open files)");

// for mixgraph
DEFINE_int64(mix_max_scan_len, 10000, "The max scan length of Iterator");
DEFINE_int64(keyrange_num, 1,
             "The number of key ranges that are in the same prefix "
             "group, each prefix range will have its key access distribution");
DEFINE_double(mix_get_ratio, 1.0,
              "The ratio of Get queries of mix_graph workload");
DEFINE_double(mix_put_ratio, 0.0,
              "The ratio of Put queries of mix_graph workload");
DEFINE_double(mix_seek_ratio, 0.0,
              "The ratio of Seek queries of mix_graph workload");
DEFINE_int32(value_size_max, 102400, "Max size of random value");
DEFINE_int32(value_size_min, 100, "Min size of random value");

DEFINE_double(compression_ratio, 0.5,
              "Arrange to generate values that shrink to this fraction of "
              "their original size after compression");
DEFINE_int64(mix_max_value_size, 1024, "The max value size of this workload");
DEFINE_int32(key_size, 16, "size of each key");
DEFINE_bool(sine_mix_rate, false,
            "Enable the sine QPS control on the mix workload");
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
DEFINE_double(key_dist_a, 0.0,
              "The parameter 'a' of key access distribution model f(x)=a*x^b");
DEFINE_double(key_dist_b, 0.0,
              "The parameter 'b' of key access distribution model f(x)=a*x^b");
DEFINE_int32(duration, 0,
             "Time in seconds for the random-ops tests to run."
             " When 0 then num & reads determine the test duration");
DEFINE_int64(reads, -1,
             "Number of read operations to do.  "
             "If negative, do FLAGS_num reads.");
DEFINE_int32(ops_between_duration_checks, 1000,
             "Check duration limit every x ops");
DEFINE_int32(prefix_size, 0,
             "control the prefix size for HashSkipList and plain table");


  enum DistributionType : unsigned char { kFixed = 0, kUniform, kNormal };
  static enum DistributionType FLAGS_value_size_distribution_type_e = kFixed;
  static unsigned int value_size = 128;
  static ROCKSDB_NAMESPACE::Env* FLAGS_env = ROCKSDB_NAMESPACE::Env::Default();


  class Random64 {
  private:
    std::mt19937_64 generator_;

  public:
    explicit Random64(uint64_t s) : generator_(s) {}

    // Generates the next random number
    uint64_t Next() { return generator_(); }

    // Returns a uniformly distributed value in the range [0..n-1]
    // REQUIRES: n > 0
    uint64_t Uniform(uint64_t n) {
      return std::uniform_int_distribution<uint64_t>(0, n - 1)(generator_);
    }

    // Randomly returns true ~"1/n" of the time, and false otherwise.
    // REQUIRES: n > 0
    bool OneIn(uint64_t n) { return Uniform(n) == 0; }

    // Skewed: pick "base" uniformly from range [0,max_log] and then
    // return "base" random bits.  The effect is to pick a number in the
    // range [0,2^max_log-1] with exponential bias towards smaller numbers.
    uint64_t Skewed(int max_log) {
      return Uniform(uint64_t(1) << Uniform(max_log + 1));
    }
  };


  struct KeyrangeUnit {
    int64_t keyrange_start;
    int64_t keyrange_access;
    int64_t keyrange_keys;
  };

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

  class FixedDistribution : public BaseDistribution {
  public:
    FixedDistribution(unsigned int size)
        : BaseDistribution(size, size), size_(size) {}

  private:
    unsigned int Get() override { return size_; }
    bool NeedTruncate() override { return false; }
    unsigned int size_;
  };

  // A very simple random number generator.  Not especially good at
  // generating truly random bits, but good enough for our needs in this
  // package.
  class Random {
  private:
    enum : uint32_t {
      M = 2147483647L  // 2^31-1
    };
    enum : uint64_t {
      A = 16807  // bits 14, 8, 7, 5, 2, 1, 0
    };

    uint32_t seed_;

    static uint32_t GoodSeed(uint32_t s) { return (s & M) != 0 ? (s & M) : 1; }

  public:
    // This is the largest value that can be returned from Next()
    enum : uint32_t { kMaxNext = M };

    explicit Random(uint32_t s) : seed_(GoodSeed(s)) {}

    void Reset(uint32_t s) { seed_ = GoodSeed(s); }

    uint32_t Next() {
      // We are computing
      //       seed_ = (seed_ * A) % M,    where M = 2^31-1
      //
      // seed_ must not be zero or M, or else all subsequent computed values
      // will be zero or M respectively.  For all other values, seed_ will end
      // up cycling through every number in [1,M-1]
      uint64_t product = seed_ * A;

      // Compute (product % M) using the fact that ((x << 31) % M) == x.
      seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
      // The first reduction may overflow by 1 bit, so we may need to
      // repeat.  mod == M is not possible; using > allows the faster
      // sign-bit-based test.
      if (seed_ > M) {
        seed_ -= M;
      }
      return seed_;
    }

    uint64_t Next64() { return (uint64_t{Next()} << 32) | Next(); }

    // Returns a uniformly distributed value in the range [0..n-1]
    // REQUIRES: n > 0
    uint32_t Uniform(int n) { return Next() % n; }

    // Randomly returns true ~"1/n" of the time, and false otherwise.
    // REQUIRES: n > 0
    bool OneIn(int n) { return Uniform(n) == 0; }

    // "Optional" one-in-n, where 0 or negative always returns false
    // (may or may not consume a random value)
    bool OneInOpt(int n) { return n > 0 && OneIn(n); }

    // Returns random bool that is true for the given percentage of
    // calls on average. Zero or less is always false and 100 or more
    // is always true (may or may not consume a random value)
    bool PercentTrue(int percentage) {
      return static_cast<int>(Uniform(100)) < percentage;
    }

    // Skewed: pick "base" uniformly from range [0,max_log] and then
    // return "base" random bits.  The effect is to pick a number in the
    // range [0,2^max_log-1] with exponential bias towards smaller numbers.
    uint32_t Skewed(int max_log) { return Uniform(1 << Uniform(max_log + 1)); }

    // Returns a random string of length "len"
    std::string RandomString(int len);

    // Generates a random string of len bytes using human-readable characters
    std::string HumanReadableString(int len);

    // Generates a random binary data
    std::string RandomBinaryString(int len);

    // Returns a Random instance for use by the current thread without
    // additional locking
    static Random* GetTLSInstance();
  };

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

    rocksdb::Slice CompressibleString(Random* rnd, double compressed_fraction, int len,
                         std::string* dst) {
      int raw = static_cast<int>(len * compressed_fraction);
      if (raw < 1) {
        raw = 1;
      }
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

void mixedWorkload(rocksdb::DB* db) {
  if (FLAGS_data_file_path.empty()) {
    fprintf(stderr, "Error: You must specify --data_file_path\n");
    return;
  }

  std::ifstream csv_file(FLAGS_data_file_path);
  if (!csv_file.is_open()) {
    fprintf(stderr, "Error: Unable to open file: %s\n", FLAGS_data_file_path.c_str());
    return;
  }

  // 首先加载所有的 keys 到内存中
  std::vector<uint64_t> keys;
  std::string line;
  std::getline(csv_file, line); // 跳过标题行

  fprintf(stderr, "Loading keys from CSV file...\n");
  for (int64_t i = 0; i < FLAGS_num+1000 && std::getline(csv_file, line); ++i) {
    std::stringstream line_stream(line);
    std::string cell;
    std::vector<std::string> row_data;
    
    while (getline(line_stream, cell, ',')) {
      row_data.push_back(cell);
    }

    if (row_data.size() >= 2) {
      keys.push_back(std::stoull(row_data[1]));
    }
  }
  csv_file.close();

  if (keys.empty()) {
    fprintf(stderr, "Error: No valid keys found in CSV file\n");
    return;
  }

  fprintf(stderr, "Loaded %zu keys from CSV file\n", keys.size());

  // 计算读写操作数量
  const int64_t total_ops = FLAGS_num;
  const int64_t read_ops = static_cast<int64_t>(total_ops * FLAGS_read_ratio);
  const int64_t write_ops = static_cast<int64_t>(total_ops * FLAGS_write_ratio);

  fprintf(stderr, "\n--- Mixed Workload Configuration ---\n");
  fprintf(stderr, "Total operations:  %ld\n", total_ops);
  fprintf(stderr, "Read operations:   %ld (%.1f%%)\n", read_ops, FLAGS_read_ratio * 100);
  fprintf(stderr, "Write operations:  %ld (%.1f%%)\n", write_ops, FLAGS_write_ratio * 100);
  fprintf(stderr, "Key size:          %d\n", FLAGS_key_size);
  fprintf(stderr, "Value size:        %d\n", FLAGS_value_size);
  fprintf(stderr, "------------------------------------\n\n");

  // 随机数生成器
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> key_dist(0, keys.size() - 1);
  std::uniform_real_distribution<double> op_dist(0.0, 1.0);

  // 统计变量
  int64_t num_reads = 0;
  int64_t num_writes = 0;
  int64_t num_found = 0;
  int64_t num_not_found = 0;
  int64_t num_read_errors = 0;
  int64_t num_write_errors = 0;

  rocksdb::WriteOptions write_options;
  rocksdb::ReadOptions read_options;

  if (FLAGS_sync) {
    write_options.sync = true;
  }

  auto start_time = std::chrono::high_resolution_clock::now();
  auto last_report_time = start_time;
  int64_t ops_at_last_report = 0;

  // 用于分别计算读写性能
  auto read_start_time = std::chrono::high_resolution_clock::now();
  auto write_start_time = std::chrono::high_resolution_clock::now();
  double total_read_time = 0.0;
  double total_write_time = 0.0;

  // 执行混合工作负载
  for (int64_t i = 0; i < total_ops; i++) {
    // 随机选择一个 key
    size_t key_idx = key_dist(gen);
    uint64_t key_num = keys[key_idx];
    
    // 格式化 key
    char key_buffer[100];
    snprintf(key_buffer, sizeof(key_buffer), "%0*llu", FLAGS_key_size, (unsigned long long)key_num);

    // 根据读写比例决定操作类型
    bool is_read = (op_dist(gen) < FLAGS_read_ratio);

    if (is_read) {
      // 执行读操作
      auto read_op_start = std::chrono::high_resolution_clock::now();
      std::string value;
      rocksdb::Status s = db->Get(read_options, key_buffer, &value);
      auto read_op_end = std::chrono::high_resolution_clock::now();
      total_read_time += std::chrono::duration<double>(read_op_end - read_op_start).count();
      
      if (s.ok()) {
        num_found++;
        
        // 如果启用了验证，检查值是否正确
        if (FLAGS_verify_reads) {
          std::string expected_value(FLAGS_value_size, 'a');
          if (value != expected_value) {
            fprintf(stderr, "Warning: Value mismatch for key %s\n", key_buffer);
          }
        }
      } else if (s.IsNotFound()) {
        num_not_found++;
      } else {
        num_read_errors++;
        fprintf(stderr, "Read error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_reads++;
    } else {
      // 执行写操作
      auto write_op_start = std::chrono::high_resolution_clock::now();
      std::string value(FLAGS_value_size, 'a');
      rocksdb::Status s = db->Put(write_options, key_buffer, value);
      auto write_op_end = std::chrono::high_resolution_clock::now();
      total_write_time += std::chrono::duration<double>(write_op_end - write_op_start).count();
      
      if (!s.ok()) {
        num_write_errors++;
        fprintf(stderr, "Write error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_writes++;
    }

    // 定期报告进度
    if (FLAGS_stats_interval_ops > 0 && 
        (i + 1 - ops_at_last_report >= FLAGS_stats_interval_ops)) {
      auto current_time = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> report_duration = current_time - last_report_time;
      
      const int64_t ops_in_interval = (i + 1) - ops_at_last_report;
      const double throughput = (report_duration.count() > 0) ? 
                               (ops_in_interval / report_duration.count()) : 0;
      const double total_elapsed_time = 
          std::chrono::duration<double>(current_time - start_time).count();

      fprintf(stdout, "[%.1fs] Ops: %ld (R:%ld/W:%ld), Throughput: %.2f ops/sec\n",
              total_elapsed_time, i + 1, num_reads, num_writes, throughput);

      last_report_time = current_time;
      ops_at_last_report = i + 1;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  // 打印最终统计信息
  fprintf(stdout, "\n--- Mixed Workload Summary ---\n");
  fprintf(stdout, "Total operations:     %ld\n", total_ops);
  fprintf(stdout, "Read operations:      %ld (%.1f%%)\n", 
          num_reads, (num_reads * 100.0) / total_ops);
  fprintf(stdout, "Write operations:     %ld (%.1f%%)\n", 
          num_writes, (num_writes * 100.0) / total_ops);
  fprintf(stdout, "\nRead Results:\n");
  fprintf(stdout, "  Keys found:         %ld (%.1f%%)\n", 
          num_found, (num_found * 100.0) / std::max(num_reads, 1L));
  fprintf(stdout, "  Keys not found:     %ld (%.1f%%)\n", 
          num_not_found, (num_not_found * 100.0) / std::max(num_reads, 1L));
  fprintf(stdout, "  Read errors:        %ld\n", num_read_errors);
  fprintf(stdout, "\nWrite Results:\n");
  fprintf(stdout, "  Write errors:       %ld\n", num_write_errors);
  fprintf(stdout, "\nPerformance:\n");
  fprintf(stdout, "  Total time:         %.2f seconds\n", duration.count());
  fprintf(stdout, "  Total throughput:   %.2f ops/sec\n", total_ops / duration.count());
  fprintf(stdout, "  Read performance:\n");
  if (num_reads > 0) {
    fprintf(stdout, "    - Read throughput:    %.2f ops/sec\n", num_reads / total_read_time);
    fprintf(stdout, "    - Avg read latency:   %.2f us\n", (total_read_time / num_reads) * 1000000);
  }
  fprintf(stdout, "  Write performance:\n");
  if (num_writes > 0) {
    fprintf(stdout, "    - Write throughput:   %.2f ops/sec\n", num_writes / total_write_time);
    fprintf(stdout, "    - Avg write latency:  %.2f us\n", (total_write_time / num_writes) * 1000000);
  }
  fprintf(stdout, "------------------------------\n");
}



void ExecuteWorkload(rocksdb::DB* db) {
  if (FLAGS_data_file_path.empty()) {
    fprintf(stderr, "Error: You must specify --data_file_path\n");
    return;
  }

  std::ifstream csv_file(FLAGS_data_file_path);
  if (!csv_file.is_open()) {
    fprintf(stderr, "Error: Unable to open file: %s\n", FLAGS_data_file_path.c_str());
    return;
  }

  // 首先加载所有的 keys 到内存中
  std::vector<uint64_t> keys;
  std::vector<std::string> ops_type;
  std::string line;
  std::getline(csv_file, line); // 跳过标题行

  fprintf(stderr, "Loading keys from CSV file...\n");
  for (int64_t i = 0; i < FLAGS_num+1000 && std::getline(csv_file, line); ++i) {
    std::stringstream line_stream(line);
    std::string cell;
    std::vector<std::string> row_data;
    
    while (getline(line_stream, cell, ',')) {
      row_data.push_back(cell);
    }

    if (row_data.size() >= 2) {
      keys.push_back(std::stoull(row_data[1]));
    }
    ops_type.push_back(row_data[5]);
  }
  csv_file.close();

  if (keys.empty()) {
    fprintf(stderr, "Error: No valid keys found in CSV file\n");
    return;
  }

  fprintf(stderr, "Loaded %zu keys from CSV file\n", keys.size());

  // 计算读写操作数量
  const int64_t total_ops = FLAGS_num;
  const int64_t read_ops = static_cast<int64_t>(total_ops * FLAGS_read_ratio);
  const int64_t write_ops = static_cast<int64_t>(total_ops * FLAGS_write_ratio);

  fprintf(stderr, "\n--- Mixed Workload Configuration ---\n");
  fprintf(stderr, "Total operations:  %ld\n", total_ops);
  fprintf(stderr, "Read operations:   %ld (%.1f%%)\n", read_ops, FLAGS_read_ratio * 100);
  fprintf(stderr, "Write operations:  %ld (%.1f%%)\n", write_ops, FLAGS_write_ratio * 100);
  fprintf(stderr, "Key size:          %d\n", FLAGS_key_size);
  fprintf(stderr, "Value size:        %d\n", FLAGS_value_size);
  fprintf(stderr, "------------------------------------\n\n");


  // 统计变量
  int64_t num_reads = 0;
  int64_t num_writes = 0;
  int64_t num_found = 0;
  int64_t num_not_found = 0;
  int64_t num_read_errors = 0;
  int64_t num_write_errors = 0;

  rocksdb::WriteOptions write_options;
  rocksdb::ReadOptions read_options;

  auto start_time = std::chrono::high_resolution_clock::now();
  auto last_report_time = start_time;
  int64_t ops_at_last_report = 0;

  // 用于分别计算读写性能
  auto read_start_time = std::chrono::high_resolution_clock::now();
  auto write_start_time = std::chrono::high_resolution_clock::now();
  double total_read_time = 0.0;
  double total_write_time = 0.0;
  
  double total_operation_time = 0.0;
  int64_t total_operations_executed = 0;

  if (FLAGS_sync) {
    write_options.sync = true;
  }

  // 执行混合工作负载
  for (int64_t i = 0; i < total_ops; i++) {
    // 随机选择一个 key
    uint64_t key_num = keys[i];
    
    // 格式化 key
    char key_buffer[100];
    snprintf(key_buffer, sizeof(key_buffer), "%0*llu", FLAGS_key_size, (unsigned long long)key_num);

    if (ops_type[i]=="get"||ops_type[i]=="gets") {
      // 执行读操作
      auto read_op_start = std::chrono::high_resolution_clock::now();
      std::string value;
      rocksdb::Status s = db->Get(read_options, key_buffer, &value);
      auto read_op_end = std::chrono::high_resolution_clock::now();

      double op_time = std::chrono::duration<double>(read_op_end - read_op_start).count();
      total_read_time += op_time;
      total_operation_time += op_time;  // 添加到总体时间
      total_operations_executed++;
      
      if (s.ok()) {
        num_found++;
        
        // 如果启用了验证，检查值是否正确
        if (FLAGS_verify_reads) {
          std::string expected_value(FLAGS_value_size, 'a');
          if (value != expected_value) {
            fprintf(stderr, "Warning: Value mismatch for key %s\n", key_buffer);
          }
        }
      } else if (s.IsNotFound()) {
        num_not_found++;
      } else {
        num_read_errors++;
        fprintf(stderr, "Read error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_reads++;
    } else if(ops_type[i]=="add"||ops_type[i]=="set") {
      // 执行写操作
      auto write_op_start = std::chrono::high_resolution_clock::now();
      std::string value(FLAGS_value_size, 'a');
      rocksdb::Status s = db->Put(write_options, key_buffer, value);
      auto write_op_end = std::chrono::high_resolution_clock::now();
      total_write_time += std::chrono::duration<double>(write_op_end - write_op_start).count();
      
      if (!s.ok()) {
        num_write_errors++;
        fprintf(stderr, "Write error for key %s: %s\n", key_buffer, s.ToString().c_str());
      }
      
      num_writes++;
    }else{}

    // 定期报告进度
    if (FLAGS_stats_interval_ops > 0 && 
        (i + 1 - ops_at_last_report >= FLAGS_stats_interval_ops)) {
      auto current_time = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> report_duration = current_time - last_report_time;
      
      const int64_t ops_in_interval = (i + 1) - ops_at_last_report;
      const double throughput = (report_duration.count() > 0) ? 
                               (ops_in_interval / report_duration.count()) : 0;
      const double total_elapsed_time = 
          std::chrono::duration<double>(current_time - start_time).count();

      fprintf(stdout, "[%.1fs] Ops: %ld (R:%ld/W:%ld), Throughput: %.2f ops/sec\n",
              total_elapsed_time, i + 1, num_reads, num_writes, throughput);

      last_report_time = current_time;
      ops_at_last_report = i + 1;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  double overall_avg_latency = (total_operations_executed > 0) ? 
                              (total_operation_time / total_operations_executed) * 1000000 : 0;

  // 打印最终统计信息
  // 打印最终统计信息
  fprintf(stdout, "\n--- Performance of Workload Summary ---\n");
  fprintf(stdout, "Total operations:     %ld\n", total_ops);
  fprintf(stdout, "Read operations:      %ld (%.1f%%)\n", 
          num_reads, (num_reads * 100.0) / total_ops);
  fprintf(stdout, "Write operations:     %ld (%.1f%%)\n", 
          num_writes, (num_writes * 100.0) / total_ops);
  fprintf(stdout, "\nRead Results:\n");
  fprintf(stdout, "  Keys found:         %ld (%.1f%%)\n", 
          num_found, (num_found * 100.0) / std::max(num_reads, 1L));
  fprintf(stdout, "  Keys not found:     %ld (%.1f%%)\n", 
          num_not_found, (num_not_found * 100.0) / std::max(num_reads, 1L));
  fprintf(stdout, "  Read errors:        %ld\n", num_read_errors);
  fprintf(stdout, "\nWrite Results:\n");
  fprintf(stdout, "  Write errors:       %ld\n", num_write_errors);
  fprintf(stdout, "\nPerformance:\n");
  fprintf(stdout, "  Total time:         %.2f seconds\n", duration.count());
  fprintf(stdout, "  Total throughput:   %.2f ops/sec\n", total_ops / duration.count());
  fprintf(stdout, "  Overall avg latency: %.2f us\n", overall_avg_latency);  // 新增总体平均延迟
  fprintf(stdout, "\n  Read performance:\n");
  if (num_reads > 0) {
    fprintf(stdout, "    - Read throughput:    %.2f ops/sec\n", num_reads / total_read_time);
    fprintf(stdout, "    - Avg read latency:   %.2f us\n", (total_read_time / num_reads) * 1000000);
  }
  fprintf(stdout, "  Write performance:\n");
  if (num_writes > 0) {
    fprintf(stdout, "    - Write throughput:   %.2f ops/sec\n", num_writes / total_write_time);
    fprintf(stdout, "    - Avg write latency:  %.2f us\n", (total_write_time / num_writes) * 1000000);
  }
  fprintf(stdout, "------------------------------\n");
}


void fillcluster(rocksdb::DB* db) {
  if (FLAGS_data_file_path.empty()) {
    fprintf(stderr, "Error: You must specify --data_file_path\n");
    return;
  }

  std::ifstream csv_file(FLAGS_data_file_path);
  if (!csv_file.is_open()) {
    fprintf(stderr, "Error: Unable to open file: %s\n", FLAGS_data_file_path.c_str());
    return;
  }

  rocksdb::WriteBatch batch;
  rocksdb::WriteOptions write_options;
  // write_options.sync = true; // 如果需要同步写入，可以打开此选项

  if (FLAGS_sync) {
    write_options.sync = true;
  }

  std::string line;
  std::getline(csv_file, line);// 读取并丢弃 CSV 文件的标题行 (如果存在)

  int64_t num_written = 0;
  int64_t num_written_at_last_report = 0;
  auto start_time = std::chrono::high_resolution_clock::now();
  auto last_report_time = start_time;

  fprintf(stderr, "num_: %ld entries_per_batch_:%d\n The key size:%d value size:%d \n"
    , FLAGS_num, FLAGS_entries_per_batch,FLAGS_key_size, FLAGS_value_size);
  for (int64_t i = 0; i < FLAGS_num; i += FLAGS_entries_per_batch) {
    batch.Clear();
    for (int64_t j = 0; j < FLAGS_entries_per_batch ; j++) {
      if (!std::getline(csv_file, line)) {
        fprintf(stderr, "Warning: Reached end of CSV file before writing %ld keys. Wrote %ld keys.\n", FLAGS_num, num_written);
        i = FLAGS_num; // 跳出外层循环
        break;
      }

      std::stringstream line_stream(line);
      std::string cell;
      std::vector<std::string> row_data;
      while (getline(line_stream, cell, ',')) {
        row_data.push_back(cell);
      }

      // 假设 key 在 CSV 的第二列 (index 1)
      if (row_data.size() < 2) {
        fprintf(stderr, "Warning: Skipping invalid CSV row: %s\n", line.c_str());
        continue;
      }

      // 格式化 Key
      char key_buffer[100];
      const uint64_t k = std::stoull(row_data[1]);
      snprintf(key_buffer, sizeof(key_buffer), "%0*llu", FLAGS_key_size, (unsigned long long)k);
      // 生成 Value
      std::string value_str(FLAGS_value_size, 'a');
      rocksdb::Slice val(value_str);

      batch.Put(key_buffer, val);
      num_written++;
    }

    if (batch.Count() > 0) {
      rocksdb::Status s = db->Write(write_options, &batch);
      if (!s.ok()) {
        fprintf(stderr, "FATAL: Batch write error: %s\n", s.ToString().c_str());
        exit(1);
      }
    }

    if (FLAGS_stats_interval_ops > 0 && (num_written - num_written_at_last_report >= FLAGS_stats_interval_ops)) {
      auto current_time = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> report_duration = current_time - last_report_time;
            
      const int64_t ops_in_interval = num_written - num_written_at_last_report;
      const double throughput = (report_duration.count() > 0) ? (ops_in_interval / report_duration.count()) : 0;
      const double total_elapsed_time = std::chrono::duration<double>(current_time - start_time).count();

      fprintf(stdout, "[%.1fs] ... Keys written: %ld, Interval Throughput: %.2f ops/sec\n",
                    total_elapsed_time, num_written, throughput);

      // 为下一个间隔重置
      last_report_time = current_time;
      num_written_at_last_report = num_written;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;

  fprintf(stdout, "\n--- LoadDataFromCSV Summary ---\n");
  fprintf(stdout, "Keys written:      %ld\n", num_written);
  fprintf(stdout, "Total time:        %.2f seconds\n", duration.count());
  fprintf(stdout, "Ops per second:    %.2f\n", num_written / duration.count());
  fprintf(stdout, "-------------------------------\n");

  csv_file.close();
}


rocksdb::Slice AllocateKey(std::unique_ptr<const char[]>* key_guard) {
  char* data = new char[FLAGS_key_size];
  const char* const_data = data;
  key_guard->reset(const_data);
  return rocksdb::Slice(key_guard->get(), FLAGS_key_size);
}



// 新增函数：打印当前进程的内存使用情况 (适用于 Linux)
void printMemoryUsage() {
  std::ifstream status_file("/proc/self/status");
  if (!status_file.is_open()) {
    fprintf(stderr, "Warning: Could not open /proc/self/status to read memory usage.\n");
    return;
  }

  std::string line;
  fprintf(stdout, "\n--- Process Memory Usage ---\n");
  while (std::getline(status_file, line)) {
    // VmRSS: Resident Set Size - 物理内存使用量
    if (line.rfind("VmRSS:", 0) == 0) {
      fprintf(stdout, "%s\n", line.c_str());
      break; // 找到后即可退出
    }
  }
  fprintf(stdout, "----------------------------\n");
  status_file.close();
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

void PrintPerfStatsIfEnabled() {
  if (FLAGS_perf_level < 2) {
    fprintf(stdout, "[Perf] perf_level=%d → Performance statistics disabled.\n", FLAGS_perf_level);
    return;
  }

  // 指针版本
  const auto* perf_context = rocksdb::get_perf_context();
  const auto* iostats_context = rocksdb::get_iostats_context();

  fprintf(stdout, "\n=== RocksDB PerfContext (perf_level=%d) ===\n", FLAGS_perf_level);
  fprintf(stdout, "%s\n", perf_context->ToString().c_str());

  fprintf(stdout, "\n=== RocksDB IOStatsContext ===\n");
  fprintf(stdout, "%s\n", iostats_context->ToString().c_str());
  fprintf(stdout, "=========================================\n");
}


int main(int argc, char** argv) {
  // 解析命令行参数
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  rocksdb::DB* db;
  rocksdb::Options options;

    // --- 根据读写比例动态配置 Smoose 参数 ---
    // 这是基于用户提供的针对不同工作负载的调优值
  // if (FLAGS_workload == "mixed") {
  //   if (std::abs(FLAGS_read_ratio - 0.5) < 0.01) {
  //     // 平衡读写 (50% 读 / 50% 写)
  //     fprintf(stderr, "Applying balanced workload Smoose configuration.\n");
  //     options.level_capacities = {7, 7, 7, 8,3};
  //     options.run_numbers = {7, 7, 7, 8, 3};
  //   } else if (std::abs(FLAGS_read_ratio - 0.9) < 0.01 || std::abs(FLAGS_read_ratio - 1.0) < 0.01) {
  //     // 重度读 (90%-100% 读)
  //     fprintf(stderr, "Applying read-heavy workload Smoose configuration.\n");
  //     options.level_capacities = {27,27,13};
  //     options.run_numbers = {2,2,1};
  //   } else {
  //     // 默认配置为重度写 (例如: 10% 读 / 90% 写)
  //     fprintf(stderr, "Applying write-heavy (default) workload Smoose configuration.\n");
  //     options.level_capacities = {10, 11, 11, 8};
  //     options.run_numbers = {10, 11, 11, 7};
  //   }
  // } else {
  //   // 对于 "fill" 负载，使用重度写配置
  //   fprintf(stderr, "Applying write-heavy configuration for 'fill' workload.\n");
  //   options.level_capacities = {10, 11, 11, 8};
  //   options.run_numbers = {10, 11, 11, 7};
  // }

  // cluster 35 51 30 1 
  // options.level_capacities = {27,27,13};
  // options.run_numbers = {2,2,1};

  //cluster 40 49 13
  options.level_capacities = {7,7,7,8,3};
  options.run_numbers = {7,7,7,8,3};



  fprintf(stdout, "=== Cluster Configuration ===\n");
  fprintf(stdout, "level_capacities: ");
  for (size_t i = 0; i < options.level_capacities.size(); i++) {
    fprintf(stdout, "%ld", options.level_capacities[i]);
    if (i < options.level_capacities.size() - 1) {
      fprintf(stdout, ", ");
    }
  }
  fprintf(stdout, "\n");

  fprintf(stdout, "run_numbers: ");
  for (size_t i = 0; i < options.run_numbers.size(); i++) {
    fprintf(stdout, "%d", options.run_numbers[i]);
      if (i < options.run_numbers.size() - 1) {
      fprintf(stdout, ", ");
    }
  }
  fprintf(stdout, "\n");


  options.create_if_missing = true;
  options.use_direct_io_for_flush_and_compaction= true;
  options.use_direct_reads = true;
  options.allow_mmap_reads = false;          // 禁用内存映射读取
  options.allow_mmap_writes = false;         // 禁用内存映射写入

  // 强制使用传统的POSIX IO
  // options.access_hint_on_compaction_start = rocksdb::Options::NONE;
  // =================================================

  size_t block_cache_size_bytes = static_cast<size_t>(FLAGS_block_cache_mb) * 1024 * 1024;
  std::shared_ptr<rocksdb::Cache> block_cache = rocksdb::NewLRUCache(
    block_cache_size_bytes,  // 128MB 缓存大小
    -1,                   // shard数量，用于减少锁竞争
    false,              // 不严格限制容量
    0.0                 // high_pri_pool_ratio，用于索引和filter blocks
  );
  options.max_open_files = static_cast<int>(FLAGS_table_cache_size);

  
  // options.write_buffer_size = 2097152;
  // options.IncreaseParallelism(); // 使用推荐的并行设置
  // options.OptimizeLevelStyleCompaction(); // 使用推荐的LSM-Tree优化

  fprintf(stderr,"The memtable size is:%lu \n",options.write_buffer_size);
  rocksdb::BlockBasedTableOptions table_options;
  table_options.block_cache = block_cache;
  table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(FLAGS_bits_per_key));
  options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));

  // 打开数据库
  std::cout << "Opening database at: " << FLAGS_db << std::endl;
  rocksdb::Status status = rocksdb::DB::Open(options, FLAGS_db, &db);

  if (!status.ok()) {
    std::cerr << "Failed to open database: " << status.ToString() << std::endl;
    return 1;
  }
  std::cout << "Database opened successfully!" << std::endl;

  // --- Perf Level ---
  if (FLAGS_perf_level <= 1) {
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kDisable);
    fprintf(stdout, "[Perf] perf_level=%d (disabled)\n", FLAGS_perf_level);
  } else if (FLAGS_perf_level <= 3) {
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kEnableCount);
    fprintf(stdout, "[Perf] perf_level=%d (basic counters)\n", FLAGS_perf_level);
  } else if (FLAGS_perf_level == 4) {
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kEnableTimeExceptForMutex);
    fprintf(stdout, "[Perf] perf_level=4 (timing without mutex)\n");
  } else {
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kEnableTimeAndCPUTimeExceptForMutex);
    fprintf(stdout, "[Perf] perf_level=5 (full detail)\n");
  }

  // 重置本线程的统计（强烈推荐，避免历史噪音）
  rocksdb::get_perf_context()->Reset();
  rocksdb::get_iostats_context()->Reset();


  // --- 调用写入函数进行测试 ---
 // 验证读写比例
  if (FLAGS_workload == "mixed") {
    double ratio_sum = FLAGS_read_ratio + FLAGS_write_ratio;
    if (std::abs(ratio_sum - 1.0) > 0.001) {  // 允许小的浮点误差
      fprintf(stderr, "Error: read_ratio (%.2f) + write_ratio (%.2f) = %.2f, must equal 1.0\n", FLAGS_read_ratio, FLAGS_write_ratio, ratio_sum);
      fprintf(stderr, "Normalizing ratios...\n");
      FLAGS_read_ratio = FLAGS_read_ratio / ratio_sum;
      FLAGS_write_ratio = FLAGS_write_ratio / ratio_sum;
      fprintf(stderr, "Adjusted: read_ratio = %.2f, write_ratio = %.2f\n", 
              FLAGS_read_ratio, FLAGS_write_ratio);
    }
  }

  if (FLAGS_workload == "fill") {
    fillcluster(db);
  } else if (FLAGS_workload == "mixed") {
    // mixedWorkload(db);
    ExecuteWorkload(db);
  } else {
    fprintf(stderr, "Error: Unknown workload type: %s\n", FLAGS_workload.c_str());
  }  

  std::string stats_str;
  if (db->GetProperty("rocksdb.stats", &stats_str)) {
    fprintf(stdout, "\n--- RocksDB Statistics ---\n");
    fprintf(stdout, "%s\n", stats_str.c_str());
    fprintf(stdout, "--------------------------\n");
  }

  // 在关闭数据库前打印内存使用情况
  PrintPerfStatsIfEnabled();

  printMemoryUsage();

  printf_mem(db);

  // 关闭并删除数据库对象
  db->Close();
  delete db;
  std::cout << "Database closed." << std::endl;

  return 0;
}