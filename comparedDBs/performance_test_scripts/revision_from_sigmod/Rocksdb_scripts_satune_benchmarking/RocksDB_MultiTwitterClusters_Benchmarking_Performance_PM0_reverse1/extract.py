import re
from pathlib import Path

try:
    current_dir = Path(__file__).resolve().parent
except NameError:
    current_dir = Path.cwd()

print(f"Scanning directory: {current_dir}")

log_files = sorted(current_dir.glob("*.log"))

if not log_files:
    raise FileNotFoundError("No .log files found in the current directory.")

print("Found log files:")
for f in log_files:
    print(f"  - {f.name}")

# =========================================================
# Match the SECOND throughput value
#
# Example:
# thread 0: (10000000,20000000) ops and (410919.8,117067.9) ops/second
#
# Extract: 117067.9
# =========================================================
number_pattern = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"

throughput_pattern = re.compile(
    rf"thread\s+0:\s*"
    rf"\([^)]*\)\s*ops\s+and\s*"
    rf"\(\s*{number_pattern}\s*,\s*(?P<throughput>{number_pattern})\s*\)\s*ops/second",
    re.IGNORECASE
)

def sanitize_var_name(filename: str) -> str:
    name = Path(filename).stem
    name = re.sub(r"[^0-9a-zA-Z_]+", "_", name)
    if re.match(r"^\d", name):
        name = "_" + name
    return name


def format_groups(values, group_size=10):
    groups = [
        values[i:i + group_size]
        for i in range(0, len(values), group_size)
    ]

    lines = []
    lines.append("np.array([")

    for group in groups:
        values_str = ", ".join(f"{v:.2f}" for v in group)

        if len(group) == group_size:
            lines.append(f"    ({values_str}),")
        else:
            lines.append(f"    ({values_str}),  # incomplete group: {len(group)} values")

    lines.append("])")
    return "\n".join(lines)


points_per_group = 10
all_results = {}

print("\n" + "=" * 80)
print("Per-file throughput extraction")
print("=" * 80)

for log_file in log_files:
    with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    values = []

    for match in throughput_pattern.finditer(content):
        throughput = float(match.group("throughput"))
        values.append(throughput)

    all_results[log_file.name] = values
    var_name = sanitize_var_name(log_file.name)

    print(f"\n# ------------------------------------------------------------")
    print(f"# File: {log_file.name}")
    print(f"# Matched throughput values: {len(values)}")
    print(f"# ------------------------------------------------------------")

    if len(values) == 0:
        print("# No throughput values matched in this file.")
        continue

    if len(values) % points_per_group != 0:
        print(
            f"# Warning: {len(values)} values is not divisible by {points_per_group}. "
            f"The last group will be incomplete."
        )

    print(f"{var_name} = {format_groups(values, group_size=points_per_group)}")


print("\n" + "=" * 80)
print("Summary")
print("=" * 80)

total = 0
for fname, values in all_results.items():
    total += len(values)
    print(f"{fname}: {len(values)} throughput values")

print(f"\nTotal throughput values across all files: {total}")