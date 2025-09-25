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

export PLATFORM_REPO_PATHS=/opt/Xilinx/Vitis/2024.1/base_platforms
export XILINX_VITIS=/opt/Xilinx/Vitis/2024.1
export COMMON_IMAGE_VERSAL=/home/mgrailoo/xilinx-versal-common-v2024.1

# ============================================================================
# XRT CONFIGURATION
# ============================================================================
# Configure Xilinx Runtime (XRT) for host-device communication
export XILINX_XRT=/opt/xilinx/xrt
export PATH=$XILINX_XRT/bin:$XILINX_VITIS/bin:$PATH

# ============================================================================
# ENVIRONMENT SETUP AND TOOLCHAIN SOURCING
# ============================================================================
# Source Versal Image, Vitis, and AIE tools
# This step sets up the cross-compilation environment and tool paths

# Source Versal common image environment setup
# This configures CXX, SDKTARGETSYSROOT, and other cross-compilation variables
source $COMMON_IMAGE_VERSAL/environment-setup-cortexa72-cortexa53-xilinx-linux

# Source Vitis settings for AIE and HLS tools
source $XILINX_VITIS/settings64.sh

# Source XRT setup for runtime environment
source /opt/xilinx/xrt/setup.sh
# ========================================================
# Set DSP Library for Vitis
# ========================================================
export DSPLIB_VITIS=/home/mgrailoo/Vitis_Libraries
# =========================================================
# Platform Selection...
# =========================================================
#tgt_plat=xilinx_vck190_base_202410_1
#export PLATFORM=$PLATFORM_REPO_PATHS/$tgt_plat/$tgt_plat\.xpfm
export PLATFORM=/media/josnu02/large_SDD/mgrailoo/platform_edge_hwemu/platform_edge_hwemu/export/platform_edge_hwemu/platform_edge_hwemu.xpfm

# ============================================================================
# TOOL INSTALLATION VALIDATION
# ============================================================================
# Verify that all required tools are properly installed and accessible
# This section displays the paths to key tools and environment variables

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
which xrt
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

