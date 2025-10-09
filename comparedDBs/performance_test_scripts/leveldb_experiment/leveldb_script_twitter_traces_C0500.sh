
echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
sudo bash -c 'ulimit -n 800000'

BASE_VALUE_SIZE=128
billion=1000000000
level1base=512

cluster_key_size_map[1]=80
cluster_key_size_map[13]=44
cluster_key_size_map[25]=49
cluster_key_size_map[30]=22
cluster_key_size_map[35]=19
cluster_key_size_map[51]=44	
cluster_key_size_map[40]=44	

cluster_value_size_map[1]=267
cluster_value_size_map[13]=4266
cluster_value_size_map[25]=28
cluster_value_size_map[30]=689
cluster_value_size_map[35]=1000
cluster_value_size_map[51]=221
cluster_value_size_map[40]=155

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

for i in {1..1}; do
    dir1="${i}B_leveldb_tuning_structure(f_c0)_experiments_C0${level1base}wa_throughput_C04_20"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1
        for value_size in 128; do
            num_entries=1000000000
            stats_interva=$((num_entries / 100))
            num_format=$(convert_to_billion_format $num_entries)

            for cluster_a in 35 ; do  #  1.2 
                for buffer_size in 67108864; do
                    buffer_size_mb=$((buffer_size / 1048576))

                    for F in 10; do

                        log_file="leveldb_${num_format}_val_${value_size}_mem${buffer_size_mb}MB_cluster${cluster_a}_factor${F}_L1${level1base}MiB.log"
                        data_file="/mnt/nvm/second_cluster${cluster_a}.sort" # 构建数据文件路径
                        memory_log_file="$(pwd)/leveldb_cluster${cluster_a}_f${F}_memory_usage_${num_format}_key16_val${value_size}_mem${buffer_size_mb}MiB_factor${F}_L1${level1base}MiB.log"  

                        # 如果日志文件存在，则跳过当前迭代
                        if [ -f "$log_file" ]; then
                            echo "Log file $log_file already exists. Skipping this iteration."
                            continue
                        fi

                        echo "num_entries: $num_entries"
                        echo "value_size:$value_size"
                        echo "stats_interval: $stats_interva"
                        echo "$num_format"

                        value_size_twitter=${cluster_value_size_map[$cluster_a]}
                        key_size_twitter=${cluster_key_size_map[$cluster_a]}

                        # 创建相应的目录
                        db_dir="/mntdisk/level10B/level10/C0_${level1base}/leveldb_f${F}_cluster${cluster_a}_mem${buffer_size_mb}MiB_level1base_${level1base}"
                        if [ ! -d "$db_dir" ]; then
                            mkdir -p "$db_dir"
                        fi

                        # 检查目录是否为空，如果不为空则删除所有内容
                        if [ "$(ls -A $db_dir)" ]; then
                            rm -rf "${db_dir:?}/"*
                        fi

                        echo fb0-=0-= | sudo -S bash -c 'echo 1 > /proc/sys/vm/drop_caches'
                            
                        cgexec -g memory:group16 ../../../leveldb/build_release_f_10_C0_${level1base}/db_bench \
                            --db=$db_dir \
                            --num=$num_entries \
                            --key_size=$key_size_twitter \
                            --value_size=$value_size_twitter \
                            --batch_size=1000 \
                            --benchmarks=fillcluster,stats \
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

                            perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_stat_${num_format}_val_${value_size}_mem${buffer_size_mb}MB_cluster${cluster_a}_factor${F}_L1${level1base}MiB.txt" &
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


