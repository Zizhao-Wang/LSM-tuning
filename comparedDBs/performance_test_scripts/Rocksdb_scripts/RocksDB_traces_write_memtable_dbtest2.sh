echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000

DEVICE_NAME="nvme1n1"

declare -A slowdown_map
declare -A stop_map

# 为不同的ct0值设定对应的slowdown和stop值
slowdown_map[4]=16
slowdown_map[8]=16
slowdown_map[16]=20
slowdown_map[32]=40
slowdown_map[64]=80

stop_map[4]=32
stop_map[8]=32
stop_map[16]=32
stop_map[32]=74
stop_map[64]=144

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

cluster_value_size_map[1]=267
cluster_value_size_map[13]=4266
cluster_value_size_map[25]=28
cluster_value_size_map[30]=689
cluster_value_size_map[35]=1796
cluster_value_size_map[51]=221


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

for i in {10..10}; do
    base_num=$(($billion * $i))
    dir1="${i}B_RocksDB_Twitter_Write_Performance"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            stats_interva=$((num_entries / 100))
            num_format=$(convert_to_billion_format $num_entries)
            num_entries=1000000000

            for cluster_a in 1 13 25 30 35 51; do  # 
                for ct0 in 4 ; do  # 
                for mb in 512; do
                for buffer_size in 2097152 8388608 33554432 134217728 268435456; do
                    # buffer_size=67108864
                    # buffer_size=2097152
                    target_file_base=67108864
                    level1_max_bytes=$(($mb * 1048576))
                    buffer_size_mb=$((buffer_size / 1048576))
                    target_file_base_mb=$((target_file_base / 1048576))

                    value_size_twitter=${cluster_value_size_map[$cluster_a]}
                    key_size_twitter=${cluster_key_size_map[$cluster_a]}

                    log_file="RocksDB_${num_format}_val${value_size_twitter}_mem${buffer_size_mb}MB_Cluster${cluster_a}_CT0${ct0}_level1base${mb}_targetbase${target_file_base_mb}.log"
                    data_file="/mnt/nvm/second_cluster${cluster_a}.sort" # 构建数据文件路径
                    memory_log_file="$(pwd)/RocksDB_${num_format}_key${key_size_twitter}_val${value_size_twitter}_Cluster${cluster_a}_mem${buffer_size_mb}MiB_CT0${ct0}.log"      

                    # 如果日志文件存在，则跳过当前迭代
                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping this iteration."
                        continue
                    fi

                    # 创建相应的目录
                    db_dir="/mnt/db_test2/rocks10B/mem${buffer_size_mb}MB_Cluster${cluster_a}_CT${ct0}_L1base${mb}_targetbase${target_file_base_mb}"
                    if [ ! -d "$db_dir" ]; then
                        mkdir -p "$db_dir"
                    fi

                    # 检查目录是否为空，如果不为空则删除所有内容
                    if [ "$(ls -A $db_dir)" ]; then
                        rm -rf "${db_dir:?}/"*
                    fi

                    # 获取对应ct0的slowdown和stop值
                    slowdown_value=${slowdown_map[$ct0]}
                    stop_value=${stop_map[$ct0]}
                    # 输出slowdown_value和stop_value的匹配情况
                    echo "For ct0=$ct0, slowdown_value=$slowdown_value, stop_value=$stop_value"

                    echo "base_num: $base_num"
                    echo "num_entries: $num_entries"
                    echo "value_size:$value_size_twitter"
                    echo "key_size:$key_size_twitter"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"

                    iostat -d 100 -x $DEVICE_NAME > RocksDB_${num_format}_val_${value_size_twitter}_Cluster${cluster_a}_mem${buffer_size_mb}MiB_CT0${ct0}_IOstats.log &
                    PID_IOSTAT=$!
                            
                        ../../../rocksdb/release/db_bench \
                        --db=$db_dir \
                        --max_bytes_for_level_base=$level1_max_bytes \
                        --num=$num_entries \
                        --key_size=$key_size_twitter \
                        --value_size_=$value_size_twitter \
                        --batch_size=1 \
                        --benchmarks=fillcluster,stats \
                        --data_file_path=$data_file  \
                        --bloom_bits=10 \
                        --cache_size=8388608 \
                        --use_direct_io_for_flush_and_compaction=true \
                        --level0_file_num_compaction_trigger=$ct0 \
                        --level0_slowdown_writes_trigger=$slowdown_value \
                        --level0_stop_writes_trigger=$stop_value \
                        --max_background_compactions=8 \
                        --max_background_flushes=1 \
                        --open_files=80000 \
                        --compression_ratio=0 \
                        --stats_interval=$stats_interva \
                        --stats_per_interval=$stats_interva \
                        --histogram=1 \
                        --write_buffer_size=$buffer_size \
                        --mem_log_file=$memory_log_file \
                        --target_file_size_base=$target_file_base   \
                        --compression_type=none \
                        &> >( tee $log_file) &  

                        # 保存 db_bench 的 PID 供监控使用
                        sleep 1

                        DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
                        echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

                        perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_stat_${num_format}_val_${value_size_twitter}_Cluster${cluster_a}_mem${MEM}MiB_CT0${ct0}.txt" &
                        PERF_PID=$!

                        wait $DB_BENCH_PID

                        # 结束 iostat 进程
                        echo "Checking if iostat process with PID $PID_IOSTAT is still running..."
                        ps -p $PID_IOSTAT
                        if [ $? -eq 0 ]; then
                            echo "iostat process $PID_IOSTAT is still running, killing now..."
                            kill -9 $PID_IOSTAT
                            if [ $? -eq 0 ]; then
                                echo "iostat process $PID_IOSTAT has been successfully killed."
                            else
                                echo "Failed to kill iostat process $PID_IOSTAT."
                            fi
                        else
                            echo "iostat process $PID_IOSTAT is no longer running."
                        fi
                    done
                    done
                    done
                done
        done
done




