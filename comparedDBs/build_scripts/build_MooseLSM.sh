# Check if the MooseLSM/release directory exists
if [ -d "../MooseLSM/release" ]; then
    # If it exists, remove the directory
    rm -rf ../MooseLSM/release
fi
# Create the MooseLSM/release directory
mkdir ../MooseLSM/release
# Change directory to MooseLSM/release, configure the build for release, make the build with 32 jobs, and then return to the original directory
cd ../MooseLSM/release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j30 && cd ../../