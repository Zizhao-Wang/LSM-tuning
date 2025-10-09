echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000

declare -A slowdown_map
declare -A stop_map
declare -A cluster_num_kvs_map



#cluster 13
cluster_L1mb_map[2]="80 100 120 200 512" #100 is C0
cluster_L1mb_map[4]="100 140 160 200 512"
cluster_L1mb_map[8]="250 300 512"
cluster_L1mb_map[12]="300 400 512"
cluster_L1mb_map[16]="400 500 550 600"

#cluster 30/49
cluster_L1mb_map[2]="10 20 25 28 30 40  100 512" #30 is optimal
cluster_L1mb_map[4]="10 20 30 40 41 100 512" #41 is optimal
cluster_L1mb_map[8]="20 30 61 70 100 512" #61 is optimal
cluster_L1mb_map[12]="20 30 77 82 90 100 512" #77 is optimal
cluster_L1mb_map[16]="20 30 80 92 100 512" #92 is optimal

#cluster 1
cluster_L1mb_map[2]="1 2 4 8 100 512" #2 is optimal
cluster_L1mb_map[4]="2 3 5 10 100 512" #3 is optimal
cluster_L1mb_map[8]="2 3 5 10 100 512" #2.92 is optimal
cluster_L1mb_map[12]="2 3 5 10 100 512" #3.25 is optimal
cluster_L1mb_map[16]="2 3 5 10 100 512" #3.5 is optimal
cluster_L1mb_map[32]="3 4 5 10 100 512" #4 is optimal
cluster_L1mb_map[64]="4 5 10 100 512 " #5 is optimal

#cluster 51
cluster_L1mb_map[2]="5 10 14 20 50 100 512" #13.42 is optimal
cluster_L1mb_map[4]="10 15 18 30 50 100 512" #17.94 is optimal
cluster_L1mb_map[8]="15 20 25 30 50 100 512" # 24.67 is optimal
cluster_L1mb_map[12]="20 30 50 100 512" #29.99 is optimal
cluster_L1mb_map[16]="20 30 35 50 100 512" #34.51 is optimal
cluster_L1mb_map[32]="20 30 50 100 200 512" #48.55 is optimal
cluster_L1mb_map[64]="50 70 90 100 200 512 " #68.11 is optimal

#cluster 40
cluster_L1mb_map[16]="160 180 200 300 512"
cluster_L1mb_map[4]="500"

# 为不同的ct0值设定对应的slowdown和stop值
slowdown_map[1]=4
slowdown_map[2]=8
slowdown_map[4]=16
slowdown_map[8]=16
slowdown_map[12]=20
slowdown_map[16]=20
slowdown_map[32]=40
slowdown_map[64]=80
slowdown_map[400000]=800000

stop_map[1]=10
stop_map[2]=12
stop_map[4]=32
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
cluster_key_size_map[1]=80
cluster_key_size_map[13]=44
cluster_key_size_map[25]=49
cluster_key_size_map[30]=22
cluster_key_size_map[35]=19
cluster_key_size_map[51]=44
cluster_key_size_map[40]=44
cluster_key_size_map[49]=44	

cluster_value_size_map[1]=267
cluster_value_size_map[13]=266
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

for i in 40; do
    dir1="SATune_SATASSD_TwitterCluster${i}_Benchmarking_Performance"
    if [ ! -d "$dir1" ]; then
        mkdir $dir1
    fi
        cd $dir1

            for cluster_a in "$i"; do  # 
                for ct0 in 4; do  # 
                for mb in 100; do
                    echo "L1base is: $mb"
                for buffer_size in 67108864; do
                for num_kvs in 20000000; do
                    num_format=$(convert_to_billion_format "$num_kvs")
                    echo "原始值: $num_kvs, 转换后: $num_format"
                    stats_interva=$((num_kvs / 5))
                for multiplier_switch in 8; do
                for after_multiplier in 50; do
                    echo "================================================="
                    echo "Running benchmark with multiplier = $multiplier and after_multiplier= $after_multiplier"
                    echo "================================================="
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

                    log_file="SATune_PreLoad_${num_format}_val${value_size_twitter}_mem${buffer_size_mb}MB_Cluster${cluster_a}_CT0${ct0}_L1${mb}_Switch${multiplier_switch}_2F${after_multiplier}.log"
                    data_file="/mnt/nvm/second_cluster${cluster_a}.sort" # 构建数据文件路径
                    memory_log_file="$(pwd)/SATune_PreLoad_${num_format}_key${key_size_twitter}_val${value_size_twitter}_Cluster${cluster_a}_mem${buffer_size_mb}MiB_CT0${ct0}.log"      
                    compaction_log_file="$(pwd)/Compaction_${num_format}_key${key_size_twitter}_val${value_size_twitter}_Cluster${cluster_a}_mem${buffer_size_mb}M_CT0${ct0}_L1${mb}_F${multiplier}_Switch${multiplier_switch}_2F${after_multiplier}.log"  
                    
                    # 如果日志文件存在，则跳过当前迭代
                    # if [ -f "$log_file" ]; then
                    #     echo "Log file $log_file already exists. Skipping this iteration."
                    #     continue
                    # fi

                    if [ -f "$compaction_log_file" ]; then
                        # 如果存在，打印一条信息并强制删除它
                        echo "Log file $compaction_log_file already exists. Deleting it now."
                        rm -f "$compaction_log_file"
                    fi

                    # 创建相应的目录
                    db_dir="/mntdisk/SATune10B/PreLoad_Cluster${cluster_a}_${num_format}_mem${buffer_size_mb}MB_CT${ct0}_L1base${mb}_targetbase${target_file_base_mb}_Block${blk_size}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}"

                    if [ ! -d "$db_dir" ]; then
                        mkdir -p "$db_dir"
                    fi

                    # 检查目录是否为空，如果不为空则删除所有内容
                    if [ "$(ls -A $db_dir)" ]; then
                        rm -rf "${db_dir:?}/"*
                    fi

                    tuning_log_dir="/home/jeff-wang/LSM-tuning/SATune_Autotuning_Test_scripts/log_files"
                    # 检查目录是否为空，如果不为空则删除所有内容
                    if [ "$(ls -A $tuning_log_dir)" ]; then
                        rm -rf "${tuning_log_dir:?}/"*
                    fi

                    echo fb0-=0-= | sudo -S bash -c 'echo 1 > /proc/sys/vm/drop_caches'
                    # 获取对应ct0的slowdown和stop值
                    slowdown_value=${slowdown_map[$ct0]}
                    stop_value=${stop_map[$ct0]}
                    # 输出slowdown_value和stop_value的匹配情况
                    echo "For ct0=$ct0, slowdown_value=$slowdown_value, stop_value=$stop_value"
                    echo "value_size:$value_size_twitter"
                    echo "key_size:$key_size_twitter"
                    echo "stats_interval: $stats_interva"
                    echo "$num_format"
                            
                    gdb --args ../../SATune/release/db_bench \
                        --db=$db_dir \
                        --max_bytes_for_level1_base=$level1_max_bytes \
                        --workload_num=$num_kvs \
                        --max_bytes_for_level1_multiplier=10 \
                        --multiplier_switch_level=3 \
                        --max_bytes_for_level_multiplier_after_switch=50 \
                        --block_size=$block_size_write  \
                        --key_size=$key_size_twitter \
                        --value_size=$value_size_twitter \
                        --batch_size=1 \
                        --benchmarks=clusterQuery,stats \
                        --level_compaction_log=$compaction_log_file \
                        --data_file=$data_file  \
                        --bloom_bits=10 \
                        --cache_size=8388608 \
                        --start_c0_tuning=false \
                        --level0_compaction_trigger=$ct0 \
                        --level0_slowdown_writes_trigger=$slowdown_value \
                        --level0_stop_writes_trigger=$stop_value \
                        --use_direct_reads_for_recovery=false \
                        --use_direct_random_access=false \
                        --use_direct_writeappend_file=false \
                        --open_files=80000 \
                        --compression_ratio=0 \
                        --tuning_log_dir=$tuning_log_dir \
                        --compression=0 \
                        --print_wa=true \
                        --stats_interval=$stats_interva \
                        --histogram=1 \
                        --use_existing_db=false \
                        --write_buffer_size=$buffer_size \
                        --mem_log_file=$memory_log_file \
                        --file_size_generated_in_compaction=$target_file_base \
                        --enable_performance_profiling=true \
                        --profile_show_details=true \
    
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