#!/usr/bin/env bash

set -euo pipefail

declare -A cluster_table_size_map
declare -A cluster_block_cache_size_map

cluster_table_size_map[35]=0
cluster_table_size_map[40]=100
cluster_table_size_map[49]=8
cluster_table_size_map[51]=3
cluster_table_size_map[30]=8
cluster_table_size_map[1]=0
cluster_table_size_map[48]=300
cluster_table_size_map[12]=200
cluster_table_size_map[13]=500

cluster_block_cache_size_map[35]=8
cluster_block_cache_size_map[40]=8
cluster_block_cache_size_map[49]=8
cluster_block_cache_size_map[51]=3
cluster_block_cache_size_map[30]=8
cluster_block_cache_size_map[1]=3
cluster_block_cache_size_map[12]=8
cluster_block_cache_size_map[13]=8
cluster_block_cache_size_map[48]=8

print_one_cluster() {
    local cluster_a="$1"
    local blk_cache_size="${cluster_block_cache_size_map[$cluster_a]:-}"
    local table_cache_size="${cluster_table_size_map[$cluster_a]:-}"

    if [[ -z "$blk_cache_size" || -z "$table_cache_size" ]]; then
        echo "Unknown cluster: $cluster_a" >&2
        return 1
    fi

    local block_cache_size=$((blk_cache_size * 1048576))

    echo "cluster_a=$cluster_a"
    echo "blk_cache_size=${blk_cache_size} MiB"
    echo "table_cache_size=$table_cache_size"
    echo "--cache_size=$block_cache_size \\"
    echo "--open_files=$table_cache_size \\"
    echo
}

if [[ $# -gt 0 ]]; then
    for cluster_a in "$@"; do
        print_one_cluster "$cluster_a"
    done
else
    for cluster_a in 1 12 13 30 35 40 48 49 51; do
        print_one_cluster "$cluster_a"
    done
fi
