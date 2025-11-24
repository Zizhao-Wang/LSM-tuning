# Check if the rocksdb_autumn/release directory exists
if [ -d "../rocksdb_autumn/release" ]; then
    # If it exists, remove the directory
    rm -rf ../rocksdb_autumn/release
fi
# Create the rocksdb_autumn/release directory
mkdir ../rocksdb_autumn/release
# Change directory to rocksdb_autumn/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../rocksdb_autumn/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j30 && cd ../../