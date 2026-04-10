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

What gets streamed to the AIE graph (inp_A path):
- The host does NOT stream directly to the AIE. It loads data into DDR; the DMA kernel
  (dma_hls) then reads DDR and streams to the graph.
- Matrix A: Host loads matrix_A_input.txt (raw row-major, GEMM_SIZE_A x GEMM_SIZE_AB)
  into the A buffer (inA_bomapped / inA_bohdl). After sync to device, the DMA kernel's
  inp_A() reads this buffer and produces 8 streams (strmOut_to_A0..A7) in cascade order
  (same as a0_casc0..a0_casc7.txt). The platform connects these to the AIE graph's
  DataInA0_CASC0..DataInA0_CASC7. So the graph receives the A data from inp_A, not from
  the host directly.
- Matrix B: Host loads b_golden.txt into the B buffer; the DMA kernel's inp_B() streams
  it to 16 B ports (DataInB0_CASC0..DataInB1_CASC7).
- Matrix C: The graph writes to 2 C streams; the DMA kernel's out_C() collects them
  into the C buffer; the host syncs and writes c.txt.

Key Features:
- Configurable matrix dimensions via config.json
- Multiple data type support (int16, int32)
- Memory alignment optimization for DMA efficiency
- Phase timing for bring-up and profiling
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
#include <string>        // String class and functions
#include <chrono>        // High-resolution timing
#include <algorithm>     // STL algorithms
#include <iomanip>       // Stream manipulators for formatting
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
 * 12. Measures and reports phase timing
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
    // DDR-only mode: All matrices loaded directly into DDR buffers
    printf("\n=== DDR-ONLY MODE ===\n");
    printf("Matrices will be loaded directly into DDR, bypassing PS RAM\n");

    // ============================================================================
    // MAIN EXECUTION PHASES WITH TIMING MEASUREMENTS
    // ============================================================================
    // Main execution uses phased timing (device init, buffers, data, kernel/graph, compute, sync, output).
    
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
        // Load matrix data directly into DDR buffers (DDR-only mode).
        // For inp_A to operate correctly: A buffer is filled with raw Matrix A from
        // matrix_A_input.txt (row-major). The DMA kernel's inp_A() will read this buffer
        // and produce 8 streams to the AIE graph (DataInA0_CASC0..7), matching a0_casc*.txt order.
        auto t_phase3 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 3: Data Transfer to Device ===\n");
        printf("Loading matrices directly into DDR buffers...\n");
        
        // A buffer length in 128-bit words: (GEMM_SIZE_A * GEMM_SIZE_AB) / WRD_LN; each word is ap_int<128>, WRD_LN elements per word.
        size_t raw_mata_words = (GEMM_SIZE_A * GEMM_SIZE_AB + WRD_LN - 1) / WRD_LN;
        
        if (loadMatrixDataDirectToDDR(inA_bomapped, inB_bomapped, outC_bomapped,
                                     inA_bohdl, inB_bohdl,
                                     raw_mata_words, EXACT_MATB_SZ, EXACT_MATC_SZ) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        phase3_us = logElapsedTime(t_phase3, "PHASE 3 TOTAL: host data load + sync to device (detail above)");

        // Optional: printInputBuffers(inA_bomapped, inB_bomapped, ALIGNED_MATA_BYTES, ALIGNED_MATB_BYTES);

        // ========================================================================
        // PHASE 4: KERNEL AND GRAPH CREATION
        // ========================================================================
        // Create kernel and graph objects for execution
        // This phase instantiates the DMA kernel and AI Engine graph
        auto t_phase4 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 4: Kernel and Graph Creation ===\n");
        // Graph handle for this scope (name "g" must match libadf)
        xrt::graph gemm_aie_gr(device, xclbin_uuid, "g");
        printf("Graph object already created and ready\n");
        xrt::kernel dma_hls_khdl;
        if (createKernel(device, xclbin_uuid, dma_hls_khdl) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }

        phase4_us = logElapsedTime(t_phase4, "PHASE 4 TOTAL: Kernel + Graph creation");

        // ========================================================================
        // PHASE 5 & 6: KERNEL LAUNCH + CORE COMPUTATION
        // ========================================================================
        printf("\n=== PHASE 5 & 6: Kernel Launch + Core Computation ===\n");

        // Phase 5: Kernel launch — DMA reads DDR and streams to AIE.
        auto t_phase5 = std::chrono::high_resolution_clock::now();
        xrt::run run_hdl;
        if (launchKernel(dma_hls_khdl, inA_bohdl, inB_bohdl, outC_bohdl, run_hdl) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        phase5_us = getElapsedMicroseconds(t_phase5);

        // Phase 6: Graph run + DMA wait
        auto t_phase6 = std::chrono::high_resolution_clock::now();
        if (executeComputation(gemm_aie_gr, run_hdl, outC_bomapped, outC_bohdl) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }

        phase6_us = getElapsedMicroseconds(t_phase6);
        gemm_aie_gr.wait();
        auto buffer_sync_start = std::chrono::high_resolution_clock::now();
        outC_bohdl.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        phase7_us = logElapsedTime(buffer_sync_start, "Output buffer sync (from device)");

        printf("Ending graph execution to clean up device state...\n");
        gemm_aie_gr.end();
        printf("Graph execution ended successfully\n");

        long long combined_us = phase5_us + phase6_us;
        printf("\n=== TIMING (Phase 5 + Phase 6, single run) ===\n");
        printf("Matrix size: %d x %d x %d (A x AB x B)\n", GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B);
        printf("Phase 5 (kernel launch):      %8lld us (%8.3f ms)\n", phase5_us, phase5_us / 1000.0);
        printf("Phase 6 (graph + DMA wait):   %8lld us (%8.3f ms)\n", phase6_us, phase6_us / 1000.0);
        printf("Combined (5+6):               %8lld us (%8.3f ms)\n", combined_us, combined_us / 1000.0);

        // ========================================================================
        // PHASE 8: OUTPUT PROCESSING AND FILE WRITING
        // ========================================================================
        auto t_phase8 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 8: Output Processing and File Writing ===\n");

        // writeOutputToFile logs if the C buffer is all zeros. Optional: printOutputBuffer(outC_bomapped, ALIGNED_MATC_BYTES);

        // Write c.txt next to cwd (e.g. /path/to/app/output_files/c.txt when run from /path/to/app).
        // Avoid /sd_card/... unless that mount exists; compare_outputs.py / golden use matrix_C_golden.txt.
        if (writeOutputToFile(outC_bomapped, outC_bohdl, "./output_files/c.txt") != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }

        phase8_us = logElapsedTime(t_phase8, "PHASE 8 TOTAL: Output processing + file writing");

        // ========================================================================
        // TIMING SUMMARY AND PERFORMANCE ANALYSIS
        // ========================================================================
        printf("\n=== OVERALL TIMING SUMMARY ===\n");
        printf("Configuration: GEMM %d x %d x %d | DIM_A=%d DIM_AB=%d DIM_B=%d | DATA_TYPE=%d (bits per scalar) | SIMPLE_OUT_C=%d\n",
               GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B, DIM_A, DIM_AB, DIM_B, DATA_TYPE, SIMPLE_OUT_C);
        printDurationUsMs(phase1_us, "Phase 1: Device + XCLBIN initialization");
        printDurationUsMs(phase2_us, "Phase 2: Buffer creation + mapping");
        printDurationUsMs(phase3_us, "Phase 3: host load + sync (sub-steps printed during Phase 3)");
        printDurationUsMs(phase4_us, "Phase 4: Kernel + Graph creation");
        printDurationUsMs(phase5_us, "Phase 5: Kernel launch");
        printDurationUsMs(phase6_us, "Phase 6: Graph run + DMA wait");
        printDurationUsMs(phase7_us, "Phase 7: Output sync");
        printDurationUsMs(phase8_us, "Phase 8: Output processing + file writing");
        printDurationUsMs(phase5_us + phase6_us, "Phases 5+6 combined: Kernel launch + graph run + DMA wait");
        printDurationUsMs(phase3_us + phase5_us + phase6_us + phase7_us, "Phases 3+5+6+7: Data transfer + kernel + graph + output sync");

        long long total_program_us = getElapsedMicroseconds(start_time);
        printDurationUsMs(total_program_us, "TOTAL PROGRAM EXECUTION TIME");

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
