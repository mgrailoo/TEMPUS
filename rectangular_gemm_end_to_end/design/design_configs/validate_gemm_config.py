#!/usr/bin/env python3
"""
Validate design/design_configs/config.json against the same rules as the Makefile,
dma_hls complex out_C, plioGen, and the AIE graph.

Call from Makefile before generating gemm_config.h. Exit code 1 on failure.

Typical cubic GEMM (GEMM_SIZE_A = GEMM_SIZE_AB = GEMM_SIZE_B = N) with SPLIT_A=SPLIT_B=2:
  - Effective tile rows/cols: DIM_A = min(DIM, N/SPLIT_A), DIM_B = min(DIM, N/SPLIT_B).
  - Block per split must tile exactly: (N/SPLIT_A) % DIM_A == 0 and (N/SPLIT_B) % DIM_B == 0.
  - Graph iterations (per Makefile): (GEMM_SIZE_A * GEMM_SIZE_B / SPLIT_B) % (DIM_A * DIM_B) == 0.
  - Inner dim: GEMM_SIZE_AB % CASC_LN_AB == 0.
  - Sub-tiles: DIM_A % SUB_TILE_A == 0, DIM_AB % SUB_TILE_AB == 0, DIM_B % SUB_TILE_B == 0.
  - PLIO packing: (DIM_A * DIM_B) % WRD_LN == 0.

int32: same tiling rules; WRD_LN=4; SUB_TILE_* from config (default 4×4×4). Makefile still corrects WRD_LN vs DATA_TYPE.
"""
from __future__ import annotations

import argparse
import json
import sys
from typing import List, Tuple


def _int(c: dict, key: str, default=None):
    v = c.get(key, default)
    if v is None:
        return None
    return int(v)


def compute_effective_dims(c: dict) -> Tuple[int, int, int, int, int, int, int, int, int]:
    ga = _int(c, "GEMM_SIZE_A")
    gab = _int(c, "GEMM_SIZE_AB")
    gb = _int(c, "GEMM_SIZE_B")
    if ga is None or gab is None or gb is None:
        raise ValueError("GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B are required")
    split_a = _int(c, "SPLIT_A") or _int(c, "SPLIT") or 2
    split_b = _int(c, "SPLIT_B") or _int(c, "SPLIT") or 2
    casc = _int(c, "CASC_LN_AB") or _int(c, "CASC_LN") or 8
    split_a, split_b, casc = max(1, split_a), max(1, split_b), max(1, casc)
    dim_base = _int(c, "DIM")
    if dim_base is None:
        dim_base = 16
    dim_a = min(dim_base, ga // split_a)
    dim_b = min(dim_base, gb // split_b)
    dim_ab = gab // casc
    return ga, gab, gb, split_a, split_b, casc, dim_a, dim_ab, dim_b


def expected_sub_tiles(data_type: str) -> Tuple[int, int, int]:
    dt = (data_type or "int16").strip().lower()
    if dt in ("int16", "int32"):
        return 4, 4, 4
    raise ValueError(f"Unsupported DATA_TYPE {data_type!r} (use int16 or int32)")


def makefile_resolved_wrd_and_subtiles(c: dict) -> Tuple[int, int, int, int]:
    """
    Match rectangular_gemm_end_to_end/Makefile: WRD_LN and SUB_TILE_B overrides
    so validation matches what gemm_config.h and the graph actually use.
    """
    dt = str(c.get("DATA_TYPE", "int16")).strip().lower()
    exp_wrd = 8 if dt == "int16" else 4 if dt == "int32" else 8
    wrd = _int(c, "WRD_LN")
    if wrd is None:
        wrd = exp_wrd
    elif dt in ("int16", "int32") and wrd != exp_wrd:
        wrd = exp_wrd

    sta = _int(c, "SUB_TILE_A")
    stab = _int(c, "SUB_TILE_AB")
    stb = _int(c, "SUB_TILE_B")
    sta_def, stab_def, stb_def = expected_sub_tiles(dt)
    if sta is None:
        sta = sta_def
    if stab is None:
        stab = stab_def
    if stb is None:
        stb = stb_def
    if dt == "int16" and stb == 2:
        stb = 4
    return sta, stab, stb, wrd


def validate_config_data(c: dict) -> List[str]:
    errors: List[str] = []

    try:
        ga, gab, gb, sa, sb, casc, dim_a, dim_ab, dim_b = compute_effective_dims(c)
    except ValueError as e:
        return [str(e)]

    dt = str(c.get("DATA_TYPE", "int16")).strip().lower()
    if dt not in ("int16", "int32"):
        errors.append(f"DATA_TYPE must be int16 or int32 (got {c.get('DATA_TYPE')!r})")
        return errors

    sta, stab, stb, wrd = makefile_resolved_wrd_and_subtiles(c)

    if ga % sa != 0:
        errors.append(f"GEMM_SIZE_A={ga} must be divisible by SPLIT_A={sa}")
    if gb % sb != 0:
        errors.append(f"GEMM_SIZE_B={gb} must be divisible by SPLIT_B={sb}")
    if gab % casc != 0:
        errors.append(f"GEMM_SIZE_AB={gab} must be divisible by CASC_LN_AB={casc}")

    rows_pb = ga // sa
    cols_pf = gb // sb
    if rows_pb % dim_a != 0:
        errors.append(
            f"Row block (GEMM_SIZE_A/SPLIT_A)={rows_pb} must be divisible by DIM_A={dim_a} "
            f"(DIM={c.get('DIM')}); else out_C tile loops leave gaps"
        )
    if cols_pf % dim_b != 0:
        errors.append(
            f"Column span per C file (GEMM_SIZE_B/SPLIT_B)={cols_pf} must be divisible by DIM_B={dim_b}"
        )

    elems_cfile = ga * cols_pf
    tile_elems = dim_a * dim_b
    if tile_elems <= 0:
        errors.append(f"DIM_A*DIM_B must be positive (got {tile_elems})")
    elif elems_cfile % tile_elems != 0:
        errors.append(
            f"GRAPH_ITER alignment: (GEMM_SIZE_A * GEMM_SIZE_B/SPLIT_B)={elems_cfile} must be "
            f"divisible by (DIM_A*DIM_B)={tile_elems}; Makefile GRAPH_ITER_CNT would truncate"
        )

    if (dim_a * dim_b) % wrd != 0:
        errors.append(f"(DIM_A*DIM_B)={dim_a*dim_b} must be divisible by WRD_LN={wrd} for 128-bit PLIO packing")

    abseg = gab // casc
    if abseg % stab != 0:
        errors.append(
            f"AB segment GEMM_SIZE_AB/CASC_LN_AB={abseg} must be divisible by SUB_TILE_AB={stab}"
        )
    if dim_a % sta != 0:
        errors.append(f"DIM_A={dim_a} must be divisible by SUB_TILE_A={sta}")
    if dim_b % stb != 0:
        errors.append(f"DIM_B={dim_b} must be divisible by SUB_TILE_B={stb}")
    if dim_ab % stab != 0:
        errors.append(f"DIM_AB={dim_ab} must be divisible by SUB_TILE_AB={stab}")

    iter_cnt = _int(c, "ITER_CNT", 1)
    if iter_cnt != -1:
        if tile_elems > 0 and elems_cfile % tile_elems == 0:
            gic = elems_cfile // tile_elems
            if gic < 1:
                errors.append(f"GRAPH_ITER_CNT would be {gic} (< 1)")

    return errors


def validate_config(path: str) -> List[str]:
    with open(path, encoding="utf-8") as f:
        c = json.load(f)
    return validate_config_data(c)


def _cubic_template(
    n: int,
    dim: int,
    split: int,
    casc: int,
    data_type: str,
) -> dict:
    """Minimal config dict; SUB_TILE/WRD_LN follow Makefile defaults for the type."""
    c = {
        "GEMM_SIZE_A": n,
        "GEMM_SIZE_AB": n,
        "GEMM_SIZE_B": n,
        "DIM": dim,
        "SPLIT_A": split,
        "SPLIT_B": split,
        "CASC_LN_AB": casc,
        "DATA_TYPE": data_type,
    }
    if data_type == "int16":
        c["WRD_LN"] = 8
        c["SUB_TILE_A"] = 4
        c["SUB_TILE_AB"] = 4
        c["SUB_TILE_B"] = 4
    else:
        c["WRD_LN"] = 4
        c["SUB_TILE_A"] = 4
        c["SUB_TILE_AB"] = 4
        c["SUB_TILE_B"] = 4
    return c


def suggest_cubic(
    n_list: List[int],
    dim_list: List[int],
    split: int = 2,
    casc: int = 8,
    data_type: str = "int16",
) -> List[str]:
    """
    Print valid (N, DIM) pairs for cubic GEMM. Returns list of error strings for failed (n,dim) cells.
    """
    dt = data_type.strip().lower()
    st = "int16 (WRD_LN=8, 4×4×4)" if dt == "int16" else "int32 (WRD_LN=4, 4×4×4)"
    print(
        f"Cubic GEMM — {st}: N in {n_list}, DIM in {dim_list}, "
        f"SPLIT_A=SPLIT_B={split}, CASC_LN_AB={casc}\n"
    )
    failures: List[str] = []
    for n in n_list:
        ok_dims = []
        for dim in dim_list:
            c = _cubic_template(n, dim, split, casc, dt)
            errs = validate_config_data(c)
            if not errs:
                ok_dims.append(dim)
            else:
                failures.append(f"N={n} DIM={dim} ({dt}): " + "; ".join(errs))
        print(f"  N={n:4d}: valid DIM -> {ok_dims if ok_dims else '(none)'}")
    print(
        "\nDIM > N/2 is clamped to N/2 for DIM_A/DIM_B; all listed sizes are powers of two so "
        "tiling and GRAPH_ITER_CNT stay exact."
    )
    return failures


def main():
    ap = argparse.ArgumentParser(description="Validate GEMM config.json for build consistency")
    ap.add_argument("config", nargs="?", default="design/design_configs/config.json", help="Path to config.json")
    ap.add_argument(
        "--suggest-cubic",
        action="store_true",
        help="Print (N,DIM) tables for int16 and int32 (reference grid 32..1024 x DIM 16..256)",
    )
    args = ap.parse_args()

    if args.suggest_cubic:
        n_list = [32, 64, 128, 256, 512, 1024]
        dim_list = [16, 32, 64, 128, 256]
        f16 = suggest_cubic(n_list, dim_list, data_type="int16")
        print()
        f32 = suggest_cubic(n_list, dim_list, data_type="int32")
        bad = f16 + f32
        if bad:
            print("\nUnexpected failures in reference grid:", file=sys.stderr)
            for b in bad:
                print(f"  {b}", file=sys.stderr)
            return 1
        print("\nReference grid: all (N, DIM) valid for both int16 and int32 under SPLIT=2, CASC_LN_AB=8.")
        return 0

    errs = validate_config(args.config)
    if errs:
        print("validate_gemm_config.py: configuration errors:", file=sys.stderr)
        for e in errs:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"validate_gemm_config.py: OK ({args.config})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
