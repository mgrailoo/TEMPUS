#!/usr/bin/bash

# ============================================================================
# AI Engine GEMM Local Workflow Script (Ubuntu/Linux)
# ============================================================================
# This script provides a local Ubuntu/Linux workflow to build and run the AIE
# GEMM design without any PuTTY/Windows dependencies. It relies on the local
# Xilinx Vitis 2024.1 tool installation.
#
# Key actions:
# - Load/save centralized config (design/design_configs/config.json)
# - Derive WRD_LN, SUB_TILE_*, GRAPH_ITER_CNT from config
# - Build (make all/run) and show logs
# - Run hw_emu launcher and application
# - Generate golden IO and compare outputs
#
# Prerequisites:
# - Vitis 2024.1 installed locally and sourced (settings64.sh)
# - jq (preferred) or Python 3 for JSON parsing
# - make, bash, python3
# ============================================================================

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENV_SCRIPT="$ROOT_DIR/Mahdieh_env_setup.sh"
# Source environment setup for local tools/root (temporarily relax nounset and errexit)
if [[ -f "$ENV_SCRIPT" ]]; then
    _nounset_was_on=0
    _errexit_was_on=0
    if [[ $- == *u* ]]; then
        _nounset_was_on=1
        set +u
    fi
    if [[ $- == *e* ]]; then
        _errexit_was_on=1
        set +e
    fi
    # shellcheck disable=SC1091
    source "$ENV_SCRIPT"
    if [[ $_nounset_was_on -eq 1 ]]; then
        set -u
    fi
    if [[ $_errexit_was_on -eq 1 ]]; then
        set -e
    fi
else
    echo "[ERROR] Mahdieh_env_setup.sh not found in $ROOT_DIR"
    exit 1
fi
CONFIG_PATH="$ROOT_DIR/design/design_configs/config.json"
LOG_DIR="$ROOT_DIR/logs"
REPORTS_DIR="$ROOT_DIR/reports"
AIE_DATA_DIR="$ROOT_DIR/design/aie_src/aiesim_data"
BUILD_ROOT="$ROOT_DIR/build"

mkdir -p "$LOG_DIR" "$REPORTS_DIR"

have_jq() { command -v jq >/dev/null 2>&1; }

# ---------- JSON helpers ----------
json_get() {
    local key="$1"
    if have_jq && [[ -f "$CONFIG_PATH" ]]; then
        jq -r --arg k "$key" '.[$k] // empty' "$CONFIG_PATH" 2>/dev/null || true
    else
        python3 - "$key" "$CONFIG_PATH" << 'PY'
import json, sys
key=sys.argv[1]
path=sys.argv[2]
try:
    with open(path,'r',encoding='utf-8') as f:
        obj=json.load(f)
    v=obj.get(key, '')
    if v is None:
        v=''
    print(v)
except Exception:
    pass
PY
    fi
}

json_set_many() {
    # Usage: json_set_many KEY VALUE [KEY VALUE]...
    if ! [[ -f "$CONFIG_PATH" ]]; then echo "{}" > "$CONFIG_PATH"; fi
    if have_jq; then
        local tmp
        tmp="$(mktemp)"
        cp "$CONFIG_PATH" "$tmp"
        while (("$#">1)); do
            local k="$1"; shift
            local v="$1"; shift
            # Heuristic: if numeric, store as number else string
            if [[ "$v" =~ ^-?[0-9]+(\.[0-9]+)?$ ]]; then
                jq --arg k "$k" --argjson v "$v" '.[$k]=$v' "$tmp" > "$tmp.j" && mv "$tmp.j" "$tmp"
            else
                jq --arg k "$k" --arg v "$v" '.[$k]=$v' "$tmp" > "$tmp.j" && mv "$tmp.j" "$tmp"
            fi
        done
        mv "$tmp" "$CONFIG_PATH"
    else
        python3 - "$CONFIG_PATH" "$@" << 'PY'
import json, sys, os
path=sys.argv[1]
pairs=sys.argv[2:]
try:
    if os.path.exists(path):
        with open(path,'r',encoding='utf-8') as f:
            obj=json.load(f)
    else:
        obj={}
except Exception:
    obj={}
for i in range(0,len(pairs),2):
    k=pairs[i]
    v=pairs[i+1]
    try:
        if isinstance(v,str) and (v.replace('.','',1).isdigit() or (v.startswith('-') and v[1:].replace('.','',1).isdigit())):
            if '.' in v:
                obj[k]=float(v)
            else:
                obj[k]=int(v)
        else:
            obj[k]=v
    except Exception:
        obj[k]=v
with open(path,'w',encoding='utf-8') as f:
    json.dump(obj,f,indent=2)
PY
    fi
}

# ---------- Derived parameters ----------
get_wrd_ln() {
    local dt="$1"
    case "$dt" in
        int16) echo 8;;
        int32|float) echo 4;;
        *) echo 8;;
    esac
}

get_subtiles() {
    local dt="$1"
    case "$dt" in
        int16) echo "4 4 4";;
        int32|float) echo "4 4 2";;
        *) echo "4 4 4";;
    esac
}

ensure_defaults() {
    local TARGET GEMM_SIZE_A GEMM_SIZE_AB GEMM_SIZE_B DIM DATA_TYPE TILE_MEM_BYTES SPLIT CASC_LN ITER_CNT N_SAMPLES GEMM_INSTS EN_TRACE PL_FREQ ENABLE_ML_BENCHMARKS
    TARGET="$(json_get TARGET)"; TARGET="${TARGET:-hw}"
    # GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B are REQUIRED - must be explicitly provided
    GEMM_SIZE_A="$(json_get GEMM_SIZE_A)"
    if [ -z "$GEMM_SIZE_A" ]; then
        echo "ERROR: GEMM_SIZE_A is required in config.json. Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B." >&2
        exit 1
    fi
    GEMM_SIZE_AB="$(json_get GEMM_SIZE_AB)"
    if [ -z "$GEMM_SIZE_AB" ]; then
        echo "ERROR: GEMM_SIZE_AB is required in config.json. Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B." >&2
        exit 1
    fi
    GEMM_SIZE_B="$(json_get GEMM_SIZE_B)"
    if [ -z "$GEMM_SIZE_B" ]; then
        echo "ERROR: GEMM_SIZE_B is required in config.json. Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B." >&2
        exit 1
    fi
    DIM="$(json_get DIM)"; DIM="${DIM:-16}"
    DATA_TYPE="$(json_get DATA_TYPE)"; DATA_TYPE="${DATA_TYPE:-int16}"
    TILE_MEM_BYTES="$(json_get TILE_MEM_BYTES)"; TILE_MEM_BYTES="${TILE_MEM_BYTES:-32768}"
    SPLIT="$(json_get SPLIT)"; SPLIT="${SPLIT:-2}"
    CASC_LN="$(json_get CASC_LN)"; CASC_LN="${CASC_LN:-8}"
    ITER_CNT="$(json_get ITER_CNT)"; ITER_CNT="${ITER_CNT:-1}"
    N_SAMPLES="$(json_get N_SAMPLES)"; N_SAMPLES="${N_SAMPLES:-1}"
    GEMM_INSTS="$(json_get GEMM_INSTS)"; GEMM_INSTS="${GEMM_INSTS:-1}"
    EN_TRACE="$(json_get EN_TRACE)"; EN_TRACE="${EN_TRACE:-0}"
    PL_FREQ="$(json_get PL_FREQ)"; PL_FREQ="${PL_FREQ:-312.5}"
    ENABLE_ML_BENCHMARKS="$(json_get ENABLE_ML_BENCHMARKS)"; ENABLE_ML_BENCHMARKS="${ENABLE_ML_BENCHMARKS:-1}"
    json_set_many \
      TARGET "$TARGET" GEMM_SIZE_A "$GEMM_SIZE_A" GEMM_SIZE_AB "$GEMM_SIZE_AB" GEMM_SIZE_B "$GEMM_SIZE_B" DIM "$DIM" DATA_TYPE "$DATA_TYPE" \
      TILE_MEM_BYTES "$TILE_MEM_BYTES" SPLIT "$SPLIT" CASC_LN "$CASC_LN" \
      ITER_CNT "$ITER_CNT" N_SAMPLES "$N_SAMPLES" GEMM_INSTS "$GEMM_INSTS" \
      EN_TRACE "$EN_TRACE" PL_FREQ "$PL_FREQ" ENABLE_ML_BENCHMARKS "$ENABLE_ML_BENCHMARKS"
}

ensure_derived() {
    ensure_defaults
    local DATA_TYPE WRD_LN SUB_TILE_M SUB_TILE_K SUB_TILE_N GEMM_SIZE_A GEMM_SIZE_AB GEMM_SIZE_B DIM SPLIT_B ITER_CNT
    DATA_TYPE="$(json_get DATA_TYPE)"; GEMM_SIZE_A="$(json_get GEMM_SIZE_A)"; GEMM_SIZE_AB="$(json_get GEMM_SIZE_AB)"; GEMM_SIZE_B="$(json_get GEMM_SIZE_B)"; DIM="$(json_get DIM)"
    SPLIT_B="$(json_get SPLIT_B)"; SPLIT_B="${SPLIT_B:-$(json_get SPLIT)}"; SPLIT_B="${SPLIT_B:-2}"
    ITER_CNT="$(json_get ITER_CNT)"
    WRD_LN="$(json_get WRD_LN)"
    SUB_TILE_M="$(json_get SUB_TILE_M)"
    SUB_TILE_K="$(json_get SUB_TILE_K)"
    SUB_TILE_N="$(json_get SUB_TILE_N)"
    if [[ -z "${WRD_LN:-}" ]]; then WRD_LN="$(get_wrd_ln "$DATA_TYPE")"; fi
    if [[ -z "${SUB_TILE_M:-}" || -z "${SUB_TILE_K:-}" || -z "${SUB_TILE_N:-}" ]]; then
        read -r SUB_TILE_M SUB_TILE_K SUB_TILE_N < <(get_subtiles "$DATA_TYPE")
    fi
    local GRAPH_ITER_CNT
    if [[ "${ITER_CNT}" == "-1" ]]; then
        GRAPH_ITER_CNT=-1
    else
        local DIM_A DIM_B
        DIM_A="$(json_get DIM_A)"; DIM_A="${DIM_A:-$DIM}"
        DIM_B="$(json_get DIM_B)"; DIM_B="${DIM_B:-$DIM}"
        local den=$(( DIM_A * DIM_B * (SPLIT_B>0?SPLIT_B:1) ))
        (( den == 0 )) && den=1
        GRAPH_ITER_CNT=$(( (GEMM_SIZE_A * GEMM_SIZE_B) / den ))
    fi
    json_set_many WRD_LN "$WRD_LN" SUB_TILE_M "$SUB_TILE_M" SUB_TILE_K "$SUB_TILE_K" SUB_TILE_N "$SUB_TILE_N" GRAPH_ITER_CNT "$GRAPH_ITER_CNT"
}

show_config() {
    ensure_derived
    echo "========================================"
    echo "Current configuration ($CONFIG_PATH):"
    if have_jq; then
        jq . "$CONFIG_PATH" || cat "$CONFIG_PATH"
    else
        cat "$CONFIG_PATH"
    fi
    echo "========================================"
}

require_vitis() {
    if ! command -v vitis >/dev/null 2>&1 && ! command -v v++ >/dev/null 2>&1; then
        echo "[WARN] Vitis tools not on PATH. Attempting to source settings64.sh..."
        if [[ -f "/opt/Xilinx/Vitis/2024.1/settings64.sh" ]]; then
            # shellcheck disable=SC1091
            source "/opt/Xilinx/Vitis/2024.1/settings64.sh"
        fi
    fi
    if ! command -v v++ >/dev/null 2>&1; then
        echo "[ERROR] Vitis tools not found. Please source settings64.sh." >&2
        exit 1
    fi
}

build_project() {
    ensure_derived
    require_vitis
    local TARGET GEMM_SIZE_A GEMM_SIZE_AB GEMM_SIZE_B DIM ENABLE_ML_BENCHMARKS EN_TRACE ITER_CNT
    TARGET="$(json_get TARGET)"; GEMM_SIZE_A="$(json_get GEMM_SIZE_A)"; GEMM_SIZE_AB="$(json_get GEMM_SIZE_AB)"; GEMM_SIZE_B="$(json_get GEMM_SIZE_B)"; DIM="$(json_get DIM)"
    ENABLE_ML_BENCHMARKS="$(json_get ENABLE_ML_BENCHMARKS)"; EN_TRACE="$(json_get EN_TRACE)"; ITER_CNT="$(json_get ITER_CNT)"
    echo "[INFO] Starting build: TARGET=$TARGET GEMM_SIZE_A=$GEMM_SIZE_A GEMM_SIZE_AB=$GEMM_SIZE_AB GEMM_SIZE_B=$GEMM_SIZE_B DIM=$DIM"
    (
        cd "$ROOT_DIR"
        if [[ "$TARGET" == "hw" ]]; then
            make all TARGET="$TARGET" ITER_CNT="$ITER_CNT" EN_TRACE="$EN_TRACE" GEMM_SIZE_A="$GEMM_SIZE_A" GEMM_SIZE_AB="$GEMM_SIZE_AB" GEMM_SIZE_B="$GEMM_SIZE_B" DIM="$DIM" ENABLE_ML_BENCHMARKS="$ENABLE_ML_BENCHMARKS" 2>&1 | tee "$LOG_DIR/build_output.log"
        else
            make run TARGET="$TARGET" ITER_CNT="$ITER_CNT" EN_TRACE="$EN_TRACE" GEMM_SIZE_A="$GEMM_SIZE_A" GEMM_SIZE_AB="$GEMM_SIZE_AB" GEMM_SIZE_B="$GEMM_SIZE_B" DIM="$DIM" ENABLE_ML_BENCHMARKS="$ENABLE_ML_BENCHMARKS" 2>&1 | tee "$LOG_DIR/build_output.log"
        fi
    )
    echo "[INFO] Build finished. Log: $LOG_DIR/build_output.log"
}

run_hw_emu() {
    ensure_derived
    require_vitis
    local GEMM_SIZE_A GEMM_SIZE_AB GEMM_SIZE_B TARGET pkg_dir
    GEMM_SIZE_A="$(json_get GEMM_SIZE_A)"; GEMM_SIZE_AB="$(json_get GEMM_SIZE_AB)"; GEMM_SIZE_B="$(json_get GEMM_SIZE_B)"
    TARGET="$(json_get TARGET)"
    pkg_dir="$BUILD_ROOT/gemm_${GEMM_SIZE_A}x${GEMM_SIZE_AB}x${GEMM_SIZE_B}/x1/${TARGET}/package"
    if [[ ! -d "$pkg_dir" ]]; then
        echo "[ERROR] Package directory not found: $pkg_dir"
        echo "Build first."
        return 1
    fi
    (
        cd "$pkg_dir"
        if [[ -x ./launch_hw_emu.sh ]]; then
            ./launch_hw_emu.sh -run-app ./gemm_aie_xrt.elf a.xclbin | tee "$LOG_DIR/hw_emu_run.log"
        else
            echo "[ERROR] launch_hw_emu.sh not found in $pkg_dir"
            return 1
        fi
    )
}

list_xclbin() {
    ensure_derived
    local GEMM_SIZE_A GEMM_SIZE_AB GEMM_SIZE_B TARGET pkg_dir
    GEMM_SIZE_A="$(json_get GEMM_SIZE_A)"; GEMM_SIZE_AB="$(json_get GEMM_SIZE_AB)"; GEMM_SIZE_B="$(json_get GEMM_SIZE_B)"
    TARGET="$(json_get TARGET)"
    pkg_dir="$BUILD_ROOT/gemm_${GEMM_SIZE_A}x${GEMM_SIZE_AB}x${GEMM_SIZE_B}/x1/${TARGET}/package"
    if [[ -f "$pkg_dir/a.xclbin" ]]; then
        echo "[INFO] XCLBIN info:"
        if command -v xclbinutil >/dev/null 2>&1; then
            xclbinutil --info --input "$pkg_dir/a.xclbin"
        else
            echo "xclbinutil not found on PATH."
        fi
    else
        echo "[ERROR] a.xclbin not found at $pkg_dir"
    fi
}

generate_golden_and_compare() {
    ensure_derived
    local GEN_DIR="$AIE_DATA_DIR"
    (
        cd "$GEN_DIR"
        echo "[INFO] Generating golden IO (plioGen.py)..."
        if command -v python3 >/dev/null 2>&1; then
            python3 plioGen.py > plioGen.log 2>&1 || {
                echo "plioGen.py failed (see $GEN_DIR/plioGen.log)"; return 1; }
            echo "[INFO] Comparing outputs (compare_outputs.py)..."
            python3 compare_outputs.py > compare_outputs.log 2>&1 && echo "[OK] Outputs match" || echo "[WARN] Differences found. Check compare_outputs.log"
        else
            echo "[ERROR] python3 not found"
            return 1
        fi
    )
}

# ---------- New: local clean and metrics extraction ----------
clean_local() {
    echo "[INFO] Cleaning local generated files..."
    local paths=(
        "$AIE_DATA_DIR/gemm_$(json_get GEMM_SIZE_A)x$(json_get GEMM_SIZE_AB)x$(json_get GEMM_SIZE_B)_ioFiles"
        "$AIE_DATA_DIR/c.txt"
        "$AIE_DATA_DIR/log.txt"
        "$AIE_DATA_DIR/plioGen.log"
        "$AIE_DATA_DIR/build_log.txt"
        "$AIE_DATA_DIR/compare_outputs.log"
        "$ROOT_DIR/build_log.txt"
        "$BUILD_ROOT"
        "$LOG_DIR/build_output.log"
    )
    for p in "${paths[@]}"; do
        if [[ -e "$p" ]]; then
            if [[ -d "$p" ]]; then
                rm -rf "$p" && echo "Removed directory: $p"
            else
                rm -f "$p" && echo "Removed file: $p"
            fi
        fi
    done
    echo "[INFO] Local cleanup completed."
}

extract_metrics() {
    echo "[INFO] Extracting metrics from reports..."
    local reports_root="$ROOT_DIR/reports"
    local summary="$ROOT_DIR/metrics_summary.txt"
    
    # Step 1: Run PowerShell script first (it generates formatted summary)
    if command -v pwsh >/dev/null 2>&1 && [[ -f "$ROOT_DIR/scripts/extract_metrics.ps1" ]]; then
        cd "$ROOT_DIR" || return 1
        pwsh -NoLogo -NoProfile -File "$ROOT_DIR/scripts/extract_metrics.ps1" || true
        cd - >/dev/null || true
    fi
    
    # Step 2: Append detailed report file contents if PowerShell output exists, otherwise create new
    if [[ -f "$summary" && -s "$summary" ]]; then
        # PowerShell already wrote content, append detailed reports
        {
            echo ""
            echo "==============================================================================="
            echo "DETAILED REPORT CONTENTS"
            echo "==============================================================================="
            echo ""
        } >> "$summary"
    else
        # No PowerShell output, create header
    {
        echo "==== Metrics Summary ($(date)) ===="
            echo ""
        } > "$summary"
    fi
    
    # Step 3: Append detailed report file contents
    {
        # Try to find reports in subdirectories (gemm_32x32x32/x1/)
        local found_reports_dir=""
        if [[ -d "$reports_root/gemm_32x32x32/x1" ]]; then
            found_reports_dir="$reports_root/gemm_32x32x32/x1"
        elif [[ -d "$reports_root" ]]; then
            # Find first subdirectory with reports
            found_reports_dir=$(find "$reports_root" -maxdepth 2 -type d -name "x1" | head -1)
            if [[ -z "$found_reports_dir" ]]; then
                found_reports_dir="$reports_root"
            fi
        fi
        
        if [[ -n "$found_reports_dir" && -d "$found_reports_dir" ]]; then
            echo "Extracting from: $found_reports_dir"
            echo ""
            # Core metrics files
        for f in \
                "$found_reports_dir/utilization_report.txt" \
                "$found_reports_dir/timing_summary.txt" \
                "$found_reports_dir/power_report.txt" \
                "$found_reports_dir/memory_power.txt" \
                "$found_reports_dir/memory_utilization.txt" \
                "$found_reports_dir/resources_by_type.txt" \
                "$found_reports_dir/resources_by_clock.txt" \
                "$found_reports_dir/aie_utilization.txt" \
                "$found_reports_dir/dsp_utilization.txt"; do
            if [[ -f "$f" ]]; then
                    echo "=================================================================================="
                echo "-- $(basename "$f") --"
                    echo "=================================================================================="
                    head -n 50 "$f" || true
                    echo ""
                echo ""
            fi
        done
        else
            echo "Warning: No reports directory found at $found_reports_dir"
            echo "Searched in: $reports_root"
        fi
    } >> "$summary"
    
    local line_count=$(wc -l < "$summary" 2>/dev/null || echo "0")
    echo "[INFO] Wrote $summary ($line_count lines)"
}

# ---------- New: Sync comprehensive logs and reports (with Vivado generation) ----------
sync_comprehensive_reports() {
    ensure_derived
    require_vitis
    local GEMM_SIZE_A GEMM_SIZE_AB GEMM_SIZE_B TARGET build_dir reports_dir pkg_dir
    GEMM_SIZE_A="$(json_get GEMM_SIZE_A)"; GEMM_SIZE_AB="$(json_get GEMM_SIZE_AB)"; GEMM_SIZE_B="$(json_get GEMM_SIZE_B)"
    TARGET="$(json_get TARGET)"
    build_dir="$BUILD_ROOT/gemm_${GEMM_SIZE_A}x${GEMM_SIZE_AB}x${GEMM_SIZE_B}/x1/${TARGET}"
    reports_dir="$REPORTS_DIR"
    pkg_dir="$build_dir/package"

    mkdir -p "$LOG_DIR" "$reports_dir"

    # 1) Generate Vivado reports if project exists
    local prj_file="$build_dir/_x/link/vivado/vpl/prj/prj.xpr"
    local vivado_out_dir="$ROOT_DIR/reports/gemm_${GEMM_SIZE_A}x${GEMM_SIZE_AB}x${GEMM_SIZE_B}/x1"
    if [[ -f "$prj_file" ]]; then
        mkdir -p "$vivado_out_dir"
        echo "[INFO] Generating Vivado reports..."
        VIVADO_PROJECT_FILE="$prj_file" VIVADO_REPORTS_DIR="$vivado_out_dir" \
            vivado -mode batch -source "$ROOT_DIR/design/vivado_metrics_scripts/report_metrics.tcl" 2>&1 | tee "$LOG_DIR/vivado_reports.log" || true
    else
        echo "[WARN] Vivado project not found: $prj_file"
    fi

    # 2) Copy top-level useful files from build dir
    local top_patterns=("*.log" "*link_summary*" "*.xclbin.info" "*.compile_summary" "*system_estimate*.xtxt" "*guidance*.html")
    for pat in "${top_patterns[@]}"; do
        for f in "$build_dir"/$pat; do
            [[ -e "$f" ]] || continue
            cp -f "$f" "$reports_dir/" 2>/dev/null || true
        done
    done

    # 3) Copy _x/reports recursively
    if [[ -d "$build_dir/_x/reports" ]]; then
        cp -r "$build_dir/_x/reports" "$reports_dir/" 2>/dev/null || true
        if [[ -d "$build_dir/_x/reports/dma_hls.hw" ]]; then
            mkdir -p "$reports_dir/dma_hls.hw"
            cp -r "$build_dir/_x/reports/dma_hls.hw/"* "$reports_dir/dma_hls.hw/" 2>/dev/null || true
        fi
    fi

    # 4) Copy HLS vitis_hls.log and hls_reports
    local hls_root="$build_dir/_x/dma_hls.${TARGET}/dma_hls"
    if [[ -f "$hls_root/vitis_hls.log" ]]; then
        cp -f "$hls_root/vitis_hls.log" "$LOG_DIR/vitis_hls.log" 2>/dev/null || true
    fi
    if [[ -d "$hls_root/hls_reports" ]]; then
        mkdir -p "$reports_dir/hls"
        cp -r "$hls_root/hls_reports/"* "$reports_dir/hls/" 2>/dev/null || true
    fi

    # 5) Copy AIE Work/reports and key logs
    if [[ -d "$build_dir/Work/reports" ]]; then
        cp -r "$build_dir/Work/reports" "$reports_dir/" 2>/dev/null || true
    fi
    for f in \
        "$build_dir/Work/graph.aiecompile_summary" \
        "$build_dir/Work/aie/active_cores.json" \
        "$build_dir/Work/aie/AddressSpace.txt" \
        "$build_dir/Work/aie/AliasAnalysisReport.txt" \
        "$build_dir/aiecompiler.log"; do
        [[ -f "$f" ]] && cp -f "$f" "$LOG_DIR/" 2>/dev/null || true
    done
    if [[ -f "$build_dir/Work/aie/Makefile" ]]; then
        cp -f "$build_dir/Work/aie/Makefile" "$LOG_DIR/aie_Makefile" 2>/dev/null || true
    fi

    # 6) Copy additional AIE logs and outputs
    find "$build_dir" -maxdepth 3 -type f \( -name "*.vcd" -o -name "*.xpe" -o -name "*.txt" -o -name "*.json" -o -name "*.csv" \) -print 2>/dev/null | while read -r f; do
        base="$(basename "$f")"
        case "$base" in
            *.log) cp -f "$f" "$LOG_DIR/" 2>/dev/null || true ;;
            *) cp -f "$f" "$reports_dir/" 2>/dev/null || true ;;
        esac
    done

    # 7) Copy runtime profiling artifacts from package
    if [[ -d "$pkg_dir" ]]; then
        [[ -f "$pkg_dir/xrt.run_summary" ]] && cp -f "$pkg_dir/xrt.run_summary" "$reports_dir/" 2>/dev/null || true
        for pat in "*profile*.csv" "*timeline*.csv" "*trace*.csv" "*aie*profile*.csv" "*device*trace*.csv"; do
            for f in "$pkg_dir"/$pat; do
                [[ -e "$f" ]] || continue
                cp -f "$f" "$reports_dir/" 2>/dev/null || true
            done
        done
    fi

    # 8) Copy build_output.log if exists at root/build logs
    [[ -f "$ROOT_DIR/build_output.log" ]] && cp -f "$ROOT_DIR/build_output.log" "$LOG_DIR/" 2>/dev/null || true

    echo "[INFO] Sync complete. Reports at: $reports_dir ; Logs at: $LOG_DIR"
}

edit_config() {
    ensure_defaults
    if [[ -n "${EDITOR:-}" ]]; then
        "$EDITOR" "$CONFIG_PATH"
    else
        nano "$CONFIG_PATH" || vi "$CONFIG_PATH"
    fi
}

set_param_prompt() {
    local key="$1" cur
    cur="$(json_get "$key")"
    read -r -p "Enter $key (current: ${cur:-unset}): " val || true
    if [[ -n "${val:-}" ]]; then
        json_set_many "$key" "$val"
    fi
}

configure_menu() {
    while true; do
        echo ""
        echo "=== Configure Settings ==="
        echo "1) Set GEMM_SIZE_A"
        echo "2) Set GEMM_SIZE_AB"
        echo "3) Set GEMM_SIZE_B"
        echo "4) Set DATA_TYPE (int16|int32|float)"
        echo "5) Set DIM"
        echo "6) Set TARGET (hw|hw_emu)"
        echo "7) Auto-derive WRD_LN and sub-tiles"
        echo "8) Edit full config (editor)"
        echo "Q) Back"
        read -r -p "Select option: " opt
        case "${opt^^}" in
            1) set_param_prompt GEMM_SIZE_A;;
            2) set_param_prompt GEMM_SIZE_AB;;
            3) set_param_prompt GEMM_SIZE_B;;
            4) set_param_prompt DATA_TYPE;;
            5) set_param_prompt DIM;;
            6) set_param_prompt TARGET;;
            7) ensure_derived; echo "Derived fields updated.";;
            Q) break;;
            *) echo "Invalid option";;
        esac
    done
}

main_menu() {
    while true; do
        echo ""
        echo "Local Workflow Menu (Ubuntu)"
        echo "1) Configure settings"
        echo "2) Clean build (local)"
        echo "3) Show current configuration"
        echo "4) Build (make all/run)"
        echo "5) Run hardware emulator (launch + run app)"
        echo "6) List XCLBIN contents"
        echo "7) Generate golden IO and compare"
        echo "8) Extract metrics from reports"
        echo "9) Sync comprehensive logs and reports (includes Vivado report generation)"
        echo "10) Exit"
        read -r -p "Enter choice: " choice
        case "$choice" in
            1) configure_menu ;;
            2) clean_local ;;
            3) show_config ;;
            4) build_project ;;
            5) run_hw_emu ;;
            6) list_xclbin ;;
            7) generate_golden_and_compare ;;
            8) extract_metrics ;;
            9) sync_comprehensive_reports ;;
            10) exit 0 ;;
            *) echo "Invalid choice" ;;
        esac
    done
}

# Entry
ensure_derived
main_menu
