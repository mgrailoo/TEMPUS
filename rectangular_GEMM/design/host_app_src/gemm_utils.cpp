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
    
    // Verify GRAPH_ITER_CNT calculation
    int expected_iter_cnt = (GEMM_SIZE_A * GEMM_SIZE_B) / (DIM_A * DIM_B) / SPLIT_B;
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
    else if (DATA_TYPE == 33) { sub_m=4; sub_k=4; sub_n=2; }
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
    printf("\nStep 1: Calculate intermediate values\n");
    printf("  GEMM_SZ_SPLIT_A = GEMM_SIZE_A / SPLIT_A = %d / %d = %d\n", GEMM_SIZE_A, SPLIT_A, gemm_sz_split_a);
    printf("  GEMM_SZ_CASC = GEMM_SIZE_AB / CASC_LN_AB = %d / %d = %d\n", GEMM_SIZE_AB, CASC_LN_AB, gemm_sz_casc);
    printf("  TILES_PER_ROW_IN_BLOCK_A = GEMM_SZ_SPLIT_A / DIM_A = %d / %d = %d\n", gemm_sz_split_a, DIM_A, tiles_per_row_in_block_a);
    printf("  BROADCAST_COUNT_A = (GEMM_SIZE_B / SPLIT_B) / DIM_B = (%d / %d) / %d = %d\n", GEMM_SIZE_B, SPLIT_B, DIM_B, broadcast_count_a);
    printf("\nStep 2: Calculate BASE_MATA_SZ (per cascade level)\n");
    printf("  BASE_MATA_SZ = (GEMM_SZ_SPLIT_A × GEMM_SZ_CASC × BROADCAST_COUNT_A × SPLIT_A) / WRD_LN\n");
    printf("                = (%d × %d × %d × %d) / %d\n", gemm_sz_split_a, gemm_sz_casc, broadcast_count_a, SPLIT_A, WRD_LN);
    int base_mata_sz_calc = (gemm_sz_split_a * gemm_sz_casc * broadcast_count_a * SPLIT_A) / WRD_LN;
    printf("                = %d / %d\n", (gemm_sz_split_a * gemm_sz_casc * broadcast_count_a * SPLIT_A), WRD_LN);
    printf("                = %d elements (per cascade level)\n", base_mata_sz_calc);
    printf("\nStep 3: Calculate EXACT_MATA_SZ (total across all cascade files)\n");
    printf("  EXACT_MATA_SZ = BASE_MATA_SZ × NUM_A_FILES\n");
    printf("                 = BASE_MATA_SZ × CASC_LN_AB\n");
    printf("                 = %d × %d\n", base_mata_sz_calc, NUM_A_FILES);
    printf("                 = %d elements\n", EXACT_MATA_SZ);
    printf("  Memory: %d elements × %zu bytes/element = %zu bytes (%.2f MB)\n",
           EXACT_MATA_SZ, sizeof(ap_int<128>), EXACT_MATA_SZ * sizeof(ap_int<128>),
           (EXACT_MATA_SZ * sizeof(ap_int<128>)) / (1024.0 * 1024.0));
    
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
    printf("  Formula: ((GEMM_SIZE_A/%d × GEMM_SIZE_AB/%d × (GEMM_SIZE_B/%d)/%d × %d) / %d) × %d\n",
           SPLIT_A, CASC_LN_AB, SPLIT_B, DIM_B, SPLIT_A, WRD_LN, CASC_LN_AB);
    printf("  Impact: Directly proportional to GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B\n");
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

// Allocate and initialize host memory
int allocateHostMemory(std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_A,
                      std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_B,
                      std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_C,
                      int exact_mata_sz, int exact_matb_sz, int exact_matc_sz) {
    // ============================================================================
    // MEMORY ALLOCATION: Resize vectors to exact matrix sizes
    // ============================================================================
    // Allocate memory for exact matrix sizes (not aligned sizes)
    // host_mem_A.size() = exact_mata_sz elements
    // host_mem_A memory = exact_mata_sz * sizeof(ap_int<128>) bytes
    // This must be <= ALIGNED_MATA_BYTES (which includes alignment padding)
    try {
        host_mem_A.resize(exact_mata_sz);  // Allocate exact number of elements
        host_mem_B.resize(exact_matb_sz);  // Allocate exact number of elements  
        host_mem_C.resize(exact_matc_sz);  // Allocate exact number of elements

        printf("Host memory allocated successfully\n");
        // printf("Matrix A: %zu elements (%zu bytes)\n", host_mem_A.size(), host_mem_A.size() * sizeof(ap_int<128>));
        // printf("Matrix B: %zu elements (%zu bytes)\n", host_mem_B.size(), host_mem_B.size() * sizeof(ap_int<128>));
        // printf("Matrix C: %zu elements (%zu bytes)\n", host_mem_C.size(), host_mem_C.size() * sizeof(ap_int<128>));

        // Verify that exact sizes are <= aligned sizes
        if (exact_mata_sz * sizeof(ap_int<128>) > ALIGNED_MATA_BYTES) {
            printf("ERROR: Matrix A exact size (%d) exceeds aligned size (%zu)\n", 
                   exact_mata_sz, ALIGNED_MATA_BYTES);
            return EXIT_FAILURE;
        }
        if (exact_matb_sz * sizeof(ap_int<128>) > ALIGNED_MATB_BYTES) {
            printf("ERROR: Matrix B exact size (%d) exceeds aligned size (%zu)\n", 
                   exact_matb_sz, ALIGNED_MATB_BYTES);
            return EXIT_FAILURE;
        }
        if (exact_matc_sz * sizeof(ap_int<128>) > ALIGNED_MATC_BYTES) {
            printf("ERROR: Matrix C exact size (%d) exceeds aligned size (%zu)\n", 
                   exact_matc_sz, ALIGNED_MATC_BYTES);
            return EXIT_FAILURE;
        }
        
    } catch (const std::bad_alloc& e) {
        printf("ERROR: Memory allocation failed: %s\n", e.what());
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        printf("ERROR: Unexpected error during memory allocation: %s\n", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// Load matrix data from files or use default patterns
int loadMatrixData(std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_A,
                   std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_B,
                   std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_C) {
    // Initialize matrices with proper alignment and bounds checking
    try {
        // Attempt to load golden files for A and B
        std::string io_dir_name = std::string("gemm_") + std::to_string(GEMM_SIZE_A) + "x" + std::to_string(GEMM_SIZE_AB) + "x" + std::to_string(GEMM_SIZE_B) + "_ioFiles/";
        std::vector<std::string> a_candidates = {
            "a_golden.txt",
            std::string("./aie_src/aiesim_data/") + io_dir_name + "a_golden.txt",
            std::string("/sd_card/") + io_dir_name + "a_golden.txt",
            std::string("/sd_card/") + "a_golden.txt",
            std::string("/mnt/") + io_dir_name + "a_golden.txt",
            std::string("/mnt/") + "a_golden.txt"
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
            printf("WARNING: golden inputs not found (A:%s B:%s). Using default patterns.\n",
                   a_path.empty() ? "missing" : a_path.c_str(),
                   b_path.empty() ? "missing" : b_path.c_str());
            std::fill(host_mem_A.begin(), host_mem_A.end(), ap_int<128>("0x00010001000100010001000100010001", 16));
            std::fill(host_mem_B.begin(), host_mem_B.end(), ap_int<128>("0x00020002000200020002000200020002", 16));
        } else {
            printf("Loading A from %s\n", a_path.c_str());
            if (!load_golden_into_vector(a_path, host_mem_A)) {
                printf("Failed reading %s, falling back to default A pattern.\n", a_path.c_str());
                std::fill(host_mem_A.begin(), host_mem_A.end(), ap_int<128>("0x00010001000100010001000100010001", 16));
            }
            printf("Loading B from %s\n", b_path.c_str());
            if (!load_golden_into_vector(b_path, host_mem_B)) {
                printf("Failed reading %s, falling back to default B pattern.\n", b_path.c_str());
                std::fill(host_mem_B.begin(), host_mem_B.end(), ap_int<128>("0x00020002000200020002000200020002", 16));
            }
        }

        std::fill(host_mem_C.begin(), host_mem_C.end(), 0);
        printf("Matrices initialized successfully\n");
        
    } catch (const std::exception& e) {
        printf("ERROR: Matrix initialization failed: %s\n", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

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
            } else if (DATA_TYPE == 33) {  // float
                uint32_t float_bits = (inA_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                float val = *reinterpret_cast<float*>(&float_bits);
                printf("%8.4f ", val);
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
            } else if (DATA_TYPE == 33) {  // float
                uint32_t float_bits = (inB_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                float val = *reinterpret_cast<float*>(&float_bits);
                printf("%8.4f ", val);
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
            if (DATA_TYPE == 33) {  // float
                uint32_t float_bits = (outC_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                float val = *reinterpret_cast<float*>(&float_bits);
                printf("%8.4f ", val);
            } else if (DATA_TYPE == 4) {  // int4
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
    
    // Calculate expected sizes
    size_t exact_mata_bytes = EXACT_MATA_SZ * sizeof(ap_int<128>);
    size_t exact_matb_bytes = EXACT_MATB_SZ * sizeof(ap_int<128>);
    size_t exact_matc_bytes = EXACT_MATC_SZ * sizeof(ap_int<128>);
    
    printf("\nMatrix A (GEMM_SIZE_A=%d x GEMM_SIZE_AB=%d):\n", GEMM_SIZE_A, GEMM_SIZE_AB);
    printf("  Exact size needed:     %zu bytes (%.2f MB) = %d elements × %zu bytes/element\n",
           exact_mata_bytes, exact_mata_bytes / (1024.0 * 1024.0), EXACT_MATA_SZ, sizeof(ap_int<128>));
    printf("  Aligned size (padding): %zu bytes (%.2f MB) = %zu bytes + alignment padding\n",
           ALIGNED_MATA_BYTES, ALIGNED_MATA_BYTES / (1024.0 * 1024.0), exact_mata_bytes);
    printf("  Actually allocated:    %zu bytes (%.2f MB) from XRT\n",
           actual_allocated_A, actual_allocated_A / (1024.0 * 1024.0));
    printf("  Padding overhead:      %zu bytes (%.2f%%) - unused but required for alignment\n",
           ALIGNED_MATA_BYTES - exact_mata_bytes,
           100.0 * (ALIGNED_MATA_BYTES - exact_mata_bytes) / exact_mata_bytes);
    
    if (actual_allocated_A != ALIGNED_MATA_BYTES) {
        printf("  ⚠️  WARNING: Allocated size (%zu) != Expected aligned size (%zu)\n",
               actual_allocated_A, ALIGNED_MATA_BYTES);
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
    size_t total_exact_bytes = exact_mata_bytes + exact_matb_bytes + exact_matc_bytes;
    size_t total_aligned_bytes = ALIGNED_MATA_BYTES + ALIGNED_MATB_BYTES + ALIGNED_MATC_BYTES;
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
           exact_mata_bytes, exact_mata_bytes / (1024.0 * 1024.0), GEMM_SIZE_A, GEMM_SIZE_AB);
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
    // Check device memory availability before buffer creation
    printf("\n=== Device Memory Check ===\n");
    size_t total_mem_bytes = ALIGNED_MATA_BYTES + ALIGNED_MATB_BYTES + ALIGNED_MATC_BYTES;
    printf("Required device memory:\n");
    printf("  Matrix A buffer: %zu bytes (%.2f MB)\n", ALIGNED_MATA_BYTES, ALIGNED_MATA_BYTES / (1024.0 * 1024.0));
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
    printf("  Matrix A (%.2f MB): Memory bank 0\n", ALIGNED_MATA_BYTES / (1024.0 * 1024.0));
    printf("  Matrix B (%.2f MB): Memory bank 1 (if available, else 0)\n", ALIGNED_MATB_BYTES / (1024.0 * 1024.0));
    printf("  Matrix C (%.2f MB): Memory bank 0\n", ALIGNED_MATC_BYTES / (1024.0 * 1024.0));
    
    // Buffer creation with error handling and memory bank distribution
    auto t_bo_create = std::chrono::high_resolution_clock::now();
    
    // Try allocating buffers one at a time to identify which fails
    try {
        printf("Allocating Matrix A buffer (%.2f MB) on bank 0...\n", ALIGNED_MATA_BYTES / (1024.0 * 1024.0));
        inA_bohdl = xrt::bo(device, ALIGNED_MATA_BYTES, 0, 0);
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
int launchKernel(xrt::kernel& dma_hls_khdl, xrt::bo& inA_bohdl, xrt::bo& inB_bohdl, xrt::bo& outC_bohdl, xrt::run& dma_hls_rhdl) {
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
    
    // Extract directory from filename
    std::string output_dir = std::string(output_filename);
    size_t last_slash = output_dir.find_last_of('/');
    if (last_slash != std::string::npos) {
        output_dir = output_dir.substr(0, last_slash + 1);
    } else {
        output_dir = "./";
    }
    
    // Write output file (write_c_txt_from_bo expects const char* for directory)
    write_c_txt_from_bo(outC_bomapped, EXACT_MATC_SZ, output_dir.c_str());
    
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
            if (DATA_TYPE == 33) {  // float
                float v = 0.0f;
                if (!(iss >> v)) v = 0.0f;
                // Pack float as 32-bit integer representation
                uint32_t float_bits = *reinterpret_cast<uint32_t*>(&v);
                packed.range((k+1)*32-1, k*32) = float_bits;
            } else {
                int v = 0;
                if (!(iss >> v)) v = 0;
                
                // Pack based on data type
                if (DATA_TYPE == 16) {  // int16
                    packed.range((k+1)*16-1, k*16) = static_cast<int16_t>(v);
                } else if (DATA_TYPE == 32) {  // int32
                    packed.range((k+1)*32-1, k*32) = static_cast<int32_t>(v);
                }
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
            if (DATA_TYPE == 33) {  // float
                float v = 0.0f;
                if (!(iss >> v)) v = 0.0f;
                // Pack float as 32-bit integer representation
                uint32_t float_bits = *reinterpret_cast<uint32_t*>(&v);
                packed.range((k+1)*32-1, k*32) = float_bits;
            } else {
                int v = 0;
                if (!(iss >> v)) v = 0;
                
                // Pack based on data type
                if (DATA_TYPE == 16) {  // int16
                    packed.range((k+1)*16-1, k*16) = static_cast<int16_t>(v);
                } else if (DATA_TYPE == 32) {  // int32
                    packed.range((k+1)*32-1, k*32) = static_cast<int32_t>(v);
                }
            }
        }
        dst[idx++] = packed;
    }
    // If file shorter than needed, pad remaining with zeros
    for (; idx < num_elements; ++idx) dst[idx] = 0;
    return true;
}

// Load matrix data directly into DDR buffers (bypasses PS RAM)
int loadMatrixDataDirectToDDR(ap_int<128>* inA_bomapped, ap_int<128>* inB_bomapped, 
                              ap_int<128>* outC_bomapped,
                              size_t exact_mata_sz, size_t exact_matb_sz, size_t exact_matc_sz) {
    printf("\n=== Loading Matrix Data Directly to DDR (Bypassing PS RAM) ===\n");
    
    try {
        // Attempt to load golden files for A and B
        std::string io_dir_name = std::string("gemm_") + std::to_string(GEMM_SIZE_A) + "x" + std::to_string(GEMM_SIZE_AB) + "x" + std::to_string(GEMM_SIZE_B) + "_ioFiles/";
        std::vector<std::string> a_candidates = {
            "a_golden.txt",
            std::string("./aie_src/aiesim_data/") + io_dir_name + "a_golden.txt",
            std::string("/sd_card/") + io_dir_name + "a_golden.txt",
            std::string("/sd_card/") + "a_golden.txt",
            std::string("/mnt/") + io_dir_name + "a_golden.txt",
            std::string("/mnt/") + "a_golden.txt"
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
            printf("WARNING: golden inputs not found (A:%s B:%s). Using default patterns.\n",
                   a_path.empty() ? "missing" : a_path.c_str(),
                   b_path.empty() ? "missing" : b_path.c_str());
            // Fill with default patterns directly in DDR
            ap_int<128> default_a("0x00010001000100010001000100010001", 16);
            ap_int<128> default_b("0x00020002000200020002000200020002", 16);
            std::fill(inA_bomapped, inA_bomapped + exact_mata_sz, default_a);
            std::fill(inB_bomapped, inB_bomapped + exact_matb_sz, default_b);
        } else {
            printf("Loading A from %s directly into DDR...\n", a_path.c_str());
            if (!load_golden_into_ddr_buffer(a_path, inA_bomapped, exact_mata_sz)) {
                printf("Failed reading %s, falling back to default A pattern.\n", a_path.c_str());
                ap_int<128> default_a("0x00010001000100010001000100010001", 16);
                std::fill(inA_bomapped, inA_bomapped + exact_mata_sz, default_a);
            }
            printf("Loading B from %s directly into DDR...\n", b_path.c_str());
            if (!load_golden_into_ddr_buffer(b_path, inB_bomapped, exact_matb_sz)) {
                printf("Failed reading %s, falling back to default B pattern.\n", b_path.c_str());
                ap_int<128> default_b("0x00020002000200020002000200020002", 16);
                std::fill(inB_bomapped, inB_bomapped + exact_matb_sz, default_b);
            }
        }

        // Initialize output buffer to zero
        std::fill(outC_bomapped, outC_bomapped + exact_matc_sz, 0);
        printf("Output buffer initialized to zero\n");
        
        // Note: Buffer synchronization is handled by XRT automatically when buffers are used
        // No need to manually sync here since we're writing directly to mapped DDR buffers
        printf("Matrices loaded directly into DDR successfully\n");
        
    } catch (const std::exception& e) {
        printf("ERROR: Matrix initialization failed: %s\n", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

// Write mapped BO contents to c.txt with dynamic word length based on data type
bool write_c_txt_from_bo(const ap_int<128>* src,
                        size_t elements,
                        const char* preferred_dir) {
    // Try preferred dir, then /mnt/output_files/, then ./output_files/
    std::vector<std::string> dirs = {
        std::string(preferred_dir ? preferred_dir : ""),
        std::string("/mnt/output_files/"),
        std::string("./output_files/")
    };

    std::string out_dir;
    for (const auto& d : dirs) {
        struct stat st;
        if (stat(d.c_str(), &st) == 0) { out_dir = d; break; }
        if (mkdir(d.c_str(), 0755) == 0 || errno == EEXIST) { out_dir = d; break; }
    }
    if (out_dir.empty()) {
        fprintf(stderr, "Error creating any output directory (tried preferred, /mnt/output_files/, ./output_files/)\n");
        return false;
    }

    std::string out_path = out_dir + "c.txt";
    std::ofstream out(out_path);
    if (!out.is_open()) {
        fprintf(stderr, "Error opening %s for write: %s\n", out_path.c_str(), strerror(errno));
        return false;
    }
    for (size_t i = 0; i < elements; ++i) {
        for (int j = 0; j < WRD_LN; ++j) {
            // Extract based on data type
            if (DATA_TYPE == 33) {  // float
                uint32_t float_bits = (src[i] >> (j * 32)) & 0xFFFFFFFF;
                float val = *reinterpret_cast<float*>(&float_bits);
                // Round to 4 decimal places before writing
                // Use printf-style formatting to match golden file precision
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%.4f", val);
                out << buffer;
            } else if (DATA_TYPE == 16) {  // int16
                ap_int<16> current_val = (src[i] >> (j * 16)) & 0xFFFF;
                out << (int16_t)current_val;
            } else if (DATA_TYPE == 32) {  // int32
                ap_int<32> current_val = (src[i] >> (j * 32)) & 0xFFFFFFFF;
                out << (int32_t)current_val;
            }
            if (j + 1 < WRD_LN) out << ' ';
        }
        out << '\n';
    }
    out.close();
    printf("Wrote C buffer to %s\n", out_path.c_str());
    return true;
}
