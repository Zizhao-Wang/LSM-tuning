# Check if the KV_stores/SATune/release directory exists
if [ -d "../SATune/release" ]; then
    # If it exists, remove the directory
    rm -rf ../SATune/release
fi
# Create the KV_stores/SATune/release directory
mkdir ../SATune/release
# Change directory to KV_stores/SATune/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../SATune/release && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j30 && cd ../../