#!/bin/bash

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

# ============================================================================
# XILINX TOOLCHAIN CONFIGURATION
# ============================================================================
# Set Platform, Vitis, and Versal Image repository paths
# These paths must be updated based on your installation

export XILINX_XRT="/home/mahdieh/data/tools/xrt/opt/xilinx/xrt"
export ROOTFS="/home/mahdieh/data/tools/xilinx/2024.1/versal_common/rootfs.ext4"
export IMAGE="/home/mahdieh/data/tools/xilinx/2024.1/versal_common/Image"
export PLATFORM_REPO_PATHS="/home/mahdieh/data/tools/Xilinx/Vitis/2024.1/base_platforms"
export VITIS_PYTHON="/home/mahdieh/data/tools/Xilinx/Vitis/2024.1/tps/lnx64/python-3.8.3"
export XILINX_VITIS="/home/mahdieh/data/tools/Xilinx/Vitis/2024.1/"
export XILINX_VIVADO="/home/mahdieh/data/tools/Xilinx/Vivado/2024.1"
export COMMON_IMAGE_VERSAL="/home/mahdieh/data/tools/xilinx/2024.1/versal_common"
export SYSROOT="/home/mahdieh/data/tools/petalinux/2024.1/sysroots/cortexa72-cortexa53-xilinx-linux"
export SDKTARGETSYSROOT="/home/mahdieh/data/tools/petalinux/2024.1/sysroots/cortexa72-cortexa53-xilinx-linux"

# AIE Tools
export AIE_Tools="/home/mahdieh/data/tools/Xilinx/Vitis/2024.1/aietools"
# ========================================================
# Set DSP Library for Vitis
# ========================================================
export DSPLIB_VITIS="/home/mahdieh/data/tools/Xilinx/Vitis/Vitis_Libraries"

# Platform Selection...
# =========================================================
# Use local rebuilt platform with CMA configuration
export PLATFORM="/home/mahdieh/data/AIE_Versal/LLM2/Versal_AI_ML_Engines_GEMM-main/platform_edge_hwemu/platform_edge_hwemu/export/platform_edge_hwemu/platform_edge_hwemu.xpfm"
# Alternative: Use system platform (uncomment to use instead)
# tgt_plat=xilinx_vck190_base_202410_1
# export PLATFORM=$PLATFORM_REPO_PATHS/$tgt_plat/$tgt_plat\.xpfm

export PATH=$PATH:/home/mahdieh/data/tools/petalinux/2024.1/scripts
export PATH="/home/mahdieh/data/tools/Xilinx/Vitis/2024.1/aietools/bin:/home/mahdieh/data/tools/Xilinx/Vitis/2024.1/bin:/home/mahdieh/data/tools/Xilinx/Vivado/2024.1/bin:/home/mahdieh/data/tools/Xilinx/Vitis_HLS/2024.1/bin:/opt/Vitis/2022.1/bin:/opt/Vitis_HLS/2022.1/bin:/opt/Vivado/2022.1/bin:$PATH"


export PATH=$XILINX_XRT/bin:$PATH

source /home/mahdieh/data/tools/Xilinx/Vitis/2024.1/settings64.sh
source /home/mahdieh/data/tools/Xilinx/Vitis_HLS/2024.1/settings64.sh # vitis_hls -classic
source /home/mahdieh/data/tools/Xilinx/Vivado/2024.1/settings64.sh
# Source XRT setup for runtime environment
source /home/mahdieh/data/tools/xrt/opt/xilinx/xrt/setup.sh

# Set cross-compiler for A72 processor (explicitly set if not already set by settings64.sh)
if [ -z "$CXX" ]; then
    export CXX="/home/mahdieh/data/tools/Xilinx/Vitis/2024.1/gnu/aarch64/lin/aarch64-linux/bin/aarch64-linux-gnu-g++"
fi
# Ensure CXX is in PATH if using relative path
export PATH="/home/mahdieh/data/tools/Xilinx/Vitis/2024.1/gnu/aarch64/lin/aarch64-linux/bin:$PATH"
# ============================================================================
# TOOL INSTALLATION VALIDATION
# ============================================================================
# Verify that all required tools are properly installed and accessible
# This section displays the paths to key tools and environment variables

# XRT
# TOOLS_ROOT="/home/mahdieh/data/tools"
# if [ -d "$TOOLS_ROOT/xrt" ]; then
#     export XILINX_XRT="$TOOLS_ROOT/xrt"
#     export PATH="$XILINX_XRT/bin:$PATH"
#     echo "✓ XRT configured"
# fi

echo ""
echo "============================================================================"
echo "TOOL INSTALLATION VALIDATION"
echo "============================================================================"
echo ""

echo "AI Engine Compiler:"
which aiecompiler
echo ""

echo "Vivado:"
which vivado
echo ""

echo "Vitis:"
which vitis
echo ""

echo "Vitis HLS:"
which vitis_hls
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
if [ -n "$XILINX_XRT" ] && [ -d "$XILINX_XRT" ]; then
    echo "$XILINX_XRT"
    if [ -f "$XILINX_XRT/lib/libxrt_core.so" ]; then
        echo "  ✓ XRT libraries found"
    fi
else
    echo "  ✗ XRT not properly configured"
fi
echo ""

echo "XRT Utility (xbutil):"
which xbutil
echo ""

echo "============================================================================"
echo "ENVIRONMENT SETUP COMPLETE"
echo "============================================================================"
echo ""




# ============================================================================
# ADDITIONAL CONFIGURATION
# ============================================================================
# Additional environment variables and configuration options

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

