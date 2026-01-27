/*
Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
SPDX-License-Identifier: MIT

AI Engine GEMM Graph Implementation
==================================

This file defines the AI Engine graph for General Matrix Multiply (GEMM) operations
using Xilinx DSP Library's matrix_mult_graph template. The graph is designed for
Versal ACAP platforms with configurable data types and matrix dimensions.

Key Features:
- Configurable data types: int16, int32, float
- Scalable matrix dimensions via config.json
- Streaming interface with DMA kernels
- Cascade and split processing for large matrices
- Runtime ratio optimization for performance

Architecture:
- Matrix A: Input via CASC_LN PLIO streams (broadcast to all splits)
- Matrix B: Input via SPLIT×CASC_LN PLIO streams (one per split)
- Matrix C: Output via SPLIT PLIO streams (one per split)

Template Parameters:
- data_t: Data type (int16/int32/float)
- DIM: Tile dimension for AI Engine processing
- GEMM_SIZE: Total matrix size
- CASC_LN: Number of cascade levels
- SPLIT: Number of parallel processing splits
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
// DSPLIB matrix_mult_graph supports: int16, int32, float
#if DATA_TYPE == 16  // int16
    typedef int16 data_t;                          // AI Engine int16 data type
    typedef int16_t int16;                         // Standard int16_t for compatibility
#elif DATA_TYPE == 32  // int32
    typedef int32 data_t;                          // AI Engine int32 data type
    typedef int32_t int32;                         // Standard int32_t for compatibility
#elif DATA_TYPE == 33  // float
    typedef float data_t;                          // AI Engine float data type
    typedef float float_t;                         // Standard float for compatibility
#else
    #error "Unsupported DATA_TYPE. Use 16 (int16), 32 (int32), or 33 (float)"
#endif

// ============================================================================
// Matrix Layout and Window Size Definitions
// ============================================================================
// Matrix layout constants for DSPLIB template
#define ROW_MAJOR 0                                // Row-major matrix layout
#define COL_MAJOR 1                                // Column-major matrix layout

// Window sizes for AI Engine data movement
// Each window contains DIM×GEMM_SIZE×N_SAMPLES elements
#define WINDOW_SIZE_A (DIM * GEMM_SIZE * N_SAMPLES)  // Matrix A window size
#define WINDOW_SIZE_B (DIM * GEMM_SIZE * N_SAMPLES)  // Matrix B window size
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
 * - Matrix A: Broadcast to all splits via CASC_LN input streams
 * - Matrix B: Distributed across splits via SPLIT×CASC_LN input streams  
 * - Matrix C: Output from each split via SPLIT output streams
 * 
 * Data Flow:
 * 1. Matrix A data flows through CASC_LN PLIO streams (broadcast pattern)
 * 2. Matrix B data flows through SPLIT×CASC_LN PLIO streams (split pattern)
 * 3. Each split processes its portion using CASC_LN cascade levels
 * 4. Results are collected via SPLIT output PLIO streams
 */
class GeMM: public adf::graph
{
   public:
      // ========================================================================
      // PLIO Interface Declarations
      // ========================================================================
      input_plio matA_inp[CASC_LN];                    // Matrix A input streams (broadcast)
      input_plio matB_inp[(SPLIT * CASC_LN)];         // Matrix B input streams (split)
      output_plio matC_out[SPLIT];                     // Matrix C output streams (split)
      
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
         // Create SPLIT instances of matrix multiplication kernels
         // Each kernel processes GEMM_SIZE×GEMM_SIZE matrices with DIM×DIM tiles
         // Template parameters: <data_t, data_t, DIM, GEMM_SIZE, DIM, 0, 0,
         //                      ROW_MAJOR, ROW_MAJOR, ROW_MAJOR, 0, 0, 0,
         //                      WINDOW_SIZE_A, WINDOW_SIZE_B, CASC_LN>
         xf::dsp::aie::blas::matrix_mult::matrix_mult_graph<
            data_t, data_t, DIM, GEMM_SIZE, DIM, 0, 0,
            ROW_MAJOR, ROW_MAJOR, ROW_MAJOR, 0, 0, 0,
            WINDOW_SIZE_A, WINDOW_SIZE_B, CASC_LN
         > mmult[SPLIT];
                              
         // ====================================================================
         // Matrix A PLIO Interface Creation (Broadcast Pattern)
         // ====================================================================
         // Matrix A is broadcast to all splits via CASC_LN input streams
         // Naming convention: "DataInA0_CASC{j}" where j = 0 to CASC_LN-1
         for(int j = 0; j < CASC_LN; ++j) {
            std::string matA_plioOut_str = "DataInA" + std::to_string(0) + "_CASC" + std::to_string(j);
            const char *matA_plioOut = matA_plioOut_str.c_str();
            
            // Create 128-bit PLIO interface for streaming data from DMA
            matA_inp[j] = input_plio::create(matA_plioOut, plio_128_bits);
         }
         
         // ====================================================================
         // Matrix B and C PLIO Interface Creation (Split Pattern)
         // ====================================================================
         for(int i = 0; i < SPLIT; ++i) {
            // Get kernel references for this split
            adf::kernel *mmult_kernels = mmult[i].getKernels();
            
            // Matrix B PLIO interfaces: one per cascade level per split
            // Naming convention: "DataInB{i}_CASC{j}" where i = split, j = cascade
            for(int j = 0; j < CASC_LN; ++j) {
               std::string matB_plioOut_str = "DataInB" + std::to_string(i) + "_CASC" + std::to_string(j);
               const char *matB_plioOut = matB_plioOut_str.c_str();
               
               // Create 128-bit PLIO interface for streaming data from DMA
               matB_inp[(i * CASC_LN) + j] = input_plio::create(matB_plioOut, plio_128_bits);
            } 
            
            // Matrix C PLIO interface: one per split
            // Naming convention: "DataOutC{i}" where i = split index
            std::string matC_plioOut_str = "DataOutC" + std::to_string(i);
            const char *matC_plioOut = matC_plioOut_str.c_str();
            
            // Create 128-bit PLIO interface for streaming data to DMA
            matC_out[i] = output_plio::create(matC_plioOut, plio_128_bits);
            
            // ================================================================
            // Graph Connections and Runtime Configuration
            // ================================================================
            // Connect PLIO streams to kernel ports and configure runtime ratios
            for(int k = 0; k < CASC_LN; ++k) {
               // Set runtime ratio for performance optimization
               // 0.9 ratio allows for 10% margin in timing closure
               adf::runtime<ratio>(mmult_kernels[k]) = 1.0;
               
               // Connect Matrix A input (broadcast to all splits)
               adf::connect<>(matA_inp[k].out[0], mmult[i].inA[k]);
               
               // Connect Matrix B input (split-specific)
               adf::connect<>(matB_inp[(i * CASC_LN) + k].out[0], mmult[i].inB[k]);
            }
            
            // Connect Matrix C output (split-specific)
            adf::connect<>(mmult[i].out[0], matC_out[i].in[0]);
         }
         
         // Optional: AI Engine tile placement constraint (commented out)
         // Uncomment to specify exact tile placement for timing optimization
         // location<graph>(*this) = area_group({{aie_tile, 0, 0, SPLIT, CASC_LN}});
         // Place GEMM splits in a compact 2x16 block for best timing
         //location<graph>(*this) = area_group({{aie_tile, 0, 0, 2, 16}}); if SPLIT is 4, you need 2x16 blocks
      }
};

#endif //ifndef _GRAPH_H_