/*
Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
SPDX-License-Identifier: MIT
*/

#include "gemm_utils.h"
#include "graph.h"
#include "../design_configs/gemm_config.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_graph.h"
#include "xrt/xrt_bo.h"
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

// ============================================================================
// TIMING AND DURATION FUNCTIONS
// ============================================================================

using HighResClock = std::chrono::high_resolution_clock;

// Returns elapsed microseconds since the provided start time
long long getElapsedMicroseconds(const HighResClock::time_point& start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(HighResClock::now() - start).count();
}

// Print a duration in both microseconds and milliseconds with a stage label
void printDurationUsMs(long long elapsed_us, const char* stage) {
    double elapsed_ms = static_cast<double>(elapsed_us) / 1000.0;
    printf("[%14lld us | %8.3f ms] %s\n", elapsed_us, elapsed_ms, stage);
}

// Helper that prints elapsed time since start and returns the duration in microseconds
long long logElapsedTime(const HighResClock::time_point& start, const char* stage) {
    long long elapsed_us = getElapsedMicroseconds(start);
    printDurationUsMs(elapsed_us, stage);
    return elapsed_us;
}

// Function to print elapsed time since start
void printElapsedTime(const HighResClock::time_point& start, const char* stage) {
    printDurationUsMs(getElapsedMicroseconds(start), stage);
}

// Print duration since t0 with a label
void printDurationSince(const HighResClock::time_point& t0, const char* label) {
    printDurationUsMs(getElapsedMicroseconds(t0), label);
}

// ============================================================================
// MEMORY AND BUFFER UTILITIES
// ============================================================================

// Function to print memory allocation info
void printMemoryInfo(const char* stage, size_t total_allocated) {
    printf("Memory status at %s: %zu KB allocated\n", stage, total_allocated / 1024);
}

// Function to verify buffer alignment
bool verifyBufferAlignment(const void* ptr) {
    return (reinterpret_cast<uintptr_t>(ptr) % BUFFER_ALIGNMENT) == 0;
}

// Function to verify buffer configuration and parameters
void verifyBufferConfiguration() {
    printf("Verifying buffer configuration and parameters...\n");
    
    // Verify GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B are powers of 2
    if ((GEMM_SIZE_A & (GEMM_SIZE_A - 1)) != 0) {
        printf("WARNING: GEMM_SIZE_A (%d) is not a power of 2\n", GEMM_SIZE_A);
    }
    if ((GEMM_SIZE_AB & (GEMM_SIZE_AB - 1)) != 0) {
        printf("WARNING: GEMM_SIZE_AB (%d) is not a power of 2\n", GEMM_SIZE_AB);
    }
    if ((GEMM_SIZE_B & (GEMM_SIZE_B - 1)) != 0) {
        printf("WARNING: GEMM_SIZE_B (%d) is not a power of 2\n", GEMM_SIZE_B);
    }
    
    // Verify DIM_A, DIM_AB, DIM_B are compatible with GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B
    if (GEMM_SIZE_A % DIM_A != 0) {
        printf("WARNING: GEMM_SIZE_A (%d) is not divisible by DIM_A (%d)\n", GEMM_SIZE_A, DIM_A);
    }
    if (GEMM_SIZE_AB % DIM_AB != 0) {
        printf("WARNING: GEMM_SIZE_AB (%d) is not divisible by DIM_AB (%d)\n", GEMM_SIZE_AB, DIM_AB);
    }
    if (GEMM_SIZE_B % DIM_B != 0) {
        printf("WARNING: GEMM_SIZE_B (%d) is not divisible by DIM_B (%d)\n", GEMM_SIZE_B, DIM_B);
    }
    
    // Verify GRAPH_ITER_CNT (same grouping as Makefile / gemm_config.h / plioGen)
    int expected_iter_cnt = (GEMM_SIZE_A * GEMM_SIZE_B / SPLIT_B) / (DIM_A * DIM_B);
    printf("Expected GRAPH_ITER_CNT: %d\n", expected_iter_cnt);
    printf("Actual GRAPH_ITER_CNT: %d\n", GRAPH_ITER_CNT);
}

// ============================================================================
// CONFIGURATION AND MATRIX INFORMATION PRINTING
// ============================================================================

// Print configuration and matrix size information
void printConfigurationInfo() {
    printf("\nBuffer size calculations:\n");
    printf("GEMM_SIZE_A = %d, GEMM_SIZE_AB = %d, GEMM_SIZE_B = %d\n", GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B);
    printf("SPLIT_A = %d, SPLIT_B = %d\n", SPLIT_A, SPLIT_B);
    printf("CASC_LN_AB = %d\n", CASC_LN_AB);
    printf("WRD_LN = %d\n", WRD_LN);
    printf("ITER_CNT = %d\n", ITER_CNT);
    printf("DIM_A = %d, DIM_AB = %d, DIM_B = %d\n", DIM_A, DIM_AB, DIM_B);
    printf("\nBase sizes:\n");
    printf("BASE_MATA_SZ = %d\n", BASE_MATA_SZ);
    printf("BASE_MATB_SZ = %d\n", BASE_MATB_SZ);
    printf("BASE_MATC_SZ = %d\n", BASE_MATC_SZ);
    printf("\nMatrix sizes (elements vs bytes):\n");
    printf("Matrix A: %d elements (%zu bytes exact, %zu bytes aligned)\n", 
           EXACT_MATA_SZ, EXACT_MATA_SZ * sizeof(ap_int<128>), ALIGNED_MATA_BYTES);
    printf("Matrix B: %d elements (%zu bytes exact, %zu bytes aligned)\n", 
           EXACT_MATB_SZ, EXACT_MATB_SZ * sizeof(ap_int<128>), ALIGNED_MATB_BYTES);
    printf("Matrix C: %d elements (%zu bytes exact, %zu bytes aligned)\n", 
           EXACT_MATC_SZ, EXACT_MATC_SZ * sizeof(ap_int<128>), ALIGNED_MATC_BYTES);
    printf("GRAPH_ITER_CNT = %d\n", GRAPH_ITER_CNT);
}

// Print valid tile dimension combinations
void printValidTileDimensions() {
    // List valid (TP_DIM_A, TP_DIM_AB, TP_DIM_B) at runtime for visibility
    int sub_m = 4, sub_k = 4, sub_n = 4;
    if (DATA_TYPE == 16) { sub_m=4; sub_k=4; sub_n=4; }
    else if (DATA_TYPE == 32) { sub_m=4; sub_k=4; sub_n=2; }
    int kseg = GEMM_SIZE_AB / CASC_LN_AB;
    printf("Valid (TP_DIM_A, TP_DIM_AB, TP_DIM_B): ");
    if ((GEMM_SIZE_AB % sub_k)==0 && (kseg % sub_k)==0) {
        int max_a = (DIM_A < (GEMM_SIZE_A / SPLIT_A)) ? DIM_A : (GEMM_SIZE_A / SPLIT_A);
        int max_b = (DIM_B < (GEMM_SIZE_B / SPLIT_B)) ? DIM_B : (GEMM_SIZE_B / SPLIT_B);
        bool first=true;
        for (int a=sub_m; a<=max_a; a+=sub_m) {
            if (((GEMM_SIZE_A / SPLIT_A) % a) != 0) continue;
            for (int b=sub_n; b<=max_b; b+=sub_n) {
                if (((GEMM_SIZE_B / SPLIT_B) % b) != 0) continue;
                if (!first) printf(", ");
                printf("(%d,%d,%d)", a, GEMM_SIZE_AB, b);
                first=false;
            }
        }
        if (first) printf("none");
    } else {
        printf("none");
    }
    printf("\n");
}

// Show detailed breakdown of how EXACT_MAT*_SZ are calculated from GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B
void printExactMatrixSizeCalculation() {
    printf("\n");
    printf("================================================================================\n");
    printf("EXACT MATRIX SIZE CALCULATION FROM GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B\n");
    printf("================================================================================\n");
    printf("\nInput Parameters:\n");
    printf("  GEMM_SIZE_A  = %d (rows of Matrix A, rows of Matrix C)\n", GEMM_SIZE_A);
    printf("  GEMM_SIZE_AB = %d (columns of Matrix A, rows of Matrix B)\n", GEMM_SIZE_AB);
    printf("  GEMM_SIZE_B  = %d (columns of Matrix B, columns of Matrix C)\n", GEMM_SIZE_B);
    printf("  SPLIT_A      = %d (row split factor for Matrix A)\n", SPLIT_A);
    printf("  SPLIT_B      = %d (column split factor for Matrix B and C)\n", SPLIT_B);
    printf("  CASC_LN_AB   = %d (cascade length for inner dimension AB)\n", CASC_LN_AB);
    printf("  DIM_A        = %d (tile dimension for A)\n", DIM_A);
    printf("  DIM_B        = %d (tile dimension for B)\n", DIM_B);
    printf("  WRD_LN       = %d (words per ap_int<128> element)\n", WRD_LN);
    
    // Calculate intermediate values
    int gemm_sz_split_a = GEMM_SIZE_A / SPLIT_A;
    int gemm_sz_casc = GEMM_SIZE_AB / CASC_LN_AB;
    int gemm_sz_split_b = GEMM_SIZE_B / SPLIT_B;
    int tiles_per_row_in_block_a = gemm_sz_split_a / DIM_A;
    int broadcast_count_a = (GEMM_SIZE_B / SPLIT_B) / DIM_B;
    int tiles_per_col_in_block_b = gemm_sz_split_b / DIM_B;
    int broadcast_count_b = (GEMM_SIZE_A / SPLIT_A) / DIM_A;
    
    printf("\n");
    printf("================================================================================\n");
    printf("MATRIX A CALCULATION (GEMM_SIZE_A × GEMM_SIZE_AB)\n");
    printf("================================================================================\n");
    printf("Matrix A dimensions: %d rows × %d columns\n", GEMM_SIZE_A, GEMM_SIZE_AB);
    printf("\nStep 1: Calculate raw matrix size in words\n");
    printf("  Raw Matrix A Words = (GEMM_SIZE_A × GEMM_SIZE_AB) / WRD_LN\n");
    printf("                     = (%d × %d) / %d\n", GEMM_SIZE_A, GEMM_SIZE_AB, WRD_LN);
    int raw_mata_words_calc = (GEMM_SIZE_A * GEMM_SIZE_AB) / WRD_LN;
    printf("                     = %d words\n", raw_mata_words_calc);
    printf("\nStep 2: Calculate EXACT_MATA_SZ (total words for raw matrix A)\n");
    printf("  EXACT_MATA_SZ = Raw Matrix A Words\n");
    printf("                 = %d elements\n", EXACT_MATA_SZ);
    printf("  Memory: %d elements × %zu bytes/element = %zu bytes (%.2f MB)\n",
           EXACT_MATA_SZ, sizeof(ap_int<128>), EXACT_MATA_SZ * sizeof(ap_int<128>),
           (EXACT_MATA_SZ * sizeof(ap_int<128>)) / (1024.0 * 1024.0));
    printf("\nNOTE: Matrix A is stored in DDR as raw row-major format.\n");
    printf("      DMA kernel (dma_hls.cpp) handles cascade transformation on-the-fly.\n");
    
    printf("\n");
    printf("================================================================================\n");
    printf("MATRIX B CALCULATION (GEMM_SIZE_AB × GEMM_SIZE_B)\n");
    printf("================================================================================\n");
    printf("Matrix B dimensions: %d rows × %d columns\n", GEMM_SIZE_AB, GEMM_SIZE_B);
    printf("\nStep 1: Calculate intermediate values\n");
    printf("  GEMM_SZ_CASC = GEMM_SIZE_AB / CASC_LN_AB = %d / %d = %d\n", GEMM_SIZE_AB, CASC_LN_AB, gemm_sz_casc);
    printf("  GEMM_SZ_SPLIT_B = GEMM_SIZE_B / SPLIT_B = %d / %d = %d\n", GEMM_SIZE_B, SPLIT_B, gemm_sz_split_b);
    printf("  TILES_PER_COL_IN_BLOCK_B = GEMM_SZ_SPLIT_B / DIM_B = %d / %d = %d\n", gemm_sz_split_b, DIM_B, tiles_per_col_in_block_b);
    printf("  BROADCAST_COUNT_B = (GEMM_SIZE_A / SPLIT_A) / DIM_A = (%d / %d) / %d = %d\n", GEMM_SIZE_A, SPLIT_A, DIM_A, broadcast_count_b);
    printf("\nStep 2: Calculate BASE_MATB_SZ (per split-cascade file)\n");
    printf("  BASE_MATB_SZ = (GEMM_SZ_CASC × GEMM_SZ_SPLIT_B × BROADCAST_COUNT_B × SPLIT_B) / WRD_LN\n");
    printf("                = (%d × %d × %d × %d) / %d\n", gemm_sz_casc, gemm_sz_split_b, broadcast_count_b, SPLIT_B, WRD_LN);
    int base_matb_sz_calc = (gemm_sz_casc * gemm_sz_split_b * broadcast_count_b * SPLIT_B) / WRD_LN;
    printf("                = %d / %d\n", (gemm_sz_casc * gemm_sz_split_b * broadcast_count_b * SPLIT_B), WRD_LN);
    printf("                = %d elements (per split-cascade file)\n", base_matb_sz_calc);
    printf("\nStep 3: Calculate EXACT_MATB_SZ (total across all split-cascade files)\n");
    printf("  EXACT_MATB_SZ = BASE_MATB_SZ × NUM_B_FILES\n");
    printf("                 = BASE_MATB_SZ × (SPLIT_B × CASC_LN_AB)\n");
    printf("                 = %d × %d\n", base_matb_sz_calc, NUM_B_FILES);
    printf("                 = %d elements\n", EXACT_MATB_SZ);
    printf("  Memory: %d elements × %zu bytes/element = %zu bytes (%.2f MB)\n",
           EXACT_MATB_SZ, sizeof(ap_int<128>), EXACT_MATB_SZ * sizeof(ap_int<128>),
           (EXACT_MATB_SZ * sizeof(ap_int<128>)) / (1024.0 * 1024.0));
    
    printf("\n");
    printf("================================================================================\n");
    printf("MATRIX C CALCULATION (GEMM_SIZE_A × GEMM_SIZE_B)\n");
    printf("================================================================================\n");
    printf("Matrix C dimensions: %d rows × %d columns\n", GEMM_SIZE_A, GEMM_SIZE_B);
    printf("\nStep 1: Calculate BASE_MATC_SZ (per split)\n");
    printf("  BASE_MATC_SZ = (GEMM_SIZE_A × GEMM_SIZE_B) / SPLIT_B / WRD_LN\n");
    printf("                = (%d × %d) / %d / %d\n", GEMM_SIZE_A, GEMM_SIZE_B, SPLIT_B, WRD_LN);
    int base_matc_sz_calc = (GEMM_SIZE_A * GEMM_SIZE_B) / SPLIT_B / WRD_LN;
    printf("                = %d / %d / %d\n", (GEMM_SIZE_A * GEMM_SIZE_B), SPLIT_B, WRD_LN);
    printf("                = %d elements (per split)\n", base_matc_sz_calc);
    printf("\nStep 2: Calculate EXACT_MATC_SZ (total across all split files)\n");
    printf("  EXACT_MATC_SZ = BASE_MATC_SZ × NUM_C_FILES\n");
    printf("                 = BASE_MATC_SZ × SPLIT_B\n");
    printf("                 = %d × %d\n", base_matc_sz_calc, NUM_C_FILES);
    printf("                 = %d elements\n", EXACT_MATC_SZ);
    printf("  Memory: %d elements × %zu bytes/element = %zu bytes (%.2f MB)\n",
           EXACT_MATC_SZ, sizeof(ap_int<128>), EXACT_MATC_SZ * sizeof(ap_int<128>),
           (EXACT_MATC_SZ * sizeof(ap_int<128>)) / (1024.0 * 1024.0));
    
    printf("\n");
    printf("================================================================================\n");
    printf("SUMMARY: EXACT SIZES IMPACTED BY GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B\n");
    printf("================================================================================\n");
    printf("Matrix A: EXACT_MATA_SZ = %d elements (%.2f MB)\n",
           EXACT_MATA_SZ, (EXACT_MATA_SZ * sizeof(ap_int<128>)) / (1024.0 * 1024.0));
    printf("  Formula: (GEMM_SIZE_A × GEMM_SIZE_AB) / WRD_LN\n");
    printf("           = (%d × %d) / %d\n", GEMM_SIZE_A, GEMM_SIZE_AB, WRD_LN);
    printf("  Impact: Directly proportional to GEMM_SIZE_A and GEMM_SIZE_AB\n");
    printf("  NOTE: Raw row-major format in DDR, DMA kernel handles cascade transformation\n");
    printf("\nMatrix B: EXACT_MATB_SZ = %d elements (%.2f MB)\n",
           EXACT_MATB_SZ, (EXACT_MATB_SZ * sizeof(ap_int<128>)) / (1024.0 * 1024.0));
    printf("  Formula: ((GEMM_SIZE_AB/%d × GEMM_SIZE_B/%d × (GEMM_SIZE_A/%d)/%d × %d) / %d) × %d\n",
           CASC_LN_AB, SPLIT_B, SPLIT_A, DIM_A, SPLIT_B, WRD_LN, CASC_LN_AB);
    printf("  Impact: Directly proportional to GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B\n");
    printf("\nMatrix C: EXACT_MATC_SZ = %d elements (%.2f MB)\n",
           EXACT_MATC_SZ, (EXACT_MATC_SZ * sizeof(ap_int<128>)) / (1024.0 * 1024.0));
    printf("  Formula: ((GEMM_SIZE_A × GEMM_SIZE_B) / %d / %d) × %d\n",
           SPLIT_B, WRD_LN, SPLIT_B);
    printf("  Impact: Directly proportional to GEMM_SIZE_A and GEMM_SIZE_B\n");
    
    size_t total_exact_bytes = (EXACT_MATA_SZ + EXACT_MATB_SZ + EXACT_MATC_SZ) * sizeof(ap_int<128>);
    printf("\nTotal Exact Memory: %zu bytes (%.2f MB, %.2f GB)\n",
           total_exact_bytes, total_exact_bytes / (1024.0 * 1024.0), total_exact_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("================================================================================\n");
    printf("\n");
}

// ============================================================================
// MEMORY ALLOCATION AND DATA LOADING
// ============================================================================


// ============================================================================
// DEBUG PRINTING FUNCTIONS
// ============================================================================

// Debug printing functions
void printInputBuffers(const ap_int<128>* inA_bomapped, const ap_int<128>* inB_bomapped, 
                      size_t aligned_mata_size, size_t aligned_matb_size) {
    // ============================================================================
    // INPUT BUFFER DEBUG PRINTING: Display first 16 elements of A and B matrices
    // ============================================================================
    printf("\n=== Input Buffer A (first 16 elements after sync to device) ===\n");
    for (int i = 0; i < std::min(16, (int)(aligned_mata_size / sizeof(ap_int<128>))); ++i) {
        printf("A[%2d]: ", i);
        for (int j = 0; j < WRD_LN; ++j) {
            if (DATA_TYPE == 16) {  // int16
                ap_int<16> val = (inA_bomapped[i] >> (j * 16)) & 0xFFFF;
                printf("%6d ", (int16_t)val);
            } else if (DATA_TYPE == 32) {  // int32
                ap_int<32> val = (inA_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                printf("%8d ", (int32_t)val);
            }
        }
        printf("\n");
    }
    
    printf("\n=== Input Buffer B (first 16 elements after sync to device) ===\n");
    for (int i = 0; i < std::min(16, (int)(aligned_matb_size / sizeof(ap_int<128>))); ++i) {
        printf("B[%2d]: ", i);
        for (int j = 0; j < WRD_LN; ++j) {
            if (DATA_TYPE == 16) {  // int16
                ap_int<16> val = (inB_bomapped[i] >> (j * 16)) & 0xFFFF;
                printf("%6d ", (int16_t)val);
            } else if (DATA_TYPE == 32) {  // int32
                ap_int<32> val = (inB_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                printf("%8d ", (int32_t)val);
            }
        }
        printf("\n");
    }
    printf("\n");
}

void printOutputBuffer(const ap_int<128>* outC_bomapped, size_t aligned_matc_size) {
    // ============================================================================
    // OUTPUT BUFFER DEBUG PRINTING: Display first 16 elements of C matrix
    // ============================================================================
    printf("\n=== Output Buffer C (first 16 elements after sync) ===\n");
    for (int i = 0; i < std::min(16, (int)(aligned_matc_size / sizeof(ap_int<128>))); ++i) {
        printf("C[%2d]: ", i);
        for (int j = 0; j < WRD_LN; ++j) {
            if (DATA_TYPE == 4) {  // int4
                ap_int<4> val = (outC_bomapped[i] >> (j * 4)) & 0xF;
                printf("%2d ", (int8_t)val);
            } else if (DATA_TYPE == 8) {  // int8
                ap_int<8> val = (outC_bomapped[i] >> (j * 8)) & 0xFF;
                printf("%4d ", (int8_t)val);
            } else if (DATA_TYPE == 16) {  // int16
                ap_int<16> val = (outC_bomapped[i] >> (j * 16)) & 0xFFFF;
                printf("%6d ", (int16_t)val);
            } else if (DATA_TYPE == 32) {  // int32
                ap_int<32> val = (outC_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                printf("%6d ", (int32_t)val);
            }
        }
        printf("\n");
    }
    printf("\n");
}

// ============================================================================
// XRT BUFFER VALIDATION FUNCTIONS
// ============================================================================

// Validate XRT buffer creation
int validateBufferCreation(const xrt::bo& inA_bohdl, const xrt::bo& inB_bohdl, const xrt::bo& outC_bohdl) {
    if (!inA_bohdl) {
        printf("ERROR: Failed to create buffer A\n");
        return EXIT_FAILURE;
    }
    if (!inB_bohdl) {
        printf("ERROR: Failed to create buffer B\n");
        return EXIT_FAILURE;
    }
    if (!outC_bohdl) {
        printf("ERROR: Failed to create buffer C\n");
        return EXIT_FAILURE;
    }
    printf("XRT buffers created successfully\n");
    return EXIT_SUCCESS;
}

// Validate buffer mapping
int validateBufferMapping(const ap_int<128>* inA_bomapped, const ap_int<128>* inB_bomapped, const ap_int<128>* outC_bomapped) {
    if (!inA_bomapped) {
        printf("ERROR: Failed to map buffer A\n");
        return EXIT_FAILURE;
    }
    if (!inB_bomapped) {
        printf("ERROR: Failed to map buffer B\n");
        return EXIT_FAILURE;
    }
    if (!outC_bomapped) {
        printf("ERROR: Failed to map buffer C\n");
        return EXIT_FAILURE;
    }
    printf("Buffers mapped successfully\n");
    return EXIT_SUCCESS;
}

// Verify DDR allocation matches exact memory sizes based on GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B
int verifyDDRAllocationMatchesGEMMSizes(const xrt::bo& inA_bohdl, const xrt::bo& inB_bohdl, const xrt::bo& outC_bohdl) {
    printf("\n=== DDR Allocation Verification (GEMM_SIZE_A=%d, GEMM_SIZE_AB=%d, GEMM_SIZE_B=%d) ===\n",
           GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B);
    
    // Get actual allocated buffer sizes from XRT
    size_t actual_allocated_A = inA_bohdl.size();
    size_t actual_allocated_B = inB_bohdl.size();
    size_t actual_allocated_C = outC_bohdl.size();
    
    // Calculate raw matrix A size (GEMM_SIZE_A * GEMM_SIZE_AB) / WRD_LN
    size_t raw_mata_words = (GEMM_SIZE_A * GEMM_SIZE_AB + WRD_LN - 1) / WRD_LN;
    size_t raw_mata_bytes = raw_mata_words * sizeof(ap_int<128>);
    size_t aligned_mata_bytes = ((raw_mata_bytes + BUFFER_ALIGNMENT - 1) / BUFFER_ALIGNMENT) * BUFFER_ALIGNMENT;
    
    // Calculate expected sizes
    size_t exact_matb_bytes = EXACT_MATB_SZ * sizeof(ap_int<128>);
    size_t exact_matc_bytes = EXACT_MATC_SZ * sizeof(ap_int<128>);
    
    printf("\nMatrix A (GEMM_SIZE_A=%d x GEMM_SIZE_AB=%d):\n", GEMM_SIZE_A, GEMM_SIZE_AB);
    printf("  Exact size needed:     %zu bytes (%.2f MB) = %zu words × %zu bytes/element\n",
           raw_mata_bytes, raw_mata_bytes / (1024.0 * 1024.0), raw_mata_words, sizeof(ap_int<128>));
    printf("  Aligned size (padding): %zu bytes (%.2f MB) = %zu bytes + alignment padding\n",
           aligned_mata_bytes, aligned_mata_bytes / (1024.0 * 1024.0), raw_mata_bytes);
    printf("  Actually allocated:    %zu bytes (%.2f MB) from XRT\n",
           actual_allocated_A, actual_allocated_A / (1024.0 * 1024.0));
    printf("  Padding overhead:      %zu bytes (%.2f%%) - unused but required for alignment\n",
           aligned_mata_bytes - raw_mata_bytes,
           100.0 * (aligned_mata_bytes - raw_mata_bytes) / raw_mata_bytes);
    
    if (actual_allocated_A != aligned_mata_bytes) {
        printf("  ⚠️  WARNING: Allocated size (%zu) != Expected aligned size (%zu)\n",
               actual_allocated_A, aligned_mata_bytes);
        return EXIT_FAILURE;
    } else {
        printf("  ✓ Allocated size matches expected aligned size\n");
    }
    
    printf("\nMatrix B (GEMM_SIZE_AB=%d x GEMM_SIZE_B=%d):\n", GEMM_SIZE_AB, GEMM_SIZE_B);
    printf("  Exact size needed:     %zu bytes (%.2f MB) = %d elements × %zu bytes/element\n",
           exact_matb_bytes, exact_matb_bytes / (1024.0 * 1024.0), EXACT_MATB_SZ, sizeof(ap_int<128>));
    printf("  Aligned size (padding): %zu bytes (%.2f MB) = %zu bytes + alignment padding\n",
           ALIGNED_MATB_BYTES, ALIGNED_MATB_BYTES / (1024.0 * 1024.0), exact_matb_bytes);
    printf("  Actually allocated:    %zu bytes (%.2f MB) from XRT\n",
           actual_allocated_B, actual_allocated_B / (1024.0 * 1024.0));
    printf("  Padding overhead:      %zu bytes (%.2f%%) - unused but required for alignment\n",
           ALIGNED_MATB_BYTES - exact_matb_bytes,
           100.0 * (ALIGNED_MATB_BYTES - exact_matb_bytes) / exact_matb_bytes);
    
    if (actual_allocated_B != ALIGNED_MATB_BYTES) {
        printf("  ⚠️  WARNING: Allocated size (%zu) != Expected aligned size (%zu)\n",
               actual_allocated_B, ALIGNED_MATB_BYTES);
        return EXIT_FAILURE;
    } else {
        printf("  ✓ Allocated size matches expected aligned size\n");
    }
    
    printf("\nMatrix C (GEMM_SIZE_A=%d x GEMM_SIZE_B=%d):\n", GEMM_SIZE_A, GEMM_SIZE_B);
    printf("  Exact size needed:     %zu bytes (%.2f MB) = %d elements × %zu bytes/element\n",
           exact_matc_bytes, exact_matc_bytes / (1024.0 * 1024.0), EXACT_MATC_SZ, sizeof(ap_int<128>));
    printf("  Aligned size (padding): %zu bytes (%.2f MB) = %zu bytes + alignment padding\n",
           ALIGNED_MATC_BYTES, ALIGNED_MATC_BYTES / (1024.0 * 1024.0), exact_matc_bytes);
    printf("  Actually allocated:    %zu bytes (%.2f MB) from XRT\n",
           actual_allocated_C, actual_allocated_C / (1024.0 * 1024.0));
    printf("  Padding overhead:      %zu bytes (%.2f%%) - unused but required for alignment\n",
           ALIGNED_MATC_BYTES - exact_matc_bytes,
           100.0 * (ALIGNED_MATC_BYTES - exact_matc_bytes) / exact_matc_bytes);
    
    if (actual_allocated_C != ALIGNED_MATC_BYTES) {
        printf("  ⚠️  WARNING: Allocated size (%zu) != Expected aligned size (%zu)\n",
               actual_allocated_C, ALIGNED_MATC_BYTES);
        return EXIT_FAILURE;
    } else {
        printf("  ✓ Allocated size matches expected aligned size\n");
    }
    
    // Summary
    size_t total_exact_bytes = raw_mata_bytes + exact_matb_bytes + exact_matc_bytes;
    size_t total_aligned_bytes = aligned_mata_bytes + ALIGNED_MATB_BYTES + ALIGNED_MATC_BYTES;
    size_t total_allocated_bytes = actual_allocated_A + actual_allocated_B + actual_allocated_C;
    size_t total_padding = total_aligned_bytes - total_exact_bytes;
    
    printf("\n=== Summary ===\n");
    printf("Total exact memory needed:    %zu bytes (%.2f MB, %.2f GB)\n",
           total_exact_bytes, total_exact_bytes / (1024.0 * 1024.0), total_exact_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("Total aligned (with padding): %zu bytes (%.2f MB, %.2f GB)\n",
           total_aligned_bytes, total_aligned_bytes / (1024.0 * 1024.0), total_aligned_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("Total actually allocated:      %zu bytes (%.2f MB, %.2f GB)\n",
           total_allocated_bytes, total_allocated_bytes / (1024.0 * 1024.0), total_allocated_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("Total padding overhead:        %zu bytes (%.2f MB, %.2f%%)\n",
           total_padding, total_padding / (1024.0 * 1024.0), 100.0 * total_padding / total_exact_bytes);
    
    printf("\n=== Memory Usage Breakdown ===\n");
    printf("Only the EXACT sizes are used for data operations:\n");
    printf("  - Matrix A: %zu bytes (%.2f MB) - based on GEMM_SIZE_A=%d × GEMM_SIZE_AB=%d\n",
           raw_mata_bytes, raw_mata_bytes / (1024.0 * 1024.0), GEMM_SIZE_A, GEMM_SIZE_AB);
    printf("  - Matrix B: %zu bytes (%.2f MB) - based on GEMM_SIZE_AB=%d × GEMM_SIZE_B=%d\n",
           exact_matb_bytes, exact_matb_bytes / (1024.0 * 1024.0), GEMM_SIZE_AB, GEMM_SIZE_B);
    printf("  - Matrix C: %zu bytes (%.2f MB) - based on GEMM_SIZE_A=%d × GEMM_SIZE_B=%d\n",
           exact_matc_bytes, exact_matc_bytes / (1024.0 * 1024.0), GEMM_SIZE_A, GEMM_SIZE_B);
    printf("Padding is allocated but NOT used for data (required for 4096-byte alignment)\n");
    
    if (total_allocated_bytes == total_aligned_bytes) {
        printf("\n✓✓✓ VERIFICATION PASSED: DDR allocation matches exact memory sizes from GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B\n");
        return EXIT_SUCCESS;
    } else {
        printf("\n✗✗✗ VERIFICATION FAILED: DDR allocation mismatch!\n");
        return EXIT_FAILURE;
    }
}

// ============================================================================
// XRT DEVICE AND BUFFER OPERATIONS
// ============================================================================

// Initialize device and load XCLBIN
int initializeDeviceAndLoadXclbin(xrt::device& device, xrt::uuid& xclbin_uuid, const char* xclbinFilename) {
    // Device initialization
    auto t_device_init = std::chrono::high_resolution_clock::now();
    device = xrt::device(0);
    printf("Device initialized successfully\n");
    printDurationSince(t_device_init, "Device initialization");

    // XCLBIN file validation
    auto t_xclbin_checks = std::chrono::high_resolution_clock::now();
    FILE* file = fopen(xclbinFilename, "r");
    if (!file) {
        printf("ERROR: Cannot open XCLBIN file: %s\n", xclbinFilename);
        printf("Error details: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    fclose(file);
    
    // Check file size to ensure it's not empty
    struct stat file_stat;
    if (stat(xclbinFilename, &file_stat) != 0) {
        printf("ERROR: Cannot get file stats for: %s\n", xclbinFilename);
        return EXIT_FAILURE;
    }
    if (file_stat.st_size == 0) {
        printf("ERROR: XCLBIN file is empty: %s\n", xclbinFilename);
        return EXIT_FAILURE;
    }
    printDurationSince(t_xclbin_checks, "XCLBIN file checks (existence, size)");
    
    // Load XCLBIN
    auto t_xclbin_load = std::chrono::high_resolution_clock::now();
    xclbin_uuid = device.load_xclbin(xclbinFilename);
    printf("XCLBIN loaded successfully, UUID: %s\n", xclbin_uuid.to_string().c_str());
    printDurationSince(t_xclbin_load, "XCLBIN load");
    
    return EXIT_SUCCESS;
}

// Create and map XRT buffers
int createAndMapBuffers(xrt::device& device, xrt::bo& inA_bohdl, xrt::bo& inB_bohdl, xrt::bo& outC_bohdl,
                       ap_int<128>*& inA_bomapped, ap_int<128>*& inB_bomapped, ap_int<128>*& outC_bomapped) {
    // Matrix A buffer: length in 128-bit words. DDR holds ap_int<128> (16 bytes) per word; each word packs WRD_LN elements (e.g. 8 for int16).
    // raw_mata_words = number of 128-bit words = (GEMM_SIZE_A * GEMM_SIZE_AB) / WRD_LN
    size_t raw_mata_words = (GEMM_SIZE_A * GEMM_SIZE_AB + WRD_LN - 1) / WRD_LN;
    size_t raw_mata_bytes = raw_mata_words * sizeof(ap_int<128>);  // 16 bytes per word
    size_t aligned_mata_bytes = ((raw_mata_bytes + BUFFER_ALIGNMENT - 1) / BUFFER_ALIGNMENT) * BUFFER_ALIGNMENT;
    
    // Check device memory availability before buffer creation
    printf("\n=== Device Memory Check ===\n");
    size_t total_mem_bytes = aligned_mata_bytes + ALIGNED_MATB_BYTES + ALIGNED_MATC_BYTES;
    printf("Required device memory:\n");
    printf("  Matrix A buffer: %zu bytes (%.2f MB) - raw matrix size\n", aligned_mata_bytes, aligned_mata_bytes / (1024.0 * 1024.0));
    printf("  Matrix B buffer: %zu bytes (%.2f MB)\n", ALIGNED_MATB_BYTES, ALIGNED_MATB_BYTES / (1024.0 * 1024.0));
    printf("  Matrix C buffer: %zu bytes (%.2f MB)\n", ALIGNED_MATC_BYTES, ALIGNED_MATC_BYTES / (1024.0 * 1024.0));
    printf("  Total required: %zu bytes (%.2f MB, %.2f GB)\n", 
           total_mem_bytes, total_mem_bytes / (1024.0 * 1024.0), total_mem_bytes / (1024.0 * 1024.0 * 1024.0));
    
    // CMA (Contiguous Memory Allocator) limitation check
    // XRT uses CMA for device memory, which is typically 512 MB on Versal boards
    // We need to check if our buffers fit within CMA limits
    const size_t CMA_SIZE_ESTIMATE = 512 * 1024 * 1024; // 512 MB typical CMA
    const size_t CMA_SAFE_LIMIT = CMA_SIZE_ESTIMATE * 0.9; // Use 90% to be safe
    
    printf("\n=== CMA Memory Constraint Check ===\n");
    printf("CMA (Contiguous Memory Allocator) is typically 512 MB on Versal boards\n");
    printf("XRT allocates device buffers from CMA, not full DDR\n");
    printf("Required: %.2f MB (%.2f GB)\n", 
           total_mem_bytes / (1024.0 * 1024.0), 
           total_mem_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("CMA estimate: %.2f MB\n", CMA_SIZE_ESTIMATE / (1024.0 * 1024.0));
    
    if (total_mem_bytes > CMA_SAFE_LIMIT) {
        printf("\n⚠️  WARNING: Required memory (%.2f MB) exceeds CMA estimate (%.2f MB)\n",
               total_mem_bytes / (1024.0 * 1024.0),
               CMA_SAFE_LIMIT / (1024.0 * 1024.0));
        printf("   This will likely fail with std::bad_alloc\n");
        printf("   Solutions:\n");
        printf("   1. Increase CMA size in device tree (requires platform rebuild)\n");
        printf("   2. Reduce GEMM_SIZE_A, GEMM_SIZE_AB, or GEMM_SIZE_B (quick fix)\n");
        printf("   3. Implement streaming/chunking (complex, requires code changes)\n");
        printf("\n   Attempting allocation anyway...\n");
    }
    
    printf("\nAttempting buffer allocation...\n");
    printf("Strategy: Using different memory banks to distribute load\n");
    printf("  Matrix A (%.2f MB): Memory bank 0\n", aligned_mata_bytes / (1024.0 * 1024.0));
    printf("  Matrix B (%.2f MB): Memory bank 1 (if available, else 0)\n", ALIGNED_MATB_BYTES / (1024.0 * 1024.0));
    printf("  Matrix C (%.2f MB): Memory bank 0\n", ALIGNED_MATC_BYTES / (1024.0 * 1024.0));
    
    // Buffer creation with error handling and memory bank distribution
    auto t_bo_create = std::chrono::high_resolution_clock::now();
    
    // Try allocating buffers one at a time to identify which fails
    try {
        printf("Allocating Matrix A buffer (%.2f MB) on bank 0...\n", aligned_mata_bytes / (1024.0 * 1024.0));
        inA_bohdl = xrt::bo(device, aligned_mata_bytes, 0, 0);
        printf("  ✓ Matrix A buffer allocated successfully\n");
    } catch (const std::bad_alloc& e) {
        printf("  ✗ ERROR: std::bad_alloc - CMA insufficient for Matrix A buffer\n");
        printf("     CMA size is likely too small (check: cat /proc/meminfo | grep -i cma)\n");
        printf("     Solution: Increase CMA size or reduce GEMM_SIZE_A\n");
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        printf("  ✗ ERROR: Failed to allocate Matrix A buffer: %s\n", e.what());
        return EXIT_FAILURE;
    }
    
    try {
        // Try bank 1 first for Matrix B (largest buffer), fallback to bank 0
        printf("Allocating Matrix B buffer (%.2f MB) on bank 1...\n", ALIGNED_MATB_BYTES / (1024.0 * 1024.0));
        try {
            inB_bohdl = xrt::bo(device, ALIGNED_MATB_BYTES, 1, 0);
            printf("  ✓ Matrix B buffer allocated successfully on bank 1\n");
        } catch (const std::bad_alloc& e1) {
            printf("  ⚠ Bank 1 failed (std::bad_alloc), trying bank 0: CMA insufficient\n");
            try {
                inB_bohdl = xrt::bo(device, ALIGNED_MATB_BYTES, 0, 0);
                printf("  ✓ Matrix B buffer allocated successfully on bank 0\n");
            } catch (const std::bad_alloc& e2) {
                printf("  ✗ ERROR: std::bad_alloc - CMA insufficient for Matrix B buffer (%.2f MB)\n",
                       ALIGNED_MATB_BYTES / (1024.0 * 1024.0));
                printf("     This is the largest buffer. CMA cannot provide %.2f MB contiguous memory\n",
                       ALIGNED_MATB_BYTES / (1024.0 * 1024.0));
                printf("     Solutions:\n");
                printf("     1. Increase CMA size: Modify device tree or kernel boot (cma=2048M)\n");
                printf("     2. Reduce GEMM_SIZE_A, GEMM_SIZE_AB, or GEMM_SIZE_B to reduce memory requirements\n");
                printf("     3. Implement streaming: Process matrices in smaller chunks\n");
                return EXIT_FAILURE;
            }
        } catch (const std::exception& e1) {
            printf("  ⚠ Bank 1 failed, trying bank 0: %s\n", e1.what());
            inB_bohdl = xrt::bo(device, ALIGNED_MATB_BYTES, 0, 0);
            printf("  ✓ Matrix B buffer allocated successfully on bank 0\n");
        }
    } catch (const std::exception& e) {
        printf("  ✗ ERROR: Failed to allocate Matrix B buffer: %s\n", e.what());
        return EXIT_FAILURE;
    }
    
    try {
        printf("Allocating Matrix C buffer (%.2f MB) on bank 0...\n", ALIGNED_MATC_BYTES / (1024.0 * 1024.0));
        outC_bohdl = xrt::bo(device, ALIGNED_MATC_BYTES, 0, 0);
        printf("  ✓ Matrix C buffer allocated successfully\n");
    } catch (const std::bad_alloc& e) {
        printf("  ✗ ERROR: std::bad_alloc - CMA insufficient for Matrix C buffer\n");
        printf("     Even small buffer (%.2f MB) failed - CMA is severely limited\n",
               ALIGNED_MATC_BYTES / (1024.0 * 1024.0));
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        printf("  ✗ ERROR: Failed to allocate Matrix C buffer: %s\n", e.what());
        return EXIT_FAILURE;
    }
    
    if (validateBufferCreation(inA_bohdl, inB_bohdl, outC_bohdl) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    printDurationSince(t_bo_create, "BO create (A, B, C)");
    
    // Buffer mapping
    auto t_map = std::chrono::high_resolution_clock::now();
    inA_bomapped = inA_bohdl.map<ap_int<128>*>();
    inB_bomapped = inB_bohdl.map<ap_int<128>*>();
    outC_bomapped = outC_bohdl.map<ap_int<128>*>();
    
    if (validateBufferMapping(inA_bomapped, inB_bomapped, outC_bomapped) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    printDurationSince(t_map, "BO map (A, B, C)");
    
    // Verify DDR allocation matches exact memory sizes based on GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B
    if (verifyDDRAllocationMatchesGEMMSizes(inA_bohdl, inB_bohdl, outC_bohdl) != EXIT_SUCCESS) {
        printf("WARNING: DDR allocation verification failed, but continuing...\n");
    }
    
    return EXIT_SUCCESS;
}

// Optional debug buffers for capturing raw C split streams (c0/c1) as they arrive from AIE
// Transfer data to device buffers
int transferDataToDevice(const std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_A,
                        const std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_B,
                        ap_int<128>* inA_bomapped, ap_int<128>* inB_bomapped, ap_int<128>* outC_bomapped,
                        xrt::bo& inA_bohdl, xrt::bo& inB_bohdl) {
    // Initialize output buffer
    printf("Initializing output buffer...\n");
    auto t_zero_out = std::chrono::high_resolution_clock::now();
    memset(outC_bomapped, 0, ALIGNED_MATC_BYTES);
    printDurationSince(t_zero_out, "Zero initialize C buffer");
    
    // Data transfer validation
    // IMPORTANT: Only EXACT sizes (based on GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B) are used for data operations
    // The aligned sizes include padding for 4096-byte alignment, but only exact sizes contain actual data
    size_t actual_mata_bytes = EXACT_MATA_SZ * sizeof(ap_int<128>);
    size_t actual_matb_bytes = EXACT_MATB_SZ * sizeof(ap_int<128>);
    
    printf("Copying exact data sizes (based on GEMM_SIZE_A=%d, GEMM_SIZE_AB=%d, GEMM_SIZE_B=%d):\n",
           GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B);
    printf("  Matrix A: %zu bytes (%.2f MB) = %d elements\n",
           actual_mata_bytes, actual_mata_bytes / (1024.0 * 1024.0), EXACT_MATA_SZ);
    printf("  Matrix B: %zu bytes (%.2f MB) = %d elements\n",
           actual_matb_bytes, actual_matb_bytes / (1024.0 * 1024.0), EXACT_MATB_SZ);
    
    if (actual_mata_bytes > ALIGNED_MATA_BYTES) {
        printf("ERROR: Buffer A size mismatch for copy operation\n");
        return EXIT_FAILURE;
    }
    if (actual_matb_bytes > ALIGNED_MATB_BYTES) {
        printf("ERROR: Buffer B size mismatch for copy operation\n");
        return EXIT_FAILURE;
    }
    
    // Copy data to device buffers (only exact sizes, not aligned sizes)
    auto t_memcpy = std::chrono::high_resolution_clock::now();
    memcpy(inA_bomapped, host_mem_A.data(), actual_mata_bytes);
    memcpy(inB_bomapped, host_mem_B.data(), actual_matb_bytes);
    printf("Data copied successfully (exact sizes only, padding not used)\n");
    printDurationSince(t_memcpy, "Memcpy host->BO (A, B)");
    
    // Sync to device
    auto t_sync_to_dev = std::chrono::high_resolution_clock::now();
    inA_bohdl.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    inB_bohdl.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    printf("Buffer synchronization completed\n");
    printDurationSince(t_sync_to_dev, "Sync to device (A, B)");
    
    return EXIT_SUCCESS;
}

// Create kernel and graph objects
int createKernel(xrt::device& device, xrt::uuid& xclbin_uuid, 
                        xrt::kernel& dma_hls_khdl) {
    printf("Creating kernel object with optimized settings...\n");
    
    // Create DMA kernel
    auto t_kernel_create = std::chrono::high_resolution_clock::now();
    dma_hls_khdl = xrt::kernel(device, xclbin_uuid, "dma_hls");
    printf("DMA kernel created successfully\n");
    printDurationSince(t_kernel_create, "Kernel create (dma_hls)");


    
    return EXIT_SUCCESS;
}

// Launch kernel with timing measurement
int launchKernel(xrt::kernel& dma_hls_khdl,
                 xrt::bo& inA_bohdl, xrt::bo& inB_bohdl, xrt::bo& outC_bohdl,
                 xrt::run& dma_hls_rhdl) {
    printf("Launching DMA kernel with optimized configuration...\n");
    auto t_kernel_launch = std::chrono::high_resolution_clock::now();
    dma_hls_rhdl = dma_hls_khdl(
        inA_bohdl, inB_bohdl, outC_bohdl,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr);
    printf("DMA kernel launched successfully\n");
    printDurationSince(t_kernel_launch, "Kernel launch (dma_hls)");
    
    return EXIT_SUCCESS;
}

// Pick first writable output directory for c.txt / split dumps.
// 1) preferred_dir from caller (e.g. ./output_files/ from ./output_files/c.txt)
// 2) ./output_files/ under current working directory (same folder you run gemm_aie_xrt.elf from)
// 3) /media/output_files/ (hw), /mnt/output_files/ (emu or legacy mounts)
static std::string pick_gemm_output_dir(const char* preferred_dir) {
    std::vector<std::string> dirs;
    if (preferred_dir && preferred_dir[0] != '\0') {
        std::string p(preferred_dir);
        while (!p.empty() && (p.back() == '/' || p.back() == '\\'))
            p.pop_back();
        if (!p.empty())
            dirs.push_back(p + "/");
    }
    dirs.push_back(std::string("./output_files/"));
#ifdef TARGET_HW
    dirs.push_back(std::string("/media/output_files/"));
#endif
    dirs.push_back(std::string("/mnt/output_files/"));

    for (const auto& d : dirs) {
        struct stat st;
        if (stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            return d;
        }
        if (mkdir(d.c_str(), 0755) == 0)
            return d;
        if (errno == EEXIST && stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            return d;
    }
    return std::string();
}

bool write_split_txt_from_bo(const ap_int<128>* src,
                             size_t num_words,
                             const char* preferred_dir,
                             const char* out_name) {
    if (!src || !out_name) return false;

    std::string out_dir = pick_gemm_output_dir(preferred_dir);
    if (out_dir.empty()) {
        fprintf(stderr, "Error creating any output directory for split dump\n");
        return false;
    }

    std::string out_path = out_dir + std::string(out_name);
    std::ofstream out(out_path);
    if (!out.is_open()) {
        fprintf(stderr, "Error opening %s for write: %s\n", out_path.c_str(), strerror(errno));
        return false;
    }

    for (size_t w = 0; w < num_words; ++w) {
        for (int j = 0; j < WRD_LN; ++j) {
            if (DATA_TYPE == 16) {  // int16
                ap_int<16> current_val = (src[w] >> (j * 16)) & 0xFFFF;
                out << (int16_t)current_val;
            } else if (DATA_TYPE == 32) {  // int32
                ap_int<32> current_val = (src[w] >> (j * 32)) & 0xFFFFFFFF;
                out << (int32_t)current_val;
            }
            if (j < WRD_LN - 1) out << ' ';
        }
        out << '\n';
    }

    out.close();
    printf("Wrote split dump to %s (%zu words, %d elems/line)\n", out_path.c_str(), num_words, WRD_LN);
    return true;
}

// Write c0.txt and c1.txt from matC buffer.
// Simple interleaved out_C (dma_hls) writes: matC[2p]=C0 word p, matC[2p+1]=C1 word p (global order).
// So we extract: c0.txt = words at even indices (0,2,4,...), c1.txt = words at odd indices (1,3,5,...).
bool write_split_txt_from_bo_interleaved(const ap_int<128>* src, size_t total_words, const char* preferred_dir) {
    if (!src) return false;
    const size_t words_per_split = total_words / NUM_C_FILES;  // total_words/2 when SPLIT_B=2
    if (total_words != words_per_split * NUM_C_FILES) return false;

    std::string out_dir = pick_gemm_output_dir(preferred_dir);
    if (out_dir.empty()) return false;

    auto write_one_split = [&](const char* out_name, int split_idx) {
        std::string path = out_dir + out_name;
        std::ofstream out(path);
        if (!out.is_open()) return false;
        for (size_t i = 0; i < words_per_split; ++i) {
            size_t word_idx = i * NUM_C_FILES + split_idx;  // even: 0,2,4,...  odd: 1,3,5,...
            for (int j = 0; j < WRD_LN; ++j) {
                if (DATA_TYPE == 16) {
                    ap_int<16> v = (src[word_idx] >> (j * 16)) & 0xFFFF;
                    out << (int16_t)v;
                } else if (DATA_TYPE == 32) {
                    ap_int<32> v = (src[word_idx] >> (j * 32)) & 0xFFFFFFFF;
                    out << (int32_t)v;
                }
                if (j < WRD_LN - 1) out << ' ';
            }
            out << '\n';
        }
        out.close();
        printf("Wrote split dump to %s (%zu words, %d elems/line)\n", path.c_str(), words_per_split, WRD_LN);
        return true;
    };
    bool ok0 = write_one_split("c0.txt", 0);
    bool ok1 = write_one_split("c1.txt", 1);
    return ok0 && ok1;
}

// Execute computation (graph run + DMA wait + Output sync)
int executeComputation(xrt::graph& gemm_aie_gr, xrt::run& dma_hls_rhdl, 
                      ap_int<128>* outC_bomapped, xrt::bo& outC_bohdl) {
    // Run graph
    auto graph_start_time = std::chrono::high_resolution_clock::now();
    printf("Running graph for %d iterations\n", GRAPH_ITER_CNT);
    gemm_aie_gr.run(GRAPH_ITER_CNT);
    printElapsedTime(graph_start_time, "Graph run");
    
    // Wait for DMA completion
    auto dma_wait_time = std::chrono::high_resolution_clock::now();
    dma_hls_rhdl.wait();
    printf("DMA kernel execution completed successfully\n");
    printElapsedTime(dma_wait_time, "DMA kernel wait");
    

    
    return EXIT_SUCCESS;
}

// Write output results to file
int writeOutputToFile(ap_int<128>* outC_bomapped, xrt::bo& outC_bohdl,
                     const char* output_filename) {
    
    // Extract directory from filename (basename is ignored: C is always written as c.txt for golden compare)
    std::string output_dir = std::string(output_filename ? output_filename : "./output_files/c.txt");
    size_t last_slash = output_dir.find_last_of('/');
    if (last_slash != std::string::npos) {
        output_dir = output_dir.substr(0, last_slash + 1);
    } else {
        output_dir = "./";
    }
    
    if (!write_c_txt_from_bo(outC_bomapped, EXACT_MATC_SZ, output_dir.c_str()))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

// Calculate and print core computation timing (Graph run + DMA wait + Output sync)
void printCoreComputationTiming(const std::chrono::high_resolution_clock::time_point& core_start_time,
                               const std::chrono::high_resolution_clock::time_point& graph_start_time,
                               const std::chrono::high_resolution_clock::time_point& dma_start_time,
                               const std::chrono::high_resolution_clock::time_point& sync_start_time) {
    
    // Calculate individual phase timings
    auto graph_duration = std::chrono::duration_cast<std::chrono::microseconds>(dma_start_time - graph_start_time).count();
    auto dma_duration = std::chrono::duration_cast<std::chrono::microseconds>(sync_start_time - dma_start_time).count();
    auto sync_duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - sync_start_time).count();
    
    // Calculate total core computation time
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - core_start_time).count();
    
    printf("*** CORE COMPUTATION BREAKDOWN ***\n");
    printf("  Graph run:        %8ld us\n", graph_duration);
    printf("  DMA wait:         %8ld us\n", dma_duration);
    printf("  Output sync:      %8ld us\n", sync_duration);
    printf("  TOTAL CORE:       %8ld us (%.3f ms)\n", total_duration, total_duration / 1000.0);
}

// ============================================================================
// FILE I/O UTILITIES
// ============================================================================

// Try to locate a file among candidate paths
std::string find_existing_file(const std::vector<std::string>& candidates) {
    for (const auto& p : candidates) {
        FILE* f = fopen(p.c_str(), "r");
        if (f) { fclose(f); return p; }
    }
    return std::string();
}

// Load golden file lines with dynamic word length based on data type
bool load_golden_into_vector(const std::string& filepath,
                            std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& dst) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        printf("ERROR: Failed to open %s\n", filepath.c_str());
        return false;
    }
    std::string line;
    size_t idx = 0;
    while (std::getline(in, line) && idx < dst.size()) {
        std::istringstream iss(line);
        ap_int<128> packed = 0;
        
        // Dynamic word length based on data type
        for (int k = 0; k < WRD_LN; ++k) {
            int v = 0;
            if (!(iss >> v)) v = 0;
            if (DATA_TYPE == 16) {  // int16
                packed.range((k+1)*16-1, k*16) = static_cast<int16_t>(v);
            } else if (DATA_TYPE == 32) {  // int32
                packed.range((k+1)*32-1, k*32) = static_cast<int32_t>(v);
            }
        }
        dst[idx++] = packed;
    }
    // If file shorter than needed, pad remaining with zeros
    for (; idx < dst.size(); ++idx) dst[idx] = 0;
    return true;
}


// Helper function to load golden file directly into DDR buffer
bool load_golden_into_ddr_buffer(const std::string& filepath,
                                ap_int<128>* dst,
                                size_t num_elements) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        printf("ERROR: Failed to open %s\n", filepath.c_str());
        return false;
    }
    std::string line;
    size_t idx = 0;
    while (std::getline(in, line) && idx < num_elements) {
        std::istringstream iss(line);
        ap_int<128> packed = 0;
        
        // Dynamic word length based on data type
        for (int k = 0; k < WRD_LN; ++k) {
            int v = 0;
            if (!(iss >> v)) v = 0;
            if (DATA_TYPE == 16) {  // int16
                packed.range((k+1)*16-1, k*16) = static_cast<int16_t>(v);
            } else if (DATA_TYPE == 32) {  // int32
                packed.range((k+1)*32-1, k*32) = static_cast<int32_t>(v);
            }
        }
        dst[idx++] = packed;
    }
    // If file shorter than needed, pad remaining with zeros
    for (; idx < num_elements; ++idx) dst[idx] = 0;
    return true;
}

// Helper function to load raw matrix file (word-by-word format) into DDR buffer
// Raw matrix format: WRD_LN elements per line (8 elements per line for int16)
// This format matches the output format of Matrix C, enabling consecutive GEMM operations
// Matrix dimensions: GEMM_SIZE_A rows × GEMM_SIZE_AB columns
// Packed into ap_int<128> words: (GEMM_SIZE_A * GEMM_SIZE_AB) / WRD_LN words
// Supports both formats:
//   - Word-by-word: WRD_LN elements per line (preferred for consecutive GEMM)
//   - Row-by-row: GEMM_SIZE_AB elements per line (legacy format, auto-detected)
bool load_raw_matrix_into_ddr_buffer(const std::string& filepath,
                                    ap_int<128>* dst,
                                    size_t num_words) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        printf("ERROR: Failed to open %s\n", filepath.c_str());
        return false;
    }
    
    // Calculate expected elements
    size_t expected_elements = GEMM_SIZE_A * GEMM_SIZE_AB;
    size_t expected_words = (expected_elements + WRD_LN - 1) / WRD_LN;  // Ceiling division
    
    if (num_words < expected_words) {
        printf("ERROR: Buffer too small. Need %zu words, got %zu\n", expected_words, num_words);
        return false;
    }
    
    // Auto-detect format by reading first line
    std::string first_line;
    if (!std::getline(in, first_line)) {
        printf("ERROR: Empty file %s\n", filepath.c_str());
        return false;
    }
    
    // Count elements in first line
    std::istringstream first_iss(first_line);
    int first_line_elements = 0;
    std::string dummy;
    while (first_iss >> dummy) first_line_elements++;
    
    // Reset file to beginning
    in.clear();
    in.seekg(0, std::ios::beg);
    
    // Determine format: word-by-word (WRD_LN elements/line) or row-by-row (GEMM_SIZE_AB elements/line)
    bool is_word_format = (first_line_elements == WRD_LN);
    bool is_row_format = (first_line_elements == GEMM_SIZE_AB);
    
    if (!is_word_format && !is_row_format) {
        printf("WARNING: Unexpected format in %s: first line has %d elements (expected %d or %d)\n",
               filepath.c_str(), first_line_elements, WRD_LN, GEMM_SIZE_AB);
        printf("Assuming word-by-word format (%d elements per line)\n", WRD_LN);
        is_word_format = true;  // Default to word format
    }
    
    ap_int<128> current_word = 0;
    int element_in_word = 0;
    size_t word_idx = 0;
    size_t elements_read = 0;
    
    std::string line;
    
    if (is_word_format) {
        // Word-by-word format: WRD_LN elements per line (matches Matrix C output format)
        printf("Reading Matrix A in word-by-word format (%d elements per line) from %s\n", WRD_LN, filepath.c_str());
        while (std::getline(in, line) && word_idx < num_words) {
            std::istringstream iss(line);
            
            // Read WRD_LN elements from this line (one word)
            for (int j = 0; j < WRD_LN; ++j) {
                int v = 0;
                if (!(iss >> v)) v = 0;
                if (DATA_TYPE == 16) {  // int16
                    current_word.range((j+1)*16-1, j*16) = static_cast<int16_t>(v);
                } else if (DATA_TYPE == 32) {  // int32
                    current_word.range((j+1)*32-1, j*32) = static_cast<int32_t>(v);
                }
                elements_read++;
            }
            
            // Store complete word
            dst[word_idx++] = current_word;
            current_word = 0;
        }
    } else {
        // Row-by-row format: GEMM_SIZE_AB elements per line (legacy format)
        printf("Reading Matrix A in row-by-row format (%d elements per line) from %s\n", GEMM_SIZE_AB, filepath.c_str());
        int row = 0;
        while (std::getline(in, line) && row < GEMM_SIZE_A) {
            std::istringstream iss(line);
            int col = 0;
            
            // Read elements from this row
            while (col < GEMM_SIZE_AB) {
                int v = 0;
                if (!(iss >> v)) v = 0;
                if (DATA_TYPE == 16) {  // int16
                    current_word.range((element_in_word+1)*16-1, element_in_word*16) = static_cast<int16_t>(v);
                } else if (DATA_TYPE == 32) {  // int32
                    current_word.range((element_in_word+1)*32-1, element_in_word*32) = static_cast<int32_t>(v);
                }
                
                element_in_word++;
                elements_read++;
                col++;
                
                // When word is full, store it
                if (element_in_word >= WRD_LN) {
                    dst[word_idx++] = current_word;
                    current_word = 0;
                    element_in_word = 0;
                }
            }
            row++;
        }
        
        // Handle remaining elements (pad with zeros)
        if (element_in_word > 0) {
            dst[word_idx++] = current_word;
        }
    }
    
    // Pad remaining words with zeros
    for (; word_idx < num_words; ++word_idx) {
        dst[word_idx] = 0;
    }
    
    printf("Loaded raw matrix: %zu elements (%zu words) from %s\n", elements_read, word_idx, filepath.c_str());
    return true;
}

// Load matrix data directly into DDR buffers (DDR-only mode).
// Syncs A and B to device inside this function so the kernel sees the data on hardware.
// A buffer: inA_bomapped has raw_mata_words entries of 128-bit words (ap_int<128>); each word holds WRD_LN elements.
// B buffer: inB_bomapped has exact_matb_sz entries of 128-bit words (cascade format, line-per-word from b_golden.txt).
int loadMatrixDataDirectToDDR(ap_int<128>* inA_bomapped, ap_int<128>* inB_bomapped,
                              ap_int<128>* outC_bomapped,
                              xrt::bo& inA_bohdl, xrt::bo& inB_bohdl,
                              size_t raw_mata_words, size_t exact_matb_sz, size_t exact_matc_sz) {
    printf("\n=== Loading Matrix Data Directly to DDR ===\n");
    
    try {
        // Matrix A: raw format (GEMM_SIZE_A × GEMM_SIZE_AB, row-major) - DMA kernel handles transformation
        // Matrix B: cascade format (b_golden.txt)
        std::string io_dir_name = std::string("gemm_") + std::to_string(GEMM_SIZE_A) + "x" + std::to_string(GEMM_SIZE_AB) + "x" + std::to_string(GEMM_SIZE_B) + "_ioFiles/";
        std::vector<std::string> a_candidates = {
            "matrix_A_input.txt",
            std::string("./aie_src/aiesim_data/") + io_dir_name + "matrix_A_input.txt",
            std::string("/sd_card/") + io_dir_name + "matrix_A_input.txt",
            std::string("/sd_card/") + "matrix_A_input.txt",
            std::string("/mnt/") + io_dir_name + "matrix_A_input.txt",
            std::string("/mnt/") + "matrix_A_input.txt"
        };
        std::vector<std::string> b_candidates = {
            "b_golden.txt",
            std::string("./") + io_dir_name + "b_golden.txt",
            std::string("/sd_card/") + io_dir_name + "b_golden.txt",
            std::string("/sd_card/") + "b_golden.txt",
            std::string("/mnt/") + io_dir_name + "b_golden.txt",
            std::string("/mnt/") + "b_golden.txt"
        };

        std::string a_path = find_existing_file(a_candidates);
        std::string b_path = find_existing_file(b_candidates);

        if (a_path.empty() || b_path.empty()) {
            printf("WARNING: Matrix input files not found (A:%s B:%s). Using default patterns.\n",
                   a_path.empty() ? "missing" : a_path.c_str(),
                   b_path.empty() ? "missing" : b_path.c_str());
            // Fill with default patterns directly in DDR
            ap_int<128> default_a("0x00010001000100010001000100010001", 16);
            ap_int<128> default_b("0x00020002000200020002000200020002", 16);
            std::fill(inA_bomapped, inA_bomapped + raw_mata_words, default_a);
            std::fill(inB_bomapped, inB_bomapped + exact_matb_sz, default_b);
        } else {
            // Load Matrix A from raw matrix format (row-major, simple format)
            printf("Loading Matrix A (raw format) from %s directly into DDR...\n", a_path.c_str());
            if (!load_raw_matrix_into_ddr_buffer(a_path, inA_bomapped, raw_mata_words)) {
                printf("Failed reading %s, falling back to default A pattern.\n", a_path.c_str());
                ap_int<128> default_a("0x00010001000100010001000100010001", 16);
                std::fill(inA_bomapped, inA_bomapped + raw_mata_words, default_a);
            }
            // Load Matrix B from cascade format
            printf("Loading Matrix B (cascade format) from %s directly into DDR...\n", b_path.c_str());
            if (!load_golden_into_ddr_buffer(b_path, inB_bomapped, exact_matb_sz)) {
                printf("Failed reading %s, falling back to default B pattern.\n", b_path.c_str());
                ap_int<128> default_b("0x00020002000200020002000200020002", 16);
                std::fill(inB_bomapped, inB_bomapped + exact_matb_sz, default_b);
            }
            // Log first loaded values so you can verify they match the golden files (not default pattern)
            if (raw_mata_words > 0) {
                ap_int<128> w = inA_bomapped[0];
                printf("  A first word (first %d elems): ", WRD_LN);
#if DATA_TYPE == 16
                for (int j = 0; j < WRD_LN; ++j) printf("%d ", (int)((w >> (j * 16)) & 0xFFFF));
#else
                for (int j = 0; j < WRD_LN; ++j) printf("%d ", (int)w.range((j + 1) * 32 - 1, j * 32));
#endif
                printf("(expect first line of matrix_A_input.txt)\n");
            }
            if (exact_matb_sz > 0) {
                ap_int<128> w = inB_bomapped[0];
                printf("  B first word (first %d elems): ", WRD_LN);
#if DATA_TYPE == 16
                for (int j = 0; j < WRD_LN; ++j) printf("%d ", (int)((w >> (j * 16)) & 0xFFFF));
#else
                for (int j = 0; j < WRD_LN; ++j) printf("%d ", (int)w.range((j + 1) * 32 - 1, j * 32));
#endif
                printf("(expect first line of b_golden.txt)\n");
            }
        }

        // Initialize output buffer to zero
        std::fill(outC_bomapped, outC_bomapped + exact_matc_sz, 0);
        printf("Output buffer initialized to zero\n");

        // Sync input buffers to device so the DMA kernel sees A and B on hardware.
        // Without this, the kernel reads device memory that was never updated → AIE gets no valid data → C stays zero.
        printf("Syncing input buffers A and B to device...\n");
        inA_bohdl.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        inB_bohdl.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        printf("Input buffers synced to device\n");

        printf("Matrices loaded directly into DDR successfully\n");
        
    } catch (const std::exception& e) {
        printf("ERROR: Matrix initialization failed: %s\n", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

// Write mapped BO contents to c.txt. Layout depends on SIMPLE_OUT_C (gemm_config.h).
// SIMPLE_OUT_C=1: buffer order (one line per word) -> compare with c_golden.txt.
// SIMPLE_OUT_C=0: row-major (one line per matrix row) -> compare with matrix_C_golden.txt.
// Call only after outC_bohdl.sync(XCL_BO_SYNC_BO_FROM_DEVICE) so DDR content is visible.
bool write_c_txt_from_bo(const ap_int<128>* src,
                        size_t elements,
                        const char* preferred_dir) {
    const size_t num_rows = GEMM_SIZE_A;
    const size_t num_cols = GEMM_SIZE_B;
    const size_t words_per_row = num_cols / WRD_LN;
    const size_t expected_words = num_rows * words_per_row;

    if (elements != expected_words) {
        fprintf(stderr, "ERROR: Buffer size mismatch. Expected %zu words, got %zu\n", expected_words, elements);
        return false;
    }

    // Diagnose all-zeros: buffer was not filled by kernel (runtime issue, not format)
    {
        bool all_zero = true;
        for (size_t i = 0; i < elements; i++) {
            if (src[i] != 0) { all_zero = false; break; }
        }
        if (all_zero) {
            fprintf(stderr,
                "WARNING: Output buffer C is all zeros. c.txt will not match golden.\n"
                "  This is a RUNTIME data-flow issue, not a format/layout bug.\n");
            printOutputBuffer(src, elements * sizeof(ap_int<128>));
        }
    }

    std::string out_dir = pick_gemm_output_dir(preferred_dir);
    if (out_dir.empty()) {
        fprintf(stderr,
                "Error creating any output directory (tried preferred, ./output_files/, "
#ifdef TARGET_HW
                "/media/output_files/, "
#endif
                "/mnt/output_files/)\n");
        return false;
    }

    std::string out_path = out_dir + "c.txt";
    std::ofstream out(out_path);
    if (!out.is_open()) {
        fprintf(stderr, "Error opening %s for write: %s\n", out_path.c_str(), strerror(errno));
        return false;
    }

#if SIMPLE_OUT_C
    // Simple out_C: matC is [c0_w0, c1_w0, c0_w1, c1_w1, ...]. Write one line per word -> c_golden.txt
    for (size_t word_idx = 0; word_idx < elements; ++word_idx) {
        const ap_int<128>& word = src[word_idx];
        for (int j = 0; j < WRD_LN; ++j) {
            if (DATA_TYPE == 16) {
                ap_int<16> v = (word >> (j * 16)) & 0xFFFF;
                out << (int16_t)v;
            } else if (DATA_TYPE == 32) {
                ap_int<32> v = (word >> (j * 32)) & 0xFFFFFFFF;
                out << (int32_t)v;
            }
            if (j < WRD_LN - 1) out << ' ';
        }
        out << '\n';
    }
    printf("Wrote C to %s (%zu words, %d elems/line) for comparison with c_golden.txt\n",
           out_path.c_str(), elements, WRD_LN);
#else
    // Complex out_C: matC is row-major, row r = [c0 words][c1 words]. Write one line per row -> matrix_C_golden.txt
    for (size_t r = 0; r < num_rows; ++r) {
        for (size_t cw = 0; cw < words_per_row; ++cw) {
            size_t buf_idx = r * words_per_row + cw;
            const ap_int<128>& word = src[buf_idx];
            for (int j = 0; j < WRD_LN; ++j) {
                if (DATA_TYPE == 16) {
                    ap_int<16> v = (word >> (j * 16)) & 0xFFFF;
                    out << (int16_t)v;
                } else if (DATA_TYPE == 32) {
                    ap_int<32> v = (word >> (j * 32)) & 0xFFFFFFFF;
                    out << (int32_t)v;
                }
                if (cw < words_per_row - 1 || j < WRD_LN - 1) out << ' ';
            }
        }
        out << '\n';
    }
    printf("Wrote C to %s (row-major, %zu rows x %zu cols) for comparison with matrix_C_golden.txt\n",
           out_path.c_str(), num_rows, num_cols);
#endif
    out.close();
    return true;
}
