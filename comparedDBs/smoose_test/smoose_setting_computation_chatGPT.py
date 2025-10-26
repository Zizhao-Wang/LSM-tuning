
from __future__ import annotations

from math import ceil, floor, sqrt, exp, log
from functools import lru_cache
from typing import List, Tuple, Optional, Dict


class SmooseResult(dict):
    def __str__(self) -> str:
        lines = []
        lines.append("=== SMOOSE Best Configuration ===")
        lines.append(f"N               : {self['N']} entries")
        lines.append(f"F (buffer)      : {self['F']} entries")
        lines.append(f"B (block)       : {self['B']} entries")
        lines.append(f"M (Bloom bits)  : {self['M']} bits")
        lines.append(f"Workload (s,u,z): {self['workload']}")
        lines.append("")
        lines.append(f"N_L (last level): {self['N_L']} entries")
        lines.append(f"L (levels)      : {len(self['r_list'])}")
        lines.append(f"r_i (size ratios): {self['r_list']}")
        lines.append(f"n_i (runs/level): {self['n_list']} (k = {self['k']:.6g})")
        lines.append("")
        lines.append(f"A = sum sqrt(r_i) : {self['A']:.6g}")
        lines.append(f"p_avg (approx)    : {self['p_avg']:.6g}")
        lines.append("Costs:")
        lines.append(f"  S (range)   = {self['S']:.6g}")
        lines.append(f"  U (update)  = {self['U']:.6g}")
        lines.append(f"  Z (point)   = {self['Z']:.6g}")
        lines.append(f"Objective Ztot = s*S + u*U + z*Z = {self['Z_total']:.6g}")
        return "\n".join(lines)


def _geomspace(a: float, b: float, n: int) -> List[float]:
    if n <= 1 or a == b:
        return [float(a)]
    if a <= 0 or b <= 0:
        step = (b - a) / (n - 1)
        return [a + i * step for i in range(n)]
    loga, logb = log(a), log(b)
    step = (logb - loga) / (n - 1)
    return [float(exp(loga + i * step)) for i in range(n)]


def smoose_optimize(
    N: int,
    F: int,
    B: int,
    M: float,
    s: float,
    u: float,
    z: float,
    N_L_min_ratio: float = 0.5,
    N_L_max_ratio: float = 1.0,
    N_L_step_multiples: int = 1,
    k_grid_points: int = 25,
    r_min: int = 2,
    r_max: int = 128,
    enforce_last_level_single_run: bool = False,
    ceil_runs: bool = True,
    enforce_integer_r_L: bool = False,
    min_levels: int = 1,  # minimum total L (including last level)
) -> SmooseResult:
    """SMOOSE search (DP over r with constraints) and grid over k.

    Parameters
    ----------
    r_min, r_max : int
        Allowable integer bounds for non-last ratios and (optionally) last ratio (if enforce_integer_r_L or base-bound check).
    enforce_last_level_single_run : bool
        If True, bound k so that ceil(k*sqrt(r_L)) == 1 (i.e., k <= 1/sqrt(r_L)).
    ceil_runs : bool
        Use ceil for n_i = ceil(k*sqrt(r_i)); else round().
    enforce_integer_r_L : bool
        If True, final r_L must be an integer in [r_min, r_max] (i.e., require N_L % (Nd+Nr) == 0 at base).
    min_levels : int
        Minimum number of total levels (including last). Useful to prevent degenerate shallow solutions.
    """
    assert 0 < N_L_min_ratio <= N_L_max_ratio <= 1.0
    assert F > 0 and N > 0 and B > 0, "N, F, B must be positive"
    assert N % F == 0, "For DP discretization, require N divisible by F"
    assert r_min >= 2 and r_max >= r_min, "Invalid r bounds"
    assert min_levels >= 1, "min_levels must be >= 1"

    # Average Bloom filter false-positive rate approximation
    p_avg = exp(-(M / float(N)) * (log(2.0) ** 2))

    best: Optional[SmooseResult] = None

    # Enumerate N_L (last level capacity) over multiples of F
    N_L_min = int(ceil(N_L_min_ratio * N / F) * F)
    N_L_max = int(floor(N_L_max_ratio * N / F) * F)
    step = max(1, int(N_L_step_multiples)) * F

    for N_L in range(N_L_min, N_L_max + 1, step):
        if N_L < 2 * F:
            continue
        N_non_last = N - N_L
        if N_non_last < 0:
            continue

        # DP memo: (Nd, Nr, used_levels_so_far) -> (cost, chosen_r or None at base)
        choices: Dict[Tuple[int, int, int], Tuple[float, Optional[int]]] = {}

        @lru_cache(maxsize=None)
        def dp(Nd: int, Nr: int, used: int) -> float:
            # Base: cannot form another full level (r>=2)
            if Nr < 2 * Nd:
                denom = Nd + Nr
                if denom <= 0:
                    return float("inf")
                total_L = used + 1  # adding last ratio
                if total_L < min_levels:
                    return float("inf")
                if enforce_integer_r_L:
                    if N_L % denom != 0:
                        return float("inf")
                    r_L = N_L // denom
                    if r_L < r_min or r_L > r_max:
                        return float("inf")
                    r_L = float(r_L)
                else:
                    r_L = N_L / float(denom)
                    # still check bounds to avoid extreme last ratios
                    if r_L < r_min or r_L > r_max:
                        return float("inf")
                cost = sqrt(r_L)
                choices[(Nd, Nr, used)] = (cost, None)
                return cost

            r_max_here = Nr // Nd
            lo = max(r_min, 2)
            hi = int(min(r_max_here, r_max))
            best_val = float("inf")
            best_r: Optional[int] = None
            feasible_child = False
            for r in range(lo, hi + 1):
                feasible_child = True
                next_Nd = Nd * r
                next_Nr = Nr - Nd * r  # NOTE: use pre-update Nd
                val = sqrt(r) + dp(next_Nd, next_Nr, used + 1)
                if val < best_val:
                    best_val = val
                    best_r = r
            if not feasible_child:
                # Try base if levels suffice and last ratio is feasible
                denom = Nd + Nr
                if denom > 0:
                    total_L = used + 1
                    if total_L >= min_levels:
                        if enforce_integer_r_L:
                            if N_L % denom == 0:
                                r_Li = N_L // denom
                                if r_min <= r_Li <= r_max:
                                    best_val = sqrt(float(r_Li))
                                    best_r = None
                        else:
                            r_Li = N_L / float(denom)
                            if r_min <= r_Li <= r_max:
                                best_val = sqrt(r_Li)
                                best_r = None
            choices[(Nd, Nr, used)] = (best_val, best_r)
            return best_val

        A = dp(F, N_non_last, 0)
        if A == float("inf"):
            continue

        # Reconstruct r_i
        r_list: List[float] = []
        Nd, Nr, used = F, N_non_last, 0
        while True:
            entry = choices.get((Nd, Nr, used), None)
            if entry is None:
                r_list = []
                break
            _, chosen_r = entry
            if chosen_r is None:
                denom = Nd + Nr
                if enforce_integer_r_L:
                    r_L = float(N_L // denom)
                else:
                    r_L = N_L / float(denom)
                r_list.append(r_L)
                break
            else:
                r_list.append(float(chosen_r))
                next_Nd = Nd * chosen_r
                next_Nr = Nr - Nd * chosen_r
                Nd, Nr = next_Nd, next_Nr
                used += 1
        if not r_list:
            continue

        # k range from r_max and (optional) last-level single-run cap
        r_max_val = max(r_list)
        if r_max_val <= 0 or r_max_val == float("inf"):
            continue
        k_min = 1.0 / sqrt(r_max_val)
        k_max = sqrt(r_max_val)
        if enforce_last_level_single_run:
            k_max = min(k_max, 1.0 / sqrt(r_list[-1]))

        # Build k candidates (include 1.0)
        if k_max / k_min > 1.000001:
            k_candidates = _geomspace(k_min, k_max, max(3, int(k_grid_points)))
            if all(abs(x - 1.0) > 1e-12 for x in k_candidates):
                k_candidates.append(1.0)
        else:
            k_candidates = [1.0]

        for k in k_candidates:
            S_cost = k * A
            U_cost = A / (k * B)
            Z_cost = k * p_avg * A
            Z_total = s * S_cost + u * U_cost + z * Z_cost

            n_exact = [k * sqrt(r) for r in r_list]
            if ceil_runs:
                n_list_out = [max(1, int(ceil(v))) for v in n_exact]
            else:
                n_list_out = [max(1, int(round(v))) for v in n_exact]

            if (best is None) or (Z_total < best["Z_total"]):
                best = SmooseResult(
                    N=N, F=F, B=B, M=M,
                    workload=(s, u, z),
                    N_L=N_L,
                    r_list=r_list,
                    n_list=n_list_out,
                    k=k,
                    A=A,
                    p_avg=p_avg,
                    S=S_cost, U=U_cost, Z=Z_cost,
                    Z_total=Z_total,
                )

    if best is None:
        raise RuntimeError("No valid configuration found. Check N,F and search bounds.")
    return best


if __name__ == "__main__":
    # Smoke test: should run without error.
    N = 1_000_000
    F = 10_000
    B = 4096
    M = 8.0 * N
    s, u, z = 0.33, 0.33, 0.33

    best = smoose_optimize(
        N=10_000_000, F=10_000, B=4096, M=8.0*10_000_000,
        s=0.33, u=0.33, z=0.33,
        N_L_min_ratio=0.5, N_L_max_ratio=1.0,
        N_L_step_multiples=1, k_grid_points=21,
        r_min=6, r_max=12,                     # 仍然限制到 [6,12]
        enforce_last_level_single_run=True,    # n_L = 1
        ceil_runs=True,                        
        enforce_integer_r_L=False,             # 关键：放宽 r_L 必须是整数
        min_levels=3,                          # 关键：先别强制 4 层
    )
    print(best)

