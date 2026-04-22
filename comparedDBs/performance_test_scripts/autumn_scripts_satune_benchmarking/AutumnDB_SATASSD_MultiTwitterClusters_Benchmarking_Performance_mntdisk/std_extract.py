import re
from pathlib import Path
from typing import Dict, Any, Optional, Tuple

# 通用：匹配所有 "Microseconds per <op>:" 后面的统计行
# 允许不同空格、允许 \r\n
BLOCK_RE = re.compile(
    r"Microseconds\s+per\s+(?P<op>[A-Za-z0-9_\-]+)\s*:\s*[\r\n]+"
    r"Count:\s*(?P<count>\d+)\s+Average:\s*(?P<avg>[0-9.]+)\s+StdDev:\s*(?P<std>[0-9.]+)",
    re.IGNORECASE
)

def parse_one_log_any_ops(text: str) -> Dict[str, Dict[str, float]]:
    """
    解析一个 log：返回所有出现的 op 的 {count, avg, std}
    例如：{"read": {...}, "write": {...}, "query": {...}}
    """
    ops: Dict[str, Dict[str, float]] = {}
    for m in BLOCK_RE.finditer(text):
        op = m.group("op").lower()
        ops[op] = {
            "count": int(m.group("count")),
            "avg": float(m.group("avg")),
            "std": float(m.group("std")),
        }
    return ops

def pick_read_write(ops: Dict[str, Dict[str, float]]) -> Dict[str, Dict[str, float]]:
    """
    从 ops 中取 read/write。
    如果你的 log 用的是别名，比如 query/insert，可以在 alias 里加映射。
    """
    alias = {
        # 你可以按需要加：
        # "query": "read",
        # "insert": "write",
        # "update": "write",
    }

    normalized: Dict[str, Dict[str, float]] = {}
    for op, v in ops.items():
        k = alias.get(op, op)
        normalized[k] = v

    out = {}
    if "read" in normalized:
        out["read"] = normalized["read"]
    if "write" in normalized:
        out["write"] = normalized["write"]
    return out

def parse_logs_in_folder(folder: str,
                         exts=(".log", ".txt"),
                         encoding="utf-8") -> Dict[str, Dict[str, Dict[str, float]]]:
    folder_path = Path(folder)
    if not folder_path.exists():
        raise FileNotFoundError(f"folder not found: {folder}")

    files = []
    for ext in exts:
        files.extend(folder_path.glob(f"*{ext}"))

    if not files:
        raise FileNotFoundError(f"no log files found in {folder} with exts={exts}")

    results = {}
    bad = []

    for p in sorted(files):
        text = p.read_text(encoding=encoding, errors="ignore")
        ops = parse_one_log_any_ops(text)

        if not ops:
            bad.append((p.name, "no 'Microseconds per <op>:' blocks found"))
            continue

        rw = pick_read_write(ops)
        if "read" not in rw or "write" not in rw:
            bad.append((p.name, f"found ops={sorted(list(ops.keys()))}, but missing read/write"))
            # 这里你可以选择 continue 跳过，或者仍然保存所有 ops
            # 我这里选择仍然保存所有 ops，方便你检查
            results[p.stem] = {"_all_ops": ops}
            continue

        results[p.stem] = rw

    if bad:
        print("\n[WARN] Some files didn't match read/write as expected:")
        for fname, reason in bad:
            print(f"  - {fname}: {reason}")
        print("Tip: 如果你的 log 用的是 query/insert 等名字，把 alias 映射加进去。\n")

    return results

def print_as_requested(results: Dict[str, Dict[str, Dict[str, float]]]) -> None:
    for name, stats in results.items():
        print(f"\n# {name}")

        # 如果没取到 read/write，就把所有 ops 打出来
        if "read" not in stats or "write" not in stats:
            if "_all_ops" in stats:
                print("# 该文件没匹配到 read/write，实际抓到的 op 如下：")
                for op, v in stats["_all_ops"].items():
                    print(f'#   "{op}": {v}')
            else:
                print("# 该文件没解析出任何 op")
            continue

        r = stats["read"]
        w = stats["write"]
        print(f'        "read":  {{"count": {r["count"]}, "avg": {r["avg"]:.4f}, "std": {r["std"]:.2f}}},')
        print(f'        "write": {{"count": {w["count"]}, "avg": {w["avg"]:.4f}, "std": {w["std"]:.2f}}},')


if __name__ == "__main__":
    LOG_DIR = r"/home/jeff-wang/LSM-tuning/comparedDBs/performance_test_scripts/autumn_scripts_satune_benchmarking/AutumnDB_SATASSD_MultiTwitterClusters_Benchmarking_Performance_pmem0"
    results = parse_logs_in_folder(LOG_DIR, exts=(".log", ".txt"))
    print_as_requested(results)
