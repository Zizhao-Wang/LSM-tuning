#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "RocksDB::rocksdb" for configuration "RelWithDebInfo"
set_property(TARGET RocksDB::rocksdb APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(RocksDB::rocksdb PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/x86_64-linux-gnu/librocksdb.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS RocksDB::rocksdb )
list(APPEND _IMPORT_CHECK_FILES_FOR_RocksDB::rocksdb "${_IMPORT_PREFIX}/lib/x86_64-linux-gnu/librocksdb.a" )

# Import target "RocksDB::rocksdb-shared" for configuration "RelWithDebInfo"
set_property(TARGET RocksDB::rocksdb-shared APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(RocksDB::rocksdb-shared PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELWITHDEBINFO "gflags::gflags_shared"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/x86_64-linux-gnu/librocksdb.so.8.9.0"
  IMPORTED_SONAME_RELWITHDEBINFO "librocksdb.so.8"
  )

list(APPEND _IMPORT_CHECK_TARGETS RocksDB::rocksdb-shared )
list(APPEND _IMPORT_CHECK_FILES_FOR_RocksDB::rocksdb-shared "${_IMPORT_PREFIX}/lib/x86_64-linux-gnu/librocksdb.so.8.9.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
