/*
Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
SPDX-License-Identifier: MIT

AI Engine GEMM Graph Implementation
==================================

This file defines the AI Engine graph for General Matrix Multiply (GEMM) operations
using Xilinx DSP Library's matrix_mult_graph template. The graph is designed for
Versal ACAP platforms with configurable data types and matrix dimensions.

Key Features:
- Configurable data types: int16, int32
- Scalable matrix dimensions via config.json
- Streaming interface with DMA kernels
- Cascade and split processing for large matrices
- Runtime ratio optimization for performance

Architecture:
- Matrix A: Input via CASC_LN_AB PLIO streams (broadcast to all splits)
- Matrix B: Input via SPLIT_B×CASC_LN_AB PLIO streams (one per split)
- Matrix C: Output via SPLIT_B PLIO streams (one per split)

Template Parameters:
- data_t: Data type (int16/int32)
- DIM_A: Tile dimension for A dimension (rows of A, rows of C)
- DIM_AB: Tile dimension for AB dimension (cols of A, rows of B)
- DIM_B: Tile dimension for B dimension (cols of B, cols of C)
- CASC_LN_AB: Number of cascade levels for AB dimension
- SPLIT_B: Number of parallel processing splits for B dimension
*/

#ifndef _GRAPH_H_
#define _GRAPH_H_

// ============================================================================
// Include Headers
// ============================================================================
#include <adf.h>                                    // AI Engine Development Framework
#include "../design_configs/gemm_config.h"         // Generated configuration constants
#include "matrix_mult_graph.hpp"                   // Xilinx DSP Library matrix multiplication
#include <aie_api/aie.hpp>                        // AI Engine API for data types
#include <string>                                  // String operations for PLIO naming
#include <cstdint>                                 // Standard integer types

using namespace adf;

// ============================================================================
// Data Type Configuration
// ============================================================================
// Dynamic data type definitions based on config.json DATA_TYPE setting
// DSPLIB matrix_mult_graph: int16 / int32 only on this platform
#if DATA_TYPE == 16  // int16
    typedef int16 data_t;                          // AI Engine int16 data type
    typedef int16_t int16;                         // Standard int16_t for compatibility
#elif DATA_TYPE == 32  // int32
    typedef int32 data_t;                          // AI Engine int32 data type
    typedef int32_t int32;                         // Standard int32_t for compatibility
#else
    #error "Unsupported DATA_TYPE. Use 16 (int16) or 32 (int32)"
#endif

// ============================================================================
// Matrix Layout and Window Size Definitions
// ============================================================================
// Matrix layout constants for DSPLIB template
#define ROW_MAJOR 0                                // Row-major matrix layout
#define COL_MAJOR 1                                // Column-major matrix layout

// Window sizes for AI Engine data movement
// Each window contains DIM_A/DIM_B×GEMM_SIZE_AB×N_SAMPLES elements (using GEMM_SIZE_AB for inner dimension)
#define WINDOW_SIZE_A (DIM_A * GEMM_SIZE_AB * N_SAMPLES)  // Matrix A window size (DIM_A × GEMM_SIZE_AB)
#define WINDOW_SIZE_B (DIM_B * GEMM_SIZE_AB * N_SAMPLES)  // Matrix B window size (DIM_B × GEMM_SIZE_AB)
// ============================================================================
// GEMM Graph Class Definition
// ============================================================================
/**
 * @class GeMM
 * @brief AI Engine graph for General Matrix Multiply operations
 * 
 * This class implements a scalable GEMM graph using Xilinx DSP Library's
 * matrix_mult_graph template. The graph supports configurable data types
 * and matrix dimensions through the configuration system.
 * 
 * Architecture Overview:
 * - Matrix A: Broadcast to all splits via CASC_LN_AB input streams
 * - Matrix B: Distributed across splits via SPLIT_B×CASC_LN_AB input streams  
 * - Matrix C: Output from each split via SPLIT_B output streams
 * 
 * Data Flow:
 * 1. Matrix A data flows through CASC_LN_AB PLIO streams (broadcast pattern)
 * 2. Matrix B data flows through SPLIT_B×CASC_LN_AB PLIO streams (split pattern)
 * 3. Each split processes its portion using CASC_LN_AB cascade levels
 * 4. Results are collected via SPLIT_B output PLIO streams
 */
class GeMM: public adf::graph
{
   public:
      // ========================================================================
      // PLIO Interface Declarations
      // ========================================================================
      input_plio matA_inp[CASC_LN_AB];                    // Matrix A input streams (broadcast)
      input_plio matB_inp[(SPLIT_B * CASC_LN_AB)];         // Matrix B input streams (split)
      output_plio matC_out[SPLIT_B];                     // Matrix C output streams (split)
      
      // ========================================================================
      // Constructor: Graph Initialization and Connection
      // ========================================================================
      /**
       * @brief Constructor initializes the GEMM graph with PLIO connections
       * 
       * This constructor:
       * 1. Instantiates SPLIT matrix multiplication kernels with cascade processing
       * 2. Creates PLIO interfaces for data streaming from/to DMA kernels
       * 3. Establishes connections between PLIO streams and kernel ports
       * 4. Configures runtime ratios for performance optimization
       */
      GeMM() {
         // ====================================================================
         // Matrix Multiplication Kernel Instantiation
         // ====================================================================
         // Create SPLIT_B instances of matrix multiplication kernels
         // Each kernel processes matrices with DIM_A×GEMM_SIZE_AB×DIM_B dimensions
         // Note: GEMM_SIZE_AB is the full AB dimension size - the template internally
         //       divides it by CASC_LN_AB to handle cascade processing
         // Template parameters: <data_t, data_t, DIM_A, GEMM_SIZE_AB, DIM_B, 0, 0,
         //                      ROW_MAJOR, ROW_MAJOR, ROW_MAJOR, 0, 0, 0,
         //                      WINDOW_SIZE_A, WINDOW_SIZE_B, CASC_LN_AB>
         // GRAPH_ITER_CNT (Makefile) must satisfy:
         //   (GEMM_SIZE_A * (GEMM_SIZE_B / SPLIT_B)) / (DIM_A * DIM_B)
         // so each c{i}.txt fills GEMM_SIZE_A rows × (GEMM_SIZE_B/SPLIT_B) elements.
         xf::dsp::aie::blas::matrix_mult::matrix_mult_graph<
            data_t, data_t, DIM_A, GEMM_SIZE_AB, DIM_B, 0, 0,
            ROW_MAJOR, ROW_MAJOR, ROW_MAJOR, 0, 0, 0,
            WINDOW_SIZE_A, WINDOW_SIZE_B, CASC_LN_AB
         > mmult[SPLIT_B];

         // ====================================================================
         // Matrix A PLIO Interface Creation (Broadcast Pattern)
         // ====================================================================
         // Matrix A is broadcast to all splits via CASC_LN_AB input streams
         // Naming convention: "DataInA0_CASC{j}" where j = 0 to CASC_LN_AB-1
         for(int j = 0; j < CASC_LN_AB; ++j) {
            std::string matA_plioOut_str = "DataInA" + std::to_string(0) + "_CASC" + std::to_string(j);
            const char *matA_plioOut = matA_plioOut_str.c_str();
            
            // Create 128-bit PLIO interface for streaming data from DMA
            #ifdef __AIESIM__
               // For AIE standalone simulation: connect to input files
               std::string matA_Out_file_str = "a" + std::to_string(0) + "_casc" + std::to_string(j) + ".txt";
               const char *matA_Out_file = matA_Out_file_str.c_str();
               matA_inp[j] = input_plio::create(matA_plioOut, plio_128_bits, matA_Out_file);
            #else
               // For full system (hw_emu/hw): connect to DMA kernel
               matA_inp[j] = input_plio::create(matA_plioOut, plio_128_bits);
            #endif
         }
         
         // ====================================================================
         // Matrix B and C PLIO Interface Creation (Split Pattern)
         // ====================================================================
         for(int i = 0; i < SPLIT_B; ++i) {
            // Get kernel references for this split
            adf::kernel *mmult_kernels = mmult[i].getKernels();
            
            // Matrix B PLIO interfaces: one per cascade level per split
            // Naming convention: "DataInB{i}_CASC{j}" where i = split, j = cascade
            for(int j = 0; j < CASC_LN_AB; ++j) {
               std::string matB_plioOut_str = "DataInB" + std::to_string(i) + "_CASC" + std::to_string(j);
               const char *matB_plioOut = matB_plioOut_str.c_str();
               
               // Create 128-bit PLIO interface for streaming data from DMA
               #ifdef __AIESIM__
                  // For AIE standalone simulation: connect to input files
                  std::string matB_Out_file_str = "b" + std::to_string(i) + "_casc" + std::to_string(j) + ".txt";
                  const char *matB_Out_file = matB_Out_file_str.c_str();
                  matB_inp[(i * CASC_LN_AB) + j] = input_plio::create(matB_plioOut, plio_128_bits, matB_Out_file);
               #else
                  // For full system (hw_emu/hw): connect to DMA kernel
                  matB_inp[(i * CASC_LN_AB) + j] = input_plio::create(matB_plioOut, plio_128_bits);
               #endif
            } 
            
            // Matrix C PLIO interface: one per split
            // Naming convention: "DataOutC{i}" where i = split index
            std::string matC_plioOut_str = "DataOutC" + std::to_string(i);
            const char *matC_plioOut = matC_plioOut_str.c_str();
            
            // Create 128-bit PLIO interface for streaming data to DMA
            #ifdef __AIESIM__
               // For AIE standalone simulation: connect to output files
               std::string matC_Out_file_str = "data/c" + std::to_string(i) + ".txt";
               const char *matC_Out_file = matC_Out_file_str.c_str();
               matC_out[i] = output_plio::create(matC_plioOut, plio_128_bits, matC_Out_file);
            #else
               // For full system (hw_emu/hw): connect to DMA kernel
               matC_out[i] = output_plio::create(matC_plioOut, plio_128_bits);
            #endif
            
            // ================================================================
            // Graph Connections and Runtime Configuration
            // ================================================================
            // Connect PLIO streams to kernel ports and configure runtime ratios
            for(int k = 0; k < CASC_LN_AB; ++k) {
               // Runtime ratio from gemm_config.h (config.json AIE_RUNTIME_RATIO).
               // Higher → more AIE time share, often shorter Phase 6; if deadlock or errors, lower toward 0.75.
               adf::runtime<ratio>(mmult_kernels[k]) = AIE_RUNTIME_RATIO;

               // Connect Matrix A input (broadcast to all splits)
               adf::connect<>(matA_inp[k].out[0], mmult[i].inA[k]);
               
               // Connect Matrix B input (split-specific)
               adf::connect<>(matB_inp[(i * CASC_LN_AB) + k].out[0], mmult[i].inB[k]);
            }
            
            // Connect Matrix C output (split-specific)
            adf::connect<>(mmult[i].out[0], matC_out[i].in[0]);
         }
         
         // Optional: AI Engine tile placement constraint (commented out)
         // Uncomment to specify exact tile placement for timing optimization
         // location<graph>(*this) = area_group({{aie_tile, 0, 0, SPLIT_B, CASC_LN_AB}});
         // Place GEMM splits in a compact 2x16 block for best timing
         //location<graph>(*this) = area_group({{aie_tile, 0, 0, 2, 16}}); if SPLIT_B is 4, you need 2x16 blocks
      }
};

#endif //ifndef _GRAPH_H_