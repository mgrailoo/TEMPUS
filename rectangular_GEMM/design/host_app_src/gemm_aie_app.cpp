/*
Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
SPDX-License-Identifier: MIT

AI Engine GEMM Host Application
===============================

This is the main host application for the AI Engine General Matrix Multiply (GEMM)
implementation on Versal ACAP platforms. The application orchestrates the complete
execution flow from host memory allocation through AI Engine computation to result
collection and validation.

Architecture Overview:
- Host Application: Manages memory, data transfer, and execution coordination
- DMA Kernels: Handle high-speed data movement between host and AI Engine
- AI Engine Graph: Performs the actual matrix multiplication computation
- XRT Runtime: Provides the interface between host and hardware

Execution Flow:
1. Configuration and Memory Setup
2. Device and XCLBIN Initialization  
3. Buffer Creation and Memory Mapping
4. Data Transfer to Device
5. Kernel and Graph Creation
6. Kernel Launch and Execution
7. Result Synchronization and Processing

Key Features:
- Configurable matrix dimensions via config.json
- Multiple data type support (int16, int32, float)
- Memory alignment optimization for DMA efficiency
- Comprehensive timing and performance measurement
- Error handling and validation throughout
- Support for both finite and infinite execution modes

Usage:
    ./gemm_aie_xrt.elf <xclbin_file>
    
Where:
    xclbin_file: Path to the compiled XCLBIN file containing the hardware design
*/

// ============================================================================
// Standard C++ Headers
// ============================================================================
#include <cstdio>        // Standard I/O functions
#include <cstdlib>       // Standard library functions (EXIT_SUCCESS, etc.)
#include <cstdint>       // Standard integer types
#include <iostream>      // Input/output stream objects
#include <fstream>       // File stream operations
#include <vector>        // Dynamic array container
#include <string>        // String class and functions
#include <chrono>        // High-resolution timing
#include <algorithm>     // STL algorithms
#include <iomanip>       // Stream manipulators for formatting
#include <cmath>         // Mathematical functions
#include <thread>        // Threading support
#include <limits>        // Numeric limits
#include <memory>        // Smart pointers and aligned_allocator
#include <random>        // Random number generation
#include <sstream>       // String stream operations

// ============================================================================
// System Headers
// ============================================================================
#include <sys/stat.h>    // File status information
#include <errno.h>       // Error number definitions
#include <cstring>       // String manipulation functions
#include <unistd.h>      // POSIX operating system API

// ============================================================================
// Xilinx Headers
// ============================================================================
#include <ap_int.h>      // Arbitrary precision integer types for HLS

// ============================================================================
// Project Headers
// ============================================================================
#include "graph.h"                           // AI Engine graph definition
#include "../design_configs/gemm_config.h"  // Centralized configuration constants
#include "gemm_utils.h"                     // Utility functions and macros

// ============================================================================
// XRT Runtime Headers
// ============================================================================
// Modern XRT C++ API headers for device management and kernel execution
#include "xrt/xrt_device.h"    // Device management and enumeration
#include "xrt/xrt_kernel.h"    // Kernel execution and control
#include "xrt/xrt_graph.h"     // AI Engine graph management
#include "xrt/xrt_bo.h"        // Buffer object management

// Experimental XRT headers (commented out - use stable API)
// #include "experimental/xrt_kernel.h"
// #include "experimental/xrt_graph.h"


// ============================================================================
// GRAPH ITERATION COUNT CONFIGURATION
// ============================================================================
// Define the number of iterations for the AI Engine graph execution
// This determines how many times the matrix multiplication will be performed
#ifndef GRAPH_ITER_CNT
#if ITER_CNT == -1
    #define GRAPH_ITER_CNT ITER_CNT    // Infinite execution mode (-1)
#else
    // Formula: GRAPH_ITER_CNT = (GEMM_SIZE_A × (GEMM_SIZE_B / SPLIT_B)) / (DIM_A × DIM_B)
    // Each output file contains: GEMM_SIZE_A rows × (GEMM_SIZE_B / SPLIT_B) columns
    // Each iteration produces: DIM_A × DIM_B elements per file
    #define GRAPH_ITER_CNT ((GEMM_SIZE_A * GEMM_SIZE_B / SPLIT_B) / (DIM_A * DIM_B))  // Finite execution mode
#endif
#endif


// ============================================================================
// MEMORY ALIGNMENT CONFIGURATION
// ============================================================================
// Two types of alignment are needed for optimal AI Engine performance:
// 1. SIZE ALIGNMENT: Buffer sizes must be multiples of page size (4096 bytes)
// 2. ADDRESS ALIGNMENT: Memory addresses must be aligned to page boundaries
//
// HOW THEY WORK TOGETHER:
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ 1. ALIGN_UP calculates aligned SIZE for XRT buffers                    │
// │    Example: 2048 bytes → 4096 bytes (adds padding)                     │
// │                                                                         │
// │ 2. aligned_allocator ensures aligned ADDRESS for host memory           │
// │    Example: 0x12345678 → 0x12345000 (rounds down to 4KB boundary)     │
// │                                                                         │
// │ 3. Result: Both size and address are 4096-byte aligned                 │
// │    This enables maximum DMA transfer efficiency                         │
// └─────────────────────────────────────────────────────────────────────────┘





// ============================================================================
// MAIN APPLICATION FUNCTION
// ============================================================================
/**
 * @brief Main function for AI Engine GEMM host application
 * 
 * This function orchestrates the complete execution flow of the GEMM application:
 * 1. Validates command line arguments
 * 2. Prints configuration information
 * 3. Allocates and initializes host memory
 * 4. Loads matrix data from files or generates test data
 * 5. Initializes XRT device and loads XCLBIN
 * 6. Creates and maps device buffers
 * 7. Transfers data to device
 * 8. Creates kernel and graph objects
 * 9. Launches kernel execution
 * 10. Executes AI Engine computation
 * 11. Synchronizes and processes results
 * 12. Measures and reports timing performance
 * 
 * @param argc Number of command line arguments
 * @param argv Array of command line argument strings
 * @return EXIT_SUCCESS on successful completion, EXIT_FAILURE on error
 * 
 * @note The application expects exactly one argument: the path to the XCLBIN file
 * @note All configuration parameters are read from config.json via gemm_config.h
 * @note Memory alignment is critical for optimal DMA performance
 */
int main(int argc, char ** argv) {

    // ========================================================================
    // PHASE 0: APPLICATION INITIALIZATION AND TIMING
    // ========================================================================
    // Start high-resolution timing for overall performance measurement
    auto start_time = std::chrono::high_resolution_clock::now();

    // ========================================================================
    // PHASE 0.1: COMMAND LINE VALIDATION
    // ========================================================================
    // Validate that the required XCLBIN file path is provided
    if (argc < 2) {
        printf("ERROR: Usage: %s <xclbin_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // ========================================================================
    // PHASE 0.2: CONFIGURATION DISPLAY AND VALIDATION
    // ========================================================================
    // Print configuration information and validate system parameters
    printExactMatrixSizeCalculation();  // Show detailed calculation of EXACT_MAT*_SZ from GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B
    // printConfigurationInfo();        // Display matrix dimensions, data types, etc.
    // printValidTileDimensions();      // Show valid tile dimension combinations
    // verifyBufferConfiguration();     // Validate memory buffer configuration

    // ========================================================================
    // PHASE 0.3: MEMORY MODE SELECTION (PS RAM vs DDR-Only)
    // ========================================================================
    // Calculate total memory requirement
    size_t actual_mata_bytes = EXACT_MATA_SZ * sizeof(ap_int<128>);  // Matrix A size in bytes
    size_t actual_matb_bytes = EXACT_MATB_SZ * sizeof(ap_int<128>);  // Matrix B size in bytes
    size_t actual_matc_bytes = EXACT_MATC_SZ * sizeof(ap_int<128>);  // Matrix C size in bytes
    size_t total_memory_bytes = actual_mata_bytes + actual_matb_bytes + actual_matc_bytes;
    const size_t PS_RAM_LIMIT = 6ULL * 1024 * 1024 * 1024;  // 6 GB (safe limit for 7 GB PS RAM)
    
    // Check config file setting first, then auto-detect based on memory size
    bool use_ddr_only_mode = false;
    bool config_forced_ddr = false;
    
    #ifdef USE_DDR_ONLY_MODE
        if (USE_DDR_ONLY_MODE == 1) {
            use_ddr_only_mode = true;
            config_forced_ddr = true;
        } else {
            use_ddr_only_mode = (total_memory_bytes > PS_RAM_LIMIT);
        }
    #else
        use_ddr_only_mode = (total_memory_bytes > PS_RAM_LIMIT);
    #endif
    
    if (use_ddr_only_mode) {
        if (config_forced_ddr) {
            printf("\n=== DDR-ONLY MODE ENABLED (from config file) ===\n");
            printf("USE_DDR_ONLY_MODE=1 in config.json forces DDR-only mode\n");
        } else {
            printf("\n=== DDR-ONLY MODE ENABLED (auto-detected) ===\n");
            printf("Total memory required: %.2f GB (exceeds PS RAM limit of 6 GB)\n", 
                   total_memory_bytes / (1024.0 * 1024.0 * 1024.0));
        }
        printf("Matrices will be loaded directly into DDR, bypassing PS RAM\n");
    } else {
        printf("\n=== STANDARD MODE (PS RAM → DDR) ===\n");
        printf("Total memory required: %.2f GB (within PS RAM limit)\n", 
               total_memory_bytes / (1024.0 * 1024.0 * 1024.0));
    }
    
    // Host memory vectors (only used in standard mode)
    std::vector<ap_int<128>, aligned_allocator<ap_int<128>>> host_mem_A;  // Matrix A host memory
    std::vector<ap_int<128>, aligned_allocator<ap_int<128>>> host_mem_B;  // Matrix B host memory
    std::vector<ap_int<128>, aligned_allocator<ap_int<128>>> host_mem_C;  // Matrix C host memory

    // Allocate host memory only if not using DDR-only mode
    if (!use_ddr_only_mode) {
        // ========================================================================
        // PHASE 0.3a: HOST MEMORY ALLOCATION WITH ADDRESS ALIGNMENT
        // ========================================================================
        // Allocate host memory using aligned_allocator for optimal DMA performance
        // This ensures memory addresses are aligned to 4096-byte boundaries
        // which enables efficient DMA transfers and maximum AI Engine performance
        if (allocateHostMemory(host_mem_A, host_mem_B, host_mem_C, 
                              EXACT_MATA_SZ, EXACT_MATB_SZ, EXACT_MATC_SZ) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        
        // ========================================================================
        // PHASE 0.4: MATRIX DATA LOADING AND INITIALIZATION
        // ========================================================================
        // Load matrix data from files or generate test patterns
        // This includes loading from input files or creating synthetic test data
        if (loadMatrixData(host_mem_A, host_mem_B, host_mem_C) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    }

    // ============================================================================
    // MAIN EXECUTION PHASES WITH TIMING MEASUREMENTS
    // ============================================================================
    // The main execution is divided into 7 distinct phases, each with specific
    // timing measurements to enable performance analysis and optimization.
    
    try {
        long long phase1_us = 0;
        long long phase2_us = 0;
        long long phase3_us = 0;
        long long phase4_us = 0;
        long long phase5_us = 0;
        long long phase6_us = 0;
        long long phase7_us = 0;
        long long phase8_us = 0;
        // ========================================================================
        // PHASE 1: DEVICE AND XCLBIN INITIALIZATION
        // ========================================================================
        // Initialize XRT device, load XCLBIN file, and prepare for execution
        // This phase includes device enumeration, XCLBIN loading, and validation
        auto t_phase1 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 1: Device and XCLBIN Initialization ===\n");
        
        xrt::device device;           // XRT device handle
        xrt::uuid xclbin_uuid;        // XCLBIN UUID for kernel identification
        
        if (initializeDeviceAndLoadXclbin(device, xclbin_uuid, argv[1]) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    phase1_us = logElapsedTime(t_phase1, "PHASE 1 TOTAL: Device + XCLBIN initialization");

        // ========================================================================
        // PHASE 2: BUFFER CREATION AND MAPPING
        // ========================================================================
        // Create XRT buffer objects and map them to host memory for data transfer
        // This phase allocates device memory and establishes host-device memory mapping
        auto t_phase2 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 2: Buffer Creation and Mapping ===\n");
        
        xrt::bo inA_bohdl, inB_bohdl, outC_bohdl;              // Buffer object handles
        ap_int<128>* inA_bomapped, *inB_bomapped, *outC_bomapped;  // Mapped host pointers
        
        if (createAndMapBuffers(device, inA_bohdl, inB_bohdl, outC_bohdl, 
                               inA_bomapped, inB_bomapped, outC_bomapped) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    phase2_us = logElapsedTime(t_phase2, "PHASE 2 TOTAL: Buffer creation + mapping");

        // ========================================================================
        // PHASE 3: DATA TRANSFER TO DEVICE
        // ========================================================================
        // Transfer matrix data from host memory to device buffers
        // This includes copying data and synchronizing to ensure completion
        auto t_phase3 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 3: Data Transfer to Device ===\n");
        
        if (use_ddr_only_mode) {
            // DDR-only mode: Load data directly into DDR buffers
            printf("Loading matrices directly into DDR buffers...\n");
            if (loadMatrixDataDirectToDDR(inA_bomapped, inB_bomapped, outC_bomapped,
                                         EXACT_MATA_SZ, EXACT_MATB_SZ, EXACT_MATC_SZ) != EXIT_SUCCESS) {
                return EXIT_FAILURE;
            }
        } else {
            // Standard mode: Copy from PS RAM to DDR
            if (transferDataToDevice(host_mem_A, host_mem_B, inA_bomapped, inB_bomapped, outC_bomapped, inA_bohdl, inB_bohdl) != EXIT_SUCCESS) {
                return EXIT_FAILURE;
            }
        }
    phase3_us = logElapsedTime(t_phase3, "PHASE 3 TOTAL: Data transfer + sync to device");

        // Debug: Print input buffers after transfer for verification
    // DEBUG: enable if input buffer inspection is required
    // printInputBuffers(inA_bomapped, inB_bomapped, ALIGNED_MATA_BYTES, ALIGNED_MATB_BYTES);

        // ========================================================================
        // PHASE 4: KERNEL AND GRAPH CREATION
        // ========================================================================
        // Create kernel and graph objects for execution
        // This phase instantiates the DMA kernel and AI Engine graph
        auto t_phase4 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 4: Kernel and Graph Creation ===\n");
                    // Graph is already created in main, just verify it's valid
        xrt::graph gemm_aie_gr(device, xclbin_uuid, "g");      // AI Engine graph handle
        
        printf("Graph object already created and ready\n");
        xrt::kernel dma_hls_khdl;                              // DMA kernel handle
        
        
        if (createKernel(device, xclbin_uuid, dma_hls_khdl) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    phase4_us = logElapsedTime(t_phase4, "PHASE 4 TOTAL: Kernel + Graph creation");

        // ========================================================================
        // PHASE 5 & 6: BENCHMARKING KERNEL LAUNCH + CORE COMPUTATION
        // ========================================================================
        // Measure average time for Phase 5 (Kernel Launch) and Phase 6 (Core Computation)
        // This matches PyTorch benchmarking methodology for fair comparison
        printf("\n=== PHASE 5 & 6: Benchmarking Kernel Launch + Core Computation ===\n");
        
        // Number of warmup and benchmark iterations (similar to PyTorch)
        const int WARMUP_ITERATIONS = 3;
        const int BENCHMARK_ITERATIONS = (N_SAMPLES > 1) ? N_SAMPLES : 10;  // Use N_SAMPLES if > 1, else default to 10
        
        std::vector<long long> phase5_times;  // Store Phase 5 times
        std::vector<long long> phase6_times;  // Store Phase 6 times
        std::vector<long long> phase5_6_combined_times;  // Store combined Phase 5+6 times
        
        xrt::run dma_hls_rhdl;  // Kernel run handle (reused across iterations)
        
        // ========================================================================
        // WARMUP RUNS (no timing measurement)
        // ========================================================================
        printf("Warming up (%d iterations)...\n", WARMUP_ITERATIONS);
        for (int warmup = 0; warmup < WARMUP_ITERATIONS; warmup++) {
            xrt::run warmup_rhdl;  // Create new run handle for each warmup
            if (launchKernel(dma_hls_khdl, inA_bohdl, inB_bohdl, outC_bohdl, warmup_rhdl) != EXIT_SUCCESS) {
                return EXIT_FAILURE;
            }
            if (executeComputation(gemm_aie_gr, warmup_rhdl, outC_bomapped, outC_bohdl) != EXIT_SUCCESS) {
                return EXIT_FAILURE;
            }
            // Wait for graph to complete before next iteration
            gemm_aie_gr.wait();
            // Sync output buffer (required for next iteration)
            outC_bohdl.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        }
        printf("Warmup complete.\n");
        
        // ========================================================================
        // BENCHMARK RUNS (with timing measurement)
        // ========================================================================
        printf("Benchmarking Phase 5 + Phase 6 (%d iterations)...\n", BENCHMARK_ITERATIONS);
        for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
            // Phase 5: Kernel Launch
            auto t_phase5 = std::chrono::high_resolution_clock::now();
            xrt::run iter_rhdl;  // Create new run handle for each benchmark iteration
            if (launchKernel(dma_hls_khdl, inA_bohdl, inB_bohdl, outC_bohdl, iter_rhdl) != EXIT_SUCCESS) {
                return EXIT_FAILURE;
            }
            long long phase5_iter_us = getElapsedMicroseconds(t_phase5);
            phase5_times.push_back(phase5_iter_us);
            
            // Phase 6: Core Computation (Graph run + DMA wait)
            auto t_phase6 = std::chrono::high_resolution_clock::now();
            if (executeComputation(gemm_aie_gr, iter_rhdl, outC_bomapped, outC_bohdl) != EXIT_SUCCESS) {
                return EXIT_FAILURE;
            }
            // Wait for graph to complete before next iteration
            gemm_aie_gr.wait();
            long long phase6_iter_us = getElapsedMicroseconds(t_phase6);
            phase6_times.push_back(phase6_iter_us);
            
            // Combined Phase 5 + Phase 6 time
            long long phase5_6_combined_us = phase5_iter_us + phase6_iter_us;
            phase5_6_combined_times.push_back(phase5_6_combined_us);
            
            // Sync output buffer (required for next iteration)
            outC_bohdl.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
            
            if ((iter + 1) % 5 == 0 || iter == BENCHMARK_ITERATIONS - 1) {
                printf("  Completed %d/%d iterations\n", iter + 1, BENCHMARK_ITERATIONS);
            }
        }
        
        // ========================================================================
        // END GRAPH EXECUTION (CRITICAL: Prevents state persistence between runs)
        // ========================================================================
        // After all iterations complete, properly end the graph to clean up device state
        // Without this, subsequent program runs may experience 1000x slowdown due to
        // graph state persisting on the device from the previous run
        printf("Ending graph execution to clean up device state...\n");
        gemm_aie_gr.end();
        printf("Graph execution ended successfully\n");
        
        // ========================================================================
        // CALCULATE STATISTICS (similar to PyTorch)
        // ========================================================================
        // Calculate statistics for Phase 5 + Phase 6 combined (most relevant for comparison)
        long long sum_combined = 0;
        long long min_combined = phase5_6_combined_times[0];
        long long max_combined = phase5_6_combined_times[0];
        
        for (size_t i = 0; i < phase5_6_combined_times.size(); i++) {
            sum_combined += phase5_6_combined_times[i];
            if (phase5_6_combined_times[i] < min_combined) min_combined = phase5_6_combined_times[i];
            if (phase5_6_combined_times[i] > max_combined) max_combined = phase5_6_combined_times[i];
        }
        
        double mean_combined_us = static_cast<double>(sum_combined) / BENCHMARK_ITERATIONS;
        
        // Calculate standard deviation
        double variance_combined = 0.0;
        for (size_t i = 0; i < phase5_6_combined_times.size(); i++) {
            double diff = phase5_6_combined_times[i] - mean_combined_us;
            variance_combined += diff * diff;
        }
        double std_combined_us = sqrt(variance_combined / BENCHMARK_ITERATIONS);
        
        // Calculate statistics for Phase 5 only
        long long sum_phase5 = 0;
        for (size_t i = 0; i < phase5_times.size(); i++) {
            sum_phase5 += phase5_times[i];
        }
        double mean_phase5_us = static_cast<double>(sum_phase5) / BENCHMARK_ITERATIONS;
        
        // Calculate statistics for Phase 6 only
        long long sum_phase6 = 0;
        for (size_t i = 0; i < phase6_times.size(); i++) {
            sum_phase6 += phase6_times[i];
        }
        double mean_phase6_us = static_cast<double>(sum_phase6) / BENCHMARK_ITERATIONS;
        
        // ========================================================================
        // PRINT BENCHMARK RESULTS (PyTorch-style format)
        // ========================================================================
        printf("\n=== BENCHMARK RESULTS (Phase 5 + Phase 6) ===\n");
        printf("Matrix Size: %dx%dx%d (A×AB×B)\n", GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B);
        printf("Iterations: %d (after %d warmup runs)\n", BENCHMARK_ITERATIONS, WARMUP_ITERATIONS);
        printf("\nPhase 5 + Phase 6 Combined (Kernel Launch + Core Computation):\n");
        printf("  Mean:   %12.0f us (%8.3f ms) ± %8.0f us (%6.3f ms)\n", 
               mean_combined_us, mean_combined_us / 1000.0, 
               std_combined_us, std_combined_us / 1000.0);
        printf("  Min:    %12lld us (%8.3f ms)\n", min_combined, min_combined / 1000.0);
        printf("  Max:    %12lld us (%8.3f ms)\n", max_combined, max_combined / 1000.0);
        printf("\nPhase 5 Only (Kernel Launch):\n");
        printf("  Mean:   %12.0f us (%8.3f ms)\n", mean_phase5_us, mean_phase5_us / 1000.0);
        printf("\nPhase 6 Only (Core Computation):\n");
        printf("  Mean:   %12.0f us (%8.3f ms)\n", mean_phase6_us, mean_phase6_us / 1000.0);
        printf("\n");
        
        // Store average values for summary (use combined Phase 5+6 for main comparison)
        phase5_us = static_cast<long long>(mean_phase5_us);
        phase6_us = static_cast<long long>(mean_phase6_us);
        
        // Sync output buffer from device (final sync after all iterations)
        auto buffer_sync_start = std::chrono::high_resolution_clock::now();
        outC_bohdl.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        printf("Output buffer synced successfully\n");
        phase7_us = logElapsedTime(buffer_sync_start, "phase 7: Buffer sync");


        // ========================================================================
        // PHASE 7: OUTPUT PROCESSING AND FILE WRITING
        // ========================================================================
        // Debug output and file writing with timing measurement
        auto t_phase8 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 8: Output Processing and File Writing ===\n");

        // Debug output
    // DEBUG: enable if output buffer inspection is required
    // printOutputBuffer(outC_bomapped, ALIGNED_MATC_BYTES);
        
        // Write output file with timing

        if (writeOutputToFile(outC_bomapped, outC_bohdl, "/sd_card/output_files/output_c.txt") != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    phase8_us = logElapsedTime(t_phase8, "PHASE 8 TOTAL: Output processing + file writing");
        
       
        // ========================================================================
        // TIMING SUMMARY AND PERFORMANCE ANALYSIS
        // ========================================================================
        // Print comprehensive timing summary for performance analysis
        // This helps identify bottlenecks and optimize the application
        printf("\n=== OVERALL TIMING SUMMARY in Configuration: GEMM_SIZE_A=%d, GEMM_SIZE_AB=%d, GEMM_SIZE_B=%d, DIM_A=%d, DIM_AB=%d, DIM_B=%d ===\n", GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B, DIM_A, DIM_AB, DIM_B);
        printDurationUsMs(phase1_us, "Phase 1: Device + XCLBIN initialization");
        printDurationUsMs(phase2_us, "Phase 2: Buffer creation + mapping");
        printDurationUsMs(phase3_us, "Phase 3: Data transfer + sync to device");
        printDurationUsMs(phase4_us, "Phase 4: Kernel + Graph creation");
        printDurationUsMs(phase5_us, "Phase 5: Kernel launch (AVERAGE)");
        printDurationUsMs(phase6_us, "PHASE 6 TOTAL-Compute: Graph run + DMA wait (AVERAGE) ***");
        printDurationUsMs(phase7_us, "PHASE 7: Output sync ***");
        printDurationUsMs(phase8_us, "PHASE 8: Output processing + file writing");
        printDurationUsMs(phase5_us + phase6_us, "PHASES 5+6 COMBINED (AVERAGE): Kernel launch + Graph run + DMA wait *** [COMPARABLE TO PYTORCH]");
        printDurationUsMs(phase3_us + phase5_us + phase6_us + phase7_us, "PHASES 3,5,6,7 TOTAL: Data transfer + Kernel launch +Graph run + DMA wait + Output sync ***");


     long long total_program_us = getElapsedMicroseconds(start_time);
     printDurationUsMs(total_program_us, "TOTAL PROGRAM EXECUTION TIME");

        // ========================================================================
        // ML BENCHMARK COMPARISON - REMOVED TO SAVE MEMORY
        // ========================================================================
        // PyTorch benchmark and matrix input files removed to reduce memory usage
        // This saves ~20 MB of SD card space and avoids additional RAM usage

        printf("\nProgram completed successfully!\n");

    } catch (const std::exception& e) {
        // ========================================================================
        // ERROR HANDLING AND CLEANUP
        // ========================================================================
        // Handle any exceptions that occur during execution
        // This ensures proper error reporting and cleanup
        printf("ERROR: Program execution failed: %s\n", e.what());
        return EXIT_FAILURE;
    }

    // ========================================================================
    // SUCCESSFUL COMPLETION
    // ========================================================================
    // Return success status to indicate successful program completion
    return EXIT_SUCCESS;
}
