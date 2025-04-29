echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000

DEVICE_NAME="sda"


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
    dir1="${i}B_RocksDB_Write_Memtable_Performance"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            stats_interva=$((num_entries / 100))
            num_format=$(convert_to_billion_format $num_entries)
            num_entries=1000000000

            for zipf_a in 1.1 1.2 1.3 1.4 1.5; do  # 
                for ct0 in 8 16 32 64; do  # 
                for mb in 100 300 512; do
                for buffer_size in 2097152 8388608 33554432; do
                    # buffer_size=67108864
                    # buffer_size=2097152
                    target_file_base=67108864
                    level1_max_bytes=$(($mb * 1048576))
                    buffer_size_mb=$((buffer_size / 1048576))
                    target_file_base_mb=$((target_file_base / 1048576))

                    log_file="RocksDB_${num_format}_val${value_size}_zipf${zipf_a}_mem${buffer_size_mb}MB_CT0${ct0}_level1base${mb}_targetbase${target_file_base_mb}.log"
                    data_file="/mnt/workloads/zipf${zipf_a}_keys10.0B.csv" # 构建数据文件路径 
                    data_file="/mnt/nvm/zipf${zipf_a}_keys10.0B.csv" # 构建数据文件路径
                    memory_log_file="$(pwd)/RocksDB_${num_format}_key16_val${value_size}_zipf${zipf_a}_mem${buffer_size_mb}MiB_CT0${ct0}_L1base${mb}_targetbase${target_file_base_mb}.log"      

                    # 如果日志文件存在，则跳过当前迭代
                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping this iteration."
                        continue
                    fi

                    # 创建相应的目录
                    db_dir="/mnt/workloads/rocks10B/mem${buffer_size_mb}MB_zipf${zipf_a}_CT${ct0}_L1base${mb}_targetbase${target_file_base_mb}"
                    if [ ! -d "$db_dir" ]; then
                        mkdir -p "$db_dir"
                    fi

                    # 检查目录是否为空，如果不为空则删除所有内容
                    if [ "$(ls -A $db_dir)" ]; then
                        rm -rf "${db_dir:?}/"*
                    fi



                    echo "base_num: $base_num"
                    echo "num_entries: $num_entries"
                    echo "value_size:$value_size"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"

                    iostat -d 100 -x $DEVICE_NAME > RocksDBIOstats_${num_format}_val_${value_size}_zipf${zipf_a}_mem${buffer_size_mb}MiB_CT0${ct0}_L1base${mb}_targetbase${target_file_base_mb}.log &
                    PID_IOSTAT=$!
                            
                        ../../../rocksdb/release/db_bench \
                        --db=$db_dir \
                        --max_bytes_for_level_base=$level1_max_bytes \
                        --num=$num_entries \
                        --value_size_=$value_size \
                        --batch_size=1 \
                        --benchmarks=fillzipf,stats \
                        --data_file_path=$data_file  \
                        --bloom_bits=10 \
                        --cache_size=8388608 \
                        --use_direct_io_for_flush_and_compaction=true \
                        --level0_file_num_compaction_trigger=$ct0 \
                        --level0_slowdown_writes_trigger=80 \
                        --level0_stop_writes_trigger=144 \
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

                        perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_stat_${num_format}_val_${value_size}_zipf${zipf_a}_mem${MEM}MiB_CT0${ct0}_L1base${mb}_targetbase${target_file_base_mb}.txt" &
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




