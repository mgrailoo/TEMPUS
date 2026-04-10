#!/usr/bin/env python3
"""
Estimate which dma_hls dataflow leg (inp_A, inp_B, out_C) likely dominates wall time.

Uses the same size formulas as gemm_config.h (see that file for definitions).

Important: ranking is *not* only DDR bytes. See design/pl_src/dma_hls.cpp:
  - inp_B: sequential EXACT_MATB_SZ reads, II=1, light switch — mostly DDR-limited.
  - inp_A: block DDR load (raw A size) + pack_cascades (× CASC_LN_AB, partial UNROLL) +
    store_beats + broadcast_loop replay — heavy on-FPGA work; wall time often tracks
    EXACT_MATA_SZ-scale replay, not raw A buffer size alone.
  - out_C: de-tile / reassembly + DDR writes — moderate logic vs EXACT_MATC_SZ.

This script prints (1) DDR traffic, (2) pipeline/stream trip-count proxies for DATAFLOW max().

Real dma_hls wait also depends on AIE rate, backpressure, and implementation — use v++
--profile.* or Vitis Analyzer on hw/hw_emu for measured stalls.

Usage:
  python3 estimate_dma_datapath.py [path/to/config.json]
Default config path: design/design_configs/config.json (relative to cwd).
"""
from __future__ import annotations

import json
import sys
from pathlib import Path


def load_config(path: Path) -> dict:
    with path.open() as f:
        return json.load(f)


def main() -> int:
    root = Path.cwd()
    cfg_path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "design/design_configs/config.json"
    if not cfg_path.is_file():
        print(f"ERROR: config not found: {cfg_path}", file=sys.stderr)
        return 1

    c = load_config(cfg_path)
    ga = int(c["GEMM_SIZE_A"])
    gab = int(c["GEMM_SIZE_AB"])
    gb = int(c["GEMM_SIZE_B"])
    dim = int(c.get("DIM", 16))
    sa = int(c.get("SPLIT_A") or c.get("SPLIT", 2))
    sb = int(c.get("SPLIT_B") or c.get("SPLIT", 2))
    cl = int(c.get("CASC_LN_AB") or c.get("CASC_LN", 8))
    wrd = int(c.get("WRD_LN", 8))
    dt = c.get("DATA_TYPE", "int16")
    if dt == "int16":
        wrd = 8
    elif dt == "int32":
        wrd = 4

    pack_unroll = int(c.get("PACK_CASCADE_UNROLL_FACTOR", 4))
    if pack_unroll not in (2, 4, 8):
        pack_unroll = 4

    dim_a = min(dim, ga // max(1, sa))
    dim_ab = gab // max(1, cl)
    dim_b = min(dim, gb // max(1, sb))

    num_a_files = cl
    num_b_files = sb * cl
    num_c_files = sb

    broadcast_count_a = max(1, (gb // sb) // dim_b)
    base_mata_sz = (
        (ga // sa) * (gab // cl) * broadcast_count_a * sa
    ) // wrd
    exact_mata_sz = base_mata_sz * num_a_files

    broadcast_count_b = (ga // sa) // dim_a
    base_matb_sz = (
        (gab // cl) * (gb // sb) * broadcast_count_b * sb
    ) // wrd
    exact_matb_sz = base_matb_sz * num_b_files

    base_matc_sz = (ga * gb) // sb // wrd
    exact_matc_sz = base_matc_sz * num_c_files

    bytes_per_word = 16  # ap_int<128>
    raw_a_words = (ga * gab + wrd - 1) // wrd
    raw_a_bytes = raw_a_words * bytes_per_word

    bb = exact_matb_sz * bytes_per_word
    bc = exact_matc_sz * bytes_per_word
    ba_exact_bytes = exact_mata_sz * bytes_per_word

    # Pipeline / stream proxies (II=1 style trip counts where the HLS uses them):
    # - inp_B: one iteration per EXACT_MATB_SZ (read + distribute).
    # - inp_A: DDR load totals raw_a_words; replay_beats × BROADCAST_COUNT_A per block scales
    #   like EXACT_MATA_SZ (see gemm_config BASE/EXACT and dma_hls broadcast_loop).
    # - out_C: EXACT_MATC_SZ writes (path may add latency beyond II=1).
    trip_b = exact_matb_sz
    trip_a_replay = exact_mata_sz
    trip_c = exact_matc_sz

    print("=== dma_hls datapath estimate (static) ===")
    print(f"Config: {cfg_path}")
    print(f"GEMM: A={ga}×{gab}, B={gab}×{gb}  |  DIM_A={dim_a}, DIM_AB={dim_ab}, DIM_B={dim_b}")
    print(f"PACK_CASCADE_UNROLL_FACTOR={pack_unroll} (inp_A pack_cascades only; see dma_hls.cpp)")
    print()
    print("1) DDR traffic (host buffer sizes & sequential B read length)")
    print(f"  inp_A  — host raw A buffer (m_axi read for load_block_a)  {raw_a_words:>12}  words  {raw_a_bytes / 1e6:>10.3f} MB")
    print(f"  inp_B  — EXACT_MATB_SZ (inp_B read loop)                  {exact_matb_sz:>12}  words  {bb / 1e6:>10.3f} MB")
    print(f"  out_C  — EXACT_MATC_SZ                                    {exact_matc_sz:>12}  words  {bc / 1e6:>10.3f} MB")
    print()
    print("2) On-FPGA / pipeline work proxies (NOT the same as DDR bytes for inp_A)")
    print("  inp_A  does pack_cascades, store_beats, then broadcast_loop replay; replay scale ~ EXACT_MATA_SZ words.")
    print(f"         EXACT_MATA_SZ (replay / cascade-domain size)        {exact_mata_sz:>12}  words  {ba_exact_bytes / 1e6:>10.3f} MB  (not host raw A size)")
    print(f"  inp_B  sequential stream beats                             {trip_b:>12}  (minimal compute per beat)")
    print(f"  out_C  write-side beats                                    {trip_c:>12}")
    print()
    print("Dominance by DDR volume (host + B read):")
    ddr_rank = sorted(
        [
            ("inp_B", bb),
            ("inp_A (raw A only)", raw_a_bytes),
            ("out_C", bc),
        ],
        key=lambda x: -x[1],
    )
    for i, (name, nbytes) in enumerate(ddr_rank, 1):
        print(f"  {i}. {name}: {nbytes / 1e6:.3f} MB")
    print()
    print("Dominance by pipeline trip-count proxy (better for DATAFLOW max(inp_A,inp_B,out_C) reasoning):")
    pipe_rank = sorted(
        [
            ("inp_B", trip_b),
            ("inp_A (EXACT_MATA / replay scale)", trip_a_replay),
            ("out_C", trip_c),
        ],
        key=lambda x: -x[1],
    )
    for i, (name, trips) in enumerate(pipe_rank, 1):
        print(f"  {i}. {name}: {trips} stream-scale beats")
    print()
    print("Notes:")
    print("  - inp_A: DDR bytes are small vs B, but PL still runs heavy pack + replay; timing closure often hits")
    print(f"    packbuf/tiled/store_beats (PACK_CASCADE_UNROLL_FACTOR={pack_unroll}), not only gmem0 bandwidth.")
    print("  - inp_B: largest sequential DDR read; tune BURST_B / OUTSTANDING_B / FIFO_B_DEPTH in dma_hls.cpp; PL_FREQ in config.json.")
    print("  - With DATAFLOW, wall time ~ max(legs); the slowest stage wins — compare both tables above.")
    print()

    mx_ddr = max(("inp_B", bb), ("inp_A", raw_a_bytes), ("out_C", bc), key=lambda x: x[1])
    mx_pipe = max(("inp_B", trip_b), ("inp_A", trip_a_replay), ("out_C", trip_c), key=lambda x: x[1])

    print(f"First target if you only trust DDR sizes: **{mx_ddr[0]}**")
    print(f"First target if you trust stream-scale work (incl. inp_A replay): **{mx_pipe[0]}**")
    ratio_ab = trip_a_replay / trip_b if trip_b else 0.0
    if mx_ddr[0] == "inp_B" and mx_pipe[0] == "inp_B":
        print("  Both views agree: focus B first; still profile inp_A if VPL/timing or stalls point there.")
    elif ratio_ab > 0.35:
        print(
            f"  inp_A replay proxy is {100.0 * ratio_ab:.0f}% of inp_B — optimize B, but treat inp_A as a serious "
            "second target (compute + fabric, not just DDR)."
        )
    print()
    if mx_pipe[0] == "inp_B":
        print("Suggestions (inp_B): BURST_B / OUTSTANDING_B / FIFO_B_DEPTH (dma_hls), PL clock, DDR QoS.")
    elif mx_pipe[0] == "inp_A":
        print("Suggestions (inp_A): PACK_CASCADE_UNROLL_FACTOR, PL_FREQ, pack/store path in csynth; gmem0 bursts for load_block_a.")
    else:
        print("Suggestions (out_C): SIMPLE_OUT_C in config.json; BURST_C / FIFO_C in dma_hls.cpp; reassembly path.")

    print()
    print("For measured stall breakdown (memory vs pipe), rebuild with profiling and open Vitis Analyzer.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
