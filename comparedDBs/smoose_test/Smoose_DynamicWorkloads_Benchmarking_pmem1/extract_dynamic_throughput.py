#!/usr/bin/env python3
"""
Extract per-workload GLOBAL CUMULATIVE throughput from SMoose dynamic workload logs.

Global cumulative throughput = total_ops_so_far / total_time_from_start

Samples every 10,000,000 ops within each workload (10 points per 100M workload).
Output format is ready to copy-paste into plotting code.
"""

import re
import os
import glob


def extract_dynamic_throughput(log_path, sample_interval=10_000_000, ops_per_workload=100_000_000):
    pattern = re.compile(
        r'\[(\d+\.?\d*)s\]\s+Ops:\s+(\d+)\s+\(R:(\d+)/W:(\d+)\)\s+\[WL\s+(\d+)\]'
    )

    results = []
    current_wl = -1
    wl_start_global_ops = 0
    last_sampled_milestone = 0  # milestone within current workload

    with open(log_path, 'r') as f:
        for line in f:
            m = pattern.search(line)
            if not m:
                continue

            abs_time = float(m.group(1))
            global_ops = int(m.group(2))
            wl_idx = int(m.group(5))

            # Detect workload switch
            if wl_idx != current_wl:
                current_wl = wl_idx
                wl_start_global_ops = wl_idx * ops_per_workload
                last_sampled_milestone = 0

            # Ops within this workload
            ops_in_wl = global_ops - wl_start_global_ops
            current_milestone = (ops_in_wl // sample_interval) * sample_interval

            if current_milestone > 0 and current_milestone > last_sampled_milestone:
                # Global cumulative: total ops / total time
                global_cumulative_tp = global_ops / abs_time if abs_time > 0 else 0

                results.append({
                    'workload': wl_idx,
                    'milestone': current_milestone,
                    'global_ops': global_ops,
                    'abs_time': abs_time,
                    'global_cumulative_throughput': global_cumulative_tp,
                })
                last_sampled_milestone = current_milestone

    return results


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    log_files = sorted(glob.glob(os.path.join(script_dir, "*.log")))

    if not log_files:
        print(f"No .log files found in {script_dir}")
        return

    for log_path in log_files:
        log_name = os.path.basename(log_path)
        print(f"=== {log_name} ===")

        results = extract_dynamic_throughput(log_path)

        if not results:
            print("  No data points extracted.\n")
            continue

        # Group by workload
        wl_data = {}
        for r in results:
            wl = r['workload']
            if wl not in wl_data:
                wl_data[wl] = []
            wl_data[wl].append(int(round(r['global_cumulative_throughput'])))

        # Output: [val1, val2, ..., val10]
        for wl in sorted(wl_data.keys()):
            vals = ", ".join(str(v) for v in wl_data[wl])
            print(f"  WL{wl}: [{vals}]")

        print()


if __name__ == '__main__':
    main()
