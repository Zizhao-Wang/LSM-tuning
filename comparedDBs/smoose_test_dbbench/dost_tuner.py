#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Dostoevsky-style Fluid LSM tuner for Moose-LSM (with progress logs)
------------------------------------------------------------------
Added runtime progress and debug outputs, without changing algorithmic logic.
"""

from dataclasses import dataclass
from math import log, ceil, exp
from typing import Dict, List, Tuple, Any
import sys, time, json

# -----------------------------
# Data classes
# -----------------------------
@dataclass
class WorkloadMix:
    w: float; r: float; v: float; q: float

@dataclass
class HardwareParams:
    omega_read_sec: float = 1.0
    mu_seq_over_rand: float = 4.0
    phi_write_over_read: float = 1.0

@dataclass
class DatasetParams:
    N: int; entry_bytes: int; block_bytes: int; P: int; M_bits: int

@dataclass
class RangeQueryParams:
    s_unique_entries: int

@dataclass
class Constraints:
    max_space_amp: float
    max_T: int = None
    limit_T_candidates: int = 64

@dataclass
class TunedConfig:
    T: int; K: int; Z: int; L: int; amp: float
    R: float; V: float; Q: float; W: float; tau: float
    bloom_fprs: List[float]; moose_config: Dict[str, Any]

# -----------------------------
# Core math helpers
# -----------------------------
def levels_count(N: int, B: int, P: int, T: int) -> int:
    numerator = (N / (B * P)) * ((T - 1) / T)
    return 1 if numerator <= 1 else ceil(log(numerator, T))

def R_zero_result_point(M_bits: int, N: int, T: int, K: int, Z: int) -> float:
    ln2_sq = (log(2))**2
    base = exp(-(M_bits / N) * ln2_sq)
    const = Z * ((T - 1) / T) * (1.0 / T) * ((T ** (T / (T - 1.0))) / (T - 1.0))
    return base * const

def V_existing_key(R: float, T: int, K: int, Z: int) -> float:
    p_L = (R * T) / (Z * (T - 1.0))
    p_L = max(0.0, min(1.0, p_L))
    return 1.0 + max(0.0, R - p_L)

def W_update_cost(T: int, K: int, Z: int, B: int, mu: float, phi: float, L: int) -> float:
    term_levels = ((T - 1.0) / (K + 1.0)) * (L - 1.0)
    term_last = (T - 1.0) / (Z + 1.0)
    return (phi / (mu * B)) * (term_levels + term_last)

def amp_space(T: int, Z: int) -> float:
    return (Z - 1.0) + (1.0 / T)

def fprs_per_level(R: float, T: int, K: int, Z: int, L: int) -> List[float]:
    common = (T - 1.0) / T
    p_L = (R / Z) * common
    fprs = []
    for i in range(1, L):
        exp_term = T ** (-(L - i))
        fprs.append((R / K) * common * exp_term)
    fprs.append(p_L)
    return [max(0.0, min(1.0, p)) for p in fprs]

# -----------------------------
# Scoring & search
# -----------------------------
def score_config(T, K, Z, ds, mix, hw, rangeq, L):
    B_entries = ds.block_bytes // max(1, ds.entry_bytes)
    R = R_zero_result_point(ds.M_bits, ds.N, T, K, Z)
    V = V_existing_key(R, T, K, Z)
    Q = K*(L-1) + Z + (1.0 / hw.mu_seq_over_rand) * (rangeq.s_unique_entries / B_entries) * (Z + 1.0/T)
    W = W_update_cost(T, K, Z, B_entries, hw.mu_seq_over_rand, hw.phi_write_over_read, L)
    denom = (mix.w * W + mix.r * R + mix.v * V + mix.q * Q)
    tau = (1.0 / hw.omega_read_sec) * (1.0 / denom) if denom > 0 else float("inf")
    fprs = fprs_per_level(R, T, K, Z, L)
    return (-tau, dict(R=R, V=V, Q=Q, W=W, tau=tau), fprs)

def enumerate_T_candidates(ds, cons):
    B = ds.block_bytes // max(1, ds.entry_bytes)
    Tlim = int(ds.N // max(1, (B * ds.P)))
    if cons.max_T is not None:
        Tlim = min(Tlim, cons.max_T)
    Tlim = max(2, Tlim)
    candidates = set(range(2, min(Tlim, 64) + 1))
    step = max(1, Tlim // max(4, cons.limit_T_candidates))
    for t in range(70, Tlim + 1, step):
        candidates.add(t)
    candidates.add(Tlim)
    return sorted(candidates)

def tune_fluid_lsm(ds, mix, hw, rangeq, cons):
    start_time = time.time()
    print(f"[INFO] Start tuning (N={ds.N}, max_T={cons.max_T}) ...", flush=True)
    B = ds.block_bytes // max(1, ds.entry_bytes)
    candidates = enumerate_T_candidates(ds, cons)
    print(f"[INFO] Enumerated {len(candidates)} T candidates: {candidates[:10]}{'...' if len(candidates)>10 else ''}", flush=True)

    best, best_detail = None, None
    total_loops = 0
    for idx, T in enumerate(candidates, 1):
        L = levels_count(ds.N, B, ds.P, T)
        if idx % 5 == 0:
            print(f"[DEBUG] Progress: {idx}/{len(candidates)} (T={T}, L={L})", flush=True)

        for K in range(1, T):
            for Z in range(1, T):
                amp = amp_space(T, Z)
                if amp > cons.max_space_amp:
                    continue
                total_loops += 1
                try:
                    neg_tau, metrics, fprs = score_config(T, K, Z, ds, mix, hw, rangeq, L)
                except Exception as e:
                    print(f"[WARN] Exception at T={T},K={K},Z={Z}: {e}", flush=True)
                    continue
                if best is None or neg_tau < best:
                    best, best_detail = neg_tau, (T, K, Z, L, amp, metrics, fprs)
        # 每个T输出一次当前最优
        if best_detail:
            T0,K0,Z0,L0,amp0,metrics0,fprs0 = best_detail
            print(f"[PROGRESS] After T={T}: best τ={metrics0['tau']:.6f}, conf=({T0},{K0},{Z0},{L0}), amp={amp0:.3f}", flush=True)

    elapsed = time.time() - start_time
    print(f"[INFO] Search finished in {elapsed:.2f}s, total evaluated configs={total_loops}", flush=True)

    if not best_detail:
        raise ValueError("No valid configuration found (check max_amp constraint).")

    T,K,Z,L,amp,metrics,fprs = best_detail
    moose_cfg = {
        "levels": L,
        "size_ratio_per_level": [T] * max(1, L-1),
        "max_runs_per_level": [K] * max(0, L-1) + [Z],
        "bloom_fpr_per_level": fprs
    }
    print(f"[DONE] Best τ={metrics['tau']:.6f}, T={T},K={K},Z={Z},L={L},amp={amp:.3f}", flush=True)

    return TunedConfig(T,K,Z,L,amp,metrics["R"],metrics["V"],metrics["Q"],metrics["W"],metrics["tau"],fprs,moose_cfg)

# -----------------------------
# CLI entry
# -----------------------------
def main():
    import argparse
    ap = argparse.ArgumentParser(description="Fluid LSM tuner with progress logs.")
    ap.add_argument("--N", type=int, required=True)
    ap.add_argument("--entry-bytes", type=int, default=128)
    ap.add_argument("--block-bytes", type=int, default=4096)
    ap.add_argument("--P", type=int, required=True)
    ap.add_argument("--M-bits", type=int, required=True)
    ap.add_argument("--mix-w", type=float, required=True)
    ap.add_argument("--mix-r", type=float, required=True)
    ap.add_argument("--mix-v", type=float, required=True)
    ap.add_argument("--mix-q", type=float, required=True)
    ap.add_argument("--omega", type=float, default=1.0)
    ap.add_argument("--mu", type=float, default=4.0)
    ap.add_argument("--phi", type=float, default=1.0)
    ap.add_argument("--s-unique", type=int, default=0)
    ap.add_argument("--max-amp", type=float, default=0.1)
    ap.add_argument("--max-T", type=int, default=None)
    ap.add_argument("--limit-T-candidates", type=int, default=64)
    args = ap.parse_args()

    ds = DatasetParams(args.N, args.entry_bytes, args.block_bytes, args.P, args.M_bits)
    mix = WorkloadMix(args.mix_w, args.mix_r, args.mix_v, args.mix_q)
    hw  = HardwareParams(args.omega, args.mu, args.phi)
    rq  = RangeQueryParams(args.s_unique)
    cons= Constraints(args.max_amp, args.max_T, args.limit_T_candidates)

    tuned = tune_fluid_lsm(ds, mix, hw, rq, cons)
    result = {
        "best_config": {
            "T": tuned.T, "K": tuned.K, "Z": tuned.Z, "L": tuned.L,
            "space_amp": tuned.amp,
            "metrics": {"R": tuned.R, "V": tuned.V, "Q": tuned.Q, "W": tuned.W, "tau": tuned.tau},
            "bloom_fpr_per_level": tuned.bloom_fprs
        },
        "moose_config": tuned.moose_config
    }
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    main()
