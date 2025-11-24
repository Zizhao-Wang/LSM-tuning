# CMake generated Testfile for 
# Source directory: /home/jeff-wang/LSM-tuning/comparedDBs/leveldb
# Build directory: /home/jeff-wang/LSM-tuning/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(leveldb_tests "/home/jeff-wang/LSM-tuning/build/leveldb_tests")
set_tests_properties(leveldb_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/LSM-tuning/comparedDBs/leveldb/CMakeLists.txt;367;add_test;/home/jeff-wang/LSM-tuning/comparedDBs/leveldb/CMakeLists.txt;0;")
add_test(c_test "/home/jeff-wang/LSM-tuning/build/c_test")
set_tests_properties(c_test PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/LSM-tuning/comparedDBs/leveldb/CMakeLists.txt;393;add_test;/home/jeff-wang/LSM-tuning/comparedDBs/leveldb/CMakeLists.txt;396;leveldb_test;/home/jeff-wang/LSM-tuning/comparedDBs/leveldb/CMakeLists.txt;0;")
add_test(env_posix_test "/home/jeff-wang/LSM-tuning/build/env_posix_test")
set_tests_properties(env_posix_test PROPERTIES  _BACKTRACE_TRIPLES "/home/jeff-wang/LSM-tuning/comparedDBs/leveldb/CMakeLists.txt;393;add_test;/home/jeff-wang/LSM-tuning/comparedDBs/leveldb/CMakeLists.txt;404;leveldb_test;/home/jeff-wang/LSM-tuning/comparedDBs/leveldb/CMakeLists.txt;0;")
subdirs("third_party/googletest")
subdirs("third_party/benchmark")
