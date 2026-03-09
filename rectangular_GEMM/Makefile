# ============================================================================
# AI Engine GEMM Build System Makefile
# ============================================================================
# Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: MIT
#
# Comprehensive build system for AI Engine GEMM on Versal ACAP platforms.
# Supports hw_emu and hw targets with configurable matrix dimensions and data types.
#
# Usage:
#   make help           # Show all targets
#   make sd_card        # Build complete design
#   make run            # Build and run emulation
#   make all            # Build, run, and generate reports
#   make cleanall       # Clean all artifacts
#
# Configuration: Set in design/design_configs/config.json
# ============================================================================

.PHONY: help
help::
	@echo ""
	@echo "AI Engine GEMM Build System"
	@echo "=========================="
	@echo ""
	@echo "Primary Targets:"
	@echo "  make sd_card        # Build complete design (kernels + graph + xsa + app + package)"
	@echo "  make run            # Build and run emulation"
	@echo "  make all            # Build, run, and generate comprehensive reports"
	@echo "  make cleanall       # Clean all build artifacts"
	@echo ""
	@echo "Build Components:"
	@echo "  make kernels        # Compile HLS kernels"
	@echo "  make graph          # Compile AI Engine graph"
	@echo "  make xsa            # Generate XSA file"
	@echo "  make application    # Compile host application"
	@echo "  make package        # Create SD card package"
	@echo ""
	@echo "Simulation & Analysis:"
	@echo "  setup_ml_environment.sh  # Setup ML environment at runtime (PyTorch, NumPy)"
	@echo "  make aiesim         # Run AI Engine simulation"
	@echo "  make vcd            # Generate VCD and XPE files for power analysis"
	@echo "  make report_metrics # Generate Vivado utilization reports (hw only)"
	@echo "  make debug_aie_integration # Debug AI Engine integration issues"
	@echo ""
	@echo "Cleanup:"
	@echo "  make clean_tgt      # Clean specific target"
	@echo "  make clean_gemmsize # Clean specific GEMM size"
	@echo "  make cleanall       # Clean everything"
	@echo ""
	@echo "Configuration: Set in design/design_configs/config.json"
	@echo "  TARGET, GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B, DATA_TYPE, SPLIT, CASC_LN, PL_FREQ, etc."
	@echo ""

# Print all options passed to Makefile
print-%  : ; @echo $* = $($*)

# ============================================================================
# ENVIRONMENT CONFIGURATION
# ============================================================================
# DSPLIB_VITIS ?= /home/mgrailoo/Vitis_Libraries
DSPLIB_ROOT = $(DSPLIB_VITIS)/dsp
# PLATFORM ?= /media/josnu02/large_SDD/mgrailoo/platform_edge_hwemu/platform_edge_hwemu/export/platform_edge_hwemu/platform_edge_hwemu.xpfm
# COMMON_IMAGE_VERSAL ?= /home/mgrailoo/xilinx-versal-common-v2024.1


# ============================================================================
# CONFIGURATION PARSING
# ============================================================================
CONFIG_JSON := design/design_configs/config.json

# Core build parameters from config.json
TARGET := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('TARGET','hw'))")
# Read GEMM_SIZE_A, AB, B (REQUIRED - must be explicitly provided in config.json)
GEMM_SIZE_A := $(shell python3 -c "import json,sys;c=json.load(open('$(CONFIG_JSON)'));v=c.get('GEMM_SIZE_A');sys.exit(1) if v is None else print(int(v))" 2>/dev/null)
GEMM_SIZE_AB := $(shell python3 -c "import json,sys;c=json.load(open('$(CONFIG_JSON)'));v=c.get('GEMM_SIZE_AB');sys.exit(1) if v is None else print(int(v))" 2>/dev/null)
GEMM_SIZE_B := $(shell python3 -c "import json,sys;c=json.load(open('$(CONFIG_JSON)'));v=c.get('GEMM_SIZE_B');sys.exit(1) if v is None else print(int(v))" 2>/dev/null)
# Validate that all three GEMM_SIZE values are provided
ifeq ($(GEMM_SIZE_A),)
    $(error GEMM_SIZE_A is required in $(CONFIG_JSON). Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B.)
endif
ifeq ($(GEMM_SIZE_AB),)
    $(error GEMM_SIZE_AB is required in $(CONFIG_JSON). Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B.)
endif
ifeq ($(GEMM_SIZE_B),)
    $(error GEMM_SIZE_B is required in $(CONFIG_JSON). Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B.)
endif
# Read DIM for calculation purposes only (used to compute DIM_A, DIM_AB, DIM_B)
# DIM is not used in code after DIM_A/AB/B are computed
DIM := $(shell python3 -c "import json;c=json.load(open('$(CONFIG_JSON)'));print(c.get('DIM', 16))")
# Calculate DIM_A, DIM_B as min(DIM, GEMM_SIZE_A/B / SPLIT); DIM_AB depends only on GEMM_SIZE_AB and CASC_LN_AB
DIM_A := $(shell python3 -c "import json;c=json.load(open('$(CONFIG_JSON)'));d=c.get('DIM',16);ga=$(GEMM_SIZE_A);sa=c.get('SPLIT_A') or c.get('SPLIT',2);print(min(d, ga // max(1, sa)))")
DIM_AB := $(shell python3 -c "import json;c=json.load(open('$(CONFIG_JSON)'));gab=$(GEMM_SIZE_AB);cl=c.get('CASC_LN_AB') or c.get('CASC_LN',8);print(gab // max(1, cl))")
DIM_B := $(shell python3 -c "import json;c=json.load(open('$(CONFIG_JSON)'));d=c.get('DIM',16);gb=$(GEMM_SIZE_B);sb=c.get('SPLIT_B') or c.get('SPLIT',2);print(min(d, gb // max(1, sb)))")
# Read new explicit naming (SPLIT_A, SPLIT_B, CASC_LN_AB) with backward compatibility
SPLIT_A := $(shell python3 -c "import json;c=json.load(open('$(CONFIG_JSON)'));print(c.get('SPLIT_A') or c.get('SPLIT',2))")
SPLIT_B := $(shell python3 -c "import json;c=json.load(open('$(CONFIG_JSON)'));print(c.get('SPLIT_B') or c.get('SPLIT',2))")
CASC_LN_AB := $(shell python3 -c "import json;c=json.load(open('$(CONFIG_JSON)'));print(c.get('CASC_LN_AB') or c.get('CASC_LN',8))")
# Legacy names for backward compatibility
SPLIT := $(SPLIT_B)  # For Matrix C split (uses SPLIT_B)
CASC_LN := $(CASC_LN_AB)
GEMM_INSTS := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('GEMM_INSTS',1))")
PL_FREQ := $(shell python3 -c "import json;v=json.load(open('$(CONFIG_JSON)')).get('PL_FREQ','312.5'); print(str(v).replace('.','_') if isinstance(v,(int,float)) else v)")
ITER_CNT := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('ITER_CNT',1))")
EN_TRACE := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('EN_TRACE',0))")
USE_DDR_ONLY_MODE := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('USE_DDR_ONLY_MODE',0))")
# ENABLE_ML_BENCHMARKS removed - ML setup done at runtime

# Data type configuration
DATA_TYPE_STR := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('DATA_TYPE','int16'))")
DATA_TYPE := $(shell python3 -c "import json; dt=json.load(open('$(CONFIG_JSON)')).get('DATA_TYPE','int16'); print(16 if dt=='int16' else 32 if dt=='int32' else 33 if dt=='float' else 16)")
WRD_LN := $(shell python3 -c 'import json; d=json.load(open("$(CONFIG_JSON)")); wr=d.get("WRD_LN"); dt=d.get("DATA_TYPE","int16"); print(int(wr) if isinstance(wr,int) else (8 if dt=="int16" else 4 if dt=="int32" else 4 if dt=="float" else 8))')
N_SAMPLES := $(shell python3 -c "import json;print(json.load(open('$(CONFIG_JSON)')).get('N_SAMPLES',1))")

# Sub-tile sizes from config.json (user-configurable)
SUB_TILE_A := $(shell python3 -c "import json;c=json.load(open('$(CONFIG_JSON)'));print(c.get('SUB_TILE_A') or 4)")
SUB_TILE_AB := $(shell python3 -c "import json;c=json.load(open('$(CONFIG_JSON)'));print(c.get('SUB_TILE_AB') or 4)")
SUB_TILE_B := $(shell python3 -c "import json;c=json.load(open('$(CONFIG_JSON)'));print(c.get('SUB_TILE_B') or 2)")

# Calculated parameters
# Use the three separate GEMM_SIZE values for calculations
GEMM_SZ_SPLIT_A := $(shell echo "$$(( $(GEMM_SIZE_A) / $(SPLIT_A) ))")
GEMM_SZ_SPLIT_B := $(shell echo "$$(( $(GEMM_SIZE_B) / $(SPLIT_B) ))")
GEMM_SZ_CASC := $(shell echo "$$(( $(GEMM_SIZE_AB) / $(CASC_LN_AB) ))")
# Effective dimensions limited by split/cascade constraints
DIM_A_EFF := $(shell if [ $(DIM_A) -lt $(GEMM_SZ_SPLIT_A) ]; then echo $(DIM_A); else echo $(GEMM_SZ_SPLIT_A); fi)
DIM_AB_EFF := $(shell if [ $(DIM_AB) -lt $(GEMM_SZ_CASC) ]; then echo $(DIM_AB); else echo $(GEMM_SZ_CASC); fi)
DIM_B_EFF := $(shell if [ $(DIM_B) -lt $(GEMM_SZ_SPLIT_B) ]; then echo $(DIM_B); else echo $(GEMM_SZ_SPLIT_B); fi)
# Use effective dimensions for calculations
DIM_A := $(DIM_A_EFF)
DIM_AB := $(DIM_AB_EFF)
DIM_B := $(DIM_B_EFF)
MAT_DIMS := $(GEMM_SIZE_A)x$(GEMM_SIZE_AB)x$(GEMM_SIZE_B)
TARGET_UPPER := $(shell echo $(TARGET) | tr a-z A-Z)
HZ_UNIT := 1000000
VPP_CLOCK_FREQ := $(shell echo "$(shell echo $(PL_FREQ) | tr '_' '.') * $(HZ_UNIT)" | bc | cut -d'.' -f1)
LAUNCH_HW_EMU_EXEC := 0

# Graph iteration count
# Formula: GRAPH_ITER_CNT = (GEMM_SIZE_A × (GEMM_SIZE_B / SPLIT_B)) / (DIM_A × DIM_B)
# Each output file contains: GEMM_SIZE_A rows × (GEMM_SIZE_B / SPLIT_B) columns
# Each iteration produces: DIM_A × DIM_B elements per file
# Therefore: GRAPH_ITER_CNT = (elements per file) / (elements per iteration)
ifeq ($(ITER_CNT),-1)
    GRAPH_ITER_CNT := -1
else
    GRAPH_ITER_CNT := $(shell echo "$$(( $(GEMM_SIZE_A) * $(GEMM_SIZE_B) / $(SPLIT_B) / $(DIM_A) / $(DIM_B) ))")
endif


# ============================================================================
# DIRECTORY STRUCTURE
# ============================================================================
PROJECT_REPO := $(shell dirname $(shell readlink -f $(lastword $(MAKEFILE_LIST))))

# Source directories
DESIGN_REPO := $(PROJECT_REPO)/design
AIE_SRC_REPO := $(DESIGN_REPO)/aie_src
PL_SRC_REPO := $(DESIGN_REPO)/pl_src
HOST_APP_SRC := $(DESIGN_REPO)/host_app_src
SYSTEM_CONFIGS_REPO := $(DESIGN_REPO)/system_configs
PROFILING_CONFIGS_REPO := $(DESIGN_REPO)/profiling_configs
EXEC_SCRIPTS_REPO := $(DESIGN_REPO)/exec_scripts
VIVADO_METRICS_SCRIPTS_REPO := $(DESIGN_REPO)/vivado_metrics_scripts
DIRECTIVES_REPO := $(DESIGN_REPO)/directives
ML_WHEELS_DIR := $(DESIGN_REPO)/ml_wheels

# Build directories
BASE_BLD_DIR := $(PROJECT_REPO)/build
GEMM_BLD_DIR := $(BASE_BLD_DIR)/gemm_$(MAT_DIMS)
INSTS_BLD_DIR := $(GEMM_BLD_DIR)/x$(GEMM_INSTS)
BUILD_TARGET_DIR := $(INSTS_BLD_DIR)/$(TARGET)

# Report directories
REPORTS_REPO := $(PROJECT_REPO)/reports
BLD_REPORTS_DIR := $(REPORTS_REPO)/gemm_$(MAT_DIMS)/x$(GEMM_INSTS)
XPE_REPO := $(PROJECT_REPO)/xpe_dir
BLD_XPE_DIR := $(XPE_REPO)/gemm_$(MAT_DIMS)/x$(GEMM_INSTS)/$(TARGET)

# Package and simulation directories
EMBEDDED_PACKAGE_OUT := $(BUILD_TARGET_DIR)/package
EMBEDDED_EXEC_SCRIPT := run_script.sh
WORK_DIR := Work
AIE_SIM_IO_BASE_DIR := $(AIE_SRC_REPO)/aiesim_data
AIE_SIM_IO_DIR := $(AIE_SIM_IO_BASE_DIR)/gemm_$(MAT_DIMS)_ioFiles

# File names
VCD_FILE_NAME := gemm_$(MAT_DIMS)_x$(GEMM_INSTS)
BLD_TGT_VCD_FILE := $(BUILD_TARGET_DIR)/$(VCD_FILE_NAME).vcd
XPE_FILE := $(BLD_XPE_DIR)/graph_$(VCD_FILE_NAME).xpe

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
	@echo "Debug GEMM_SIZE_A=$(GEMM_SIZE_A), GEMM_SIZE_AB=$(GEMM_SIZE_AB), GEMM_SIZE_B=$(GEMM_SIZE_B), DIM_A=$(DIM_A), DIM_AB=$(DIM_AB), DIM_B=$(DIM_B), SPLIT=$(SPLIT), GRAPH_ITER_CNT=$(GRAPH_ITER_CNT)"
	@echo '#ifndef GEMM_CONFIG_H' > $(GEMM_CONFIG_HDR)
	@echo '#define GEMM_CONFIG_H' >> $(GEMM_CONFIG_HDR)
	@echo '#define TARGET_$(TARGET_UPPER) 1' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix dimensions (A, AB, B)' >> $(GEMM_CONFIG_HDR)
	@echo '#define GEMM_SIZE_A $(GEMM_SIZE_A)' >> $(GEMM_CONFIG_HDR)
	@echo '#define GEMM_SIZE_AB $(GEMM_SIZE_AB)' >> $(GEMM_CONFIG_HDR)
	@echo '#define GEMM_SIZE_B $(GEMM_SIZE_B)' >> $(GEMM_CONFIG_HDR)
	@echo '#define DATA_TYPE $(DATA_TYPE)' >> $(GEMM_CONFIG_HDR)
	@echo '// Tile dimensions (A, AB, B)' >> $(GEMM_CONFIG_HDR)
	@echo '// DIM is not defined here - only DIM_A, DIM_AB, DIM_B are used in code' >> $(GEMM_CONFIG_HDR)
	@echo '#define DIM_A $(DIM_A)' >> $(GEMM_CONFIG_HDR)
	@echo '#define DIM_AB $(DIM_AB)' >> $(GEMM_CONFIG_HDR)
	@echo '#define DIM_B $(DIM_B)' >> $(GEMM_CONFIG_HDR)
	@echo '#define SPLIT_A $(SPLIT_A)' >> $(GEMM_CONFIG_HDR)
	@echo '#define SPLIT_B $(SPLIT_B)' >> $(GEMM_CONFIG_HDR)
	@echo '#define CASC_LN_AB $(CASC_LN_AB)' >> $(GEMM_CONFIG_HDR)
	@echo '// Legacy names for backward compatibility' >> $(GEMM_CONFIG_HDR)
	@echo '#define SPLIT $(SPLIT_B)  // For Matrix C split (uses SPLIT_B)' >> $(GEMM_CONFIG_HDR)
	@echo '#define CASC_LN $(CASC_LN_AB)' >> $(GEMM_CONFIG_HDR)
	@echo '#define WRD_LN $(WRD_LN)' >> $(GEMM_CONFIG_HDR)
	@echo '#define N_SAMPLES $(N_SAMPLES)' >> $(GEMM_CONFIG_HDR)
	@echo '#define ITER_CNT $(ITER_CNT)' >> $(GEMM_CONFIG_HDR)
	@echo '#define GEMM_INSTS $(GEMM_INSTS)' >> $(GEMM_CONFIG_HDR)
	@echo '#define EN_TRACE $(EN_TRACE)' >> $(GEMM_CONFIG_HDR)
	@echo '#define PL_FREQ $(PL_FREQ)' >> $(GEMM_CONFIG_HDR)
	@echo '// DDR-only mode: If 1, forces DDR-only mode (bypasses PS RAM)' >> $(GEMM_CONFIG_HDR)
	@echo '#define USE_DDR_ONLY_MODE $(USE_DDR_ONLY_MODE)' >> $(GEMM_CONFIG_HDR)
	@echo '// Calculated graph iteration count' >> $(GEMM_CONFIG_HDR)
	@echo '// GRAPH_ITER_CNT = (GEMM_SIZE_A * GEMM_SIZE_B / SPLIT_B) / (DIM_A * DIM_B)' >> $(GEMM_CONFIG_HDR)
	@echo '#define GRAPH_ITER_CNT $(GRAPH_ITER_CNT)' >> $(GEMM_CONFIG_HDR)
	@echo '// Additional constants needed by host app and other components' >> $(GEMM_CONFIG_HDR)
	@echo '#define NUM_A_FILES $(CASC_LN_AB)' >> $(GEMM_CONFIG_HDR)
	@echo '#define NUM_B_FILES $(shell echo "$$(( $(SPLIT_B) * $(CASC_LN_AB) ))")' >> $(GEMM_CONFIG_HDR)
	@echo '#define NUM_C_FILES $(SPLIT_B)' >> $(GEMM_CONFIG_HDR)
	@if [ -n "$(TILE_MEM_BYTES)" ]; then echo '#define TILE_MEM_BYTES $(TILE_MEM_BYTES)' >> $(GEMM_CONFIG_HDR); fi
	@if [ -n "$(SUB_TILE_A)" ]; then echo '#define SUB_TILE_A $(SUB_TILE_A)' >> $(GEMM_CONFIG_HDR); fi
	@if [ -n "$(SUB_TILE_AB)" ]; then echo '#define SUB_TILE_AB $(SUB_TILE_AB)' >> $(GEMM_CONFIG_HDR); fi
	@if [ -n "$(SUB_TILE_B)" ]; then echo '#define SUB_TILE_B $(SUB_TILE_B)' >> $(GEMM_CONFIG_HDR); fi
	@echo '// Calculate base sizes for individual matrices based on corrected plioGen.py formula' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix A: BASE_MATA_SZ = GEMM_SZ_SPLIT_A * GEMM_SZ_CASC * broadcast_count_a * SPLIT_A // WRD_LN' >> $(GEMM_CONFIG_HDR)
	@echo '// where: tiles_per_row_in_block_a = (GEMM_SZ_SPLIT_A) // DIM_A' >> $(GEMM_CONFIG_HDR)
	@echo '//        broadcast_count_a = max(1, tiles_per_row_in_block_a)' >> $(GEMM_CONFIG_HDR)
	@echo '//        GEMM_SZ_SPLIT_A = GEMM_SIZE_A // SPLIT_A' >> $(GEMM_CONFIG_HDR)
	@echo '//        GEMM_SZ_CASC = GEMM_SIZE_AB // CASC_LN_AB' >> $(GEMM_CONFIG_HDR)
	@echo '#define TILES_PER_ROW_IN_BLOCK_A ((GEMM_SIZE_A / SPLIT_A) / DIM_A)' >> $(GEMM_CONFIG_HDR)
	@echo '#define BROADCAST_COUNT_A ((GEMM_SIZE_B / SPLIT_B) / DIM_B)' >> $(GEMM_CONFIG_HDR)
	@echo '#define BASE_MATA_SZ (((GEMM_SIZE_A / SPLIT_A) * (GEMM_SIZE_AB / CASC_LN_AB) * BROADCAST_COUNT_A * SPLIT_A) / WRD_LN)' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix B: BASE_MATB_SZ = GEMM_SZ_CASC * GEMM_SZ_SPLIT_B * broadcast_count_b * SPLIT_B // WRD_LN' >> $(GEMM_CONFIG_HDR)
	@echo '// TILES_PER_COL_IN_BLOCK_B: number of tiles per column in block (based on column split, not row cascade)' >> $(GEMM_CONFIG_HDR)
	@echo '#define TILES_PER_COL_IN_BLOCK_B ((GEMM_SIZE_B / SPLIT_B) / DIM_B)' >> $(GEMM_CONFIG_HDR)
	@echo '#define BROADCAST_COUNT_B ((GEMM_SIZE_A / SPLIT_A) / DIM_A)' >> $(GEMM_CONFIG_HDR)
	@echo '#define BASE_MATB_SZ (((GEMM_SIZE_AB / CASC_LN_AB) * (GEMM_SIZE_B / SPLIT_B) * BROADCAST_COUNT_B * SPLIT_B) / WRD_LN)' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix C: Each split has (GEMM_SIZE_A * GEMM_SIZE_B) / SPLIT_B / WRD_LN elements' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix C is split only in columns by SPLIT_B (all rows, split columns)' >> $(GEMM_CONFIG_HDR)
	@echo '#define BASE_MATC_SZ ((GEMM_SIZE_A * GEMM_SIZE_B) / SPLIT_B / WRD_LN)' >> $(GEMM_CONFIG_HDR)
	@echo '// Exact sizes: Total matrix sizes for rectangular matrices (GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B)' >> $(GEMM_CONFIG_HDR)
	@echo '// These are calculated by multiplying BASE sizes by the number of files (NUM_*_FILES)' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix A: BASE_MATA_SZ is per cascade file, multiply by NUM_A_FILES (= CASC_LN_AB) to get total' >> $(GEMM_CONFIG_HDR)
	@echo '#define EXACT_MATA_SZ (BASE_MATA_SZ * NUM_A_FILES)' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix B: BASE_MATB_SZ is per split-cascade file, multiply by NUM_B_FILES (= SPLIT_B * CASC_LN_AB) to get total' >> $(GEMM_CONFIG_HDR)
	@echo '// Note: BASE_MATB_SZ is per split-cascade file (one file per split per cascade level)' >> $(GEMM_CONFIG_HDR)
	@echo '#define EXACT_MATB_SZ (BASE_MATB_SZ * NUM_B_FILES)' >> $(GEMM_CONFIG_HDR)
	@echo '// Matrix C: BASE_MATC_SZ is per split file, multiply by NUM_C_FILES (= SPLIT_B) to get total' >> $(GEMM_CONFIG_HDR)
	@echo '#define EXACT_MATC_SZ (BASE_MATC_SZ * NUM_C_FILES)' >> $(GEMM_CONFIG_HDR)
	@echo '// Aligned buffer sizes (with 4096-byte alignment padding)' >> $(GEMM_CONFIG_HDR)
	@echo '// ap_int<128> is 16 bytes, so we calculate aligned sizes' >> $(GEMM_CONFIG_HDR)
	@echo '#define BUFFER_ALIGNMENT 4096' >> $(GEMM_CONFIG_HDR)
	@echo '#define ALIGNED_MATA_BYTES (((EXACT_MATA_SZ * 16) + BUFFER_ALIGNMENT - 1) / BUFFER_ALIGNMENT * BUFFER_ALIGNMENT)' >> $(GEMM_CONFIG_HDR)
	@echo '#define ALIGNED_MATB_BYTES (((EXACT_MATB_SZ * 16) + BUFFER_ALIGNMENT - 1) / BUFFER_ALIGNMENT * BUFFER_ALIGNMENT)' >> $(GEMM_CONFIG_HDR)
	@echo '#define ALIGNED_MATC_BYTES (((EXACT_MATC_SZ * 16) + BUFFER_ALIGNMENT - 1) / BUFFER_ALIGNMENT * BUFFER_ALIGNMENT)' >> $(GEMM_CONFIG_HDR)
	@echo '#endif' >> $(GEMM_CONFIG_HDR)
	@# Persist computed GRAPH_ITER_CNT back into config.json for visibility in tools/scripts
	@python3 -c "import json,sys; p='$(CONFIG_JSON)'; d=json.load(open(p)); d['GRAPH_ITER_CNT']=$(GRAPH_ITER_CNT); json.dump(d, open(p,'w'), indent=2); print(f'Updated GRAPH_ITER_CNT={$(GRAPH_ITER_CNT)} in {p}')" || echo 'Warning: could not update GRAPH_ITER_CNT in $(CONFIG_JSON)'
	@# (Listing of valid TP_DIMs moved to host_app logging to avoid Makefile portability issues)

# ============================================================================
# BUILD TARGETS
# ============================================================================
# Kernel targets
DATAMOVER_KERNEL_TOP := dma_hls
DATAMOVER_KERNEL_XO := $(DATAMOVER_KERNEL_TOP).$(TARGET)
DATAMOVER_KERNEL_SRC := $(PL_SRC_REPO)/$(DATAMOVER_KERNEL_TOP).cpp

# AI Engine targets
LIBADF_A := $(BUILD_TARGET_DIR)/libadf.a
# LIBADF_A_SIM is now defined in graph_sim target to use libadf_sim.a (separate file)
GRAPH_SRC_CPP := $(AIE_SRC_REPO)/graph.cpp

# Application targets
APP_ELF := gemm_aie_xrt.elf
APP_ELF_INF_RUN := gemm_aie_xrt_inf_run.elf
APP_SRC_CPP := $(HOST_APP_SRC)/gemm_aie_app.cpp $(HOST_APP_SRC)/gemm_utils.cpp
AIE_CONTROL_CPP := $(BUILD_TARGET_DIR)/$(WORK_DIR)/ps/c_rts/aie_control_xrt.cpp

# Hardware targets
XSA := ve2302_aie_gemm.$(TARGET).xsa

# ============================================================================
# AI ENGINE COMPILER FLAGS
# ============================================================================
AIE_FLAGS := -include=$(AIE_SRC_REPO)
AIE_FLAGS += -include=$(DESIGN_REPO)/design_configs
AIE_FLAGS += -include=$(PROJECT_REPO)
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/include/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/src/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/tests/aie/inc
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/tests/aie/src
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L2/include/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L2/tests/aie/common/inc
AIE_FLAGS += --verbose
AIE_FLAGS += --Xpreproc="-DITER_CNT=$(ITER_CNT)"
AIE_FLAGS += --Xpreproc="-DGEMM_SIZE_A=$(GEMM_SIZE_A)"
AIE_FLAGS += --Xpreproc="-DGEMM_SIZE_AB=$(GEMM_SIZE_AB)"
AIE_FLAGS += --Xpreproc="-DGEMM_SIZE_B=$(GEMM_SIZE_B)"
AIE_FLAGS += --Xpreproc="-DGEMM_INSTS=$(GEMM_INSTS)"
AIE_FLAGS += --Xpreproc="-DSPLIT_A=$(SPLIT_A)"
AIE_FLAGS += --Xpreproc="-DSPLIT_B=$(SPLIT_B)"
AIE_FLAGS += --Xpreproc="-DCASC_LN_AB=$(CASC_LN_AB)"
# Legacy names for backward compatibility
AIE_FLAGS += --Xpreproc="-DSPLIT=$(SPLIT_B)"
AIE_FLAGS += --Xpreproc="-DCASC_LN=$(CASC_LN_AB)"
AIE_FLAGS += --Xpreproc="-DDIM_A=$(DIM_A)"
AIE_FLAGS += --Xpreproc="-DDIM_AB=$(DIM_AB)"
AIE_FLAGS += --Xpreproc="-DDIM_B=$(DIM_B)"
AIE_FLAGS += --Xpreproc="-DDATA_TYPE=$(DATA_TYPE)"
AIE_FLAGS += --platform=$(PLATFORM)
AIE_FLAGS += --log-level=5
AIE_FLAGS += --pl-freq=$(shell echo $(PL_FREQ) | tr '_' '.')
AIE_FLAGS += --Xmapper=BufferOptLevel5
AIE_FLAGS += --Xrouter=DMAFIFOsInFreeBankOnly
AIE_FLAGS += --workdir=$(WORK_DIR)
AIE_FLAGS += --output-archive=libadf.a


# AI Engine simulator flags
AIE_SIM_FLAGS := --pkg-dir $(WORK_DIR)/
AIE_SIM_FLAGS += -i=$(AIE_SIM_IO_DIR)

# AI Engine compiler flags for simulation (includes __AIESIM__ macro)
AIE_FLAGS_SIM := $(AIE_FLAGS)
AIE_FLAGS_SIM += --Xpreproc="-D__AIESIM__"

# ============================================================================
# COMPILER FLAGS
# ============================================================================
# Host application compiler flags
GCC_FLAGS := -O -c -std=c++17 -D__linux__ -D__PS_ENABLE_AIE__ -DXAIE_DEBUG
GCC_FLAGS += -DGEMM_INSTS=$(GEMM_INSTS) -DGEMM_SIZE_A=$(GEMM_SIZE_A) -DGEMM_SIZE_AB=$(GEMM_SIZE_AB) -DGEMM_SIZE_B=$(GEMM_SIZE_B) -D__AIEARCH__=10
GCC_FLAGS += -DITER_CNT=$(ITER_CNT) -DDATA_TYPE=$(DATA_TYPE)

# Host application include paths
GCC_INC_FLAGS := -I$(SDKTARGETSYSROOT)/usr/include/xrt -I$(XILINX_VITIS)/aietools/include/
GCC_INC_FLAGS += -I$(SDKTARGETSYSROOT)/usr/include -I$(SDKTARGETSYSROOT)/usr/lib
GCC_INC_FLAGS += -I$(AIE_SRC_REPO) -I$(HOST_APP_SRC) -I$(DESIGN_REPO)/design_configs -I$(PROJECT_REPO)
GCC_INC_FLAGS += -I$(DSPLIB_ROOT)/L1/include/aie -I$(DSPLIB_ROOT)/L1/src/aie
GCC_INC_FLAGS += -I$(DSPLIB_ROOT)/L1/tests/aie/inc -I$(DSPLIB_ROOT)/L1/tests/aie/src
GCC_INC_FLAGS += -I$(DSPLIB_ROOT)/L2/include/aie -I$(DSPLIB_ROOT)/L2/tests/aie/common/inc
GCC_INC_FLAGS += -I$(XILINX_XRT)/include -I$(XILINX_VITIS)/include
GCC_INC_FLAGS += -I$(XILINX_HLS)/include/ -I$(PL_SRC_REPO)
GCC_INC_FLAGS += -I$(SDKTARGETSYSROOT)/usr/include/eigen3

# Host application library paths and libraries
# Use --sysroot to tell linker where to find system libraries
GCC_INC_LIB := -Wl,--sysroot=$(SDKTARGETSYSROOT) -L$(SDKTARGETSYSROOT)/usr/lib -L$(SDKTARGETSYSROOT)/lib
GCC_INC_LIB += -L$(XILINX_VITIS)/aietools/lib/aarch64.o -L$(XILINX_VITIS)/aietools/lib/lnx64.o
GCC_INC_LIB += -L$(XILINX_XRT)/lib
GCC_LIB := -ladf_api_xrt -lxrt_coreutil -luuid

# V++ compiler flags
VPP_FLAGS := --platform $(PLATFORM) --save-temps --temp_dir $(BUILD_TARGET_DIR)/_x --verbose -g



# Kernel V++ flags
DATAMOVER_KERNEL_VPP_FLAGS := --hls.clock $(VPP_CLOCK_FREQ):$(DATAMOVER_KERNEL_TOP)
DATAMOVER_KERNEL_VPP_FLAGS += -D GEMM_SIZE_A=$(GEMM_SIZE_A) -D GEMM_SIZE_AB=$(GEMM_SIZE_AB) -D GEMM_SIZE_B=$(GEMM_SIZE_B) -D SPLIT=$(SPLIT) -D CASC_LN=$(CASC_LN)
DATAMOVER_KERNEL_VPP_FLAGS += -D DIM_A=$(DIM_A) -D DIM_AB=$(DIM_AB) -D DIM_B=$(DIM_B) -D DATA_TYPE=$(DATA_TYPE)
DATAMOVER_KERNEL_VPP_FLAGS += --hls.jobs 32

# V++ link flags
VPP_LINK_FLAGS := --clock.freqHz $(VPP_CLOCK_FREQ):$(DATAMOVER_KERNEL_TOP)_0
VPP_LINK_FLAGS += --clock.defaultTolerance 0.001
VPP_LINK_FLAGS += --config $(SYSTEM_CONFIGS_REPO)/x$(GEMM_INSTS).cfg
VPP_LINK_FLAGS += --vivado.prop fileset.sim_1.xsim.simulate.log_all_signals=true
VPP_LINK_FLAGS += --vivado.prop run.impl_1.STEPS.PLACE_DESIGN.TCL.PRE=$(DIRECTIVES_REPO)/prohibit_select_bli_bels_for_hold.tcl
VPP_LINK_FLAGS += --vivado.prop run.synth_1.STEPS.SYNTH_DESIGN.ARGS.CONTROL_SET_OPT_THRESHOLD=16
VPP_LINK_FLAGS += --vivado.prop run.impl_1.{strategy}={Performance_ExplorePostRoutePhysOpt}
VPP_LINK_FLAGS += --vivado.prop run.impl_1.{STEPS.PLACE_DESIGN.ARGS.DIRECTIVE}={SSI_BalanceSLLs}
VPP_LINK_FLAGS += --vivado.prop run.impl_1.{STEPS.PHYS_OPT_DESIGN.ARGS.IS_ENABLED}={true}
VPP_LINK_FLAGS += --vivado.prop run.impl_1.{STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE}={AggressiveExplore}

# Profiling flags
ifeq ($(EN_TRACE),1)
    ifeq ($(TARGET),hw)
        VPP_LINK_FLAGS += --profile.data $(DATAMOVER_KERNEL_TOP):all:strmInp_from_C0
    endif
endif

# ============================================================================
# PACKAGING FLAGS
# ============================================================================
PKG_FLAGS := -t $(TARGET) --save-temps --temp_dir $(BUILD_TARGET_DIR)/_x -f $(PLATFORM)
PKG_FLAGS += --package.rootfs $(COMMON_IMAGE_VERSAL)/rootfs.ext4
PKG_FLAGS += --package.kernel_image $(COMMON_IMAGE_VERSAL)/Image
PKG_FLAGS += --package.boot_mode=sd --package.out_dir $(EMBEDDED_PACKAGE_OUT)
PKG_FLAGS += --package.image_format=ext4 --package.defer_aie_run
# FAT32 size: Default (512 MB) - Enough after removing large golden files
# PKG_FLAGS += --package.ext4_fat32_size=3
PKG_FLAGS += --package.sd_file $(BUILD_TARGET_DIR)/$(APP_ELF) $(BUILD_TARGET_DIR)/$(XSA) $(LIBADF_A)
PKG_FLAGS += --package.sd_file $(BUILD_TARGET_DIR)/$(APP_ELF_INF_RUN)
PKG_FLAGS += --package.sd_file $(EXEC_SCRIPTS_REPO)/$(EMBEDDED_EXEC_SCRIPT)
# Removed large golden files (~2GB) to keep boot partition small and compatible
PKG_FLAGS += --package.sd_file $(AIE_SIM_IO_DIR)/a_golden.txt
PKG_FLAGS += --package.sd_file $(AIE_SIM_IO_DIR)/b_golden.txt
# PKG_FLAGS += --package.sd_file $(AIE_SIM_IO_DIR)/c_golden.txt
# Matrix input files and PyTorch benchmark removed to save memory (~20 MB)
# PKG_FLAGS += --package.sd_file $(AIE_SIM_IO_DIR)/matrix_A_input.txt
# PKG_FLAGS += --package.sd_file $(AIE_SIM_IO_DIR)/matrix_B_input.txt
PKG_FLAGS += --package.sd_file $(DESIGN_REPO)/setup_ml_environment.sh
# PKG_FLAGS += --package.sd_file $(DESIGN_REPO)/RUNTIME_ML_SETUP_GUIDE.md  # File does not exist - commented out
# PKG_FLAGS += --package.sd_file $(DESIGN_REPO)/README_RUNTIME_ML.md
PKG_FLAGS += --package.sd_file $(HOST_APP_SRC)/pytorch_benchmark.py
# PKG_FLAGS += --package.sd_file $(HOST_APP_SRC)/numpy_benchmark.py

# Include local PyTorch wheel cache in the SD card payload if it exists
# DISABLED: PyTorch benchmark removed, so ml_wheels not needed (saves 112 MB)
ifneq ($(wildcard $(ML_WHEELS_DIR)),)
PKG_FLAGS += --package.sd_dir $(ML_WHEELS_DIR)
endif

# Profiling and XRT flags
ifeq ($(EN_TRACE),1)
    ifeq ($(TARGET),hw)
        PKG_FLAGS += --package.sd_file $(PROFILING_CONFIGS_REPO)/xrt.ini
    endif
endif

ifdef XRT_ROOT
    PKG_FLAGS += --package.sd_dir $(XRT_ROOT)
endif

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
# Graph compilation for hardware (without __AIESIM__)
# Uses Work/ directory (default) - separate from Work_sim/ used by graph_sim
graph: $(GEMM_CONFIG_HDR) $(LIBADF_A)

$(LIBADF_A):  $(AIE_SRC_REPO)/graph.* $(GEMM_CONFIG_HDR)
	@echo "Building AI Engine graph for hardware (without __AIESIM__)..."
	mkdir -p $(BUILD_TARGET_DIR); \
	cd $(BUILD_TARGET_DIR); \
	aiecompiler $(AIE_FLAGS) $(GRAPH_SRC_CPP) 2>&1 | tee -a aiecompiler.log; \
	echo ""; \
	echo "AIE Compilation Complete (Hardware Build)..."; \
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

# Graph compilation for simulation (with __AIESIM__ defined)
# Note: graph_sim uses separate output file and Work directory to avoid conflicts
LIBADF_A_SIM := $(BUILD_TARGET_DIR)/libadf_sim.a
WORK_DIR_SIM := Work_sim
graph_sim: $(GEMM_CONFIG_HDR) $(LIBADF_A_SIM)

$(LIBADF_A_SIM):  $(AIE_SRC_REPO)/graph.* $(GEMM_CONFIG_HDR)
	mkdir -p $(BUILD_TARGET_DIR); \
	cd $(BUILD_TARGET_DIR); \
	aiecompiler $(AIE_FLAGS_SIM) --workdir=$(WORK_DIR_SIM) --output-archive=libadf_sim.a $(GRAPH_SRC_CPP) 2>&1 | tee -a aiecompiler.log; \
	echo ""; \
	echo "AIE Compilation for Simulation Complete (__AIESIM__ defined)..."; \
	echo "####################################"; \
	echo ""

aiesim: create_ioFiles graph_sim
	cd "$(BUILD_TARGET_DIR)"; \
	# Use Work_sim directory for simulation (created by graph_sim)
	aiesimulator --pkg-dir $(WORK_DIR_SIM)/ -i=$(AIE_SIM_IO_DIR) 2>&1 | tee -a aiesim.log
	@echo ""
	@echo "AIE Simulation, Without Profiling, Complete..."
	@echo "####################################"
	@echo ""

aiesim_profile: create_ioFiles graph_sim
	cd "$(BUILD_TARGET_DIR)"; \
	# Use Work_sim directory for simulation (created by graph_sim)
	aiesimulator --pkg-dir $(WORK_DIR_SIM)/ -i=$(AIE_SIM_IO_DIR) --profile 2>&1 | tee -a aiesim.log
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
# 
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
application: graph $(BUILD_TARGET_DIR)/$(APP_ELF)

REG_GCC_FLAGS := $(GCC_FLAGS)
REG_GCC_FLAGS += -DITER_CNT=$(ITER_CNT)
REG_GCC_FLAGS += -DTARGET_STR=\"$(TARGET)\"

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

application_inf_run: graph $(BUILD_TARGET_DIR)/$(APP_ELF_INF_RUN)

INF_RUN_GCC_FLAGS := $(GCC_FLAGS)
INF_RUN_GCC_FLAGS += -DITER_CNT=-1
INF_RUN_GCC_FLAGS += -DTARGET_STR=\"$(TARGET)\"

$(BUILD_TARGET_DIR)/$(APP_ELF_INF_RUN): $(HOST_APP_SRC)/* $(LIBADF_A)
	@rm -rf $(BUILD_TARGET_DIR)/app_control_inf_run.o $(BUILD_TARGET_DIR)/gemm_aie_app_inf_run.o $(BUILD_TARGET_DIR)/gemm_utils_inf_run.o $(BUILD_TARGET_DIR)/$(APP_ELF_INF_RUN)
	@# Fix accumulator types in aie_control_xrt.cpp for hw target
	@if [ "$(DATA_TYPE)" = "16" ]; then \
		sed -i 's/output_stream<acc64>/output_stream<acc48>/g' $(AIE_CONTROL_CPP); \
		sed -i 's/input_stream<acc64>/input_stream<acc48>/g' $(AIE_CONTROL_CPP); \
		echo "Fixed accumulator types for int16 (acc64 -> acc48)"; \
	elif [ "$(DATA_TYPE)" = "32" ]; then \
		sed -i 's/output_stream<acc64>/output_stream<acc80>/g' $(AIE_CONTROL_CPP); \
		sed -i 's/input_stream<acc64>/input_stream<acc80>/g' $(AIE_CONTROL_CPP); \
		echo "Fixed accumulator types for int32 (acc64 -> acc80)"; \
	elif [ "$(DATA_TYPE)" = "33" ]; then \
		echo "Using acc64 for float (platform supports ACC64)"; \
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

# ML Environment setup removed - done at runtime using setup_ml_environment.sh

# PyTorch and NumPy benchmark targets removed - use setup_ml_environment.sh at runtime

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
package: create_ioFiles application application_inf_run xsa 
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

run: sd_card run_emu # I added create_ioFiles, origin run did not have it

# PyTorch benchmarking removed - use setup_ml_environment.sh --offline at runtime

all: run report_metrics # vcd aiesim_profile 

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






