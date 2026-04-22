#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
扫描目录下所有 .log 文件，支持按 cluster 过滤，解析文件名中的：
- cluster
- C0
- L1 size (MB)
- f

并提取：
1) 最后一个 level-based "** Compaction Stats [default] **" 表
   用于计算 reduction factor
2) 最后一个 priority-based "** Compaction Stats [default] **" 表
   用于提取最终的 compaction read/write 速率（取 Low 行）
3) DB Stats 中的 ingest
4) Flush(GB): cumulative

输出：
- 终端打印
- CSV 文件

注意：
- reduction factor 用 Level 表
- compaction_rd_mb_s / compaction_wr_mb_s 用最后一个 Priority 表里的 Low 行
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# ============================================================
# 用户默认配置
# ============================================================

# 默认只处理 Cluster40
# 如果想处理全部，改成 None
TARGET_CLUSTER: Optional[int] = 40


# ============================================================
# 基础工具
# ============================================================

UNIT_TO_GB = {
    "KB": 1.0 / (1024 * 1024),
    "MB": 1.0 / 1024,
    "GB": 1.0,
    "TB": 1024.0,
    "PB": 1024.0 * 1024.0,
}


def to_float(x: str) -> float:
    return float(x.strip())


def size_to_gb(value: str, unit: str) -> float:
    unit = unit.strip().upper()
    if unit not in UNIT_TO_GB:
        raise ValueError(f"Unknown unit: {unit}")
    return float(value) * UNIT_TO_GB[unit]


def safe_div(num: Optional[float], den: Optional[float]) -> Optional[float]:
    if num is None or den is None or den == 0:
        return None
    return num / den


def fmt(x: Optional[float], digits: int = 6) -> str:
    if x is None:
        return "NA"
    return f"{x:.{digits}f}"


# ============================================================
# 文件名解析
# ============================================================

def parse_log_filename(filename: str) -> Optional[Dict[str, int]]:
    """
    例如：
    RocksDBDynamic_0.1B_val1000_mem64MB_Cluster40_CT04_L1256_Blkcache8_Tabcache100_f10.log

    解析出：
    - cluster = 40
    - c0 = 4
    - l1_mb = 256
    - f = 10
    """
    pattern = re.compile(
        r"Cluster(?P<cluster>\d+)"
        r"_CT0*(?P<c0>\d+)"
        r"_L1(?P<l1>\d+)"
        r".*?_f(?P<f>\d+)\.log$",
        re.IGNORECASE,
    )
    m = pattern.search(filename)
    if not m:
        return None

    return {
        "cluster": int(m.group("cluster")),
        "c0": int(m.group("c0")),
        "l1_mb": int(m.group("l1")),
        "f": int(m.group("f")),
    }


def make_config_string(c0: int, l1_mb: int, f: int) -> str:
    return f"C0={c0},L1={l1_mb}MB,f={f}"


# ============================================================
# 提取最后一个 Level-based Compaction Stats
# ============================================================

def extract_last_level_compaction_block(text: str) -> Optional[str]:
    """
    提取最后一个这种表：

    ** Compaction Stats [default] **
    Level    Files   ...
    ----
      L0 ...
      L1 ...
      ...
    """
    pattern = re.compile(
        r"\*\* Compaction Stats \[default\] \*\*\s*\n"
        r"Level\s+Files.*?\n"
        r"-+\n"
        r"(?P<body>.*?)(?=\n\*\* Compaction Stats \[default\] \*\*|\nBlob file count:|\Z)",
        re.S,
    )
    matches = list(pattern.finditer(text))
    if not matches:
        return None
    return matches[-1].group("body")


def parse_level_rows(block_body: str) -> Dict[str, Dict[str, float]]:
    """
    解析 Level 表中的每个 Lx 行。
    列位置：
      [0]  level
      [1]  files
      [2]  size_value
      [3]  size_unit
      [4]  score
      [5]  read_gb
      [6]  rn_gb
      [7]  rnp1_gb
      [8]  write_gb
      [9]  wnew_gb
      [10] moved_gb
      [11] w_amp
      [12] rd_mb_s
      [13] wr_mb_s
      ...
    """
    levels: Dict[str, Dict[str, float]] = {}

    for raw_line in block_body.splitlines():
        line = raw_line.strip()
        if not re.match(r"^L\d+\b", line):
            continue

        tokens = line.split()
        if len(tokens) < 14:
            continue

        level_name = tokens[0]

        try:
            row = {
                "score": to_float(tokens[4]),
                "read_gb": to_float(tokens[5]),
                "rn_gb": to_float(tokens[6]),
                "rnp1_gb": to_float(tokens[7]),
                "write_gb": to_float(tokens[8]),
                "wnew_gb": to_float(tokens[9]),
                "moved_gb": to_float(tokens[10]),
                "w_amp": to_float(tokens[11]),
                "rd_mb_s": to_float(tokens[12]),
                "wr_mb_s": to_float(tokens[13]),
            }
            levels[level_name] = row
        except Exception:
            continue

    return levels


# ============================================================
# 提取最后一个 Priority-based Compaction Stats
# ============================================================

def extract_last_priority_compaction_block(text: str) -> Optional[str]:
    """
    提取最后一个这种表：

    ** Compaction Stats [default] **
    Priority    Files   ...
    ----
    Low   ...
    High  ...
    """
    pattern = re.compile(
        r"\*\* Compaction Stats \[default\] \*\*\s*\n"
        r"Priority\s+Files.*?\n"
        r"-+\n"
        r"(?P<body>.*?)(?=\nBlob file count:|\Z)",
        re.S,
    )
    matches = list(pattern.finditer(text))
    if not matches:
        return None
    return matches[-1].group("body")


def parse_priority_rows(block_body: str) -> Dict[str, Dict[str, float]]:
    """
    解析 Priority 表中的 Low / High 行。
    列位置与 Level 表一致，只是第一列是 Low / High。
    """
    rows: Dict[str, Dict[str, float]] = {}

    for raw_line in block_body.splitlines():
        line = raw_line.strip()
        if not re.match(r"^(Low|High)\b", line):
            continue

        tokens = line.split()
        if len(tokens) < 14:
            continue

        name = tokens[0]

        try:
            row = {
                "score": to_float(tokens[4]),
                "read_gb": to_float(tokens[5]),
                "rn_gb": to_float(tokens[6]),
                "rnp1_gb": to_float(tokens[7]),
                "write_gb": to_float(tokens[8]),
                "wnew_gb": to_float(tokens[9]),
                "moved_gb": to_float(tokens[10]),
                "w_amp": to_float(tokens[11]),
                "rd_mb_s": to_float(tokens[12]),
                "wr_mb_s": to_float(tokens[13]),
            }
            rows[name] = row
        except Exception:
            continue

    return rows


def extract_final_compaction_rates_from_priority(text: str) -> Tuple[Optional[float], Optional[float]]:
    """
    从最后一个 Priority 表的 Low 行提取：
    - compaction_rd_mb_s
    - compaction_wr_mb_s

    例如：
    Low ... 748.2 657.6 ...
    """
    block = extract_last_priority_compaction_block(text)
    if block is None:
        return None, None

    rows = parse_priority_rows(block)
    low = rows.get("Low")
    if low is None:
        return None, None

    return low.get("rd_mb_s"), low.get("wr_mb_s")


# ============================================================
# 提取 DB Stats 中的 ingest / Flush
# ============================================================

def extract_last_ingest_gb(text: str) -> Optional[float]:
    """
    例如：
    Cumulative writes: ... ingest: 48.13 GB, 63.93 MB/s
    """
    pattern = re.compile(
        r"^Cumulative writes:.*?ingest:\s*([0-9.]+)\s*([KMGTP]?B)\b",
        re.M,
    )
    matches = pattern.findall(text)
    if not matches:
        return None
    value, unit = matches[-1]
    return size_to_gb(value, unit)


def extract_last_flush_gb(text: str) -> Optional[float]:
    """
    例如：
    Flush(GB): cumulative 36.167, interval 5.277
    """
    pattern = re.compile(
        r"^Flush\(GB\):\s*cumulative\s*([0-9.]+)",
        re.M,
    )
    matches = pattern.findall(text)
    if not matches:
        return None
    return float(matches[-1])


# ============================================================
# reduction factor 计算
# ============================================================

def get_transition_factor(
    levels: Dict[str, Dict[str, float]],
    dest_level: str,
) -> Tuple[Optional[float], Optional[float], Optional[float]]:
    """
    X -> Y 用 Y 这一行：
      reduction factor = Wnew(Y) / Rn(Y)
    返回：
      (factor, input_rn_gb, output_wnew_gb)
    """
    row = levels.get(dest_level)
    if not row:
        return None, None, None

    rn = row["rn_gb"]
    wnew = row["wnew_gb"]
    return safe_div(wnew, rn), rn, wnew


def get_l1_to_others_factor(
    levels: Dict[str, Dict[str, float]],
) -> Tuple[Optional[float], Optional[float], Optional[float]]:
    """
    L1 -> others:
      sum(Wnew of L2+) / sum(Rn of L2+)
    """
    total_rn = 0.0
    total_wnew = 0.0

    for level_name, row in levels.items():
        m = re.match(r"^L(\d+)$", level_name)
        if not m:
            continue
        level_num = int(m.group(1))
        if level_num >= 2:
            total_rn += row["rn_gb"]
            total_wnew += row["wnew_gb"]

    if total_rn == 0:
        return None, None, None

    return total_wnew / total_rn, total_rn, total_wnew


def compute_metrics_from_text(text: str) -> Dict[str, Optional[float]]:
    result: Dict[str, Optional[float]] = {}

    # 1) reduction factor 仍然来自最后一个 Level 表
    block = extract_last_level_compaction_block(text)
    if block is None:
        raise ValueError("Cannot find the last level-based '** Compaction Stats [default] **' block.")

    levels = parse_level_rows(block)

    # 2) ingest / flush
    ingest_gb = extract_last_ingest_gb(text)
    flush_gb = extract_last_flush_gb(text)

    result["ingest_gb"] = ingest_gb
    result["flush_gb"] = flush_gb
    result["flush_to_l0"] = safe_div(flush_gb, ingest_gb)

    # 3) transitions
    factor, inp, out = get_transition_factor(levels, "L1")
    result["l0_to_l1"] = factor
    result["l0_to_l1_input_rn_gb"] = inp
    result["l0_to_l1_output_wnew_gb"] = out

    factor, inp, out = get_transition_factor(levels, "L2")
    result["l1_to_l2"] = factor
    result["l1_to_l2_input_rn_gb"] = inp
    result["l1_to_l2_output_wnew_gb"] = out

    factor, inp, out = get_transition_factor(levels, "L3")
    result["l2_to_l3"] = factor
    result["l2_to_l3_input_rn_gb"] = inp
    result["l2_to_l3_output_wnew_gb"] = out

    factor, inp, out = get_transition_factor(levels, "L4")
    result["l3_to_l4"] = factor
    result["l3_to_l4_input_rn_gb"] = inp
    result["l3_to_l4_output_wnew_gb"] = out

    factor, inp, out = get_l1_to_others_factor(levels)
    result["l1_to_others"] = factor
    result["l1_to_others_input_rn_gb"] = inp
    result["l1_to_others_output_wnew_gb"] = out

    # 4) 最终 compaction 速率来自最后一个 Priority 表的 Low 行
    comp_rd, comp_wr = extract_final_compaction_rates_from_priority(text)
    result["compaction_rd_mb_s"] = comp_rd
    result["compaction_wr_mb_s"] = comp_wr

    return result


# ============================================================
# 主流程
# ============================================================

def scan_log_files(base_dir: Path) -> List[Path]:
    return sorted(base_dir.glob("*.log"))


def filter_and_parse_files(
    log_files: List[Path],
    target_cluster: Optional[int],
) -> List[Tuple[Path, Dict[str, int]]]:
    selected: List[Tuple[Path, Dict[str, int]]] = []

    for log_file in log_files:
        meta = parse_log_filename(log_file.name)
        if meta is None:
            continue

        if target_cluster is not None and meta["cluster"] != target_cluster:
            continue

        selected.append((log_file, meta))

    return selected


def process_one_file(log_path: Path, meta: Dict[str, int]) -> Dict[str, Optional[float]]:
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    metrics = compute_metrics_from_text(text)

    metrics["config"] = make_config_string(meta["c0"], meta["l1_mb"], meta["f"])
    metrics["cluster"] = meta["cluster"]
    metrics["c0"] = meta["c0"]
    metrics["l1_mb"] = meta["l1_mb"]
    metrics["f"] = meta["f"]
    metrics["file"] = log_path.name

    return metrics


def sort_rows(rows: List[Dict[str, Optional[float]]]) -> List[Dict[str, Optional[float]]]:
    def key_func(r: Dict[str, Optional[float]]):
        cluster = r.get("cluster")
        c0 = r.get("c0")
        l1_mb = r.get("l1_mb")
        f = r.get("f")
        return (
            int(cluster) if cluster is not None else 10**9,
            int(c0) if c0 is not None else 10**9,
            int(l1_mb) if l1_mb is not None else 10**9,
            int(f) if f is not None else 10**9,
        )

    return sorted(rows, key=key_func)


def print_result(row: Dict[str, Optional[float]]) -> None:
    print("=" * 110)
    print(f"Config:  {row['config']}")
    print(f"File:    {row['file']}")
    print("-" * 110)

    print(f"ingest_gb                          = {fmt(row.get('ingest_gb'))}")
    print(f"flush_gb                           = {fmt(row.get('flush_gb'))}")
    print(f"flush_to_l0                        = {fmt(row.get('flush_to_l0'))}")
    print()

    print(f"L0 -> L1  input(Rn)               = {fmt(row.get('l0_to_l1_input_rn_gb'))} GB")
    print(f"L0 -> L1  output(Wnew)            = {fmt(row.get('l0_to_l1_output_wnew_gb'))} GB")
    print(f"L0 -> L1  reduction_factor        = {fmt(row.get('l0_to_l1'))}")
    print()

    print(f"L1 -> L2  input(Rn)               = {fmt(row.get('l1_to_l2_input_rn_gb'))} GB")
    print(f"L1 -> L2  output(Wnew)            = {fmt(row.get('l1_to_l2_output_wnew_gb'))} GB")
    print(f"L1 -> L2  reduction_factor        = {fmt(row.get('l1_to_l2'))}")
    print()

    print(f"L2 -> L3  input(Rn)               = {fmt(row.get('l2_to_l3_input_rn_gb'))} GB")
    print(f"L2 -> L3  output(Wnew)            = {fmt(row.get('l2_to_l3_output_wnew_gb'))} GB")
    print(f"L2 -> L3  reduction_factor        = {fmt(row.get('l2_to_l3'))}")
    print()

    print(f"L3 -> L4  input(Rn)               = {fmt(row.get('l3_to_l4_input_rn_gb'))} GB")
    print(f"L3 -> L4  output(Wnew)            = {fmt(row.get('l3_to_l4_output_wnew_gb'))} GB")
    print(f"L3 -> L4  reduction_factor        = {fmt(row.get('l3_to_l4'))}")
    print()

    print(f"L1 -> others input(sum Rn L2+)    = {fmt(row.get('l1_to_others_input_rn_gb'))} GB")
    print(f"L1 -> others output(sum Wnew)     = {fmt(row.get('l1_to_others_output_wnew_gb'))} GB")
    print(f"L1 -> others reduction_factor     = {fmt(row.get('l1_to_others'))}")
    print()

    print(f"compaction_rd_mb_s                = {fmt(row.get('compaction_rd_mb_s'))}")
    print(f"compaction_wr_mb_s                = {fmt(row.get('compaction_wr_mb_s'))}")
    print()


def write_csv(rows: List[Dict[str, Optional[float]]], out_path: Path) -> None:
    fieldnames = [
        "config",
        "cluster",
        "c0",
        "l1_mb",
        "f",

        "ingest_gb",
        "flush_gb",
        "flush_to_l0",

        "l0_to_l1_input_rn_gb",
        "l0_to_l1_output_wnew_gb",
        "l0_to_l1",

        "l1_to_l2_input_rn_gb",
        "l1_to_l2_output_wnew_gb",
        "l1_to_l2",

        "l2_to_l3_input_rn_gb",
        "l2_to_l3_output_wnew_gb",
        "l2_to_l3",

        "l3_to_l4_input_rn_gb",
        "l3_to_l4_output_wnew_gb",
        "l3_to_l4",

        "l1_to_others_input_rn_gb",
        "l1_to_others_output_wnew_gb",
        "l1_to_others",

        "compaction_rd_mb_s",
        "compaction_wr_mb_s",

        "file",
    ]

    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({k: row.get(k) for k in fieldnames})


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "directory",
        nargs="?",
        default=str(Path(__file__).resolve().parent),
        help="Directory containing .log files. Default: script directory",
    )
    parser.add_argument(
        "--cluster",
        type=int,
        default=TARGET_CLUSTER,
        help="Only process this cluster. Default comes from TARGET_CLUSTER in script.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    base_dir = Path(args.directory).resolve()
    target_cluster = args.cluster

    if not base_dir.exists() or not base_dir.is_dir():
        print(f"[ERROR] Directory does not exist: {base_dir}")
        sys.exit(1)

    all_logs = scan_log_files(base_dir)
    if not all_logs:
        print(f"[ERROR] No .log files found in: {base_dir}")
        sys.exit(1)

    selected = filter_and_parse_files(all_logs, target_cluster)
    if not selected:
        if target_cluster is None:
            print("[ERROR] No parsable .log files found.")
        else:
            print(f"[ERROR] No parsable .log files found for cluster {target_cluster}.")
        sys.exit(1)

    all_rows: List[Dict[str, Optional[float]]] = []

    for log_file, meta in selected:
        try:
            row = process_one_file(log_file, meta)
            all_rows.append(row)
            print_result(row)
        except Exception as e:
            print("=" * 110)
            print(f"Config: {make_config_string(meta['c0'], meta['l1_mb'], meta['f'])}")
            print(f"File:   {log_file.name}")
            print(f"[ERROR] {e}")
            print()

    all_rows = sort_rows(all_rows)

    suffix = f"_cluster{target_cluster}" if target_cluster is not None else "_all_clusters"
    out_csv = base_dir / f"reduction_factors{suffix}.csv"
    write_csv(all_rows, out_csv)

    print(f"[DONE] CSV written to: {out_csv}")


if __name__ == "__main__":
    main()