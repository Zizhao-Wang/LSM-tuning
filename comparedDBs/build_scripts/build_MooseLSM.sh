# Check if the MooseLSM/build directory exists
if [ -d "../MooseLSM/build" ]; then
    # If it exists, remove the directory
    rm -rf ../MooseLSM/build
fi
# Create the MooseLSM/build directory
mkdir ../MooseLSM/build
# Change directory to MooseLSM/build, configure the build for build, make the build with 32 jobs, and then return to the original directory
cd ../MooseLSM/build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j30 && cd ../../