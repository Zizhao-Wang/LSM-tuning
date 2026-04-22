echo fb0-=0-= | sudo -S bash -c 'echo 800000 > /proc/sys/fs/file-max'
bash -c 'ulimit -n 800000'

billion=1000000000

convert_to_billion_format() {
    local num=$1
    local integer_part=$((num / billion))
    local decimal_part=$((num % billion))

    if ((decimal_part == 0)); then
        echo "${integer_part}B"
    else
        local formatted_decimal=$(echo "scale=9; $decimal_part / $billion" | bc | sed 's/0*$//' | sed 's/\.$//')
        formatted_decimal=$(echo $formatted_decimal | cut -d'.' -f2)
        echo "${integer_part}.${formatted_decimal}B"
    fi
}

# ========== Dynamic Workload Configuration ==========
# Define workload sequence (cluster IDs in order)
WORKLOAD_SEQUENCE=(51 49 40 13 35)

# Build comma-separated lists for each parameter
data_files=""
key_sizes=""
value_sizes=""
level_capacities_list=""
run_numbers_list=""

for workload_id in "${WORKLOAD_SEQUENCE[@]}"; do
    # data_files
    if [ -n "$data_files" ]; then
        data_files+=","
        key_sizes+=","
        value_sizes+=","
        level_capacities_list+=";"
        run_numbers_list+=";"
    fi
    data_files+="/mnt/workloads/second_cluster${workload_id}.sort"

    # Per-cluster key_size, value_size, level_capacities, run_numbers
    case $workload_id in
        51)
            key_sizes+="24"
            value_sizes+="1000"
            level_capacities_list+="27,27,13"
            run_numbers_list+="2,2,1"
            ;;
        49)
            key_sizes+="24"
            value_sizes+="1000"
            level_capacities_list+="7,7,7,8,3"
            run_numbers_list+="7,7,7,8,3"
            ;;
        40)
            key_sizes+="24"
            value_sizes+="1000"
            level_capacities_list+="7,7,7,8,3"
            run_numbers_list+="7,7,7,8,3"
            ;;
        13)
            key_sizes+="44"
            value_sizes+="4266"
            level_capacities_list+="7,7,7,8,3"
            run_numbers_list+="7,7,7,8,3"
            ;;
        35)
            key_sizes+="24"
            value_sizes+="1000"
            level_capacities_list+="27,27,13"
            run_numbers_list+="2,2,1"
            ;;
        12)
            key_sizes+="44"
            value_sizes+="1030"
            level_capacities_list+="11,11,12,6"
            run_numbers_list+="2,2,2,1"
            ;;
        *)
            echo "WARNING: Unknown cluster $workload_id, using defaults"
            key_sizes+="24"
            value_sizes+="1000"
            level_capacities_list+="10,11,11,8"
            run_numbers_list+="10,11,11,7"
            ;;
    esac
done

echo "=== Dynamic Workload Configuration ==="
echo "Workload sequence: ${WORKLOAD_SEQUENCE[*]}"
echo "Data files: $data_files"
echo "Key sizes: $key_sizes"
echo "Value sizes: $value_sizes"
echo "Level capacities: $level_capacities_list"
echo "Run numbers: $run_numbers_list"
echo "======================================="

# ========== Benchmark Parameters ==========

# Use first cluster's key/value as the "base" for naming
cluster_a="${WORKLOAD_SEQUENCE[0]}"

declare -A cluster_key_size_map
cluster_key_size_map[51]=24; cluster_key_size_map[49]=24; cluster_key_size_map[40]=24
cluster_key_size_map[13]=44; cluster_key_size_map[35]=24; cluster_key_size_map[12]=44
cluster_key_size_map[48]=44; cluster_key_size_map[1]=24;  cluster_key_size_map[30]=22

declare -A cluster_value_size_map
cluster_value_size_map[51]=1000; cluster_value_size_map[49]=1000; cluster_value_size_map[40]=1000
cluster_value_size_map[13]=4266; cluster_value_size_map[35]=1000; cluster_value_size_map[12]=1030
cluster_value_size_map[48]=70;   cluster_value_size_map[1]=1000;  cluster_value_size_map[30]=1000

declare -A cluster_block_cache_size_map
cluster_block_cache_size_map[51]=0;  cluster_block_cache_size_map[49]=8; cluster_block_cache_size_map[40]=8
cluster_block_cache_size_map[13]=8;  cluster_block_cache_size_map[35]=8; 

declare -A cluster_table_size_map
cluster_table_size_map[51]=0;   cluster_table_size_map[49]=0;   cluster_table_size_map[40]=100
cluster_table_size_map[13]=500; cluster_table_size_map[35]=100;   

dir1="Smoose_DynamicWorkloads_Benchmarking_nvm"
if [ ! -d "$dir1" ]; then
    mkdir "$dir1"
fi
cd "$dir1"

stats_interva=1000000

for ct0 in 4; do
for mb in 512; do
for buffer_size in 67108864; do
for workload_kvs in 100000000; do
    num_format=$(convert_to_billion_format "$workload_kvs")
    echo "Per-workload ops: $workload_kvs ($num_format)"

    for blk_cache_size in ${cluster_block_cache_size_map[$cluster_a]}; do
    for table_cache_size in ${cluster_table_size_map[$cluster_a]}; do
    for perf_tier in 1; do

        level1_max_bytes=$(($mb * 1048576))
        buffer_size_mb=$((buffer_size / 1048576))

        value_size_twitter=${cluster_value_size_map[$cluster_a]}
        key_size_twitter=${cluster_key_size_map[$cluster_a]}

        workload_tag=$(IFS=_; echo "${WORKLOAD_SEQUENCE[*]}")
        log_file="Smoose_Dynamic_${num_format}_WL${workload_tag}_mem${buffer_size_mb}MB_CT0${ct0}_BlockCache${blk_cache_size}_Perf${perf_tier}.log"

        # Skip if log already exists
        if [ -f "$log_file" ]; then
            echo "Log file $log_file already exists. Skipping."
            continue
        fi

        # Create and clean DB directory
        db_dir="/mnt/nvm/MooseLSM/Dynamic_WL${workload_tag}_${num_format}_mem${buffer_size_mb}MB_CT${ct0}"
        if [ ! -d "$db_dir" ]; then
            mkdir -p "$db_dir"
        fi
        if [ "$(ls -A $db_dir)" ]; then
            rm -rf "${db_dir:?}/"*
        fi

        echo "=== Running Dynamic Workload Benchmark ==="
        echo "Log: $log_file"
        echo "DB dir: $db_dir"

        ../build/smoose_tester \
            --workload=mixed_dynamic \
            --db=$db_dir \
            --data_file_path="$data_files" \
            --workload_num=$workload_kvs \
            --num=$((workload_kvs * ${#WORKLOAD_SEQUENCE[@]})) \
            --key_size=$key_size_twitter \
            --value_size=$value_size_twitter \
            --key_size_per_workload="$key_sizes" \
            --value_size_per_workload="$value_sizes" \
            --level_capacities_per_workload="$level_capacities_list" \
            --run_numbers_per_workload="$run_numbers_list" \
            --read_ratio=0.0 \
            --write_ratio=1.0 \
            --verify_reads=false \
            --bits_per_key=10 \
            --block_cache_mb=$blk_cache_size \
            --table_cache_size=$table_cache_size \
            --stats_interval_ops="$stats_interva" \
            --perf_level=$perf_tier \
            &> >(tee $log_file) &

            sleep 1
            DB_BENCH_PID=$(pgrep -af "smoose_tester --workload=mixed_dynamic --db=$db_dir" | grep -v 'sudo' | awk '{print $1}')
            echo "Selected PID: $DB_BENCH_PID"

            wait $DB_BENCH_PID

    done
    done
    done
done
done
done
done

cd ..
