# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(SmooseTester)

# 设置 C++ 标准，RocksDB 通常需要 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)



# 生成我们的可执行文件，源文件是 main.cpp
add_executable(smoose_tester main.cpp mixgraph_workload.cc random.cc port_posix.cc histogram.cc persistent_cache_tier.cc )


# 告诉 CMake 去哪里找 smoose 的头文件 (.h 文件)
# 这个路径需要根据你的实际情况调整，通常是 smoose 项目的 include 目录
target_include_directories(smoose_tester PRIVATE
    /home/jeff-wang/LSM-tuning/comparedDBs/MooseLSM/include
)

# 链接我们的程序需要的所有库
# - rocksdb: 这是 smoose 编译出来的核心库
# - pthread: RocksDB 依赖的多线程库
# - gflags, snappy, lz4, zstd, zlib: 这些是 RocksDB 常用的依赖库，必须加上
target_link_libraries(smoose_tester
    /home/jeff-wang/LSM-tuning/comparedDBs/MooseLSM/release/librocksdb.a 
    pthread
    gflags
    /usr/lib/x86_64-linux-gnu/libsnappy.so
    lz4
    zstd
    uring
    /usr/lib/x86_64-linux-gnu/libz.so
)