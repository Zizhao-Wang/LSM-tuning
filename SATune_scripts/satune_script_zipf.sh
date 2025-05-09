
echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000
percentages=(1 5 10 15 20 25 30) # 定义百分比值
range_dividers=(1)
DEVICE_NAME="sdd"
F=10
level1base=5000

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
    dir1="${i}B_SATune_tuning_experiments"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
            num_entries=1000000000
            stats_interva=$((num_entries / 100))

            num_format=$(convert_to_billion_format $num_entries)

            for zipf_a in 1.1 1.2 1.3 1.4 1.5; do  #  1.2 
                for buffer_size in 67108864; do

                    buffer_size_mb=$((buffer_size / 1048576))
                            
                    log_file="SAtune_${num_format}_val_${value_size}_mem${buffer_size_mb}MB_zipf${zipf_a}_factor${F}_level1base${level1base}MiB.log"
                    data_file="/mnt/workloads/zipf${zipf_a}_keys10.0B.csv" # 构建数据文件路径
                    memory_log_file="$(pwd)/Satune_zipf${zipf_a}_f${F}_memory_usage_${num_format}_key16_val${value_size}_mem${buffer_size_mb}MiB_factor${F}_level1base${level1base}MiB.log"  

                    # 如果日志文件存在，则跳过当前迭代
                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping this iteration."
                        continue
                    fi

                    echo "base_num: $base_num"
                    echo "num_entries: $num_entries"
                    echo "value_size:$value_size"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"

                    # 创建相应的目录
                    db_dir="/mnt/db_test/SAtune10B/leveldb_f${F}_${zipf_a}_mem${buffer_size_mb}MiB_level1base_${level1base}"
                    if [ ! -d "$db_dir" ]; then
                        mkdir -p "$db_dir"
                    fi

                    # 检查目录是否为空，如果不为空则删除所有内容
                    if [ "$(ls -A $db_dir)" ]; then
                        rm -rf "${db_dir:?}/"*
                    fi

                    echo fb0-=0-= | sudo -S bash -c 'echo 1 > /proc/sys/vm/drop_caches'

                    iostat -d 100 -x $DEVICE_NAME > leveldbf${F}_${num_format}_val_${value_size}_mem${buffer_size_mb}MB_zipf${zipf_a}_IOstats_factor${F}_level1base${level1base}MiB.log &
                    PID_IOSTAT=$!
                        
                    cgexec -g memory:group16 ../../SATune/release/db_bench \
                        --db=$db_dir \
                        --num=$num_entries \
                        --value_size=$value_size \
                        --batch_size=1000 \
                        --benchmarks=fillzipf,stats \
                        --data_file=$data_file  \
                        --logpath=/mnt/logs \
                        --bloom_bits=10 \
                        --log=1  \
                        --cache_size=8388608 \
                        --mem_log_file=$memory_log_file \
                        --open_files=40000 \
                        --compression=0 \
                        --stats_interval=$stats_interva \
                        --histogram=1 \
                        --write_buffer_size=$buffer_size \
                        --max_file_size=$buffer_size   \
                        --print_wa=true \
                        &> >( tee $log_file) &  

                        # 保存 db_bench 的 PID 供监控使用
                        sleep 1

                        DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
                        echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

                        perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_stat_${num_format}_val_${value_size}_mem${buffer_size_mb}MB_zipf${zipf_a}_factor${F}_level1base${level1base}MiB.txt" &
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


