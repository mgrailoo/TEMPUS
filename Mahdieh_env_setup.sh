#!/usr/bin/bash

# ============================================================================
# AI Engine GEMM Development Environment Setup Script
# ============================================================================
# Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: MIT
#
# This script sets up the development environment for AI Engine GEMM
# applications on Versal ACAP platforms. It configures all necessary
# environment variables, paths, and tool settings for the development
# workflow.
#
# Key Features:
# - Xilinx Vitis 2024.1 toolchain configuration
# - Versal common image setup for cross-compilation
# - XRT (Xilinx Runtime) configuration
# - DSP Library path configuration
# - Platform selection and validation
# - Tool installation verification
#
# Usage:
#   source ./Mahdieh_env_setup.sh
#
# Prerequisites:
# - Xilinx Vitis 2024.1 installed
# - Versal common image downloaded
# - XRT installed and configured
# - DSP Library available
#
# Environment Variables Set:
# - PLATFORM_REPO_PATHS: Base platforms directory
# - XILINX_VITIS: Vitis installation path
# - COMMON_IMAGE_VERSAL: Versal common image path
# - XILINX_XRT: XRT installation path
# - DSPLIB_VITIS: DSP Library path
# - PLATFORM: Selected platform file
# - PATH: Updated with tool paths
# - CXX: Cross-compiler for A72 processor
# - SDKTARGETSYSROOT: Target system root
# ============================================================================
unset LD_LIBRARY_PATH
# Determine workspace root (directory of this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Optional user tools root (provided by user)
TOOLS_ROOT_DEFAULT="/home/mahdieh/data/tools"
TOOLS_ROOT="${TOOLS_ROOT:-$TOOLS_ROOT_DEFAULT}"

# Discovered preferred paths from system scan
PREF_VITIS_DIR="$TOOLS_ROOT/Xilinx_backup_20250917_144025/Vitis/2024.1"
PREF_VIVADO_DIR="$TOOLS_ROOT/Xilinx_backup_20250917_144025/Vivado/2024.1"
PREF_VITIS_HLS_DIR="$TOOLS_ROOT/Xilinx_backup_20250917_144025/Vitis_HLS/2024.1"
PREF_MODEL_COMPOSER_DIR="$TOOLS_ROOT/Xilinx_backup_20250917_144025/Model_Composer/2024.1"
PREF_PDM_DIR="$TOOLS_ROOT/Xilinx_backup_20250917_144025/PDM/2024.1"
PREF_BASE_PLATFORMS="$TOOLS_ROOT/Xilinx_backup_20250917_144025/Vitis/2024.1/base_platforms"
PREF_COMMON_IMAGE_XILINX="$TOOLS_ROOT/xilinx/2024.1/versal_common"
PREF_COMMON_IMAGE_PETALINUX="$TOOLS_ROOT/petalinux/2024.1"
PREF_VITIS_LIBS="$TOOLS_ROOT/Vitis_Libraries"

# ============================================================================
# XILINX TOOLCHAIN CONFIGURATION
# ============================================================================
# Prefer discovered paths, then fall back to generic locations
# CRITICAL: Force use of backup Vitis installation (matches Vivado version)
if [ -d "$PREF_VITIS_DIR" ]; then
    export XILINX_VITIS="$PREF_VITIS_DIR"
    echo "[INFO] Using preferred Vitis: $PREF_VITIS_DIR"
elif [ -d "$TOOLS_ROOT/Vitis/2024.1" ]; then
    # Only use this if backup doesn't exist
    export XILINX_VITIS="$TOOLS_ROOT/Vitis/2024.1"
    echo "[WARN] Using fallback Vitis: $TOOLS_ROOT/Vitis/2024.1 (backup not found)"
elif [ -d "$TOOLS_ROOT/Vitis" ]; then
    export XILINX_VITIS="$TOOLS_ROOT/Vitis"
else
    export XILINX_VITIS="${XILINX_VITIS:-/opt/Xilinx/Vitis/2024.1}"
fi

if [ -d "$PREF_BASE_PLATFORMS" ]; then
    export PLATFORM_REPO_PATHS="$PREF_BASE_PLATFORMS"
elif [ -d "$TOOLS_ROOT/base_platforms" ]; then
    export PLATFORM_REPO_PATHS="$TOOLS_ROOT/base_platforms"
else
    export PLATFORM_REPO_PATHS="${PLATFORM_REPO_PATHS:-/opt/Xilinx/Vitis/2024.1/base_platforms}"
fi

# Versal common image directory (directory containing environment-setup file)
if [ -f "$PREF_COMMON_IMAGE_XILINX/environment-setup-cortexa72-cortexa53-xilinx-linux" ]; then
    export COMMON_IMAGE_VERSAL="$PREF_COMMON_IMAGE_XILINX"
elif [ -f "$PREF_COMMON_IMAGE_PETALINUX/environment-setup-cortexa72-cortexa53-xilinx-linux" ]; then
    export COMMON_IMAGE_VERSAL="$PREF_COMMON_IMAGE_PETALINUX"
elif [ -d "$TOOLS_ROOT/xilinx-versal-common-v2024.1" ]; then
    export COMMON_IMAGE_VERSAL="$TOOLS_ROOT/xilinx-versal-common-v2024.1"
elif [ -d "$TOOLS_ROOT/versal_common_image" ]; then
    export COMMON_IMAGE_VERSAL="$TOOLS_ROOT/versal_common_image"
else
    export COMMON_IMAGE_VERSAL="${COMMON_IMAGE_VERSAL:-$SCRIPT_DIR/versal_common_image}"
fi

# ============================================================================
# XRT CONFIGURATION
# ============================================================================
# Configure Xilinx Runtime (XRT) for host-device communication
if [ -d "$TOOLS_ROOT/xrt" ]; then
    export XILINX_XRT="$TOOLS_ROOT/xrt"
else
    export XILINX_XRT="${XILINX_XRT:-/opt/xilinx/xrt}"
fi
export PATH=$XILINX_XRT/bin:$XILINX_VITIS/bin:$PATH

# ============================================================================
# ENVIRONMENT SETUP AND TOOLCHAIN SOURCING
# ============================================================================
# Source Versal Image, Vitis, and AIE tools

# Source Versal common image environment setup if available
if [ -f "$COMMON_IMAGE_VERSAL/environment-setup-cortexa72-cortexa53-xilinx-linux" ]; then
    source "$COMMON_IMAGE_VERSAL/environment-setup-cortexa72-cortexa53-xilinx-linux"
else
    echo "[WARN] Versal common image environment not found at: $COMMON_IMAGE_VERSAL"
    echo "      Set COMMON_IMAGE_VERSAL to your local path if cross-compiling."
fi

# Direct PATH/INCLUDE/LIB overrides (avoid sourcing settings64 that pull missing hooks)
append_path_if_dir() { [ -d "$1" ] && case ":$PATH:" in *":$1:"*) :;; *) export PATH="$1:$PATH";; esac; }
append_var_if_dir() { # $1=VARNAME, $2=dir
    if [ -d "$2" ]; then
        eval current="\${$1}"
        case ":$current:" in *":$2:"*) :;; *) eval export $1="$2:".$1;; esac
    fi
}

# Binaries
append_path_if_dir "$XILINX_VITIS/bin"
append_path_if_dir "$PREF_VIVADO_DIR/bin"
append_path_if_dir "$PREF_VITIS_HLS_DIR/bin"
append_path_if_dir "$XILINX_VITIS/aietools/bin"
append_path_if_dir "$XILINX_XRT/bin"

# Includes and libs (for ap_int.h etc.)
if [ -d "$PREF_VITIS_HLS_DIR/include" ]; then
    export CPLUS_INCLUDE_PATH="$PREF_VITIS_HLS_DIR/include:${CPLUS_INCLUDE_PATH:-}"
fi
if [ -d "$XILINX_VITIS/include" ]; then
    export CPLUS_INCLUDE_PATH="$XILINX_VITIS/include:${CPLUS_INCLUDE_PATH:-}"
fi
# XRT headers/libs if installed
if [ -d "$XILINX_XRT/include" ]; then export CPLUS_INCLUDE_PATH="$XILINX_XRT/include:${CPLUS_INCLUDE_PATH:-}"; fi
if [ -d "$XILINX_XRT/lib" ]; then export LIBRARY_PATH="$XILINX_XRT/lib:${LIBRARY_PATH:-}"; export LD_LIBRARY_PATH="$XILINX_XRT/lib:${LD_LIBRARY_PATH:-}"; fi

# DSP Libraries
if [ -d "$PREF_VITIS_LIBS" ]; then
    export DSPLIB_VITIS="$PREF_VITIS_LIBS"
elif [ -d "$TOOLS_ROOT/Vitis_Libraries" ]; then
    export DSPLIB_VITIS="$TOOLS_ROOT/Vitis_Libraries"
else
    export DSPLIB_VITIS="${DSPLIB_VITIS:-$SCRIPT_DIR/../Vitis_Libraries}"
fi
# =========================================================
# Platform Selection...
# =========================================================
# Prefer local workspace platform at platform_edge_hwemu/
# CRITICAL: Always use local platform, not base platform from Vitis installation
LOCAL_PLATFORM_XPFM="$SCRIPT_DIR/platform_edge_hwemu/platform_edge_hwemu/export/platform_edge_hwemu/platform_edge_hwemu.xpfm"
if [ -f "$LOCAL_PLATFORM_XPFM" ]; then
    export PLATFORM="$LOCAL_PLATFORM_XPFM"
    echo "[INFO] Using local platform: $LOCAL_PLATFORM_XPFM"
else
    echo "[WARN] Local platform not found at: $LOCAL_PLATFORM_XPFM"
    echo "      Ensure the platform is present or set PLATFORM manually."
    # Don't fall back to base platform - it will cause compatibility issues
fi

# ============================================================================
# TOOL INSTALLATION VALIDATION
# ============================================================================

echo ""
echo "============================================================================"
echo "TOOL INSTALLATION VALIDATION"
echo "============================================================================"
echo ""

echo "AI Engine Compiler:"
which aiecompiler || true
echo ""

echo "Vivado:"
which vivado || true
echo ""

echo "Vitis:"
which vitis || true
echo ""

echo "Vitis HLS:"
which vitis_hls || true
echo ""

echo "Cross-Compiler (CXX):"
echo "$CXX"
echo ""

echo "Target System Root:"
echo "$SDKTARGETSYSROOT"
echo ""

echo "DSP Library Path:"
echo "$DSPLIB_VITIS"
echo ""

echo "XRT Runtime:"
which xrt || true
echo ""

echo "XRT Utility (xbutil):"
which xbutil || true
echo ""

echo "Platform XPFM:"
echo "${PLATFORM:-not set}"
echo ""

echo "============================================================================"
echo "ENVIRONMENT SETUP COMPLETE"
echo "============================================================================"
echo ""




# ============================================================================
# ADDITIONAL CONFIGURATION
# ============================================================================

# Disable low area mode for better performance
export DISABLE_LOW_AREA_MODE=1

# Optional: Add XRT include path to C++ include path
# export CPLUS_INCLUDE_PATH=/opt/xilinx/xrt/include:$CPLUS_INCLUDE_PATH

# ============================================================================
# SCRIPT COMPLETION
# ============================================================================
# Environment setup is now complete. You can proceed with building and
# running the AI Engine GEMM application.
#
# Next steps:
# 1. Run: make help                    # See all available build targets
# 2. Run: make sd_card                # Build complete design
# 3. Run: make run                    # Build and run emulation
# 4. Run: make all                    # Build, run, and generate reports
#
# For more information, see the project README.md file.
# ============================================================================



