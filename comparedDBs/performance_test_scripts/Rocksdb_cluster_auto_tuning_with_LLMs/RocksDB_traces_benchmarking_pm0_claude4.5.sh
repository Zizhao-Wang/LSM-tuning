echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000

declare -A slowdown_map
declare -A stop_map


slowdown_map[1]=3
slowdown_map[2]=5
slowdown_map[4]=8
slowdown_map[8]=16
slowdown_map[12]=20
slowdown_map[16]=20
slowdown_map[32]=40
slowdown_map[64]=80
slowdown_map[400000]=800000

stop_map[1]=100
stop_map[2]=100
stop_map[4]=20
stop_map[8]=32
stop_map[12]=32
stop_map[16]=32
stop_map[32]=74
stop_map[64]=144
stop_map[400000]=800000

# Define the mapping for value_size and key_size for each cluster
declare -A cluster_value_size_map
declare -A cluster_key_size_map

# Define the cluster-specific value sizes and key sizes
cluster_key_size_map[1]=24
cluster_key_size_map[12]=44
cluster_key_size_map[13]=44
cluster_key_size_map[25]=49
cluster_key_size_map[30]=22
cluster_key_size_map[35]=24
cluster_key_size_map[51]=24
cluster_key_size_map[40]=24
cluster_key_size_map[48]=44	
cluster_key_size_map[49]=24	
cluster_key_size_map[51]=24	

cluster_value_size_map[1]=1000
cluster_value_size_map[12]=1030
cluster_value_size_map[13]=4266
cluster_value_size_map[25]=28
cluster_value_size_map[30]=1000
cluster_value_size_map[35]=1000
cluster_value_size_map[51]=221
cluster_value_size_map[40]=1000
cluster_value_size_map[48]=70
cluster_value_size_map[49]=1000
cluster_value_size_map[51]=1000

declare -A table_cache_size

cluster_table_size_map[35]=0
cluster_table_size_map[40]=100
cluster_table_size_map[49]=8
cluster_table_size_map[51]=3
cluster_table_size_map[30]=8
cluster_table_size_map[1]=0
cluster_table_size_map[48]=300
cluster_table_size_map[12]=200
cluster_table_size_map[13]=500

declare -A cluster_block_cache_size_map

cluster_block_cache_size_map[35]=8
cluster_block_cache_size_map[40]=8
cluster_block_cache_size_map[49]=8
cluster_block_cache_size_map[51]=3
cluster_block_cache_size_map[30]=8
cluster_block_cache_size_map[1]=3
cluster_block_cache_size_map[12]=8
cluster_block_cache_size_map[13]=8
cluster_block_cache_size_map[48]=8

convert_to_billion_format() {
    local num=$1
    local integer_part=$((num / billion))
    local decimal_part=$((num % billion))

    if ((decimal_part == 0)); then
        echo "${integer_part}B"
    else
        # Convert decimal part to the format of "x billionths"
        local formatted_decimal=$(echo "scale=9; $decimal_part / $billion" | bc | sed 's/0*$//' | sed 's/\.$//')
        # Extract only the fractional part after the decimal point
        formatted_decimal=$(echo $formatted_decimal | cut -d'.' -f2)
        echo "${integer_part}.${formatted_decimal}B"
    fi
}

for i in 35; do
    dir1="RocksDB_PM0_MultiTwitterClusters_Benchmarking_Claude4.5"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
            for cluster_a in "$i"; do  #  
                for workload_kvs in 100000000 ; do 
                    num_format2=$(convert_to_billion_format "$workload_kvs")
                    stats_interva=$((workload_kvs / 10))
                    echo "原始值: $workload_kvs, 转换后: $num_format2"
                    for blk_cache_size in ${cluster_block_cache_size_map[$cluster_a]} ; do
                    for table_cache_size in ${cluster_table_size_map[$cluster_a]}; do


                        block_cache_size=$(($blk_cache_size * 1048576))
                        value_size_twitter=${cluster_value_size_map[$cluster_a]}
                        key_size_twitter=${cluster_key_size_map[$cluster_a]}

                        log_file="RocksDB_${num_format2}_val${value_size_twitter}_mem${buffer_size_mb}MB_Cluster${cluster_a}_L1${mb}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_iteration8.log"
                        # data_file="/mnt/nvm/second_cluster${cluster_a}.sort" # 构建数据文件路径
                        data_file="/mnt/workloads/second_cluster${cluster_a}.sort" # 构建数据文件路径
                        memory_log_file="$(pwd)/RocksDB_Memory_${num_format2}_key${key_size_twitter}_val${value_size_twitter}_Cluster${cluster_a}_mem${buffer_size_mb}MiB_L1${mb}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_iteration8.log"      

                        # 如果日志文件存在，则跳过当前迭代
                        if [ -f "$log_file" ]; then
                            echo "Log file $log_file already exists. Skipping this iteration."
                            continue
                        fi

                        # 创建相应的目录
                        db_dir="/mnt/pmem0/rocks10B/PreLoad_Cluster${cluster_a}_mem${buffer_size_mb}MB_L1base${mb}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}"
                        if [ ! -d "$db_dir" ]; then
                            mkdir -p "$db_dir"
                        fi

                        # 检查目录是否为空，如果不为空则删除所有内容
                        if [ "$(ls -A $db_dir)" ]; then
                            rm -rf "${db_dir:?}/"*
                        fi
                        

                        echo "num_entries: $num_entries"
                        echo "value_size:$value_size_twitter"
                        echo "key_size:$key_size_twitter"
                        echo "stats_interval: $stats_interva"
                        echo "$num_format2"

                                
                        ../../../rocksdb/release/db_bench \
                            --db=$db_dir \
                            --workload_num=$workload_kvs \
                            --key_size=$key_size_twitter \
                            --value_size=$value_size_twitter \
                            --value_size_=$value_size_twitter \
                            --data_file_path=$data_file  \
                            --cache_size=$block_cache_size \
                            --stats_interval=$stats_interva \
                            --stats_per_interval=$stats_interva \
                            --open_files=$table_cache_size \
                            --mem_log_file=$memory_log_file \
                            --use_direct_reads=true \
                            --batch_size=1 \
                            --benchmarks=clusterQuery,stats \
                            --bloom_bits=15 \
                            --max_bytes_for_level_base=536870912  \
                            --use_direct_io_for_flush_and_compaction=true \
                            --level0_file_num_compaction_trigger=2 \
                            --level0_slowdown_writes_trigger=16 \
                            --level0_stop_writes_trigger=24 \
                            --compression_ratio=0 \
                            --histogram=1 \
                            --use_existing_db=false \
                            --write_buffer_size=67108864 \
                            --compression_type=none \
                            --compaction_readahead_size=16777216  \
                            --delayed_write_rate=33554432  \
                            --writable_file_max_buffer_size=2097152   \
                            --subcompactions=1 \
                            --max_background_jobs=2 \
                            --max_background_compactions=1 \
                            --max_background_flushes=1 \
                            --write_thread_max_yield_usec=100 \
                            --write_thread_slow_yield_usec=3 \
                            --db_write_buffer_size=0 \
                            --table_cache_numshardbits=4 \
                            --file_opening_threads=16 \
                            --unordered_write=false \
                            --advise_random_on_open=true \
                            --target_file_size_base=536870912 \
                            --max_bytes_for_level_multiplier=8.000000 \
                            --arena_block_size=4194304  \
                            --target_file_size_multiplier=1 \
                            --max_write_buffer_number=2 \
                            --min_write_buffer_number_to_merge=1 \
                            --metadata_block_size=8192 \
                            --block_size=4096   \
                            --pin_l0_filter_and_index_blocks_in_cache=false \
                            --cache_index_and_filter_blocks=true \
                            &> >( tee $log_file) &  

                            # 保存 db_bench 的 PID 供监控使用
                            sleep 1

                            DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
                            echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

                            perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_PreLoad_stat_${num_format2}_val_${value_size_twitter}_Cluster${cluster_a}_mem${MEM}MiB_L1${mb}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_iteration2.txt" &
                            PERF_PID=$!

                            wait $DB_BENCH_PID

                            # echo "Checking if perf stat process with PID $PERF_PID is still running..."
                            ps -p $PERF_PID > /dev/null # 重定向标准输出到 /dev/null，只检查退出状态
                            if [ $? -eq 0 ]; then
                                echo "perf stat process $PERF_PID is still running, killing now..."
                                kill -INT $PERF_PID # 尝试发送 SIGINT 信号，让 perf stat 优雅结束
                                sleep 1 # 给 perf stat 1秒时间来处理信号和输出报告
                                ps -p $PERF_PID > /dev/null
                                if [ $? -eq 0 ]; then
                                    echo "perf stat process $PERF_PID did not terminate, forcing kill..."
                                    kill -9 $PERF_PID # 如果仍未结束，强制杀死
                                    if [ $? -eq 0 ]; then
                                        echo "perf stat process $PERF_PID has been successfully killed."
                                    else
                                        echo "Failed to force kill perf stat process $PERF_PID."
                                    fi
                                else
                                    echo "perf stat process $PERF_PID has been successfully terminated."
                                fi
                            else
                                echo "perf stat process $PERF_PID is no longer running (it might have ended with db_bench)."
                            fi
                done
                done
            done
        done
    cd ..
    done
