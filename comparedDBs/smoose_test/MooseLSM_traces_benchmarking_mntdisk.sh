echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000

DEVICE_NAME="nvme1n1"

# Define the mapping for value_size and key_size for each cluster
declare -A cluster_value_size_map
declare -A cluster_key_size_map

# Define the cluster-specific value sizes and key sizes
cluster_key_size_map[1]=80
cluster_key_size_map[13]=44
cluster_key_size_map[25]=49
cluster_key_size_map[30]=22
cluster_key_size_map[35]=19
cluster_key_size_map[51]=44	
cluster_key_size_map[40]=44
cluster_key_size_map[49]=44	

cluster_value_size_map[1]=267
cluster_value_size_map[13]=4266
cluster_value_size_map[25]=28
cluster_value_size_map[30]=689
cluster_value_size_map[35]=1000
cluster_value_size_map[51]=221
cluster_value_size_map[40]=155
cluster_value_size_map[49]=1024

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

READ_RATIOS=( 0.5 )

for i in 40 ; do
    dir1="10B_RocksDB_SATASSD_TwitterCluster${i}_Benchmarking_Performance"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
    cd $dir1
    
        for cluster_a in "$i"; do  # 
        for buffer_size in 67108864; do
        for num_kvs in 200000000 ; do
            stats_interva=$((num_kvs / 5)) 
            num_format=$(convert_to_billion_format "$num_kvs")
            echo "原始值: $num_kvs, 转换后: $num_format"
        for blk_cache_size in 128 ; do
        for table_cache_size in 1000 ; do
        for read_ratio in "${READ_RATIOS[@]}"; do
                    write_ratio=$(echo "1.0 - $read_ratio" | bc)
                    target_file_base=67108864

                    buffer_size_mb=$((buffer_size / 1048576))
                    target_file_base_mb=$((target_file_base / 1048576))

                    value_size_twitter=${cluster_value_size_map[$cluster_a]}
                    key_size_twitter=${cluster_key_size_map[$cluster_a]}

                    log_file="Smoose_PreLoad_${num_format}_val${value_size_twitter}_mem${buffer_size_mb}MB_Cluster${cluster_a}_L1${mb}_read_${read_ratio}_write_${write_ratio}.log"
                    data_file="/mnt/nvm/second_cluster${cluster_a}.sort" # 构建数据文件路径
                    memory_log_file="$(pwd)/Smoose_PreLoadMemory_${num_format}_key${key_size_twitter}_val${value_size_twitter}_Cluster${cluster_a}_mem${buffer_size_mb}MiB_Blkcache${blk_cache_size}_Tabcache${table_cache_size}.log"      

                    # 如果日志文件存在，则跳过当前迭代
                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping this iteration."
                        continue
                    fi

                    # 创建相应的目录
                    db_dir="/mntdisk/MooseLSM/PreLoad_Cluster${cluster_a}_${num_format}_mem${buffer_size_mb}MB_L1base${mb}_targetbase${target_file_base_mb}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_mixed_read_${read_ratio}_write_${write_ratio}"
                    if [ ! -d "$db_dir" ]; then
                        mkdir -p "$db_dir"
                    fi

                    # 检查目录是否为空，如果不为空则删除所有内容
                    if [ "$(ls -A $db_dir)" ]; then
                        rm -rf "${db_dir:?}/"*
                    fi

                    echo "value_size:$value_size_twitter"
                    echo "key_size:$key_size_twitter"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"
                            
                    ../build/smoose_tester \
                        --workload=mixed \
                        --db=$db_dir \
                        --data_file_path=$data_file \
                        --num=$num_kvs \
                        --key_size=$key_size_twitter \
                        --value_size=$value_size_twitter \
                        --read_ratio=$read_ratio \
                        --write_ratio=$write_ratio \
                        --verify_reads=false \
                        --sync=true \
                        --bits_per_key=10 \
                        --stats_interval_ops="$stats_interva" \
                        &> >(tee $log_file) &

                        # 保存 db_bench 的 PID 供监控使用
                        sleep 5

                        DB_BENCH_PID=$(pgrep -af "smoose_tester --workload=mixed" | grep -v 'sudo' | awk '{print $1}')
                        echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

                        # perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_PreLoad_stat_${num_format}_val_${value_size_twitter}_Cluster${cluster_a}_mem${MEM}MiB_Blkcache${blk_cache_size}_Tabcache${table_cache_size}.txt" &
                        # perf record -F 999 -p $DB_BENCH_PID -g -e cycles:u,instructions:u -o "perf_smoose.data" &
                        # PERF_PID=$!

                        wait $DB_BENCH_PID

                        echo "Checking if perf stat process with PID $PERF_PID is still running..."
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
                    done
                    done
            done
        cd ..
        done
    