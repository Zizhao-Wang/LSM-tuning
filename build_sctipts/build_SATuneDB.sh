# Check if the KV_stores/SATunedb/release directory exists
if [ -d "../SATunedb/release" ]; then
    # If it exists, remove the directory
    rm -rf ../SATunedb/release
fi
# Create the KV_stores/SATunedb/release directory
mkdir ../SATunedb/release
# Change directory to KV_stores/SATunedb/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../SATunedb/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j30 && cd ../../