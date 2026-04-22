echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


BASE_VALUE_SIZE=128
billion=1000000000

declare -A slowdown_map
declare -A stop_map

# 为不同的ct0值设定对应的slowdown和stop值
slowdown_map[1]=4
slowdown_map[2]=8
slowdown_map[3]=3
slowdown_map[4]=8
slowdown_map[5]=16
slowdown_map[8]=16
slowdown_map[10]=20
slowdown_map[12]=20
slowdown_map[16]=20
slowdown_map[32]=60
slowdown_map[64]=100
slowdown_map[400000]=800000

stop_map[1]=4
stop_map[2]=2
stop_map[3]=3
stop_map[4]=12
stop_map[5]=20
stop_map[8]=32
stop_map[10]=32
stop_map[12]=32
stop_map[16]=32
stop_map[32]=80
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

# 用这个变量控制后面固定参数取哪个 cluster 的配置
cluster_a=51

# 在这里改 workload 编号，脚本会自动拼成逗号分隔的文件名字符串
data_files=""
cache_sizes=""
open_files_list=""
key_sizes=""
value_sizes=""

for workload_id in 51 49 40 13 35; do
    if [ -n "$data_files" ]; then
        data_files+=","
        cache_sizes+=","
        open_files_list+=","
        key_sizes+=","
        value_sizes+=","
    fi
    data_files+="/mnt/workloads/second_cluster${workload_id}.sort"
    case $workload_id in
        51) cache_sizes+="0";  open_files_list+="0";  key_sizes+="24"; value_sizes+="1000" ;;
        49) cache_sizes+="3145728";  open_files_list+="0";  key_sizes+="24"; value_sizes+="1000" ;;
        40) cache_sizes+="8388608";  open_files_list+="100"; key_sizes+="24"; value_sizes+="1000" ;;
        13) cache_sizes+="8388608";  open_files_list+="500"; key_sizes+="44"; value_sizes+="4266" ;;
        35) cache_sizes+="8388608";  open_files_list+="100";  key_sizes+="24"; value_sizes+="1000" ;;
    esac
done

dir1="RocksDB_MultiTwitterClusters_Benchmarking_Performance_PM0"
if [ ! -d "$dir1" ]; then
    mkdir $dir1
fi
        cd $dir1
                for ct0 in 4; do  # 
                for mb in 256; do
                for buffer_size in 67108864; do
                for workload_kvs in 100000000 ; do 
                    num_format2=$(convert_to_billion_format "$workload_kvs")
                    stats_interva=$((workload_kvs / 10))
                    echo "原始值: $workload_kvs, 转换后: $num_format2"
                    for blk_cache_size in 0 ; do
                    for table_cache_size in 0 ; do
                    for mu in 10; do

                        # buffer_size=67108864
                        # buffer_size=2097152
                        level1_max_bytes=$(($mb * 1048576))
                        block_cache_size=$(($blk_cache_size * 1048576))
                        buffer_size_mb=$((buffer_size / 1048576))

                        value_size_twitter=${cluster_value_size_map[$cluster_a]}
                        key_size_twitter=${cluster_key_size_map[$cluster_a]}

                        log_file="RocksDB_${num_format2}_val${value_size_twitter}_mem${buffer_size_mb}MB_Cluster${cluster_a}_CT0${ct0}_L1${mb}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_f${mu}.log"
                        data_file="$data_files"
                        memory_log_file="$(pwd)/RocksDB_Memory_${num_format2}_key${key_size_twitter}_val${value_size_twitter}_Cluster${cluster_a}_mem${buffer_size_mb}MiB_CT0${ct0}_L1${mb}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}.log"      

                        # 如果日志文件存在，则跳过当前迭代
                        if [ -f "$log_file" ]; then
                            echo "Log file $log_file already exists. Skipping this iteration."
                            continue
                        fi

                        # 创建相应的目录
                        db_dir="/mnt/pmem1/rocks10B/PreLoad_Cluster${cluster_a}_mem${buffer_size_mb}MB_CT${ct0}_L1base${mb}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}"
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

                        echo "num_entries: $num_entries"
                        echo "value_size:$value_size_twitter"
                        echo "key_size:$key_size_twitter"
                        echo "stats_interval: $stats_interva"
                        echo "$num_format2"
                        echo "cache size: $block_cache_size"

                                
                        ../../../../rocksdb_tiered/release/db_bench \
                            --db=$db_dir \
                            --workload_num=$workload_kvs \
                            --use_direct_reads=true \
                            --key_size=$key_size_twitter \
                            --value_size=$value_size_twitter \
                            --value_size_=$value_size_twitter \
                            --batch_size=1 \
                            --benchmarks=clusterQuery,stats \
                            --data_file_path=$data_file  \
                            --bloom_bits=10 \
                            --use_direct_io_for_flush_and_compaction=true \
                            --max_background_compactions=1 \
                            --max_background_flushes=1 \
                            --compaction_style=1 \
                            --cache_size=$block_cache_size \
                            --open_files=$table_cache_size \
                            --cache_size_per_workload=$cache_sizes \
                            --open_files_per_workload=$open_files_list \
                            --key_size_per_workload=$key_sizes \
                            --value_size_per_workload=$value_sizes \
                            --compression_ratio=0 \
                            --stats_interval=$stats_interva \
                            --stats_per_interval=$stats_interva \
                            --histogram=1 \
                            --use_existing_db=false \
                            --write_buffer_size=$buffer_size \
                            --mem_log_file=$memory_log_file \
                            --compression_type=none \
                            &> >( tee $log_file) &  

                            # 保存 db_bench 的 PID 供监控使用
                            sleep 1

                            DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
                            echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

                            # perf stat -p $DB_BENCH_PID 2>&1 | tee "perf_PreLoad_stat_${num_format2}_val_${value_size_twitter}_Cluster${cluster_a}_mem${MEM}MiB_CT0${ct0}_L1${mb}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}.txt" &
                            # PERF_PID=$!

                            wait $DB_BENCH_PID

                            # echo "Checking if perf stat process with PID $PERF_PID is still running..."
                            # ps -p $PERF_PID > /dev/null # 重定向标准输出到 /dev/null，只检查退出状态
                            # if [ $? -eq 0 ]; then
                            #     echo "perf stat process $PERF_PID is still running, killing now..."
                            #     kill -INT $PERF_PID # 尝试发送 SIGINT 信号，让 perf stat 优雅结束
                            #     sleep 1 # 给 perf stat 1秒时间来处理信号和输出报告
                            #     ps -p $PERF_PID > /dev/null
                            #     if [ $? -eq 0 ]; then
                            #         echo "perf stat process $PERF_PID did not terminate, forcing kill..."
                            #         kill -9 $PERF_PID # 如果仍未结束，强制杀死
                            #         if [ $? -eq 0 ]; then
                            #             echo "perf stat process $PERF_PID has been successfully killed."
                            #         else
                            #             echo "Failed to force kill perf stat process $PERF_PID."
                            #         fi
                            #     else
                            #         echo "perf stat process $PERF_PID has been successfully terminated."
                            #     fi
                            # else
                            #     echo "perf stat process $PERF_PID is no longer running (it might have ended with db_bench)."
                            # fi
                done
                done
                done
                done
                done
                done
                done
    cd ..
