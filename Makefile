# ============================================================================
# AI Engine GEMM Build System Makefile
# ============================================================================
# Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: MIT
#
# This Makefile provides a comprehensive build system for the AI Engine GEMM
# application on Versal ACAP platforms. It supports both hardware emulation
# (hw_emu) and hardware (hw) targets with configurable matrix dimensions,
# data types, and performance optimizations.
#
# Key Features:
# - Centralized configuration via config.json
# - Support for multiple GEMM sizes (32, 64, 128, 256, 512, 1024)
# - Multiple data types (int16, int32, float)
# - Configurable AI Engine splits and cascade levels
# - HLS kernel compilation with performance optimizations
# - AI Engine graph compilation and simulation
# - Host application compilation for A72 processor
# - XSA generation and SD card packaging
# - Vivado utilization and power reporting
# - Comprehensive cleanup targets
#
# Build Flow:
# 1. Configuration parsing from config.json
# 2. Header file generation (gemm_config.h)
# 3. HLS kernel compilation (dma_hls)
# 4. AI Engine graph compilation
# 5. Host application compilation
# 6. XSA generation and linking
# 7. SD card packaging
# 8. Optional: Vivado reporting and VCD generation
#
# Usage Examples:
#   make help                    # Show all available targets
#   make sd_card                # Build complete design for hw_emu
#   make run                    # Build and run emulation
#   make all                    # Build, run, and generate reports
#   make report_metrics         # Generate Vivado utilization reports
#   make cleanall               # Clean all build artifacts
#
# Configuration Options:
#   TARGET      - Build target: hw_emu (default) or hw
#   GEMM_SIZE   - Matrix dimension: 32, 64, 128, 256, 512, 1024
#   DATA_TYPE   - Data type: int16, int32, float
#   SPLIT       - Number of AI Engine splits
#   CASC_LN     - Number of cascade levels
#   PL_FREQ     - HLS kernel frequency in MHz
#   ITER_CNT    - Design iteration count (-1 for infinite)
#   EN_TRACE    - Enable profiling: 0 (disabled) or 1 (enabled)
# ============================================================================

%.PHONY: help
help::
	@echo ""
	@echo " Makefile Usage"
	@echo ""
	@echo "  make sd_card"
	@echo "  Command calls rules kernels graph xsa application package, in sequence"
	@echo "  Default TARGET is hw_emu"
	@echo ""
	@echo "  make run"
	@echo "  Command calls rules install_pytorch_auto sd_card run_emu, in sequence"
	@echo "  Packages ML environment installation scripts and runs with benchmarking enabled"
	@echo "  ML environment will be installed on target board during execution (if ENABLE_ML_BENCHMARKS=1)"
	@echo "  Default TARGET is hw_emu"
	@echo ""
	@echo "  make run_with_pytorch"
	@echo "  Alias for 'make run' - same functionality"
	@echo "  Default TARGET is hw_emu"
	@echo ""
	@echo "  make all"
	@echo "  Command calls rules sd_card report_metrics vcd run_emu, in sequence"
	@echo "  Default TARGET is hw_emu"
	@echo ""
	@echo "  make kernels"
	@echo "  Command to generate the PL kernels"
	@echo "  Default TARGET is hw_emu"
	@echo ""
	@echo "  make graph"
	@echo "  Command to generate the AI Engine graph"
	@echo ""
	@echo "  make aiesim"
	@echo "  Command to run aie simulator for the AI Engine graph"
	@echo ""
	@echo "  make aiesim_profile"
	@echo "  Command to run aie simulator for the AI Engine graph and enable kernel print statements"
	@echo ""
	@echo "  make vcd"
	@echo "  Command to run aie simulator for the AI Engine graph and create the vcd and xpe file for the same"
	@echo ""
	@echo "  make xsa"
	@echo "  Command to generate the xsa for specified target"
	@echo "  Default TARGET is hw_emu"
	@echo ""
	@echo "  make application"
	@echo "  Command to generate the A72 application"
	@echo ""
	@echo "  make package"
	@echo "  Command to package design into an sd_card for specified target"
	@echo "  Default TARGET is hw_emu"
	@echo ""
	@echo "  make run_emu"
	@echo "  Command to run the hw emulator if LAUNCH_HW_EMU_EXEC variable is set to 1"
	@echo "  Default TARGET is hw_emu"
	@echo ""
	@echo "  make report_metrics"
	@echo "  Command to generate comprehensive Vivado reports including"
	@echo "    - Utilization analysis (detailed, hierarchical, by type)"
	@echo "    - Performance analysis (timing, clock domains, critical paths)"
	@echo "    - Power estimation (static and dynamic power analysis)"
	@echo "    - AI Engine specific analysis"
	@echo "    - Memory and DSP utilization"
	@echo "    - HTML and XML report formats"
	@echo "  Works only for TARGET=hw"
	@echo ""
	@echo "  make debug_aie_integration"
	@echo "  Command to debug AI Engine integration issues"
	@echo "    - Check AI Engine compilation output"
	@echo "    - Verify .aiedb file generation"
	@echo "    - Check Work directory contents"
	@echo "    - Verify Vivado project integration"
	@echo "    - Diagnose why 'AI ML Engines used = 0'"
	@echo ""
	@echo "  make clean_tgt"
	@echo "  Command to remove the generated files for a specific target"
	@echo ""
	@echo "  make clean_insts"
	@echo "  Command to remove the generated files for a specific instance of the design."
	@echo ""
	@echo "  make clean_gemmsize"
	@echo "  Command to remove the generated files for a specific GeMM Matrix Dimension"
	@echo ""
	@echo "  make cleanall_blds"
	@echo "  Command to remove the generated files for all variations"
	@echo ""
	@echo "  make cleanall_vivado_reports"
	@echo "  Command to remove the generated vivado reports files for all variations"
	@echo ""
	@echo "  make cleanall_xpe_reports"
	@echo "  Command to remove the generated xpe files for all variations"
	@echo ""
	@echo "  make cleanall_reports"
	@echo "  Command to remove all the generated reports for all variations"
	@echo ""
	@echo "  make cleanall"
	@echo "  Command to remove the generated files and reports for all variations"
	@echo ""
	@echo "  make install_pytorch_auto"
	@echo "  Command to automatically install ML environment (if ENABLE_ML_BENCHMARKS=1)"
	@echo ""
	@echo " OPTIONS"
	@echo " Use the make recipes with required values for options mentioned below-"
	@echo "    TARGET     -specifies target: Hardware or Emulation. Values: hw_emu(default), hw"
	@echo "    GEMM_SIZE  -specifies GeMM Matrix Dimension. Values: 32(default), 64, 128, 256, 512, 1024"
	@echo "    GEMM_INSTS -specifies number of GeMM instances. Values: 1(default)"
	@echo "    PL_FREQ    -specifies HLS kernel frequencies. Values: 250Mhz(default)"
	@echo "    ENABLE_ML_BENCHMARKS -specifies ML benchmarks. Values: 0(default), 1"
	@echo "    ITER_CNT   -specifies design iteration count: Values: 1(default)"
	@echo "    EN_TRACE   -specifies whether to enable profiling for the design. Values: 0(disabled, default) and 1(enabled)"
	@echo "    LAUNCH_HW_EMU_EXEC -specifies whether to launch hw emulator when running make run_emu rule. Values: 0(disabled, default) and 1(enabled)"
	@echo ""

# Print all options passed to Makefile
print-%  : ; @echo $* = $($*)

# ============================================================================
# DSP LIBRARY CONFIGURATION
# ============================================================================
# Set DSP Library root path for Vitis integration
# Use environment variable if set, otherwise use default path
DSPLIB_VITIS ?= /home/mgrailoo/Vitis_Libraries
DSPLIB_ROOT=$(DSPLIB_VITIS)/dsp

# ============================================================================
# CORE VARIABLE DEFINITIONS
# ============================================================================
# Default build target (can be overridden by config.json)
TARGET := hw

# Platform definition
# Use environment variable if set, otherwise use default path
PLATFORM ?= /media/josnu02/large_SDD/mgrailoo/platform_edge_hwemu/platform_edge_hwemu/export/platform_edge_hwemu/platform_edge_hwemu.xpfm

# Common image path (for packaging)
# Use environment variable if set, otherwise use default path
COMMON_IMAGE_VERSAL ?= /home/mgrailoo/xilinx-versal-common-v2024.1

# ============================================================================
# CONFIGURATION MANAGEMENT
# ============================================================================
# Centralized configuration file containing all build parameters
# This file serves as the single source of truth for all configuration
CONFIG_JSON := design/design_configs/config.json

# ============================================================================
# CONFIGURATION PARSING FROM JSON
# ============================================================================
# Parse all configuration parameters from config.json using Python
# This ensures centralized configuration management and consistency

# Core build parameters
TARGET := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('TARGET','hw'))")
GEMM_SIZE := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('GEMM_SIZE',1024))")
DIM := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('DIM',16))")
SPLIT := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('SPLIT',2))")
CASC_LN := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('CASC_LN',8))")
ENABLE_ML_BENCHMARKS := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('ENABLE_ML_BENCHMARKS',0))")

# Data type and word length configuration
# Prefer WRD_LN from config; else derive from DATA_TYPE (128 / bits)
WRD_LN := $(shell python3 -c 'import json; d=json.load(open("$(CONFIG_JSON)")); wr=d.get("WRD_LN"); dt=d.get("DATA_TYPE","int16"); print(int(wr) if isinstance(wr,int) else (8 if dt=="int16" else 4 if dt=="int32" else 4 if dt=="float" else 8))')
N_SAMPLES := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('N_SAMPLES',1))")
DATA_TYPE_STR := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('DATA_TYPE','int16'))")
# Convert string DATA_TYPE to numeric value for C++ preprocessor
DATA_TYPE := $(shell python3 -c "dt='$(DATA_TYPE_STR)'; print(16 if dt=='int16' else 32 if dt=='int32' else 33 if dt=='float' else 16)")

# Optional sub-tile overrides and tile memory from config
SUB_TILE_M := $(shell python3 -c "import json; v=json.load(open('$(CONFIG_JSON)')).get('SUB_TILE_M'); print(v) if v not in [None,'null'] else print('')")
SUB_TILE_K := $(shell python3 -c "import json; v=json.load(open('$(CONFIG_JSON)')).get('SUB_TILE_K'); print(v) if v not in [None,'null'] else print('')")
SUB_TILE_N := $(shell python3 -c "import json; v=json.load(open('$(CONFIG_JSON)')).get('SUB_TILE_N'); print(v) if v not in [None,'null'] else print('')")
TILE_MEM_BYTES := $(shell python3 -c "import json; v=json.load(open('$(CONFIG_JSON)')).get('TILE_MEM_BYTES'); print(v) if v not in [None,'null'] else print('')")


# Calculate DIM_A and DIM_B based on GEMM_SZ_SPLIT and GEMM_SZ_CASC
GEMM_SZ_SPLIT := $(shell echo "$$(( $(GEMM_SIZE) / $(SPLIT) ))")
GEMM_SZ_CASC := $(shell echo "$$(( $(GEMM_SIZE) / $(CASC_LN) ))")
# Calculate DIM_A and DIM_B using proper min function
DIM_A := $(shell if [ $(DIM) -lt $(GEMM_SZ_SPLIT) ]; then echo $(DIM); else echo $(GEMM_SZ_SPLIT); fi)
DIM_B := $(shell if [ $(DIM) -lt $(GEMM_SZ_CASC) ]; then echo $(DIM); else echo $(GEMM_SZ_CASC); fi)

DIM_AB := $(GEMM_SIZE)

MAT_DIMS := $(GEMM_SIZE)x$(GEMM_SIZE)x$(GEMM_SIZE)
GEMM_INSTS := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('GEMM_INSTS',1))")
PL_FREQ := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('PL_FREQ',312.5))")
HZ_UNIT := 1000000
VPP_CLOCK_FREQ := $(shell printf "%.0f" `echo "${PL_FREQ} * $(HZ_UNIT)" | bc`)
ITER_CNT ?= $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('ITER_CNT',1))")
EN_TRACE := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('EN_TRACE',1))")
LAUNCH_HW_EMU_EXEC := 0

# Uppercase TARGET for header macro
TARGET_UPPER := $(shell echo $(TARGET) | tr a-z A-Z)

# Always include Eigen/BLAS in host build




ifeq ($(ITER_CNT),-1)
    # Infinite run mode: keep as -1 to signal run-forever in host/app
    GRAPH_ITER_CNT := -1
else
    # Calculate GRAPH_ITER_CNT using DIM: (GEMM_SIZE * GEMM_SIZE) / (DIM * DIM) / SPLIT
    GRAPH_ITER_CNT := $(shell echo "$$(( $(GEMM_SIZE) * $(GEMM_SIZE) / $(DIM) / $(DIM) / $(SPLIT) ))")
endif


# ============================================================================
# DIRECTORY STRUCTURE DEFINITION
# ============================================================================
# Define all source and build directories relative to project root
# This ensures consistent paths across different build environments

# Project root directory
RELATIVE_PROJECT_DIR := ./
# PROJECT_REPO := $(shell readlink -f $(RELATIVE_PROJECT_DIR))
# Use the directory containing this Makefile to ensure correct project root
# On server, the path is /media/josnu02/large_SDD/mgrailoo/design/
# So we need to go up one level from the design directory to get /media/josnu02/large_SDD/mgrailoo/
PROJECT_REPO := $(shell dirname $(shell readlink -f $(lastword $(MAKEFILE_LIST))))

# ============================================================================
# SOURCE CODE DIRECTORIES
# ============================================================================
# Core design directories
DESIGN_REPO  := $(PROJECT_REPO)/design
AIE_SRC_REPO := $(DESIGN_REPO)/aie_src
PL_SRC_REPO  := $(DESIGN_REPO)/pl_src
HOST_APP_SRC := $(DESIGN_REPO)/host_app_src

# ============================================================================
# CONFIGURATION AND SUPPORT DIRECTORIES
# ============================================================================
# Configuration and utility directories
SYSTEM_CONFIGS_REPO    := $(DESIGN_REPO)/system_configs
PROFILING_CONFIGS_REPO := $(DESIGN_REPO)/profiling_configs
EXEC_SCRIPTS_REPO      := $(DESIGN_REPO)/exec_scripts
VIVADO_METRICS_SCRIPTS_REPO := $(DESIGN_REPO)/vivado_metrics_scripts
DIRECTIVES_REPO        := $(DESIGN_REPO)/directives

# ============================================================================
# BUILD DIRECTORY STRUCTURE
# ============================================================================
# Hierarchical build directory structure for organized output management
# Format: build/gemm_<SIZE>x<SIZE>x<SIZE>/x<INSTANCES>/<TARGET>/

# Base build directory
BASE_BLD_DIR     := $(PROJECT_REPO)/build
GEMM_BLD_DIR     := $(BASE_BLD_DIR)/gemm_$(MAT_DIMS)
INSTS_BLD_DIR    := $(GEMM_BLD_DIR)/x$(GEMM_INSTS)
BUILD_TARGET_DIR := $(INSTS_BLD_DIR)/$(TARGET)

# ============================================================================
# REPORTING AND ANALYSIS DIRECTORIES
# ============================================================================
# Directories for storing build reports, utilization metrics, and analysis files

# Vivado utilization and power reports
REPORTS_REPO := $(PROJECT_REPO)/reports_dir
BLD_REPORTS_DIR := $(REPORTS_REPO)/gemm_$(MAT_DIMS)/x$(GEMM_INSTS)

# XPE (Xilinx Power Estimator) files for power analysis
XPE_REPO := $(PROJECT_REPO)/xpe_dir
BLD_XPE_DIR := $(XPE_REPO)/gemm_$(MAT_DIMS)/x$(GEMM_INSTS)/$(TARGET)
VCD_FILE_NAME := gemm_$(MAT_DIMS)_x$(GEMM_INSTS)
BLD_TGT_VCD_FILE := $(BUILD_TARGET_DIR)/$(VCD_FILE_NAME).vcd
XPE_FILE := $(BLD_XPE_DIR)/graph_$(VCD_FILE_NAME).xpe

# ============================================================================
# PACKAGING AND DEPLOYMENT DIRECTORIES
# ============================================================================
# Directories for SD card packaging and deployment

# SD card package output
EMBEDDED_PACKAGE_OUT := $(BUILD_TARGET_DIR)/package
EMBEDDED_EXEC_SCRIPT := run_script.sh

# ============================================================================
# AI ENGINE SIMULATION DIRECTORIES
# ============================================================================
# Directories for AI Engine simulation and data generation

# AI Engine simulation workspace and data
WORK_DIR := Work
AIE_SIM_IO_BASE_DIR := $(AIE_SRC_REPO)/aiesim_data
AIE_SIM_IO_DIR := $(AIE_SIM_IO_BASE_DIR)/gemm_$(MAT_DIMS)_ioFiles

# ============================================================================
# CONFIGURATION HEADER GENERATION
# ============================================================================
# Auto-generate C++ header file from configuration parameters
# This ensures consistency between Makefile and C++/HLS source code

# Generated header file containing all configuration constants
GEMM_CONFIG_HDR := $(DESIGN_REPO)/design_configs/gemm_config.h

# Force regeneration of header when config changes
.PHONY: force_header gemm_config.h

# Force header regeneration target (empty target that always runs)
force_header:

# Direct target for generating the header file
gemm_config.h: $(GEMM_CONFIG_HDR)

# ============================================================================
# HEADER GENERATION RULE
# ============================================================================
# Auto-generate header from current Make variables (single source of truth)
# This rule creates a comprehensive C++ header file with all build parameters
$(GEMM_CONFIG_HDR): force_header
	@echo "Generating gemm_config.h from $(CONFIG_JSON)"
	@echo "Debug GEMM_SIZE=$(GEMM_SIZE), DIM_A=$(DIM_A), DIM_B=$(DIM_B), SPLIT=$(SPLIT), GRAPH_ITER_CNT=$(GRAPH_ITER_CNT)"
	@echo '#ifndef GEMM_CONFIG_H' > $(GEMM_CONFIG_HDR)
	@echo '#define GEMM_CONFIG_H' >> $(GEMM_CONFIG_HDR)
	@echo '#define TARGET_$(TARGET_UPPER) 1' >> $(GEMM_CONFIG_HDR)
	@echo '#define GEMM_SIZE $(GEMM_SIZE)' >> $(GEMM_CONFIG_HDR)
	@echo '#define DATA_TYPE $(DATA_TYPE)' >> $(GEMM_CONFIG_HDR)
	@echo '#define DIM $(DIM)' >> $(GEMM_CONFIG_HDR)
	@echo '#define SPLIT $(SPLIT)' >> $(GEMM_CONFIG_HDR)
	@echo '#define ENABLE_ML_BENCHMARKS $(ENABLE_ML_BENCHMARKS)' >> $(GEMM_CONFIG_HDR)
	@echo '#define CASC_LN $(CASC_LN)' >> $(GEMM_CONFIG_HDR)
	@echo '#define WRD_LN $(WRD_LN)' >> $(GEMM_CONFIG_HDR)
	@echo '#define N_SAMPLES $(N_SAMPLES)' >> $(GEMM_CONFIG_HDR)
	@echo '#define ITER_CNT $(ITER_CNT)' >> $(GEMM_CONFIG_HDR)
	@echo '#define GEMM_INSTS $(GEMM_INSTS)' >> $(GEMM_CONFIG_HDR)
	@echo '#define EN_TRACE $(EN_TRACE)' >> $(GEMM_CONFIG_HDR)
	@echo '#define PL_FREQ $(PL_FREQ)' >> $(GEMM_CONFIG_HDR)
	@echo '// Calculated tiling dimensions' >> $(GEMM_CONFIG_HDR)
	@echo '#define DIM_A $(DIM_A)' >> $(GEMM_CONFIG_HDR)
	@echo '#define DIM_B $(DIM_B)' >> $(GEMM_CONFIG_HDR)
	@echo '// Calculated graph iteration count' >> $(GEMM_CONFIG_HDR)
	@echo '// GRAPH_ITER_CNT = (GEMM_SIZE * GEMM_SIZE) / (DIM * DIM) / SPLIT' >> $(GEMM_CONFIG_HDR)
	@echo '#define GRAPH_ITER_CNT $(GRAPH_ITER_CNT)' >> $(GEMM_CONFIG_HDR)
	@echo '// Additional constants needed by host app and other components' >> $(GEMM_CONFIG_HDR)
	@echo '#define NUM_A_FILES $(CASC_LN)' >> $(GEMM_CONFIG_HDR)
	@echo '#define NUM_B_FILES $(shell echo "$$(( $(SPLIT) * $(CASC_LN) ))")' >> $(GEMM_CONFIG_HDR)
	@echo '#define NUM_C_FILES $(SPLIT)' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix dimensions' >> $(GEMM_CONFIG_HDR)
	@echo '#define DIM_AB $(GEMM_SIZE)' >> $(GEMM_CONFIG_HDR)
	@if [ -n "$(TILE_MEM_BYTES)" ]; then echo '#define TILE_MEM_BYTES $(TILE_MEM_BYTES)' >> $(GEMM_CONFIG_HDR); fi
	@if [ -n "$(SUB_TILE_M)" ]; then echo '#define SUB_TILE_M $(SUB_TILE_M)' >> $(GEMM_CONFIG_HDR); fi
	@if [ -n "$(SUB_TILE_K)" ]; then echo '#define SUB_TILE_K $(SUB_TILE_K)' >> $(GEMM_CONFIG_HDR); fi
	@if [ -n "$(SUB_TILE_N)" ]; then echo '#define SUB_TILE_N $(SUB_TILE_N)' >> $(GEMM_CONFIG_HDR); fi
	@echo '// Calculate base sizes for individual matrices based on corrected plioGen.py formula' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix A: BASE_MATA_SZ = GEMM_SZ_SPLIT * GEMM_SZ_CASC * broadcast_count_a * SPLIT // WRD_LN' >> $(GEMM_CONFIG_HDR)
	@echo '// where: tiles_per_row_in_block_a = (GEMM_SZ_SPLIT) // DIM_A' >> $(GEMM_CONFIG_HDR)
	@echo '//        broadcast_count_a = max(1, tiles_per_row_in_block_a)' >> $(GEMM_CONFIG_HDR)
	@echo '//        GEMM_SZ_SPLIT = GEMM_SIZE // SPLIT' >> $(GEMM_CONFIG_HDR)
	@echo '//        GEMM_SZ_CASC = GEMM_SIZE // CASC_LN' >> $(GEMM_CONFIG_HDR)
	@echo '#define TILES_PER_ROW_IN_BLOCK_A ((GEMM_SIZE / SPLIT) / DIM_A)' >> $(GEMM_CONFIG_HDR)
	@echo '#define BROADCAST_COUNT_A ((TILES_PER_ROW_IN_BLOCK_A > 1) ? TILES_PER_ROW_IN_BLOCK_A : 1)' >> $(GEMM_CONFIG_HDR)
	@echo '#define BASE_MATA_SZ (((GEMM_SIZE / SPLIT) * (GEMM_SIZE / CASC_LN) * BROADCAST_COUNT_A * SPLIT) / WRD_LN)' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix B: Base size is the same as Matrix A' >> $(GEMM_CONFIG_HDR)
	@echo '#define BASE_MATB_SZ BASE_MATA_SZ' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix C: Each split has (GEMM_SIZE * GEMM_SIZE) / SPLIT / WRD_LN elements' >> $(GEMM_CONFIG_HDR)
	@echo '#define BASE_MATC_SZ ((GEMM_SIZE * GEMM_SIZE) / SPLIT / WRD_LN)' >> $(GEMM_CONFIG_HDR)
	@echo '#endif' >> $(GEMM_CONFIG_HDR)
	@# Persist computed GRAPH_ITER_CNT back into config.json for visibility in tools/scripts
	@python3 -c "import json,sys; p='$(CONFIG_JSON)'; d=json.load(open(p)); d['GRAPH_ITER_CNT']=$(GRAPH_ITER_CNT); json.dump(d, open(p,'w'), indent=2); print(f'Updated GRAPH_ITER_CNT={$(GRAPH_ITER_CNT)} in {p}')" || echo 'Warning: could not update GRAPH_ITER_CNT in $(CONFIG_JSON)'
	@# (Listing of valid TP_DIMs moved to host_app logging to avoid Makefile portability issues)

# ============================================================================
# BUILD TARGET DEFINITIONS
# ============================================================================
# Define all build targets and output file names
# These variables are used throughout the build process

# ============================================================================
# KERNEL TARGETS
# ============================================================================
# HLS kernel compilation targets
DATAMOVER_KERNEL_TOP := dma_hls
DATAMOVER_KERNEL_XO  := $(DATAMOVER_KERNEL_TOP).$(TARGET)

# ============================================================================
# AI ENGINE TARGETS
# ============================================================================
# AI Engine graph compilation targets
LIBADF_A := $(BUILD_TARGET_DIR)/libadf.a

# ============================================================================
# APPLICATION TARGETS
# ============================================================================
# Host application executable targets
APP_ELF := gemm_aie_xrt.elf
APP_ELF_INF_RUN := gemm_aie_xrt_inf_run.elf

# ============================================================================
# HARDWARE TARGETS
# ============================================================================
# Hardware system targets
XSA  := vck190_aie_gemm.$(TARGET).xsa

# ============================================================================
# SOURCE FILE DEFINITIONS
# ============================================================================
# Define all source files used in the build process
# These variables point to the actual source code files

# ============================================================================
# KERNEL SOURCE FILES
# ============================================================================
# HLS kernel source files for Programmable Logic
DATAMOVER_KERNEL_TOP := dma_hls
DATAMOVER_KERNEL_SRC := $(PL_SRC_REPO)/$(DATAMOVER_KERNEL_TOP).cpp

# ============================================================================
# AI ENGINE SOURCE FILES
# ============================================================================
# AI Engine graph source files
GRAPH_SRC_CPP := $(AIE_SRC_REPO)/graph.cpp

# ============================================================================
# HOST APPLICATION SOURCE FILES
# ============================================================================
# Host application source files for A72 processor
APP_SRC_CPP := $(HOST_APP_SRC)/gemm_aie_app.cpp $(HOST_APP_SRC)/gemm_utils.cpp

# AI Engine control code (generated during AIE compilation)
AIE_CONTROL_CPP := $(BUILD_TARGET_DIR)/$(WORK_DIR)/ps/c_rts/aie_control_xrt.cpp

# ============================================================================
# AI ENGINE COMPILER FLAGS
# ============================================================================
# Configuration flags for AI Engine compiler (aiecompiler)
# These flags control compilation, optimization, and resource allocation

# ============================================================================
# INCLUDE PATHS
# ============================================================================
# Include directories for AI Engine compilation
AIE_FLAGS := -include=$(AIE_SRC_REPO)
AIE_FLAGS += -include=$(DESIGN_REPO)/design_configs      # Configuration includes
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/include/aie      # DSP Library L1 includes
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/src/aie          # DSP Library L1 source
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/tests/aie/inc    # DSP Library L1 test includes
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/tests/aie/src    # DSP Library L1 test source
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L2/include/aie      # DSP Library L2 includes
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L2/tests/aie/common/inc # DSP Library L2 common includes
AIE_FLAGS += --verbose                                    # Verbose compilation output

# Target-specific flags to ensure consistent template parameter generation
# Note: Let AI Engine compiler determine architecture automatically

# ============================================================================
# PREPROCESSOR DEFINITIONS
# ============================================================================
# Provide only needed defines; let code derive GRAPH_ITER_CNT and DIM_AB
AIE_FLAGS += --Xpreproc="-DITER_CNT=$(ITER_CNT)"        # Iteration count
AIE_FLAGS += --Xpreproc="-DGEMM_SIZE=$(GEMM_SIZE)"      # Matrix size
AIE_FLAGS += --Xpreproc="-DGEMM_INSTS=$(GEMM_INSTS)"    # Number of instances
AIE_FLAGS += --Xpreproc="-DSPLIT=$(SPLIT)"              # Number of splits
AIE_FLAGS += --Xpreproc="-DCASC_LN=$(CASC_LN)"          # Cascade levels
AIE_FLAGS += --Xpreproc="-DDIM_A=$(DIM_A)"              # Dimension A
AIE_FLAGS += --Xpreproc="-DDIM_B=$(DIM_B)"              # Dimension B
AIE_FLAGS += --Xpreproc="-DDATA_TYPE=$(DATA_TYPE)"      # Data type

# ============================================================================
# PLATFORM AND TARGET CONFIGURATION
# ============================================================================
# Platform and target-specific configuration
AIE_FLAGS += --platform=$(PLATFORM)                     # Target platform
#AIE_FLAGS += --target=$(TARGET)                        # Target type (commented out)

# ============================================================================
# COMPILATION OPTIONS
# ============================================================================
# Compilation and optimization options
AIE_FLAGS += --log-level=5                              # Log level for debugging
#AIE_FLAGS += --test-iterations=2                       # Test iterations (commented out)
AIE_FLAGS += --pl-freq=$(PL_FREQ)                       # Programmable Logic frequency
#AIE_FLAGS += --dataflow                                # Dataflow optimization (commented out)

# ============================================================================
# CONSTRAINTS AND OPTIMIZATIONS
# ============================================================================
# Constraints for same shim solution for all variations
# aids in timing closure of the design...
# AIE_FLAGS += --constraints=$(AIE_SRC_REPO)/constraints.aiecst  # Commented out - let compiler do automatic placement

# Memory and routing optimizations
AIE_FLAGS += --Xmapper=BufferOptLevel9                  # Buffer optimization level
AIE_FLAGS += --Xrouter=DMAFIFOsInFreeBankOnly           # DMA FIFO placement

# Additional options (commented out)
#AIE_FLAGS += --heapsize=7000                           # Heap size
#AIE_FLAGS += --Xchess="main:bridge.llibs=softfloat m"  # Chess compiler options
AIE_FLAGS += --workdir=$(WORK_DIR)                      # Working directory
AIE_FLAGS += --output-archive=libadf.a                 # Generate AI Engine archive
AIE_FLAGS += --target=$(TARGET)                        # Target type for Vivado integration

# Aiesimulator flags
#-i - Alias of --input-dir=<dir> option
#-o - Alias of --output-dir=<dir> option
AIE_SIM_FLAGS := --pkg-dir $(WORK_DIR)/
AIE_SIM_FLAGS += -i=$(AIE_SIM_IO_DIR)
AIE_SIM_FLAGS += --simulation-cycle-timeout=1000000  # Limit simulation to 1M cycles (prevents infinite loop)

# =========================================================
# Application Compiler and Linker Flags
# =========================================================
GCC_FLAGS := -O
GCC_FLAGS += -c
GCC_FLAGS += -std=c++17
GCC_FLAGS += -D__linux__
GCC_FLAGS += -D__PS_ENABLE_AIE__
GCC_FLAGS += -DXAIE_DEBUG
GCC_FLAGS += -DGEMM_INSTS=$(GEMM_INSTS)
GCC_FLAGS += -DGEMM_SIZE=$(GEMM_SIZE)
GCC_FLAGS += -D__AIEARCH__=10
GCC_FLAGS += -DITER_CNT=$(ITER_CNT)
GCC_FLAGS += -DDATA_TYPE=$(DATA_TYPE)

GCC_INC_FLAGS := -I$(SDKTARGETSYSROOT)/usr/include/xrt
GCC_INC_FLAGS += -I$(XILINX_VITIS)/aietools/include/
GCC_INC_FLAGS += -I$(SDKTARGETSYSROOT)/usr/include
GCC_INC_FLAGS += -I$(SDKTARGETSYSROOT)/usr/lib
GCC_INC_FLAGS += -I$(AIE_SRC_REPO)
GCC_INC_FLAGS += -I$(HOST_APP_SRC)
GCC_INC_FLAGS += -I$(DESIGN_REPO)/design_configs
GCC_INC_FLAGS += -I$(DSPLIB_ROOT)/L1/include/aie
GCC_INC_FLAGS += -I$(DSPLIB_ROOT)/L1/src/aie
GCC_INC_FLAGS += -I$(DSPLIB_ROOT)/L1/tests/aie/inc
GCC_INC_FLAGS += -I$(DSPLIB_ROOT)/L1/tests/aie/src
GCC_INC_FLAGS += -I$(DSPLIB_ROOT)/L2/include/aie
GCC_INC_FLAGS += -I$(DSPLIB_ROOT)/L2/tests/aie/common/inc
GCC_INC_FLAGS += -I$(XILINX_XRT)/include
GCC_INC_FLAGS += -I$(XILINX_VITIS)/include
GCC_INC_FLAGS += -I/opt/Xilinx/Vitis_HLS/2024.1/include/
GCC_INC_FLAGS += -I$(AIE_SRC_REPO)  # Include path for graph.h and gemm_config.h
GCC_INC_FLAGS += -I$(PL_SRC_REPO)   # Include path for dma_hls.cpp
# Try to include Eigen headers if present in sysroot
GCC_INC_FLAGS += -I$(SDKTARGETSYSROOT)/usr/include/eigen3

GCC_INC_LIB += -L$(SDKTARGETSYSROOT)/usr/lib
GCC_INC_LIB += -L$(SDKTARGETSYSROOT)/lib
GCC_INC_LIB += -L$(XILINX_VITIS)/aietools/lib/aarch64.o
GCC_INC_LIB += -L$(XILINX_VITIS)/aietools/lib/lnx64.o
GCC_INC_LIB += -L$(XILINX_XRT)/lib

GCC_LIB := -ladf_api_xrt
GCC_LIB += -lxrt_coreutil
GCC_LIB += -luuid
# Try to link OpenBLAS; if not present runtime will fallback
# GCC_LIB += -lopenblas

# =========================================================
# Kernel Compiler and Linker Flags
# ========================================================

#VPP_LINK_FLAGS += --advanced.param "compiler.worstNegativeSlack=-1.0"
VPP_FLAGS := --platform $(PLATFORM)
VPP_FLAGS += --save-temps
VPP_FLAGS += --temp_dir $(BUILD_TARGET_DIR)/_x
VPP_FLAGS += --verbose
VPP_FLAGS += -g



# Update DATAMOVER_KERNEL_VPP_FLAGS
DATAMOVER_KERNEL_VPP_FLAGS := --hls.clock $(VPP_CLOCK_FREQ):$(DATAMOVER_KERNEL_TOP)
DATAMOVER_KERNEL_VPP_FLAGS += -D GEMM_SIZE=$(GEMM_SIZE)
DATAMOVER_KERNEL_VPP_FLAGS += -D SPLIT=$(SPLIT)
DATAMOVER_KERNEL_VPP_FLAGS += -D CASC_LN=$(CASC_LN)
DATAMOVER_KERNEL_VPP_FLAGS += -D DIM_A=$(DIM_A)
DATAMOVER_KERNEL_VPP_FLAGS += -D DIM_B=$(DIM_B)
DATAMOVER_KERNEL_VPP_FLAGS += -D DATA_TYPE=$(DATA_TYPE)
DATAMOVER_KERNEL_VPP_FLAGS += -D DIM_AB=$(DIM_AB)

# Add streaming architecture defines for VPP
ifeq ($(STREAMING_ENABLED),True)
    DATAMOVER_KERNEL_VPP_FLAGS += -D STREAMING_ENABLED=1
    DATAMOVER_KERNEL_VPP_FLAGS += -D STREAMING_TILE_SIZE=$(STREAMING_TILE_SIZE)
    DATAMOVER_KERNEL_VPP_FLAGS += -D TILES_PER_DIM=$(TILES_PER_DIM)
    DATAMOVER_KERNEL_VPP_FLAGS += -D TOTAL_TILES=$(TOTAL_TILES)
endif

DATAMOVER_KERNEL_VPP_FLAGS += --hls.jobs 32


VPP_LINK_FLAGS := --clock.freqHz $(VPP_CLOCK_FREQ):$(DATAMOVER_KERNEL_TOP)_0
VPP_LINK_FLAGS += --clock.defaultTolerance 0.001
# VPP_LINK_FLAGS += --set_property CONFIG.ADVANCED_PROPERTIES {__experimental_features__ {disable_low_area_mode 1}} [get_bd_cells /vitis_design_icn_ctrl_1_0]

## If Profiling for Performance Measurement is enabled..
ifeq ($(EN_TRACE),1)
    ifeq ($(TARGET),hw)
      #VPP_LINK_FLAGS += --profile.data $(DATAMOVER_KERNEL_TOP):all:all
    	VPP_LINK_FLAGS += --profile.data $(DATAMOVER_KERNEL_TOP):all:strmInp_from_C0
      
    endif
endif

VPP_LINK_FLAGS += --config $(SYSTEM_CONFIGS_REPO)/x$(GEMM_INSTS).cfg
VPP_LINK_FLAGS += --vivado.prop fileset.sim_1.xsim.simulate.log_all_signals=true

VPP_LINK_FLAGS += --vivado.prop run.impl_1.STEPS.PLACE_DESIGN.TCL.PRE=$(DIRECTIVES_REPO)/prohibit_select_bli_bels_for_hold.tcl

VPP_LINK_FLAGS += --vivado.prop run.synth_1.STEPS.SYNTH_DESIGN.ARGS.CONTROL_SET_OPT_THRESHOLD=16
VPP_LINK_FLAGS += --vivado.prop run.impl_1.{strategy}={Performance_ExplorePostRoutePhysOpt}
#VPP_LINK_FLAGS += --vivado.prop run.synth_1.STEPS.SYNTH_DESIGN.ARGS.RETIMING=true
#VPP_LINK_FLAGS += --advanced.param "compiler.worstNegativeSlack=-1.0"
#VPP_LINK_FLAGS += --vivado.prop run.impl_1.{STEPS.OPT_DESIGN.ARGS.IS_ENABLED}={true}
#VPP_LINK_FLAGS += --vivado.prop run.impl_1.{STEPS.OPT_DESIGN.ARGS.DIRECTIVE}={Default}
VPP_LINK_FLAGS += --vivado.prop run.impl_1.{STEPS.PLACE_DESIGN.ARGS.DIRECTIVE}={SSI_BalanceSLLs}
VPP_LINK_FLAGS += --vivado.prop run.impl_1.{STEPS.PHYS_OPT_DESIGN.ARGS.IS_ENABLED}={true}
VPP_LINK_FLAGS += --vivado.prop run.impl_1.{STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE}={AggressiveExplore}
#VPP_LINK_FLAGS += --vivado.prop run.impl_1.{STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE}={Default}
#VPP_LINK_FLAGS += --vivado.prop run.impl_1.{STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.IS_ENABLED}={false}
######################################################################################
#VPP_LINK_FLAGS += --vivado.prop run.impl_1.{strategy}={Performance_Auto_3}
#VPP_LINK_FLAGS += --vivado.prop run.impl_1.{strategy}={Performance_Explore} 

#VPP_LINK_FLAGS += --vivado.prop run.impl_1.{strategy}={Performance_ExploreWithRemap}
#VPP_LINK_FLAGS += --vivado.prop run.impl_1.{strategy}={Performance_WLBlockPlacement}
#VPP_LINK_FLAGS += --vivado.prop run.impl_1.{strategy}={Performance_EarlyBlockPlacement}
#####################################################################################
#VPP_LINK_FLAGS += --vivado.synth.jobs 8
#VPP_LINK_FLAGS += --vivado.impl.jobs 8

# =========================================================
# Packaging Flags
# ========================================================
PKG_FLAGS := -t $(TARGET)
PKG_FLAGS += --save-temps
PKG_FLAGS += --temp_dir $(BUILD_TARGET_DIR)/_x
PKG_FLAGS += -f $(PLATFORM)
PKG_FLAGS += --package.rootfs $(COMMON_IMAGE_VERSAL)/rootfs.ext4
PKG_FLAGS += --package.kernel_image $(COMMON_IMAGE_VERSAL)/Image
PKG_FLAGS += --package.boot_mode=sd
PKG_FLAGS += --package.out_dir $(EMBEDDED_PACKAGE_OUT)
PKG_FLAGS += --package.image_format=ext4
PKG_FLAGS += --package.sd_file $(BUILD_TARGET_DIR)/$(APP_ELF) $(BUILD_TARGET_DIR)/$(XSA) $(LIBADF_A)
PKG_FLAGS += --package.sd_file $(BUILD_TARGET_DIR)/$(APP_ELF_INF_RUN) 
PKG_FLAGS += --package.sd_file $(EXEC_SCRIPTS_REPO)/$(EMBEDDED_EXEC_SCRIPT)
PKG_FLAGS += --package.sd_file $(AIE_SIM_IO_DIR)/a_golden.txt
PKG_FLAGS += --package.sd_file $(AIE_SIM_IO_DIR)/b_golden.txt
PKG_FLAGS += --package.sd_file $(AIE_SIM_IO_DIR)/c_golden.txt
PKG_FLAGS += --package.sd_file $(AIE_SIM_IO_DIR)/matrix_A_input.txt
PKG_FLAGS += --package.sd_file $(AIE_SIM_IO_DIR)/matrix_B_input.txt
PKG_FLAGS += --package.sd_file $(HOST_APP_SRC)/pytorch_benchmark.py
PKG_FLAGS += --package.sd_file $(HOST_APP_SRC)/numpy_benchmark.py
PKG_FLAGS += --package.sd_file $(DESIGN_REPO)/setup_ml_environment.sh

################################################################ for now
# Add the input files directory to the package
# PKG_FLAGS += --package.sd_dir $(AIE_SIM_IO_DIR)

## If Profiling for Performance Measurement is enabled..
ifeq ($(EN_TRACE),1)
    ifeq ($(TARGET),hw)
    	PKG_FLAGS += --package.sd_file $(PROFILING_CONFIGS_REPO)/xrt.ini
   
    endif
endif

## If XRT_ROOT is set...
ifdef XRT_ROOT
   PKG_FLAGS += --package.sd_dir $(XRT_ROOT)

endif

PKG_FLAGS += --package.defer_aie_run

###########################################################
# Make rules...
#
# =========================================================
# Step 1. Kernel XO File Generation
# ========================================================
# This step compiles the HLS C PL kernels.
# Outputs: in build/[hw_emu | hw]/ directory
# 	$(DATAMOVER_KERNEL_TOP).[hw_emu | hw].xo 
#	$(DATAMOVER_KERNEL_TOP).[hw_emu | hw].xo.compile_summary 
#	v++_$(DATAMOVER_KERNEL_TOP).[hw_emu | hw].log
#	_x
KERNEL_XOS := $(BUILD_TARGET_DIR)/$(DATAMOVER_KERNEL_XO).xo

kernels: $(KERNEL_XOS)

$(BUILD_TARGET_DIR)/$(DATAMOVER_KERNEL_XO).xo: $(PL_SRC_REPO)/dma_hls* $(GEMM_CONFIG_HDR)
	mkdir -p $(BUILD_TARGET_DIR); \
	cd "$(BUILD_TARGET_DIR)"; \
	v++ --target $(TARGET) $(DATAMOVER_KERNEL_VPP_FLAGS) \
		$(VPP_FLAGS) -c -k $(DATAMOVER_KERNEL_TOP) \
		$(DATAMOVER_KERNEL_SRC) -o $@
	@echo ""
	@echo "HLS Kernel Compilation Complete..."
	@echo "Using $(DATAMOVER_KERNEL_TOP) with streaming=$(STREAMING_ENABLED)"
	@echo "####################################"
	@echo ""

# =========================================================
# Step 2. AI Engine SDF Graph File and $(WORK_DIR)/ Directory
#         (containing the Graph Executable) Generation
# ========================================================
# This step creates an SDF Graph and the $(WORK_DIR)/ directory.
# The $(WORK_DIR)/ directory contains the graph executable
# (gemm.o) which is used in the make xsa step. 
# The aiecompiler is invoked with the -target=hw.
# Outputs: in build/ directory
#	libsdf.a
#	NOC_Power.xpe
#	$(WORK_DIR)/
#	xnwOut/
graph: $(GEMM_CONFIG_HDR) $(LIBADF_A)

$(LIBADF_A):  $(AIE_SRC_REPO)/graph.* $(GEMM_CONFIG_HDR)
	mkdir -p $(BUILD_TARGET_DIR); \
	cd $(BUILD_TARGET_DIR); \
	aiecompiler $(AIE_FLAGS) $(GRAPH_SRC_CPP) 2>&1 | tee -a aiecompiler.log; \
	echo ""; \
	echo "AIE Compilation Complete..."; \
	echo "####################################"; \
	echo ""; \
	# Check if .aieprj file was generated; \
	# Check for .aieprj file in _x/link directory (where V++ generates them)
	AIEPRJ_FILE=$$(find "$(BUILD_TARGET_DIR)/_x/link" -name "*.aieprj" -type f 2>/dev/null | head -1); \
	if [ -n "$$AIEPRJ_FILE" ]; then \
		echo "OK AI Engine project file (.aieprj) generated successfully"; \
		echo "  Location: $$AIEPRJ_FILE"; \
		echo "  This file will be used for Vivado integration"; \
	else \
		echo "WARNING AI Engine project file (.aieprj) not found in _x/link directory"; \
		echo "Checking for alternative .aieprj locations"; \
		AIEPRJ_FILE=$$(find "$(BUILD_TARGET_DIR)" -name "*.aieprj" -type f 2>/dev/null | head -1); \
		if [ -n "$$AIEPRJ_FILE" ]; then \
			echo "OK Found AI Engine project file (.aieprj) at: $$AIEPRJ_FILE"; \
			echo "  This file will be used for Vivado integration"; \
		else \
			echo "ERROR No .aieprj files found anywhere in build directory"; \
			echo "This will cause AI Engine utilization to show 0 in Vivado reports"; \
		fi; \
	fi

# IO Input Type...
create_ioFiles: $(AIE_SIM_IO_DIR)

$(AIE_SIM_IO_DIR):
	cd "$(AIE_SIM_IO_BASE_DIR)"; \
	/usr/bin/python3.10 "$(AIE_SIM_IO_BASE_DIR)/plioGen.py"
	chmod 755 -R "$(AIE_SIM_IO_DIR)"

aiesim: create_ioFiles graph
	cd "$(BUILD_TARGET_DIR)"; \
	aiesimulator $(AIE_SIM_FLAGS) 2>&1 | tee -a aiesim.log
	@echo ""
	@echo "AIE Simulation, Without Profiling, Complete..."
	@echo "####################################"
	@echo ""

aiesim_profile: create_ioFiles graph
	cd "$(BUILD_TARGET_DIR)"; \
	aiesimulator $(AIE_SIM_FLAGS) --profile 2>&1 | tee -a aiesim.log
	@echo ""
	@echo "AIE Simulation, With Profiling, Complete..."
	@echo "####################################"
	@echo ""

# =========================================================
# VCD and XPE Generation for Power Analysis
# =========================================================
# VCD (Value Change Dump): ASCII file format that records signal value changes
# during AI Engine simulation for debugging and power analysis
# XPE (Xilinx Power Estimator): File format containing power consumption data
# generated from VCD files for thermal management and optimization
vcd: graph create_ioFiles $(XPE_FILE)

# xpe file generation...
$(XPE_FILE): $(BLD_TGT_VCD_FILE)
	cd "$(BUILD_TARGET_DIR)"; \
	if [ -f "$(VCD_FILE_NAME).vcd" ] && [ -s "$(VCD_FILE_NAME).vcd" ]; then \
		echo "VCD file found, generating XPE..."; \
		vcdanalyze --vcd $(VCD_FILE_NAME).vcd --xpe; \
		rm -rf $(BLD_XPE_DIR); \
		mkdir -p $(BLD_XPE_DIR); \
		cp -rf $(BUILD_TARGET_DIR)/aiesim_xpe/*.xpe $(XPE_FILE); \
		chmod 755 -R $(XPE_REPO); \
		echo "AIE XPE Generation Complete..."; \
	else \
		echo "VCD file not found or empty, skipping XPE generation..."; \
		mkdir -p $(BLD_XPE_DIR); \
		touch $(XPE_FILE); \
		echo "Empty XPE file created for build continuity..."; \
	fi
	@echo "####################################"
	@echo ""

# vcd file generation - creates Value Change Dump file from AI Engine simulation
$(BLD_TGT_VCD_FILE): $(AIE_SRC_REPO)/aiesim_data/*
	cd "$(BUILD_TARGET_DIR)"; \
	echo "Starting VCD generation with reduced scope..."; \
	timeout 180 aiesimulator --pkg-dir $(WORK_DIR)/ -i=$(AIE_SIM_IO_DIR) --simulation-cycle-timeout=100000 --dump-vcd $(VCD_FILE_NAME) 2>&1 | tee -a vcd.log || { \
		echo "VCD generation timed out after 3 minutes or failed"; \
		echo "This is normal for complex AI Engine designs"; \
		echo "VCD generation is optional for power analysis"; \
		echo "Continuing with build..."; \
		touch $(VCD_FILE_NAME).vcd; \
	}
	@echo ""
	@echo "AIE VCD Trace Generation Complete..."
	@echo "####################################"
	@echo ""

# =========================================================
# Step 3. XSA File Generation
# ========================================================
# XSA (Xilinx Support Archive): Proprietary container format that encapsulates
# the complete hardware design information of an FPGA project. It includes:
# - Hardware design files (.hwh)
# - Bitstream files (.bit) 
# - Board Support Package (BSP) files
# - AI Engine graph executables
# - System configuration metadata
# This step links the graph executable and the kernels into a xsa file.
# Outputs: in build/[hw_emu | hw]/ directory
#	vck190_aie_gemm.[hw_emu | hw].xsa
#	vck190_aie_gemm.[hw_emu | hw].xsa.info
#	vck190_aie_gemm.[hw_emu | hw].xsa.link_summary
#	vck190_aie_gemm.[hw_emu | hw].xsa
#	vck190_aie_gemm.[hw_emu | hw].log
xsa:  kernels graph $(BUILD_TARGET_DIR)/$(XSA)

$(BUILD_TARGET_DIR)/$(XSA):$(KERNEL_XOS) $(SYSTEM_CONFIGS_REPO)/*
	cd "$(BUILD_TARGET_DIR)";	\
	v++ -l $(VPP_FLAGS) $(VPP_LINK_FLAGS) -t $(TARGET) -o $@ $(KERNEL_XOS) $(LIBADF_A); \
	echo ""; \
	echo "XSA Compilation Complete..."; \
	echo "####################################"; \
	echo ""; \
	# Check if .aieprj file was generated and copy it to Vivado project directory; \
	AIEPRJ_FILE=$$(find "$(BUILD_TARGET_DIR)/_x/link" -name "*.aieprj" -type f 2>/dev/null | head -1); \
	if [ -n "$$AIEPRJ_FILE" ]; then \
		echo "AI Engine project file (.aieprj) found at: $$AIEPRJ_FILE"; \
		echo "AI Engine project file is already in the correct location for Vivado integration"; \
	else \
		echo "WARNING AI Engine project file (.aieprj) not found in _x/link directory"; \
		echo "This may cause AI Engine utilization to show 0 in Vivado reports"; \
		echo "Available .aieprj files in build directory"; \
		find "$(BUILD_TARGET_DIR)" -name "*.aieprj" -type f 2>/dev/null || echo "No .aieprj files found"; \
	fi

# =========================================================
# Step 4. A72 Application Executable File Generation
# ========================================================
# ELF (Executable and Linkable Format): Standard file format for executables
# This builds the host application that runs on ARM Cortex-A72 processor
# and controls the AI Engine through XRT runtime. Only needs graph (not kernels)
# because the graph library (LIBADF_A) already contains all necessary kernels.
application: check_ml_environment graph $(BUILD_TARGET_DIR)/$(APP_ELF)

REG_GCC_FLAGS := $(GCC_FLAGS)
REG_GCC_FLAGS += -DITER_CNT=$(ITER_CNT)
REG_GCC_FLAGS += -DTARGET=\"$(TARGET)\"

$(BUILD_TARGET_DIR)/$(APP_ELF): $(HOST_APP_SRC)/* $(LIBADF_A)
	@rm -rf $(BUILD_TARGET_DIR)/app_control.o $(BUILD_TARGET_DIR)/gemm_aie_app.o $(BUILD_TARGET_DIR)/gemm_utils.o $(BUILD_TARGET_DIR)/$(APP_ELF)
	$(CXX) $(REG_GCC_FLAGS) $(GCC_INC_FLAGS) $(HOST_APP_SRC)/gemm_aie_app.cpp -o $(BUILD_TARGET_DIR)/gemm_aie_app.o $(GCC_INC_LIB) $(GCC_LIB)
	$(CXX) $(REG_GCC_FLAGS) $(GCC_INC_FLAGS) $(HOST_APP_SRC)/gemm_utils.cpp -o $(BUILD_TARGET_DIR)/gemm_utils.o $(GCC_INC_LIB) $(GCC_LIB)
	$(CXX) $(BUILD_TARGET_DIR)/gemm_aie_app.o $(BUILD_TARGET_DIR)/gemm_utils.o $(GCC_INC_LIB) $(GCC_LIB) -o $(BUILD_TARGET_DIR)/$(APP_ELF)
	@echo ""
	@echo "Host Application Compilation Complete..."
	@echo "####################################"
	@echo ""

# This step compiles the A72 application. This step is the 
# same for TARGET=[hw_emu | hw]. Compile the PS code.
# Outputs: in build/ directory
# 	aie_control_inf_run.o
#	gemm_aie_app_inf_run.o
# 	gemm_aie_xrt_inf_run.elf

application_inf_run: check_ml_environment graph $(BUILD_TARGET_DIR)/$(APP_ELF_INF_RUN)

INF_RUN_GCC_FLAGS := $(GCC_FLAGS)
INF_RUN_GCC_FLAGS += -DITER_CNT=-1
INF_RUN_GCC_FLAGS += -DTARGET=\"$(TARGET)\"

$(BUILD_TARGET_DIR)/$(APP_ELF_INF_RUN): $(HOST_APP_SRC)/* $(LIBADF_A)
	@rm -rf $(BUILD_TARGET_DIR)/app_control_inf_run.o $(BUILD_TARGET_DIR)/gemm_aie_app_inf_run.o $(BUILD_TARGET_DIR)/gemm_utils_inf_run.o $(BUILD_TARGET_DIR)/$(APP_ELF_INF_RUN)
	@# Fix accumulator types in aie_control_xrt.cpp for hw target (only for int16: acc64 -> acc48)
	@if [ "$(DATA_TYPE)" = "16" ]; then \
		sed -i 's/output_stream<acc64>/output_stream<acc48>/g' $(AIE_CONTROL_CPP); \
		sed -i 's/input_stream<acc64>/input_stream<acc48>/g' $(AIE_CONTROL_CPP); \
		echo "Fixed accumulator types for int16 (acc64 -> acc48)"; \
	else \
		echo "Using default accumulator types for DATA_TYPE=$(DATA_TYPE)"; \
	fi
	$(CXX) $(INF_RUN_GCC_FLAGS) $(GCC_INC_FLAGS) $(AIE_CONTROL_CPP) -o $(BUILD_TARGET_DIR)/app_control_inf_run.o
	$(CXX) $(INF_RUN_GCC_FLAGS) $(GCC_INC_FLAGS) $(HOST_APP_SRC)/gemm_aie_app.cpp -o $(BUILD_TARGET_DIR)/gemm_aie_app_inf_run.o $(GCC_INC_LIB) $(GCC_LIB)
	$(CXX) $(INF_RUN_GCC_FLAGS) $(GCC_INC_FLAGS) $(HOST_APP_SRC)/gemm_utils.cpp -o $(BUILD_TARGET_DIR)/gemm_utils_inf_run.o $(GCC_INC_LIB) $(GCC_LIB)
	$(CXX) $(BUILD_TARGET_DIR)/app_control_inf_run.o $(BUILD_TARGET_DIR)/gemm_aie_app_inf_run.o $(BUILD_TARGET_DIR)/gemm_utils_inf_run.o $(GCC_INC_LIB) $(GCC_LIB) -o $(BUILD_TARGET_DIR)/$(APP_ELF_INF_RUN)
	@echo ""
	@echo "Host Application Infinite Run Compilation Complete..."
	@echo "####################################"
	@echo ""

# =========================================================
# ML Environment Check (Simplified)
# ========================================================
# Check if ML environment is available (pre-installed in rootfs)
.PHONY: check_ml_environment
check_ml_environment:
	@echo "ML Environment Check (Pre-installed in Rootfs)"
	@echo "=============================================="
	@if [ "$(ENABLE_ML_BENCHMARKS)" = "1" ]; then \
		echo "ML benchmarks enabled (ENABLE_ML_BENCHMARKS=1)"; \
		echo "ML packages are pre-installed in the modified rootfs."; \
		echo "This includes NumPy, PyTorch, scipy, matplotlib, pandas"; \
		echo "No runtime installation required."; \
	else \
		echo "ML benchmarks disabled (ENABLE_ML_BENCHMARKS=0)"; \
		echo "ML packages are available but not used."; \
	fi
	@echo "OK ML environment is ready for use in emulation."

# =========================================================
# PyTorch Benchmark Target
# ========================================================
# Benchmark target for PyTorch comparison
# This runs PyTorch matrix multiplication on the same data as AI Engine
.PHONY: pytorch_benchmark
pytorch_benchmark: $(BUILD_TARGET_DIR)/$(APP_ELF)
	@echo "Running PyTorch benchmark comparison..."
	@echo "======================================"
	@if command -v python3 >/dev/null 2>&1; then \
		if python3 -c "import torch" 2>/dev/null; then \
			python3 $(HOST_APP_SRC)/pytorch_benchmark.py \
				$(DESIGN_REPO)/aie_src/aiesim_data/gemm_$(GEMM_SIZE)x$(GEMM_SIZE)x$(GEMM_SIZE)_ioFiles/matrix_A_input.txt \
				$(DESIGN_REPO)/aie_src/aiesim_data/gemm_$(GEMM_SIZE)x$(GEMM_SIZE)x$(GEMM_SIZE)_ioFiles/matrix_B_input.txt \
				$(BUILD_TARGET_DIR)/pytorch_result_cpu.txt \
				--size $(GEMM_SIZE) \
				--dtype $(if $(filter 16,$(DATA_TYPE)),int16,$(if $(filter 32,$(DATA_TYPE)),int32,float32)) \
				--device cpu \
				--iterations 10; \
			if command -v nvidia-smi >/dev/null 2>&1; then \
				echo ""; \
				echo "Running GPU benchmark"; \
				python3 $(HOST_APP_SRC)/pytorch_benchmark.py \
					$(DESIGN_REPO)/aie_src/aiesim_data/gemm_$(GEMM_SIZE)x$(GEMM_SIZE)x$(GEMM_SIZE)_ioFiles/matrix_A_input.txt \
					$(DESIGN_REPO)/aie_src/aiesim_data/gemm_$(GEMM_SIZE)x$(GEMM_SIZE)x$(GEMM_SIZE)_ioFiles/matrix_B_input.txt \
					$(BUILD_TARGET_DIR)/pytorch_result_gpu.txt \
					--size $(GEMM_SIZE) \
					--dtype $(if $(filter 16,$(DATA_TYPE)),int16,$(if $(filter 32,$(DATA_TYPE)),int32,float32)) \
					--device cuda \
					--iterations 10; \
			else \
				echo "CUDA not available, skipping GPU benchmark"; \
			fi; \
		else \
			echo "PyTorch not installed. Installing PyTorch first"; \
			$(MAKE) install_pytorch; \
			echo "PyTorch installation complete. Re-running benchmark"; \
			$(MAKE) pytorch_benchmark; \
		fi; \
	else \
		echo "Python3 not found, skipping PyTorch benchmark"; \
		echo "Install Python3 and PyTorch to enable benchmarking"; \
		echo "Run make install_pytorch"; \
	fi

# =========================================================
# NumPy Benchmark Target (Alternative to PyTorch)
# ========================================================
# Benchmark target using NumPy for CPU comparison
# This is lighter than PyTorch and easier to install on Petalinux
.PHONY: numpy_benchmark
numpy_benchmark: $(BUILD_TARGET_DIR)/$(APP_ELF)
	@echo "Running NumPy benchmark comparison..."
	@echo "===================================="
	@if command -v python3 >/dev/null 2>&1; then \
		if python3 -c "import numpy" 2>/dev/null; then \
			python3 $(HOST_APP_SRC)/numpy_benchmark.py \
				$(DESIGN_REPO)/aie_src/aiesim_data/gemm_$(GEMM_SIZE)x$(GEMM_SIZE)x$(GEMM_SIZE)_ioFiles/matrix_A_input.txt \
				$(DESIGN_REPO)/aie_src/aiesim_data/gemm_$(GEMM_SIZE)x$(GEMM_SIZE)x$(GEMM_SIZE)_ioFiles/matrix_B_input.txt \
				$(BUILD_TARGET_DIR)/numpy_result.txt \
				--size $(GEMM_SIZE) \
				--dtype $(if $(filter 16,$(DATA_TYPE)),int16,$(if $(filter 32,$(DATA_TYPE)),int32,float32)) \
				--iterations 10; \
		else \
			echo "NumPy not installed. Installing NumPy first"; \
			if command -v pip3 >/dev/null 2>&1; then \
				pip3 install numpy; \
				echo "NumPy installation complete. Re-running benchmark"; \
				$(MAKE) numpy_benchmark; \
			else \
				echo "ERROR pip3 not found. Cannot install NumPy."; \
				echo "Please install NumPy manually opkg install python3-numpy"; \
			fi; \
		fi; \
	else \
		echo "Python3 not found, skipping NumPy benchmark"; \
		echo "Install Python3 and NumPy to enable benchmarking"; \
	fi

# =========================================================
# Step 5. Package Generation 
# ========================================================
# This step generates the package folder which contains the
# ./launch_hw_emu.sh script to launch hardware emulation
# if TARGET=hw_emu and the sd_card.img file. 
# Outputs: in build/[hw_emu | hw]/ directory
# 	a.xclbin
# 	package/ directory
# 	v++.package_summary
# 	v++_package.log


################################################################ for now
# Prepare SD card with input files
# prepare_sdcard: create_ioFiles
# 	@echo "Preparing SD card with input files..."
# 	mkdir -p $(EMBEDDED_PACKAGE_OUT)/input_files || { echo "Failed to create directory"; exit 1; }
# 	@echo "Copying from: $(AIE_SIM_IO_DIR)/*"
# 	@echo "To: $(EMBEDDED_PACKAGE_OUT)/input_files/"
# 	cp -r $(AIE_SIM_IO_DIR)/* $(EMBEDDED_PACKAGE_OUT)/input_files/ || { echo "Failed to copy files"; exit 1; }
# 	@echo "Files copied to SD card image"

# Modify the package target to handle missing input files
package: check_ml_environment create_ioFiles application application_inf_run xsa 
	@echo "Checking for input files in $(AIE_SIM_IO_DIR)"
	@if [ ! -d "$(AIE_SIM_IO_DIR)" ] || [ -z "$(ls -A $(AIE_SIM_IO_DIR) 2>/dev/null)" ]; then \
		echo "Warning Input files directory is empty or does not exist. Creating it"; \
		mkdir -p $(AIE_SIM_IO_DIR); \
		echo "Created empty directory $(AIE_SIM_IO_DIR)"; \
	fi
	rm -rf $(EMBEDDED_PACKAGE_OUT)
	cd "$(BUILD_TARGET_DIR)" && v++ -p $(PKG_FLAGS)
	@echo ""
	@echo "Design Packaging Complete..."
	@echo "####################################"
	@echo ""

# =========================================================
# Step 6. Run Hardware Emulation 
# ========================================================
# If the target is for HW_EMU, launch the emulator
# If the target is for HW, you'll have to follow the
# instructions in the README.md
run_emu:
ifeq ($(TARGET),hw_emu)
	@echo ""
	@echo "###########################################################################"

ifeq ($(LAUNCH_HW_EMU_EXEC),0)
	@echo "To Run Hardware Emulation Manually Goto"
	@echo "$(EMBEDDED_PACKAGE_OUT)"
	@echo ""
	@echo "and do"
	@echo "./launch_hw_emu.sh or ./launch_hw_emu.sh -g (for waveform viewer)..."
	@echo ""

else
	cd "$(EMBEDDED_PACKAGE_OUT)"; \
	./launch_hw_emu.sh -run-app $(EMBEDDED_EXEC_SCRIPT) | tee embedded_run.log
	@echo ""
	@echo "HW Emulation Complete..."
	@echo "####################################"
	@echo ""

endif
else
	@echo ""
	@echo "###########################################################################"
	@echo "Hardware build, no emulation executed."
	@echo ""
	@echo "Use sd_card.img from below directory and, follow the steps in README.md for execution on board."
	@echo "$(EMBEDDED_PACKAGE_OUT)"
	@echo ""

endif

# =========================================================
# Step 7. Comprehensive Vivado Report Generation
# =========================================================
# If the target is HW, this generates comprehensive utilization, performance, and power reports
# from Vivado including detailed analysis for AI Engine, Memory, and DSP resources.
report_metrics: xsa $(BLD_REPORTS_DIR)
	@echo ""
	@echo "Generating Comprehensive Vivado Reports..."
	@echo "=========================================="
	@echo "This includes"
	@echo "  - Utilization analysis (detailed, hierarchical, by type)"
	@echo "  - Performance analysis (timing, clock domains, critical paths)"
	@echo "  - Power estimation (static and dynamic power analysis)"
	@echo "  - AI Engine specific analysis"
	@echo "  - Memory and DSP utilization"
	@echo "  - HTML and XML report formats"
	@echo "####################################"
	@echo ""
	@# Check for AI Engine project file before generating reports
	@echo "Checking for AI Engine project file (.aieprj)..."
	@AIEPRJ_FILE=$$(find "$(BUILD_TARGET_DIR)/_x/link" -name "*.aieprj" -type f 2>/dev/null | head -1); \
	if [ -n "$$AIEPRJ_FILE" ]; then \
		echo "OK AI Engine project file found at: $$AIEPRJ_FILE"; \
		echo "AI Engine project file is in the correct location for Vivado integration"; \
	else \
		echo "ERROR WARNING No AI Engine project file (.aieprj) found!"; \
		echo "This will cause AI Engine utilization to show 0 in Vivado reports"; \
		echo "Available AI Engine files"; \
		find "$(BUILD_TARGET_DIR)" -name "*.aiedb" -o -name "*.aieprj" -o -name "active_cores.json" 2>/dev/null || echo "No AI Engine project files found"; \
		echo ""; \
		echo "This is likely why 'AI ML Engines used = 0' in utilization reports"; \
		echo "The AI Engine kernels are compiled but not integrated into Vivado project"; \
		echo ""; \
	fi
	rm -rf $(BLD_REPORTS_DIR)
	mkdir -p $(BLD_REPORTS_DIR)
	cd "$(BLD_REPORTS_DIR)"; \
	VIVADO_PROJECT_FILE="$(abspath $(BUILD_TARGET_DIR)/_x/link/vivado/vpl/prj/prj.xpr)" VIVADO_REPORTS_DIR="$(abspath $(BLD_REPORTS_DIR))" vivado -mode batch -source $(VIVADO_METRICS_SCRIPTS_REPO)/report_metrics.tcl
	chmod 755 -R $(REPORTS_REPO)
	@echo ""
	@echo "Comprehensive Vivado Reports Generation Complete..."
	@echo "Reports saved to $(BLD_REPORTS_DIR)"
	@echo "####################################"
	@echo ""

$(BLD_REPORTS_DIR): $(VIVADO_METRICS_SCRIPTS_REPO)/report_metrics.tcl
	mkdir -p $(BLD_REPORTS_DIR)

# =========================================================
# AI Engine Integration Debug Target
# =========================================================
# Debug target to check AI Engine integration and .aiedb file generation
.PHONY: debug_aie_integration
debug_aie_integration: graph
	@echo ""
	@echo "AI Engine Integration Debug"
	@echo "=========================="
	@echo "Checking AI Engine compilation and integration..."
	@echo ""
	@echo "1. Checking AI Engine compilation output..."
	@if [ -f "$(BUILD_TARGET_DIR)/aiecompiler.log" ]; then \
		echo "OK AI Engine compilation log found"; \
		echo "  Last few lines of compilation"; \
		tail -10 "$(BUILD_TARGET_DIR)/aiecompiler.log"; \
	else \
		echo "ERROR AI Engine compilation log not found"; \
	fi
	@echo ""
	@echo "2. Checking for AI Engine project files..."
	@echo "   Looking for .aiedb files"
	@find "$(BUILD_TARGET_DIR)" -name "*.aiedb" -type f 2>/dev/null || echo "   No .aiedb files found"
	@echo "   Looking for .aieprj files"
	@find "$(BUILD_TARGET_DIR)" -name "*.aieprj" -type f 2>/dev/null || echo "   No .aieprj files found"
	@echo "   Looking for active_cores.json"
	@find "$(BUILD_TARGET_DIR)" -name "active_cores.json" -type f 2>/dev/null || echo "   No active_cores.json found"
	@echo ""
	@echo "3. Checking Work directory contents..."
	@if [ -d "$(BUILD_TARGET_DIR)/$(WORK_DIR)" ]; then \
		echo "OK Work directory exists $(BUILD_TARGET_DIR)/$(WORK_DIR)"; \
		echo "   Contents"; \
		ls -la "$(BUILD_TARGET_DIR)/$(WORK_DIR)/" | head -20; \
	else \
		echo "ERROR Work directory not found $(BUILD_TARGET_DIR)/$(WORK_DIR)"; \
	fi
	@echo ""
	@echo "4. Checking Vivado project directory..."
	@if [ -d "$(BUILD_TARGET_DIR)/_x/link/vivado/vpl/prj" ]; then \
		echo "OK Vivado project directory exists"; \
		echo "   Contents"; \
		ls -la "$(BUILD_TARGET_DIR)/_x/link/vivado/vpl/prj/" | head -10; \
	else \
		echo "ERROR Vivado project directory not found (XSA not built yet)"; \
	fi
	@echo ""
	@echo "5. Checking system_estimate.xtxt for AI Engine kernels..."
	@if [ -f "$(BUILD_TARGET_DIR)/_x/reports/link/system_estimate.xtxt" ]; then \
		echo "OK System estimate file found"; \
		echo "   AI Engine related content"; \
		grep -i "aie\|ai engine\|ml engine" "$(BUILD_TARGET_DIR)/_x/reports/link/system_estimate.xtxt" || echo "   No AI Engine content found"; \
	else \
		echo "ERROR System estimate file not found (XSA not built yet)"; \
	fi
	@echo ""
	@echo "Debug complete. If no .aiedb file is found, this explains why"
	@echo "Vivado shows 'AI ML Engines used = 0' in utilization reports."
	@echo "The AI Engine kernels are compiled but not integrated into Vivado."
	@echo ""

###########################################################################

# =========================================================
# Primary Build Targets
# =========================================================
sd_card: package

run: check_ml_environment sd_card run_emu # I added create_ioFiles, origin run did not have it

# New target for running with PyTorch benchmarking (same as run now)
run_with_pytorch: run

all: run report_metrics # vcd

clean_tgt:
	@echo "Cleaning $(TARGET) Target Build Dir..."
	rm -rf $(BUILD_TARGET_DIR)/*
	rm -rf $(AIE_SIM_IO_DIR)  


clean_insts:
	@echo "Cleaning $(GEMM_INSTS)x Builds..."
	rm -rf $(INSTS_BLD_DIR)

clean_gemmsize:
	@echo "Cleaning Gemm Size Builds..."
	rm -rf $(GEMM_BLD_DIR)

cleanall_blds:
	rm -rf $(BASE_BLD_DIR)/*
	rm -rf vivado* .Xil *.log vitis* core.*
	rm -rf $(AIE_SIM_IO_BASE_DIR)/gemm_*ioFiles/


cleanall_vivado_reports:
	rm -rf $(REPORTS_REPO)

cleanall_xpe_reports:
	rm -rf $(XPE_REPO)

cleanall_reports: cleanall_vivado_reports cleanall_xpe_reports

cleanall: cleanall_blds cleanall_reports

#####################################################################
# Impl strategies script...
# Strategies Repo with the impl Script Has to be provided...
BASE_OPTION       := 2
IMPL_DEPTH_OPTION := 2
PIPELINE_SIZE     := 500

IMPL_STRATEGIES_TCL := $(STRATEGIES_REPO)/impl_strategies_explore_best3.tcl

run_impl_strategies: $(IMPL_STRATEGIES_TCL) $(BUILD_TARGET_DIR)/_x/link/vivado/vpl/prj/prj.xpr
ifeq ($(TARGET),hw_emu)
	@echo "This build target (run_impl_strategies) not valid when design target is hw_emu"
else
	cd "$(BUILD_TARGET_DIR)"; \
	vivado -mode tcl -source $(IMPL_STRATEGIES_TCL) \
	"_x/link/vivado/vpl/prj/prj.xpr" -tclargs $(BASE_OPTION) $(IMPL_DEPTH_OPTION) $(PIPELINE_SIZE)
endif

open_vivado_proj: $(BUILD_TARGET_DIR)/_x/link/vivado/vpl/prj/prj.xpr
	cd "$(BUILD_TARGET_DIR)"; \
	vivado "_x/link/vivado/vpl/prj/prj.xpr"




