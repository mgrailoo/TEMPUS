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
    #define GRAPH_ITER_CNT ((GEMM_SIZE * GEMM_SIZE) / (DIM_A * DIM_B) / SPLIT)  // Finite execution mode
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
    printConfigurationInfo();        // Display matrix dimensions, data types, etc.
    printValidTileDimensions();      // Show valid tile dimension combinations
    verifyBufferConfiguration();     // Validate memory buffer configuration

    // ========================================================================
    // PHASE 0.3: HOST MEMORY ALLOCATION WITH ADDRESS ALIGNMENT
    // ========================================================================
    // Allocate host memory using aligned_allocator for optimal DMA performance
    // This ensures memory addresses are aligned to 4096-byte boundaries
    // which enables efficient DMA transfers and maximum AI Engine performance
    std::vector<ap_int<128>, aligned_allocator<ap_int<128>>> host_mem_A;  // Matrix A host memory
    std::vector<ap_int<128>, aligned_allocator<ap_int<128>>> host_mem_B;  // Matrix B host memory
    std::vector<ap_int<128>, aligned_allocator<ap_int<128>>> host_mem_C;  // Matrix C host memory

    // Allocate and initialize host memory with proper alignment
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

    // Calculate actual data sizes for verification and debugging
    size_t actual_mata_bytes = EXACT_MATA_SZ * sizeof(ap_int<128>);  // Matrix A size in bytes
    size_t actual_matb_bytes = EXACT_MATB_SZ * sizeof(ap_int<128>);  // Matrix B size in bytes
    size_t actual_matc_bytes = EXACT_MATC_SZ * sizeof(ap_int<128>);  // Matrix C size in bytes

    // ============================================================================
    // MAIN EXECUTION PHASES WITH TIMING MEASUREMENTS
    // ============================================================================
    // The main execution is divided into 7 distinct phases, each with specific
    // timing measurements to enable performance analysis and optimization.
    
    try {
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
        printElapsedTime(t_phase1, "PHASE 1 TOTAL: Device + XCLBIN initialization");

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
        printElapsedTime(t_phase2, "PHASE 2 TOTAL: Buffer creation + mapping");

        // ========================================================================
        // PHASE 3: DATA TRANSFER TO DEVICE
        // ========================================================================
        // Transfer matrix data from host memory to device buffers
        // This includes copying data and synchronizing to ensure completion
        auto t_phase3 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 3: Data Transfer to Device ===\n");
        
        if (transferDataToDevice(host_mem_A, host_mem_B, inA_bomapped, inB_bomapped, outC_bomapped, inA_bohdl, inB_bohdl) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        printElapsedTime(t_phase3, "PHASE 3 TOTAL: Data transfer + sync to device");

        // Debug: Print input buffers after transfer for verification
        printInputBuffers(inA_bomapped, inB_bomapped, ALIGNED_MATA_BYTES, ALIGNED_MATB_BYTES);

        // ========================================================================
        // PHASE 4: KERNEL AND GRAPH CREATION
        // ========================================================================
        // Create kernel and graph objects for execution
        // This phase instantiates the DMA kernel and AI Engine graph
        auto t_phase4 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 4: Kernel and Graph Creation ===\n");
        
        xrt::kernel dma_hls_khdl;                              // DMA kernel handle
        xrt::graph gemm_aie_gr(device, xclbin_uuid, "g");      // AI Engine graph handle
        
        if (createKernelAndGraph(device, xclbin_uuid, dma_hls_khdl, gemm_aie_gr) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        printElapsedTime(t_phase4, "PHASE 4 TOTAL: Kernel + Graph creation");

        // ========================================================================
        // PHASE 5: KERNEL LAUNCH
        // ========================================================================
        // Launch the DMA kernel to begin data movement and computation
        // This phase starts the kernel execution but doesn't wait for completion
        auto t_phase5 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 5: Kernel Launch ===\n");
        
        xrt::run dma_hls_rhdl;                                 // Kernel run handle
        
        if (launchKernel(dma_hls_khdl, inA_bohdl, inB_bohdl, outC_bohdl, dma_hls_rhdl) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        printElapsedTime(t_phase5, "PHASE 5 TOTAL: Kernel launch");

        // ========================================================================
        // PHASE 6: CORE COMPUTATION (Graph run + DMA wait + Output sync)
        // ========================================================================
        // Execute the AI Engine graph, wait for DMA operations, and synchronize output
        // This phase runs the actual GEMM computation and handles all data movement
        // This is the most important runtime measurement for performance evaluation
        auto t_core_computation = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 6: Core Computation (Graph run + DMA wait + Output sync) ===\n");
        
        // Core computation (Graph run + DMA wait + Output sync)
        if (executeComputation(gemm_aie_gr, dma_hls_rhdl, outC_bomapped, outC_bohdl) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        // Print the combined core computation time (most important metric)
        printElapsedTime(t_core_computation, "*** CORE COMPUTATION TOTAL: Graph run + DMA wait + Output sync ***");

        // ========================================================================
        // PHASE 7: OUTPUT PROCESSING AND FILE WRITING
        // ========================================================================
        // Debug output and file writing with timing measurement
        auto t_phase7 = std::chrono::high_resolution_clock::now();
        printf("\n=== PHASE 7: Output Processing and File Writing ===\n");
        
        // Debug output
        printOutputBuffer(outC_bomapped, ALIGNED_MATC_BYTES);
        
        // Write output file with timing
        auto t_file_write = std::chrono::high_resolution_clock::now();
        if (writeOutputToFile(outC_bomapped, outC_bohdl, host_mem_C) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        printElapsedTime(t_file_write, "File writing");
        printElapsedTime(t_phase7, "PHASE 7 TOTAL: Output processing + file writing");
        
       
        // ========================================================================
        // TIMING SUMMARY AND PERFORMANCE ANALYSIS
        // ========================================================================
        // Print comprehensive timing summary for performance analysis
        // This helps identify bottlenecks and optimize the application
        printf("\n=== OVERALL TIMING SUMMARY ===\n");
        printElapsedTime(t_phase1, "Phase 1: Device + XCLBIN initialization");
        printElapsedTime(t_phase2, "Phase 2: Buffer creation + mapping");
        printElapsedTime(t_phase3, "Phase 3: Data transfer + sync to device");
        printElapsedTime(t_phase4, "Phase 4: Kernel + Graph creation");
        printElapsedTime(t_phase5, "Phase 5: Kernel launch");
        printElapsedTime(t_core_computation, "*** CORE COMPUTATION: Graph run + DMA wait + Output sync ***");
        printElapsedTime(start_time, "TOTAL PROGRAM EXECUTION TIME");

        // ========================================================================
        // ML BENCHMARK COMPARISON (CONFIGURABLE)
        // ========================================================================
        // Run PyTorch and NumPy benchmarks for performance comparison
        // This provides a CPU baseline for AI Engine performance evaluation
        // Controlled by ENABLE_ML_BENCHMARKS parameter in config.json
        
        #if ENABLE_ML_BENCHMARKS == 1
        printf("\n=== ML BENCHMARK COMPARISON (ENABLED) ===\n");
        printf("AI Engine Target: %s\n", TARGET);
        
        // Check if ML packages are already installed (pre-installed in PetaLinux image)
        int numpy_check = system("python3 -c 'import numpy' 2>/dev/null");
        int pytorch_check = system("python3 -c 'import torch' 2>/dev/null");
        
        if (numpy_check == 0 && pytorch_check == 0) {
            printf("✓ ML packages pre-installed in PetaLinux image - ready to use!\n");
        } else {
            printf("ML packages not found - installing ML environment...\n");
            printf("Note: For better performance, consider pre-installing ML packages in PetaLinux image\n");
            int install_result = system("./setup_ml_environment.sh");
            if (install_result == 0) {
                printf("✓ ML environment setup completed successfully!\n");
            } else {
                printf("⚠ ML environment setup failed, but continuing...\n");
            }
        }
        #else
        // Interactive prompt for ML benchmarks when not enabled in config
        printf("\n=== ML BENCHMARK COMPARISON ===\n");
        printf("ML benchmarks are disabled in config.json (ENABLE_ML_BENCHMARKS=0)\n");
        printf("Would you like to run PyTorch and NumPy benchmarks? (y/N): ");

        char response;
        std::cin >> response;

        if (response != 'y' && response != 'Y') {
            printf("Skipping ML benchmarks as requested.\n");
        } else {
            printf("Installing ML environment...\n");
            int install_result = system("./setup_ml_environment.sh");
            if (install_result == 0) {
                printf("✓ ML environment setup completed successfully!\n");
            } else {
                printf("⚠ ML environment setup failed, but continuing...\n");
            }
            printf("\n=== ML BENCHMARK COMPARISON (ENABLED) ===\n");
            printf("AI Engine Target: %s\n", TARGET);
        }
        #endif
        
        // Only run ML benchmarks if enabled or user chose to run them
        #if ENABLE_ML_BENCHMARKS == 1
        bool run_ml_benchmarks = true;
        #else
        bool run_ml_benchmarks = (response == 'y' || response == 'Y');
        #endif
        
        if (run_ml_benchmarks) {
            // Declare variables for ML benchmarks
            std::string pytorch_script = "pytorch_benchmark.py";
            std::string numpy_script = "numpy_benchmark.py";
            std::string matrix_a_file = "matrix_A_input.txt";
            std::string matrix_b_file = "matrix_B_input.txt";
            std::string output_cpu = "pytorch_result_cpu.txt";
            std::string output_numpy = "numpy_result_cpu.txt";
            std::string dtype_str = (DATA_TYPE == 16) ? "int16" : (DATA_TYPE == 32) ? "int32" : "float32";
            
            // Check if input files exist
            std::ifstream file_a(matrix_a_file);
            std::ifstream file_b(matrix_b_file);
            if (file_a.good() && file_b.good()) {
            
            // Check if Python packages are available
            printf("Checking Python ML stack availability...\n");
            int numpy_check = system("python3 -c 'import numpy' 2>/dev/null");
            int pytorch_check = system("python3 -c 'import torch' 2>/dev/null");
            
            if (numpy_check != 0 || pytorch_check != 0) {
                printf("⚠ Python ML packages not found on the board.\n");
                printf("Please install them manually by running: ./setup_ml_environment.sh\n");
                printf("Then re-run the application to enable benchmark comparisons.\n");
                printf("Skipping benchmark comparisons for this run.\n");
                return EXIT_SUCCESS;
            } else {
                printf("✓ Python ML stack is available.\n");
            }
            
            // Run NumPy benchmark (always available)
            printf("Running NumPy CPU benchmark...\n");
            std::string numpy_cmd = "python3 " + numpy_script + " " + matrix_a_file + " " + matrix_b_file + 
                                    " " + output_numpy + " --size " + std::to_string(GEMM_SIZE) + 
                                    " --dtype " + dtype_str + " --iterations 10 --target " + std::string(TARGET);
            
            int numpy_result = system(numpy_cmd.c_str());
            if (numpy_result == 0) {
                printf("NumPy CPU benchmark completed successfully\n");
            } else {
                printf("NumPy CPU benchmark failed (exit code: %d)\n", numpy_result);
            }
            
            // Check if PyTorch is available (reuse previous check)
            printf("Checking PyTorch availability...\n");

            if (pytorch_check == 0) {
                printf("PyTorch is available. Running PyTorch benchmark...\n");
                printf("Running PyTorch CPU benchmark...\n");

                // Run CPU benchmark
                std::string cpu_cmd = "python3 " + pytorch_script + " " + matrix_a_file + " " + matrix_b_file +
                                      " " + output_cpu + " --size " + std::to_string(GEMM_SIZE) +
                                      " --dtype " + dtype_str + " --device cpu --iterations 10 --target " + std::string(TARGET);

                int cpu_result = system(cpu_cmd.c_str());
                if (cpu_result == 0) {
                    printf("PyTorch CPU benchmark completed successfully\n");
                } else {
                    printf("PyTorch CPU benchmark failed (exit code: %d)\n", cpu_result);
                }
            } else {
                printf("PyTorch not available. Skipping PyTorch benchmark.\n");
                printf("To install manually, run: ./setup_ml_environment.sh\n");
                printf("Then re-run the program to enable PyTorch benchmarking.\n");
            }
            } else {
                printf("Input matrix files not found. Skipping benchmark comparisons.\n");
                printf("Expected files:\n");
                printf("  - %s\n", matrix_a_file.c_str());
                printf("  - %s\n", matrix_b_file.c_str());
            }
        }

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
