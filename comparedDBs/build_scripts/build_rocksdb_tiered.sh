# Check if the rocksdb_tiered/release directory exists
if [ -d "../rocksdb_tiered/release" ]; then
    # If it exists, remove the directory
    rm -rf ../rocksdb_tiered/release
fi
# Create the rocksdb_tiered/release directory
mkdir ../rocksdb_tiered/release
# Change directory to rocksdb_tiered/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../rocksdb_tiered/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j30 && cd ../../