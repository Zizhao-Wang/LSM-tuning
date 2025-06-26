echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000

declare -A slowdown_map
declare -A stop_map
declare -A cluster_num_kvs_map

cluster_num_kvs_map[13]="20000000 40000000 60000000 80000000 100000000"
cluster_num_kvs_map[1]="200000000 600000000 1000000000 2000000000 3000000000"
cluster_num_kvs_map[25]="200000000 600000000 1000000000 2000000000 3000000000"
cluster_num_kvs_map[30]="200000000 600000000 1000000000 2000000000 3000000000"
cluster_num_kvs_map[35]="200000000 600000000 1000000000 2000000000 3000000000"
cluster_num_kvs_map[51]="200000000 600000000 1000000000 2000000000 3000000000"
cluster_num_kvs_map[40]="2000000000 "
cluster_num_kvs_map[49]="20000000"

# 为不同的ct0值设定对应的slowdown和stop值
slowdown_map[4]=8
stop_map[4]=12

slowdown_map[8]=16
stop_map[8]=32

slowdown_map[16]=20
stop_map[16]=32

slowdown_map[32]=40
stop_map[32]=74

slowdown_map[64]=80
stop_map[64]=144

slowdown_map[400000]=800000
stop_map[400000]=800000

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
cluster_value_size_map[35]=1796
cluster_value_size_map[51]=221
cluster_value_size_map[40]=155
cluster_value_size_map[49]=10658


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

for i in 40 ; do
    dir1="SATune_SATASSD_TwitterCluster${i}_LoadPerformance"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1

            for cluster_a in "$i"; do  # 
                for ct0 in 4 ; do  # 
                for mb in 100 200 ; do
                for multiplier in 2; do
                for after_multiplier in 30 50; do
                    echo "================================================="
                    echo "Running benchmark with multiplier = $multiplier and after_multiplier= $after_multiplier"
                    echo "================================================="
                for buffer_size in 67108864; do
                for num_kvs in ${cluster_num_kvs_map[$cluster_a]}; do
                    num_format=$(convert_to_billion_format "$num_kvs")
                    echo "原始值: $num_kvs, 转换后: $num_format"
                    stats_interva=$((num_kvs / 10))
                for blk_size in 4; do
                for blk_cache_size in 128; do
                for table_cache_size in 1000 ; do
                    # buffer_size=67108864
                    # buffer_size=2097152
                    target_file_base=67108864
                    level1_max_bytes=$(($mb * 1048576))
                    buffer_size_mb=$((buffer_size / 1048576))
                    target_file_base_mb=$((target_file_base / 1048576))
                    block_size_write=$(($blk_size * 1024))

                    value_size_twitter=${cluster_value_size_map[$cluster_a]}
                    key_size_twitter=${cluster_key_size_map[$cluster_a]}

                    log_file="SATune_PreLoad_${num_format}_val${value_size_twitter}_mem${buffer_size_mb}MB_Cluster${cluster_a}_CT0${ct0}_level1base${mb}_targetbase${target_file_base_mb}_Block${blk_size}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_F${multiplier}_2F${after_multiplier}.log"
                    data_file="/mnt/nvm/second_cluster${cluster_a}.sort" # 构建数据文件路径
                    memory_log_file="$(pwd)/SATune_PreLoad_${num_format}_key${key_size_twitter}_val${value_size_twitter}_Cluster${cluster_a}_mem${buffer_size_mb}MiB_CT0${ct0}_Block${blk_size}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_F${multiplier}_2F${after_multiplier}.log"
                    compaction_log_file="$(pwd)/Compaction_${num_format}_key${key_size_twitter}_val${value_size_twitter}_Cluster${cluster_a}_mem${buffer_size_mb}MiB_CT0${ct0}_Block${blk_size}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_F${multiplier}_2F${after_multiplier}.log"  
                    
                    if [ -f "$log_file" ]; then
                        echo "Log file $log_file already exists. Skipping this iteration."
                        continue
                    fi     

                    # 创建相应的目录
                    db_dir="/mnt/db_test2/SATune10B/PreLoad_Cluster${cluster_a}_${num_format}_mem${buffer_size_mb}MB_CT${ct0}_L1base${mb}_targetbase${target_file_base_mb}_Block${blk_size}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_F${multiplier}_2F${after_multiplier}"
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
                    echo "value_size:$value_size_twitter"
                    echo "key_size:$key_size_twitter"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"
                            
                    ../../SATune/release/db_bench \
                        --db=$db_dir \
                        --max_bytes_for_level1_base=$level1_max_bytes \
                        --num=$num_kvs \
                        --max_bytes_for_level1_multiplier=$multiplier \
                        --multiplier_switch_level=3 \
                        --max_bytes_for_level_multiplier_after_switch=$after_multiplier \
                        --block_size=$block_size_write  \
                        --key_size=$key_size_twitter \
                        --value_size=$value_size_twitter \
                        --batch_size=1 \
                        --benchmarks=fillcluster,stats \
                        --data_file=$data_file  \
                        --bloom_bits=10 \
                        --cache_size=8388608 \
                        --level_compaction_log=$compaction_log_file \
                        --level0_compaction_trigger=$ct0 \
                        --level0_slowdown_writes_trigger=$slowdown_value \
                        --level0_stop_writes_trigger=$stop_value \
                        --use_direct_reads_for_recovery=false \
                        --use_direct_random_access=false \
                        --use_direct_writeappend_file=false \
                        --open_files=80000 \
                        --compression_ratio=0 \
                        --compression=0 \
                        --stats_interval=$stats_interva \
                        --histogram=1 \
                        --print_wa=true \
                        --use_existing_db=false \
                        --write_buffer_size=$buffer_size \
                        --mem_log_file=$memory_log_file \
                        --file_size_generated_in_compaction=$target_file_base \
                        &> >( tee $log_file) &  

                        #保存 db_bench 的 PID 供监控使用
                        sleep 1

                        DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
                        echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

                        perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_PreLoad_stat_${num_format}_val_${value_size_twitter}_Cluster${cluster_a}_mem${MEM}MiB_CT0${ct0}_Block${blk_size}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_F${multiplier}_2F${after_multiplier}.txt" &
                        PERF_PID=$!

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
                    done
                    done
        done
    cd ..
done