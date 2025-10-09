echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'


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


BASE_VALUE_SIZE=128
billion=1000000000

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


for i in {10..10}; do
  base_num=$(($billion * $i))
  dir1="${i}B_SATune_YCSB_Benchmarking"
  if [ ! -d "$dir1" ]; then
    mkdir $dir1
  fi
  cd $dir1
  for value_size in 100; do
    num_entries=$(($base_num * $BASE_VALUE_SIZE / $value_size))
    stats_interva=$((num_entries / 1000))
    num_entries=1000000000

    for zipf_a in 1.5 ; do  # 1.2 1.3 1.4 1.5
    for iteration in 1; do
    for ct0 in 4 ; do  # 
    for mb in 300; do
    for buffer_size in 67108864; do
    for num_kvs2 in 1000000000; do
      num_format3=$(convert_to_billion_format "$num_kvs2")
    for workload_kvs in 10000000; do #200000000 300000000 400000000 500000000
      num_format2=$(convert_to_billion_format "$workload_kvs")
      echo "原始值: $workload_kvs, 转换后: $num_format2"
    for blk_size in 4 ; do
    for blk_cache_size in 512; do
    for table_cache_size in 10000; do
      # buffer_size=67108864
      # buffer_size=2097152
      key_siz=16
      target_file_base=67108864
      level1_max_bytes=$(($mb * 1048576))
      block_cache_size=$(($blk_cache_size * 1048576))
      buffer_size_mb=$((buffer_size / 1048576))
      target_file_base_mb=$((target_file_base / 1048576))
      block_size_write2=$(($blk_size * 1024))

      log_file="SATune_${num_format2}_in${num_format3}_val${value_size}_mem${buffer_size_mb}MB_zipf${zipf_a}_CT0${ct0}_L1${mb}_targetbase${target_file_base_mb}_Blk${blk_size}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_${iteration}.log"
      data_file="/mnt/workloads/zipf_keys10.0B_random_read.csv" # 构建数据文件路径
      memory_log_file="$(pwd)/SATune_BenchMemory_${num_format2}in${num_format3}_key${key_siz}_val${value_size}_mem${buffer_size_mb}MiB_zipf${zipf_a}_CT0${ct0}_Block${blk_size}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}.log"      

      # 如果日志文件存在，则跳过当前迭代
      if [ -f "$log_file" ]; then
        echo "Log file $log_file already exists. Skipping this iteration."
        continue
      fi

      # 创建相应的目录
      db_dir="/mntdisk/SATune/SATuen_PreLoad_zipf${zipf_a}_${num_format3}_mem${buffer_size_mb}MB_CT${ct0}_L1base${mb}_targetbase${target_file_base_mb}_Block${blk_size}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}_${iteration}"

      # 获取对应ct0的slowdown和stop值
      slowdown_value=${slowdown_map[$ct0]}
      stop_value=${stop_map[$ct0]}
      # 输出slowdown_value和stop_value的匹配情况
      echo "For ct0=$ct0, slowdown_value=$slowdown_value, stop_value=$stop_value"

      echo fb0-=0-= | sudo -S bash -c 'echo 1 > /proc/sys/vm/drop_caches'

      echo "base_num: $base_num"
      echo "num_entries: $num_entries"
      echo "value_size:$value_size"
      echo "key_size:$key_siz"
      echo "stats_interval: $stats_interva"
      echo "$num_format2"
                                
      ../../SATune/release/db_bench \
        --db=$db_dir \
        --max_bytes_for_level1_base=$level1_max_bytes \
        --max_bytes_for_level1_multiplier=10 \
        --num=$num_kvs2 \
        --workload_num=$workload_kvs \
        --block_size=$block_size_write2  \
        --key_size=$key_siz \
        --value_size_=$value_size \
        --value_size=$value_size \
        --batch_size=1 \
        --benchmarks=YCSBQuery,stats \
        --data_file=$data_file  \
        --bloom_bits=10 \
        --cache_size=$block_cache_size \
        --level0_compaction_trigger=$ct0 \
        --level0_slowdown_writes_trigger=$slowdown_value \
        --level0_stop_writes_trigger=$stop_value \
        --open_files=$table_cache_size \
        --compression_ratio=0 \
        --stats_interval=$stats_interva \
        --histogram=1 \
        --use_existing_db=true \
        --write_buffer_size=$buffer_size \
        --print_wa=true \
        --mem_log_file=$memory_log_file \
        --file_size_generated_in_compaction=$target_file_base   \
        --cache_id_type=1 \
        --enable_performance_profiling=true \
        --profile_show_details=true \
        &> >( tee $log_file) &  
        # 保存 db_bench 的 PID 供监控使用
        sleep 1

      DB_BENCH_PID=$(pgrep -af "db_bench --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
      echo "Selected DB_BENCH_PID: $DB_BENCH_PID"

      perf stat -p $DB_BENCH_PID 2>&1 | tee "SATune_perf_stat_${num_format2}in${num_format3}_val_${value_size}_zipf${zipf_a}_mem${MEM}MiB_CT0${ct0}_Block${blk_size}_Blkcache${blk_cache_size}_Tabcache${table_cache_size}.txt" &
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
      done
done