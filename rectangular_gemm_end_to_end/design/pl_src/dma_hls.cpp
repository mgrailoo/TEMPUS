/*
Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
SPDX-License-Identifier: MIT

AI Engine GEMM DMA Kernel Implementation
=======================================

This file implements the high-performance DMA kernel for the AI Engine GEMM
application on Versal ACAP platforms. The kernel handles high-speed data
movement between host memory and AI Engine processing units via streaming
interfaces.

Architecture Overview:
- DMA Kernel: Manages data transfer between host and AI Engine
- Matrix A: Broadcast to all AI Engine splits via 8 PLIO streams
- Matrix B: Distributed across splits via 16 PLIO streams (2 splits × 8 cascade levels)
- Matrix C: Collected from AI Engine via 2 PLIO streams (1 per split)

Key Features:
- Deadlock-free pipeline design with non-rewinding loops
- Burst-optimized memory access for maximum throughput
- Configurable burst lengths and FIFO depths based on matrix size
- Memory interface mapping to NoC DDR4 interfaces (S00_AXI, S01_AXI, S02_AXI)
- Sequential data distribution to eliminate stream order warnings
- Pair-wise output processing for optimal II=1 performance

Memory Interface Mapping:
- gmem0 -> S00_AXI (C1_DDR_LOW0/C1_DDR_LOW1) - NoC DDR4 interface for Matrix A
- gmem1 -> S01_AXI (C2_DDR_LOW0/C2_DDR_LOW1) - NoC DDR4 interface for Matrix B
- gmem2 -> S02_AXI (C3_DDR_LOW0/C3_DDR_LOW1) - NoC DDR4 interface for Matrix C

Performance Optimizations:
- HLS pipeline with II=1 for maximum throughput
- Burst transfers up to 32 words per transaction
- Outstanding requests for parallel memory access
- Memory dependence optimization for better synthesis
- Unroll factor of 4 for loop optimization

Usage:
This kernel is automatically instantiated by the host application and
communicates with the AI Engine graph via PLIO streaming interfaces.
*/

// ============================================================================
// Include Headers
// ============================================================================
#include "dma_hls.h"                           // Kernel function declarations
#include "../design_configs/gemm_config.h"    // Centralized configuration constants
#include <ap_int.h>                            // Arbitrary precision integer types
#include <hls_stream.h>                        // HLS streaming interfaces
#include <ap_axi_sdata.h>                      // AXI-Stream data types


// ============================================================================
// STREAM CONFIGURATION CONSTANTS
// ============================================================================
// Number of PLIO streams for each matrix type are defined in gemm_config.h:
//   NUM_A_FILES = 8  (Matrix A: 8 streams for broadcast to all splits)
//   NUM_B_FILES = 16 (Matrix B: 16 streams = SPLIT_B × CASC_LN_AB = 2 × 8)
//   NUM_C_FILES = 2  (Matrix C: 2 streams = SPLIT_B, one per split)
// These are already defined in gemm_config.h - do NOT redefine here

// ============================================================================
// MATRIX SIZE CALCULATIONS
// ============================================================================
// Input Matrix A format (raw, stored in matA buffer):
//   - Simple row-major matrix: GEMM_SIZE_A rows × GEMM_SIZE_AB columns
//   - Packed in ap_int<128> words: (GEMM_SIZE_A * GEMM_SIZE_AB) / WRD_LN words
//   - No pre-processing required - DMA kernel handles all cascade transformation
//
// Output Matrix A format (after DMA transformation):
//   - Transformed to cascade format matching plioGen.py write_matrix_A_cascade()
//   - Streamed to NUM_A_FILES (= CASC_LN_AB) PLIO streams
//   - Each cascade gets its column range with proper block/tile/sub-tile ordering
//   - Broadcasting applied per block (BROADCAST_COUNT_A times)
//
// Matrix B and C sizes are defined in gemm_config.h:
//   EXACT_MATB_SZ = BASE_MATB_SZ * NUM_B_FILES  (from gemm_config.h)
//   EXACT_MATC_SZ = BASE_MATC_SZ * NUM_C_FILES  (from gemm_config.h)
// Do NOT redefine them here - use the values from gemm_config.h

// ============================================================================
// SUB-TILE PARAMETERS
// ============================================================================
// Sub-tile dimensions control the granularity of element processing within tiles:
//   - SUB_TILE_A: Sub-tile dimension for A dimension (rows of Matrix A)
//   - SUB_TILE_AB: Sub-tile dimension for AB dimension (columns of A / rows of B)
//   - SUB_TILE_B: Sub-tile dimension for B dimension (columns of Matrix B/C)
//   - Data-type dependent values (from AI Engine-ML matrix_mult instruction set):
//     * int16: SUB_TILE_A=4, SUB_TILE_AB=4, SUB_TILE_B=4 (4×4×4 sub-tiles)
//     * int32: SUB_TILE_A=4, SUB_TILE_AB=4, SUB_TILE_B=2 (4×4×2 sub-tiles)
//     * float: SUB_TILE_A=4, SUB_TILE_AB=4, SUB_TILE_B=2 (4×4×2 sub-tiles)
//   - SUB_TILE_B is smaller for int32/float because WRD_LN=4 (vs WRD_LN=8 for int16)
//   - Used in inp_A_producer for element-level processing within tiles
//   - Note: SUB_TILE_M, SUB_TILE_K, SUB_TILE_N are deprecated (use SUB_TILE_A/AB/B)
//
// ============================================================================
// DDR-ONLY MODE
// ============================================================================
// This DMA kernel always operates in DDR-only mode:
//   - Matrix A: Read directly from DDR (gmem0) without PS RAM
//   - Matrix B: Read directly from DDR (gmem1) without PS RAM
//   - Matrix C: Write directly to DDR (gmem2) without PS RAM
//   - USE_DDR_ONLY_MODE in config.json controls host application behavior only
//   - If USE_DDR_ONLY_MODE=1: Forces DDR-only mode (bypasses PS RAM)
//   - If USE_DDR_ONLY_MODE=0: Auto-detects based on memory size (>6GB)
//   - Useful for testing DDR mode with small matrices

// ============================================================================
// DMA AND FIFO PARAMETERS CONFIGURATION
// ============================================================================
// Configure burst lengths and FIFO depths based on matrix size
// These parameters are optimized for different GEMM sizes to balance
// performance, resource usage, and timing closure

#define BURST_A 32        // Matrix A burst length (words per transaction)
#define BURST_B 32        // Matrix B burst length (words per transaction)
#define BURST_C 32        // Matrix C burst length (words per transaction)
#define FIFO_A_DEPTH 16   // Matrix A FIFO depth (streaming buffer size)
#define FIFO_B_DEPTH 16   // Matrix B FIFO depth (streaming buffer size)
#define FIFO_C_DEPTH 16   // Matrix C FIFO depth (streaming buffer size)

// ============================================================================
// OUTSTANDING REQUEST CONFIGURATION
// ============================================================================
// Configure outstanding requests for DMA - optimized for lower resource pressure
// These parameters control how many memory transactions can be in-flight simultaneously
#ifndef OUTSTANDING_A 
#define OUTSTANDING_A 32     // Matrix A outstanding read requests
#endif
#ifndef OUTSTANDING_B
#define OUTSTANDING_B 32     // Matrix B outstanding read requests
#endif
#ifndef OUTSTANDING_C
#define OUTSTANDING_C 32     // Matrix C outstanding write requests
#endif



// ============================================================================
// MATRIX A PRODUCER FUNCTION - CASCADE TRANSFORMATION (DATAFLOW)
// ============================================================================
/**
 * @brief Producer: Reads DDR (128-bit words) and emits same element sequence as Python, as 128-bit stream words
 * 
 * DDR: matA[] is row-major for the WHOLE matrix; each 128-bit word = WRD_LN elements (8 for int16).
 *   linear_idx = row*GEMM_SIZE_AB + col  ->  word_idx = linear_idx/WRD_LN,  pos = linear_idx%WRD_LN.
 * Python: builds cascade element-by-element (no 8-by-8); then chunks into lines of WRD_LN for the file.
 * We: traverse in the SAME order (blocks, tiles row-major, sub-tiles row-major, elements row-major),
 *     read 128-bit words from DDR and extract the correct element per (row,col), append to logical
 *     stream, and pack every WRD_LN elements into one 128-bit word for block_stream. So our stream
 *     matches a0_casc<j>.txt line-by-line (same element order, chunked into words of WRD_LN).
 * 
 * Sub-tile: we read one 128-bit word per sub-tile row (one matrix row) and take 4 elements
 * (SUB_TILE_AB=4) at positions src_elem_start..src_elem_start+3; next sub-tile row = next 128-bit word.
 * 
 * @param matA Raw Matrix A in DDR (128-bit words, row-major whole matrix, WRD_LN elements/word)
 * @param block_stream Output: 128-bit words, each WRD_LN elements in order (matches Python file lines)
 * @param block_size_stream Output: number of words for this block
 * @param casc_idx Cascade index (0-7)
 * @param block_idx Block index (0 to SPLIT_A-1)
 */
void inp_A_producer(
   ap_int<128>* matA,
   hls::stream<ap_int<128>> &block_stream,
   hls::stream<int> &block_size_stream,
   int casc_idx,
   int block_idx
) {
    // --- Line-by-line: how streams are generated ---
    // 1) HLS: allow parallel/out-of-order reads from matA
    #pragma HLS DEPENDENCE variable=matA inter false
    #pragma HLS DEPENDENCE variable=matA intra false

    // 2) Block/cascade bounds: this call produces ONE block for ONE cascade.
    const int cols_per_casc = GEMM_SIZE_AB / CASC_LN_AB;
    const int rows_per_block = GEMM_SIZE_A / SPLIT_A;
    const int dim_a_eff = (DIM_A < rows_per_block) ? DIM_A : rows_per_block;
    const int dim_ab_eff = (DIM_AB < cols_per_casc) ? DIM_AB : cols_per_casc;
    const int tiles_per_row = rows_per_block / dim_a_eff;
    const int tiles_per_col = cols_per_casc / dim_ab_eff;
    const int sub_tiles_per_row = dim_a_eff / SUB_TILE_A;
    const int sub_tiles_per_col = dim_ab_eff / SUB_TILE_AB;
    const int casc_start_col = casc_idx * cols_per_casc;
    const int casc_end_col = casc_start_col + cols_per_casc;
    const int block_start_row = block_idx * rows_per_block;
    const int block_end_row = block_start_row + rows_per_block;
    // 3) word_buffer: accumulate WRD_LN elements (8 for int16), then pack into one 128-bit word and stream.
    ap_int<128> word_buffer[WRD_LN];
    #pragma HLS ARRAY_PARTITION variable=word_buffer complete
    
    int element_counter = 0;  // Logical stream position: we emit elements in same order as Python, then pack every WRD_LN into one 128-bit word
    int words_produced = 0;
    
    // Traversal order (single source of truth, must match Python): block (this call) -> tiles row-major -> sub-tiles row-major -> elements row-major within sub-tile.
    // For each (global_row, global_col) we map to DDR: word_idx = (row*GEMM_SIZE_AB+col)/WRD_LN, pos = (row*GEMM_SIZE_AB+col)%WRD_LN; then pack every WRD_LN elements into one output word.
    collect_and_stream: for (int tile_row = 0; tile_row < tiles_per_row; tile_row++) {
        #pragma HLS LOOP_FLATTEN off
        for (int tile_col = 0; tile_col < tiles_per_col; tile_col++) {
            #pragma HLS LOOP_FLATTEN off
            const int tile_start_row = block_start_row + tile_row * dim_a_eff;
            const int tile_start_col = casc_start_col + tile_col * dim_ab_eff;
            // Tile-end bounds (must match Python: min(tile_start + dim_eff, block_end) for correct DIM 8)
            const int tile_end_row = (tile_start_row + dim_a_eff <= block_end_row) ? (tile_start_row + dim_a_eff) : block_end_row;
            const int tile_end_col = (tile_start_col + dim_ab_eff <= casc_end_col) ? (tile_start_col + dim_ab_eff) : casc_end_col;
            
            for (int st_row = 0; st_row < sub_tiles_per_row; st_row++) {
                #pragma HLS LOOP_FLATTEN off
                for (int st_col = 0; st_col < sub_tiles_per_col; st_col++) {
                    #pragma HLS LOOP_FLATTEN off
                    const int st_start_row = tile_start_row + st_row * SUB_TILE_A;
                    const int st_start_col = tile_start_col + st_col * SUB_TILE_AB;
                    // Sub-tile end boundaries: clip to TILE boundary (same as plio_utils.write_matrix_A_cascade)
                    const int st_end_row = (st_start_row + SUB_TILE_A <= tile_end_row) ? (st_start_row + SUB_TILE_A) : tile_end_row;
                    const int st_end_col = (st_start_col + SUB_TILE_AB <= tile_end_col) ? (st_start_col + SUB_TILE_AB) : tile_end_col;
                    
                    // Within sub-tile: emit elements in row-major (match Python). DDR words are 128-bit = WRD_LN elements (row-major whole matrix).
                    // One sub-tile row = 4 elements -> 4 of the 8 in one DDR word; next sub-tile row = next DDR word (next matrix row), same col range.
                    // So we read one 128-bit word per sub-tile row and extract 4 elements at src_elem_start..src_elem_start+3.
                    for (int st_r = 0; st_r < SUB_TILE_A; st_r++) {
                        const int global_row = st_start_row + st_r;
                        const int linear_base = global_row * GEMM_SIZE_AB + st_start_col;
                        const int src_word_idx = linear_base / WRD_LN;
                        const int src_elem_start = linear_base % WRD_LN;  // 0..4 (4 elements from this word)
                        if (global_row >= st_end_row) break;
                        ap_int<128> src_word = matA[src_word_idx];  // one 128-bit read per sub-tile row
                        for (int st_c = 0; st_c < SUB_TILE_AB; st_c++) {
                            #pragma HLS PIPELINE II=1
                            const int global_col = st_start_col + st_c;
                            if (global_col >= st_end_col || global_row >= GEMM_SIZE_A || global_col >= GEMM_SIZE_AB) continue;
                            const int pos = src_elem_start + st_c;
                            ap_int<128> element;
                            if (DATA_TYPE == 16) {
                                element = (src_word >> (pos * 16)) & 0xFFFF;
                            } else {
                                element = (src_word >> (pos * 32)) & 0xFFFFFFFF;
                            }
                            const int buffer_pos = element_counter % WRD_LN;
                            if (DATA_TYPE == 16) {
                                word_buffer[buffer_pos] = element << (buffer_pos * 16);
                            } else {
                                word_buffer[buffer_pos] = element << (buffer_pos * 32);
                            }
                            element_counter++;
                            // Every WRD_LN elements: pack into one 128-bit word (same as Python chunking into file lines of WRD_LN elements)
                            if ((element_counter & (WRD_LN - 1)) == 0) {
                                ap_int<128> packed_word = 0;
                                for (int i = 0; i < WRD_LN; i++) {
                                    #pragma HLS UNROLL
                                    packed_word |= word_buffer[i];
                                }
                                block_stream.write(packed_word);
                                words_produced++;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Handle remaining elements in partial word
    const int remaining = element_counter % WRD_LN;
    if (remaining > 0) {
        ap_int<128> packed_word = 0;
        for (int i = 0; i < remaining; i++) {
            #pragma HLS UNROLL
            packed_word |= word_buffer[i];
        }
        block_stream.write(packed_word);
        words_produced++;
    }
    
    // Send block size to consumer
    block_size_stream.write(words_produced);
}

// ============================================================================
// MATRIX A CONSUMER FUNCTION - BROADCAST (DATAFLOW)
// ============================================================================
/**
 * @brief Consumer: Reads block data from stream and broadcasts to output
 * 
 * This function reads block data as it's produced and broadcasts it BROADCAST_COUNT_A times.
 * Runs in parallel with producer using dataflow.
 * 
 * @param block_stream Input stream for block data
 * @param block_size_stream Input stream for block size
 * @param strmOut_to_A0-A7 All output streams (routed based on casc_idx)
 * @param casc_idx Cascade index (for routing and last packet detection)
 * @param block_idx Block index (for last packet detection)
 */
void inp_A_consumer(
   hls::stream<ap_int<128>> &block_stream,
   hls::stream<int> &block_size_stream,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A0,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A1,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A2,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A3,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A4,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A5,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A6,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A7,
   int casc_idx,
   int block_idx
) {
    // Read block size
    int words_per_block = block_size_stream.read();
    
    // Use BRAM buffer for broadcasting (need to re-read data)
    ap_int<128> block_buffer[1024];
    #pragma HLS ARRAY_PARTITION variable=block_buffer cyclic factor=4
    
    // Read block data from stream into buffer
    read_block: for (int w = 0; w < words_per_block; w++) {
        #pragma HLS PIPELINE II=1
        block_buffer[w] = block_stream.read();
    }
    
    // Broadcast block exactly BROADCAST_COUNT_A times (bc = 0..BROADCAST_COUNT_A-1, not +1)
    broadcast_loop: for (int bc = 0; bc < BROADCAST_COUNT_A; bc++) {
        write_block: for (int w = 0; w < words_per_block; w++) {
        #pragma HLS PIPELINE II=1
        
        ap_axiu<128, 0, 0, 0> pkt;
            pkt.data = block_buffer[w];
        pkt.keep = -1;
        pkt.strb = -1;
            pkt.last = ((bc == BROADCAST_COUNT_A - 1) && (w == words_per_block - 1) && 
                       (block_idx == SPLIT_A - 1) && (casc_idx == CASC_LN_AB - 1)) ? 1 : 0;
        
            // Route to appropriate cascade stream. strmOut_to_Aj carries the exact 128-bit word
            // sequence of a0_cascj.txt: line k of a0_casc<j>.txt = k-th word on strmOut_to_Aj.
            switch(casc_idx) {
            case 0: strmOut_to_A0.write(pkt); break;
            case 1: strmOut_to_A1.write(pkt); break;
            case 2: strmOut_to_A2.write(pkt); break;
            case 3: strmOut_to_A3.write(pkt); break;
            case 4: strmOut_to_A4.write(pkt); break;
            case 5: strmOut_to_A5.write(pkt); break;
            case 6: strmOut_to_A6.write(pkt); break;
            case 7: strmOut_to_A7.write(pkt); break;
            }
        }
    }
}

// ============================================================================
// MATRIX A INPUT FUNCTION - CASCADE TRANSFORMATION AND BROADCAST (DATAFLOW)
// ============================================================================
//
// DDR LAYOUT (Matrix A in DDR)
// ----------------------------
// DDR holds A as 128-bit words in ROW-MAJOR order for the WHOLE matrix.
//   - Word index:  matA[word_idx] covers linear indices [word_idx*WRD_LN .. word_idx*WRD_LN+WRD_LN-1].
//   - Matrix (row,col) -> linear_idx = row*GEMM_SIZE_AB + col.
//   - word_idx = linear_idx / WRD_LN,  position_in_word = linear_idx % WRD_LN.
// So we read 128-bit words (8 int16 elements per word); each word = one row, 8 consecutive columns.
//
// PYTHON (plio_utils.write_matrix_A_cascade) — ELEMENT-BY-ELEMENT, NOT 8-BY-8
// ---------------------------------------------------------------------------
// Python does NOT read/write in 128-bit chunks. It iterates (block -> tiles row-major -> sub-tiles
// row-major -> elements within sub-tile row-major) and appends one element at a time. Only when
// writing the file does it chunk the flat element list into lines of WRD_LN (8) elements. So the
// logical stream is: elem0, elem1, ..., elem7 (first word), elem8, ..., elem15 (second word), ...
//
// HLS STREAM BUILDING (must match Python element sequence)
// -------------------------------------------------------
// We must produce the SAME element sequence as Python, then pack every WRD_LN elements into one
// 128-bit output word. Traversal order (single source of truth):
//   - Blocks:     one block per (casc_idx, block_idx) call.
//   - Tiles:      row-major (tile_row, tile_col).
//   - Sub-tiles:  row-major (st_row, st_col).
//   - Elements:   row-major within sub-tile (row then col).
// For each (global_row, global_col) we: (1) compute DDR word index and position in word from
// linear_idx = global_row*GEMM_SIZE_AB + global_col; (2) read that 128-bit word (or reuse when
// same word); (3) append element to stream; (4) every WRD_LN elements pack into one word and
// write to block_stream. So our output words = Python's file lines (same element order, chunked).
//
// EIGHT CASCADES -> a_golden INTERLEAVING
// ---------------------------------------
// Each cascade produces one stream (content = a0_casc<j>.txt line-by-line). strmOut_to_A0..A7
// carry these. a_golden.txt = row-interleave of the 8 files: for each row r, write line r of
// a0_casc0, then line r of a0_casc1, ... line r of a0_casc7 (generate_a_golden). So the 8
// streams we emit are element-interleaved (by row) when compared to a_golden; each stream itself
// has elements in 128-bit words (WRD_LN per word).
//
// DIM 8 (8x8 tiles): Sub-tile end bounds must be clipped to the TILE boundary (tile_end_row,
// tile_end_col), not only to block/cascade end, to match plio_utils.write_matrix_A_cascade exactly.
//
/**
 * @brief Reads raw Matrix A (GEMM_SIZE_A × GEMM_SIZE_AB) and transforms to cascade format
 * 
 * Uses dataflow pattern: producer processes elements and streams them, consumer broadcasts.
 * This enables streaming elements as they're produced rather than buffering everything first.
 * 
 * Key Features:
 * - Dataflow design: producer and consumer run in parallel
 * - Elements streamed as they're produced (true dataflow)
 * - Deadlock-free pipeline design
 * - Matches plioGen.py cascade format exactly (see EQUIVALENCE WITH a_golden.txt above)
 * 
 * @param matA Pointer to raw Matrix A (GEMM_SIZE_A × GEMM_SIZE_AB, row-major, packed)
 * @param strmOut_to_A0-A7 Output streams for each cascade level (0-7); content matches a0_casc0..7.txt
 */
void inp_A(
   ap_int<128>* matA,                                    // Raw Matrix A (GEMM_SIZE_A × GEMM_SIZE_AB, row-major)
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A0,   // Cascade 0 output stream
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A1,   // Cascade 1 output stream
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A2,   // Cascade 2 output stream
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A3,   // Cascade 3 output stream
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A4,   // Cascade 4 output stream
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A5,   // Cascade 5 output stream
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A6,   // Cascade 6 output stream
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A7    // Cascade 7 output stream
) {
    // Memory access optimization
    #pragma HLS DEPENDENCE variable=matA inter false
    #pragma HLS DEPENDENCE variable=matA intra false

    // Bounds checking
    if (matA == nullptr) {
        return;
    }

    // Calculate dimensions for cascade transformation
    const int cols_per_casc = GEMM_SIZE_AB / CASC_LN_AB;
    const int rows_per_block = GEMM_SIZE_A / SPLIT_A;
    const int dim_a_eff = (DIM_A < rows_per_block) ? DIM_A : rows_per_block;
    const int dim_ab_eff = (DIM_AB < cols_per_casc) ? DIM_AB : cols_per_casc;
    
    // Total output size per cascade: BASE_MATA_SZ words
    // Process in cascade order matching plioGen.py write_matrix_A_cascade()
    
    // Process each cascade level (each gets its own stream)
    cascade_loop: for (int casc_idx = 0; casc_idx < CASC_LN_AB; casc_idx++) {
        // Process each block (SPLIT_A blocks per cascade)
        block_loop: for (int block_idx = 0; block_idx < SPLIT_A; block_idx++) {
            // Dataflow streams for producer-consumer pattern
            hls::stream<ap_int<128>> block_stream;
            hls::stream<int> block_size_stream;
            #pragma HLS STREAM variable=block_stream depth=512
            #pragma HLS STREAM variable=block_size_stream depth=2
            
            // DATAFLOW: Producer and consumer run in parallel
            // Producer processes elements and streams them as they're produced
            // Consumer reads from stream and broadcasts
            #pragma HLS DATAFLOW
            
            inp_A_producer(matA, block_stream, block_size_stream, casc_idx, block_idx);
            inp_A_consumer(block_stream, block_size_stream, 
                         strmOut_to_A0, strmOut_to_A1, strmOut_to_A2, strmOut_to_A3,
                         strmOut_to_A4, strmOut_to_A5, strmOut_to_A6, strmOut_to_A7,
                         casc_idx, block_idx);
        }
    }
}

// ============================================================================
// MATRIX B INPUT FUNCTION - DEADLOCK-FREE DISTRIBUTION
// ============================================================================
/**
 * @brief Reads Matrix B data from memory and distributes to 16 PLIO streams
 * 
 * This function implements a deadlock-free pipeline for reading Matrix B data
 * from host memory and distributing it across 16 PLIO streams for split
 * processing. The distribution pattern supports 2 splits × 8 cascade levels,
 * enabling parallel processing across multiple AI Engine tiles.
 * 
 * Key Features:
 * - Non-rewinding pipeline design prevents deadlocks
 * - Sequential data distribution across 16 streams
 * - Fixed 4×4 sub-tile distribution pattern
 * - Burst-optimized memory access with unroll factor of 4
 * - Memory dependence optimization for better synthesis
 * - Bounds checking to prevent segmentation faults
 * 
 * @param matB Pointer to Matrix B data in host memory
 * @param strmOut_to_B0-B15 Output streams for Matrix B data distribution
 * 
 * @note This function runs in a dataflow context and must be deadlock-free
 * @note Memory access is optimized for burst transfers up to 32 words
 * @note Sequential access pattern eliminates stream order warnings
 * @note Distribution pattern: 16 streams = 2 splits × 8 cascade levels
 */
void inp_B(
   ap_int<128>* matB,                                    // Matrix B memory pointer
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B0,   // Output stream 0
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B1,   // Output stream 1
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B2,   // Output stream 2
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B3,   // Output stream 3
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B4,   // Output stream 4
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B5,   // Output stream 5
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B6,   // Output stream 6
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B7,   // Output stream 7
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B8,   // Output stream 8
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B9,   // Output stream 9
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B10,  // Output stream 10
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B11,  // Output stream 11
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B12,  // Output stream 12
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B13,  // Output stream 13
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B14,  // Output stream 14
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B15   // Output stream 15
) {
    // Memory access optimization
    #pragma HLS DEPENDENCE variable=matB inter false
    #pragma HLS DEPENDENCE variable=matB intra false

    // Bounds checking to prevent segmentation faults
    if (matB == nullptr) {
        return; // Early exit if null pointer
    }

    // DEADLOCK-FREE: Non-rewinding pipeline with burst optimization
    read_B_sequential: for (int i = 0; i < EXACT_MATB_SZ; i++) {
        #pragma HLS PIPELINE II=1
        #pragma HLS LOOP_TRIPCOUNT min=EXACT_MATB_SZ max=EXACT_MATB_SZ
        #pragma HLS UNROLL factor=4
        
        // Safe memory access with bounds checking
        ap_int<128> data = matB[i];
        ap_axiu<128, 0, 0, 0> pkt;
        pkt.data = data;
        pkt.keep = -1;
        pkt.strb = -1;
        pkt.last = (i == EXACT_MATB_SZ - 1) ? 1 : 0;
        
        // SEQUENTIAL ACCESS: Eliminates stream order warnings
        // Fixed 4×4 sub-tile distribution across 16 streams
        int stream_idx = i % NUM_B_FILES;
        switch(stream_idx) {
            case 0:  strmOut_to_B0.write(pkt); break;
            case 1:  strmOut_to_B1.write(pkt); break;
            case 2:  strmOut_to_B2.write(pkt); break;
            case 3:  strmOut_to_B3.write(pkt); break;
            case 4:  strmOut_to_B4.write(pkt); break;
            case 5:  strmOut_to_B5.write(pkt); break;
            case 6:  strmOut_to_B6.write(pkt); break;
            case 7:  strmOut_to_B7.write(pkt); break;
            case 8:  strmOut_to_B8.write(pkt); break;
            case 9:  strmOut_to_B9.write(pkt); break;
            case 10: strmOut_to_B10.write(pkt); break;
            case 11: strmOut_to_B11.write(pkt); break;
            case 12: strmOut_to_B12.write(pkt); break;
            case 13: strmOut_to_B13.write(pkt); break;
            case 14: strmOut_to_B14.write(pkt); break;
            case 15: strmOut_to_B15.write(pkt); break;
        }
    }
}

// ============================================================================
// MATRIX C OUTPUT FUNCTION - REVERSE TILING AND RECONSTRUCTION
// ============================================================================
/**
 * @brief Collects Matrix C data from 2 PLIO streams, de-tiles, and writes raw row-major format
 *
 * REFERENCE: Stream format is identical to design/aie_src/aiesim_data/gemm_*_ioFiles/
 *   c0_golden.txt and c1_golden.txt (same word/line order as generated by plioGen.py).
 * The Python script run_c_fake_test.py (and plio_utils.generate_c_fake_from_split_files)
 * merges c0_golden.txt + c1_golden.txt into a file identical to matrix_C_golden.txt.
 * out_C must produce the same result: matC written here, after the host syncs and writes
 * via write_c_txt_from_bo(), must yield c.txt identical to matrix_C_golden.txt
 * (GEMM_SIZE_A rows × GEMM_SIZE_B columns, WRD_LN elements per word, row-major).
 *
 * This function implements the reverse transformation of generate_split_output_files:
 * - Reads tiled data from c0 and c1 streams (in tiled/permuted format)
 * - De-tiles the data to reconstruct raw row-major blocks
 * - Interleaves columns from c0 and c1 to reconstruct full matrix
 * - Writes in raw row-major format matching matrix_C_golden.txt
 * 
 * Sub-Tile Parameters:
 * - SUB_TILE_B: Sub-tile dimension for B dimension (columns of Matrix B/C)
 *   * Data-type dependent: SUB_TILE_B=4 for int16, SUB_TILE_B=2 for int32/float
 *   * int32 has WRD_LN=4 (4 elements/word) vs int16 WRD_LN=8 (8 elements/word)
 * - Note: out_C processes at tile level (DIM_A × DIM_B), not sub-tile level
 * - Sub-tiles are handled internally by AI Engine graph, not by DMA kernel
 * - SUB_TILE_A and SUB_TILE_AB are not used in out_C (only in inp_A)
 * 
 * DDR-Only Mode:
 * - This DMA kernel always operates in DDR-only mode
 * - Matrix C is written directly to DDR memory (gmem2) without PS RAM involvement
 * - USE_DDR_ONLY_MODE in config.json controls host application behavior, not DMA kernel
 * 
 * Data Ordering (reverse of generate_split_output_files):
 * - Files: split by SPLIT_B (columns) → c0 has cols 0 to cols_per_file-1, c1 has cols_per_file to end
 * - Blocks within files: split by SPLIT_A (rows) → split_a blocks per file
 * - Tiles within blocks: DIM_A × DIM_B, written in column-major order
 * - Elements within tiles: row-major order (sub-tiles in row-major, elements in row-major)
 * 
 * Reverse transformation:
 * 1. Read tiled data from streams (column-major tiles within blocks)
 * 2. De-tile: reconstruct row-major blocks from tiles (reverse column-major tile order)
 * 3. Interleave columns: combine c0 and c1 columns to form full matrix rows
 * 4. Write in raw row-major format: GEMM_SIZE_A rows × GEMM_SIZE_B columns
 * 
 * @param strmInp_from_C0 Input stream 0 from AI Engine (split 0, columns 0 to cols_per_file-1)
 * @param strmInp_from_C1 Input stream 1 from AI Engine (split 1, columns cols_per_file to end)
 * @param matC Pointer to Matrix C data in host memory (raw row-major format)
 * 
 * @note This function runs in a dataflow context and must be deadlock-free
 * @note Output format matches matrix_C_golden.txt (raw row-major, packed in ap_int<128>)
 *
 * CONTRACT WITH HOST (write_c_txt_from_bo in gemm_utils.cpp):
 * - matC layout: row-major, word index = row * words_per_row_in_matrix + col_word.
 * - Row order: block_row 0..SPLIT_A-1; within each row: c0 words then c1 words.
 * - Word layout: element j in word = bits [ (j+1)*16-1 : j*16 ] (int16); host reads same.
 * - Host must sync BO from device (XCL_BO_SYNC_BO_FROM_DEVICE) before reading matC for c.txt.
 */
#if SIMPLE_OUT_C
 void out_C(
    hls::stream<ap_axiu<128, 0, 0, 0>> &strmInp_from_C0,  // Input stream 0 (split 0)
    hls::stream<ap_axiu<128, 0, 0, 0>> &strmInp_from_C1,  // Input stream 1 (split 1)
    ap_int<128>* matC                                     // Matrix C memory pointer
 ) {
     // Memory access optimization
     #pragma HLS DEPENDENCE variable=matC inter false
     #pragma HLS DEPENDENCE variable=matC intra false
 
     // Bounds checking to prevent segmentation faults
     if (matC == nullptr) {
         return; // Early exit if null pointer
     }
 
     // DEADLOCK-FREE: Pair-wise writes to achieve II=1 without per-iteration branching
     #define MATC_PAIRS (EXACT_MATC_SZ / NUM_C_FILES)
     write_C_pairs: for (int p = 0; p < MATC_PAIRS; ++p) {
         #pragma HLS PIPELINE II=1
         #pragma HLS LOOP_TRIPCOUNT min=MATC_PAIRS max=MATC_PAIRS
         ap_axiu<128, 0, 0, 0> pkt0 = strmInp_from_C0.read();
         ap_axiu<128, 0, 0, 0> pkt1 = strmInp_from_C1.read();
         int baseIndex = (p << 1);
         matC[baseIndex]     = pkt0.data;
         matC[baseIndex + 1] = pkt1.data;
     }
     // Handle odd tail element if present
     if ((EXACT_MATC_SZ & 1) != 0) {
         ap_axiu<128, 0, 0, 0> pkt_tail = strmInp_from_C0.read();
         matC[EXACT_MATC_SZ - 1] = pkt_tail.data;
     }
 }
#else
 void out_C(
     hls::stream<ap_axiu<128, 0, 0, 0>> &strmInp_from_C0,  // Input stream 0 (split 0)
     hls::stream<ap_axiu<128, 0, 0, 0>> &strmInp_from_C1,  // Input stream 1 (split 1)
     ap_int<128>* matC                                     // Matrix C memory pointer (raw row-major)
 ) {
     #pragma HLS DEPENDENCE variable=matC inter false
     #pragma HLS DEPENDENCE variable=matC intra false

     if (matC == nullptr) return;

     // CRITICAL: cols_per_file can be < WRD_LN (e.g. 4 cols, WRD_LN=8). Use ceiling division
     // so words_per_row_in_file >= 1 and words_per_block is correct; otherwise both become 0 and matC stays zero.
     const int cols_per_file = GEMM_SIZE_B / SPLIT_B;
     const int rows_per_block = GEMM_SIZE_A / SPLIT_A;
     const int tiles_per_block_row = rows_per_block / DIM_A;
     const int tiles_per_block_col = cols_per_file / DIM_B;
     const int words_per_tile = (DIM_A * DIM_B) / WRD_LN;
     const int words_per_row_in_file = (cols_per_file + WRD_LN - 1) / WRD_LN;  // was cols_per_file/WRD_LN -> 0 when cols_per_file=4
     const int words_per_row_in_matrix = GEMM_SIZE_B / WRD_LN;
     const int words_per_block = rows_per_block * words_per_row_in_file;

     ap_int<128> block_buffer_c0[512];
     ap_int<128> block_buffer_c1[512];
     #pragma HLS ARRAY_PARTITION variable=block_buffer_c0 cyclic factor=8
     #pragma HLS ARRAY_PARTITION variable=block_buffer_c1 cyclic factor=8
     #pragma HLS DEPENDENCE variable=block_buffer_c0 inter false
     #pragma HLS DEPENDENCE variable=block_buffer_c1 inter false

     for (int block_row = 0; block_row < SPLIT_A; block_row++) {
         #pragma HLS LOOP_TRIPCOUNT min=SPLIT_A max=SPLIT_A
         const int block_start_row = block_row * rows_per_block;

         for (int i = 0; i < words_per_block; i++) {
             #pragma HLS PIPELINE II=1
             #pragma HLS LOOP_TRIPCOUNT min=8 max=512
             block_buffer_c0[i] = 0;
             block_buffer_c1[i] = 0;
         }

         const int sub_tiles_per_row = DIM_A / SUB_TILE_A;
         const int sub_tiles_per_col = DIM_B / SUB_TILE_B;

         // De-tile c0: ALWAYS process tiles column-major within each block,
         // matching c0_golden (and your expected order):
         // (0,0), (1,0), ..., (tiles_per_block_row-1,0),
         // then (0,1), (1,1), ..., etc.
         for (int tile_col = 0; tile_col < tiles_per_block_col; tile_col++) {
             #pragma HLS LOOP_FLATTEN off
             #pragma HLS LOOP_TRIPCOUNT min=1 max=8
             for (int tile_row = 0; tile_row < tiles_per_block_row; tile_row++) {
                 #pragma HLS LOOP_FLATTEN off
                 #pragma HLS LOOP_TRIPCOUNT min=1 max=8
                 const int tile_start_row_in_block = tile_row * DIM_A;
                 const int tile_start_col = tile_col * DIM_B;

                 ap_int<128> tile_words[512];
                 #pragma HLS ARRAY_PARTITION variable=tile_words cyclic factor=4

                 for (int w = 0; w < words_per_tile; w++) {
                     #pragma HLS PIPELINE II=1
                     #pragma HLS LOOP_TRIPCOUNT min=8 max=512
                     ap_axiu<128, 0, 0, 0> pkt = strmInp_from_C0.read();
                     tile_words[w] = pkt.data;
                 }

                 // When DIM_B < WRD_LN (e.g. 4, WRD_LN=8), DIM_B/WRD_LN=0 so we never write to block buffer. Use ceiling.
                 const int words_per_row_in_tile = (DIM_B + WRD_LN - 1) / WRD_LN;
                 ap_int<16> tile_elements[DIM_A * DIM_B];
                 #pragma HLS ARRAY_PARTITION variable=tile_elements cyclic factor=4

                 // Stream order MUST match plioGen generate_split_output_files: sub-tiles row-major, elements within sub-tile row-major, chunked into words of WRD_LN.
                 int element_idx = 0;
                 for (int sub_row = 0; sub_row < sub_tiles_per_row; sub_row++) {
                     for (int sub_col = 0; sub_col < sub_tiles_per_col; sub_col++) {
                         for (int st_row = 0; st_row < SUB_TILE_A; st_row++) {
                             for (int st_col = 0; st_col < SUB_TILE_B; st_col++) {
                                 const int row_in_tile = sub_row * SUB_TILE_A + st_row;
                                 const int col_in_tile = sub_col * SUB_TILE_B + st_col;
                                 const int word_idx = element_idx / WRD_LN;
                                 const int elem_in_word = element_idx % WRD_LN;
                                 ap_int<128> word = tile_words[word_idx];
                                 ap_int<16> element = (DATA_TYPE == 16) ?
                                     (word.range((elem_in_word + 1) * 16 - 1, elem_in_word * 16)) :
                                     (word.range((elem_in_word + 1) * 32 - 1, elem_in_word * 32));
                                 tile_elements[row_in_tile * DIM_B + col_in_tile] = element;
                                 element_idx++;
                             }
                         }
                     }
                 }

                 for (int row_in_tile = 0; row_in_tile < DIM_A; row_in_tile++) {
                     #pragma HLS LOOP_FLATTEN off
                     const int global_row_in_block = tile_start_row_in_block + row_in_tile;
                     for (int word_in_row = 0; word_in_row < words_per_row_in_tile; word_in_row++) {
                         #pragma HLS PIPELINE II=1
                         ap_int<128> buffer_word = 0;
                         const int word_start_col = word_in_row * WRD_LN;
                         for (int elem_in_word = 0; elem_in_word < WRD_LN; elem_in_word++) {
                             #pragma HLS UNROLL
                             const int col_in_tile = word_start_col + elem_in_word;
                             if (col_in_tile < DIM_B) {
                                 ap_int<16> element = tile_elements[row_in_tile * DIM_B + col_in_tile];
                                 if (DATA_TYPE == 16)
                                     buffer_word.range((elem_in_word + 1) * 16 - 1, elem_in_word * 16) = element;
                                 else
                                     buffer_word.range((elem_in_word + 1) * 32 - 1, elem_in_word * 32) = element;
                             }
                         }
                         const int global_col_in_file = tile_start_col + word_start_col;
                         const int word_col_in_file = global_col_in_file / WRD_LN;
                         const int buffer_word_idx = global_row_in_block * words_per_row_in_file + word_col_in_file;
                         const int elem_offset = global_col_in_file % WRD_LN;
                         if (elem_offset != 0) {
                             // Merge into existing word when multiple tiles share same word (e.g. DIM_B=4, WRD_LN=8, tile 1 at col 4)
                             ap_int<128> existing = block_buffer_c0[buffer_word_idx];
                             if (DATA_TYPE == 16) {
                                 for (int i = 0; i < DIM_B; i++)
                                     existing.range((elem_offset + i + 1) * 16 - 1, (elem_offset + i) * 16) = buffer_word.range((i + 1) * 16 - 1, i * 16);
                             } else {
                                 for (int i = 0; i < DIM_B; i++)
                                     existing.range((elem_offset + i + 1) * 32 - 1, (elem_offset + i) * 32) = buffer_word.range((i + 1) * 32 - 1, i * 32);
                             }
                             block_buffer_c0[buffer_word_idx] = existing;
                         } else {
                             block_buffer_c0[buffer_word_idx] = buffer_word;
                         }
                     }
                 }
             }
         }

         // De-tile c1: same tile order as c0 (ALWAYS column-major between tiles)
         for (int tile_col = 0; tile_col < tiles_per_block_col; tile_col++) {
             #pragma HLS LOOP_FLATTEN off
             for (int tile_row = 0; tile_row < tiles_per_block_row; tile_row++) {
                 #pragma HLS LOOP_FLATTEN off
                 const int tile_start_row_in_block = tile_row * DIM_A;
                 const int tile_start_col = tile_col * DIM_B;

                 ap_int<128> tile_words[512];
                 #pragma HLS ARRAY_PARTITION variable=tile_words cyclic factor=4
                 for (int w = 0; w < words_per_tile; w++) {
                     #pragma HLS PIPELINE II=1
                     ap_axiu<128, 0, 0, 0> pkt = strmInp_from_C1.read();
                     tile_words[w] = pkt.data;
                 }

                 const int words_per_row_in_tile_c1 = (DIM_B + WRD_LN - 1) / WRD_LN;
                 ap_int<16> tile_elements_c1[DIM_A * DIM_B];
                 #pragma HLS ARRAY_PARTITION variable=tile_elements_c1 cyclic factor=4

                 // Same as c0: sub-tiles row-major, elements row-major (match plioGen)
                 int element_idx_c1 = 0;
                 for (int sub_row = 0; sub_row < sub_tiles_per_row; sub_row++) {
                     for (int sub_col = 0; sub_col < sub_tiles_per_col; sub_col++) {
                         for (int st_row = 0; st_row < SUB_TILE_A; st_row++) {
                             for (int st_col = 0; st_col < SUB_TILE_B; st_col++) {
                                 const int row_in_tile = sub_row * SUB_TILE_A + st_row;
                                 const int col_in_tile = sub_col * SUB_TILE_B + st_col;
                                 const int word_idx = element_idx_c1 / WRD_LN;
                                 const int elem_in_word = element_idx_c1 % WRD_LN;
                                 ap_int<128> word = tile_words[word_idx];
                                 ap_int<16> element = (DATA_TYPE == 16) ?
                                     (word.range((elem_in_word + 1) * 16 - 1, elem_in_word * 16)) :
                                     (word.range((elem_in_word + 1) * 32 - 1, elem_in_word * 32));
                                 tile_elements_c1[row_in_tile * DIM_B + col_in_tile] = element;
                                 element_idx_c1++;
                             }
                         }
                     }
                 }

                 for (int row_in_tile = 0; row_in_tile < DIM_A; row_in_tile++) {
                     #pragma HLS LOOP_FLATTEN off
                     const int global_row_in_block = tile_start_row_in_block + row_in_tile;
                     for (int word_in_row = 0; word_in_row < words_per_row_in_tile_c1; word_in_row++) {
                         #pragma HLS PIPELINE II=1
                         ap_int<128> buffer_word = 0;
                         const int word_start_col = word_in_row * WRD_LN;
                         for (int elem_in_word = 0; elem_in_word < WRD_LN; elem_in_word++) {
                             #pragma HLS UNROLL
                             const int col_in_tile = word_start_col + elem_in_word;
                             if (col_in_tile < DIM_B) {
                                 ap_int<16> element = tile_elements_c1[row_in_tile * DIM_B + col_in_tile];
                                 if (DATA_TYPE == 16)
                                     buffer_word.range((elem_in_word + 1) * 16 - 1, elem_in_word * 16) = element;
                                 else
                                     buffer_word.range((elem_in_word + 1) * 32 - 1, elem_in_word * 32) = element;
                             }
                         }
                         const int global_col_in_file = tile_start_col + word_start_col;
                         const int word_col_in_file = global_col_in_file / WRD_LN;
                         const int buffer_word_idx = global_row_in_block * words_per_row_in_file + word_col_in_file;
                         const int elem_offset_c1 = global_col_in_file % WRD_LN;
                         if (elem_offset_c1 != 0) {
                             ap_int<128> existing = block_buffer_c1[buffer_word_idx];
                             if (DATA_TYPE == 16) {
                                 for (int i = 0; i < DIM_B; i++)
                                     existing.range((elem_offset_c1 + i + 1) * 16 - 1, (elem_offset_c1 + i) * 16) = buffer_word.range((i + 1) * 16 - 1, i * 16);
                             } else {
                                 for (int i = 0; i < DIM_B; i++)
                                     existing.range((elem_offset_c1 + i + 1) * 32 - 1, (elem_offset_c1 + i) * 32) = buffer_word.range((i + 1) * 32 - 1, i * 32);
                             }
                             block_buffer_c1[buffer_word_idx] = existing;
                         } else {
                             block_buffer_c1[buffer_word_idx] = buffer_word;
                         }
                     }
                 }
             }
         }

         // Write block to matC. When cols_per_file < WRD_LN we have 1 word per row per split:
         // merge c0 and c1 into one word per row (first WRD_LN/2 elements from c0, next from c1) so matC has 1 word per row.
         for (int row_in_block = 0; row_in_block < rows_per_block; row_in_block++) {
             #pragma HLS LOOP_TRIPCOUNT min=16 max=64
             const int global_row = block_start_row + row_in_block;
             const int row_base_c0 = row_in_block * words_per_row_in_file;
             const int row_base_output = global_row * words_per_row_in_matrix;

             if (words_per_row_in_file == 1 && words_per_row_in_matrix == 1) {
                 // Merge one c0 word and one c1 word into one row word (cols 0..WRD_LN-1)
                 ap_int<128> w0 = block_buffer_c0[row_base_c0];
                 ap_int<128> w1 = block_buffer_c1[row_base_c0];
                 ap_int<128> merged = 0;
                 const int half = WRD_LN / 2;
                 if (DATA_TYPE == 16) {
                     for (int j = 0; j < half; j++) {
                         #pragma HLS UNROLL
                         merged.range((j + 1) * 16 - 1, j * 16) = w0.range((j + 1) * 16 - 1, j * 16);
                         merged.range((j + half + 1) * 16 - 1, (j + half) * 16) = w1.range((j + 1) * 16 - 1, j * 16);
                     }
                 } else {
                     for (int j = 0; j < half; j++) {
                         #pragma HLS UNROLL
                         merged.range((j + 1) * 32 - 1, j * 32) = w0.range((j + 1) * 32 - 1, j * 32);
                         merged.range((j + half + 1) * 32 - 1, (j + half) * 32) = w1.range((j + 1) * 32 - 1, j * 32);
                     }
                 }
                 matC[row_base_output] = merged;
             } else {
                 // Sequential c0 then c1 per row to match matrix_C_golden and plio_utils.generate_c_fake_from_split_files:
                 // row = [c0_w0, c0_w1, ..., c1_w0, c1_w1, ...] so cols 0..(cols_per_file-1) from c0, then c1.
                 for (int k = 0; k < words_per_row_in_file; k++) {
                     #pragma HLS PIPELINE II=1
                     matC[row_base_output + k] = block_buffer_c0[row_base_c0 + k];
                 }
                 for (int k = 0; k < words_per_row_in_file; k++) {
                     #pragma HLS PIPELINE II=1
                     matC[row_base_output + words_per_row_in_file + k] = block_buffer_c1[row_base_c0 + k];
                 }
             }
         }
     }
}
#endif
// ============================================================================
// TOP-LEVEL DMA KERNEL FUNCTION - ULTRA-FAST DEADLOCK-FREE
// ============================================================================
/**
 * @brief Top-level DMA kernel function for AI Engine GEMM data movement
 * 
 * This is the main entry point for the DMA kernel that orchestrates all data
 * movement between host memory and AI Engine processing units. The function
 * implements a deadlock-free dataflow design with explicit start propagation
 * to ensure maximum performance and reliability.
 * 
 * Architecture:
 * - Matrix A: Broadcast to all splits via 8 PLIO streams
 * - Matrix B: Distributed across splits via 16 PLIO streams
 * - Matrix C: Collected from AI Engine via 2 PLIO streams
 * 
 * Memory Interface Mapping:
 * - gmem0 -> S00_AXI (C1_DDR_LOW0/C1_DDR_LOW1) - NoC DDR4 interface for Matrix A
 * - gmem1 -> S01_AXI (C2_DDR_LOW0/C2_DDR_LOW1) - NoC DDR4 interface for Matrix B
 * - gmem2 -> S02_AXI (C3_DDR_LOW0/C3_DDR_LOW1) - NoC DDR4 interface for Matrix C
 * 
 * Key Features:
 * - Deadlock-free dataflow design with explicit start propagation
 * - Burst-optimized memory access for maximum throughput
 * - Configurable burst lengths and outstanding requests
 * - Memory interface mapping to NoC DDR4 interfaces
 * - Clock domain association for proper timing
 * - AXI-Stream interfaces with configurable FIFO depths
 * 
 * @param matA Matrix A memory pointer (input, broadcast)
 * @param matB Matrix B memory pointer (input, distributed)
 * @param matC Matrix C memory pointer (output, collected)
 * @param strmOut_to_A0-A7 Matrix A output streams (8 streams for broadcast)
 * @param strmOut_to_B0-B15 Matrix B output streams (16 streams for distribution)
 * @param strmInp_from_C0-C1 Matrix C input streams (2 streams for collection)
 * 
 * @note This function is the main kernel entry point and must be C-linkable
 * @note All interface pragmas are defined here for proper HLS synthesis
 * @note Dataflow design ensures parallel execution of inp_A, inp_B, and out_C
 * @note Memory interfaces use NoC DDR4, not HP/HPC ports
 */
extern "C" {

void dma_hls(
   ap_int<128>* matA,                                    // Matrix A memory pointer
   ap_int<128>* matB,                                    // Matrix B memory pointer
   ap_int<128>* matC,                                    // Matrix C memory pointer
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A0,   // Matrix A output stream 0
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A1,   // Matrix A output stream 1
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A2,   // Matrix A output stream 2
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A3,   // Matrix A output stream 3
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A4,   // Matrix A output stream 4
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A5,   // Matrix A output stream 5
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A6,   // Matrix A output stream 6
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A7,   // Matrix A output stream 7
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B0,   // Matrix B output stream 0
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B1,   // Matrix B output stream 1
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B2,   // Matrix B output stream 2
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B3,   // Matrix B output stream 3
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B4,   // Matrix B output stream 4
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B5,   // Matrix B output stream 5
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B6,   // Matrix B output stream 6
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B7,   // Matrix B output stream 7
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B8,   // Matrix B output stream 8
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B9,   // Matrix B output stream 9
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B10,  // Matrix B output stream 10
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B11,  // Matrix B output stream 11
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B12,  // Matrix B output stream 12
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B13,  // Matrix B output stream 13
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B14,  // Matrix B output stream 14
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B15,  // Matrix B output stream 15
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmInp_from_C0, // Matrix C input stream 0
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmInp_from_C1  // Matrix C input stream 1
) {
   // Memory interfaces with proper clock association
   // Memory interface mapping (from build logs analysis):
   // gmem0 -> S00_AXI (C1_DDR_LOW0/C1_DDR_LOW1) - NoC DDR4 interface for matA
   // gmem1 -> S01_AXI (C2_DDR_LOW0/C2_DDR_LOW1) - NoC DDR4 interface for matB  
   // gmem2 -> S02_AXI (C3_DDR_LOW0/C3_DDR_LOW1) - NoC DDR4 interface for matC
   // Note: These are NOT using HP1-3 or HPC1-2 ports, but NoC DDR4 memory interfaces
   
   #pragma HLS INTERFACE m_axi port=matA offset=slave bundle=gmem0 max_read_burst_length=BURST_A depth=EXACT_MATA_SZ num_read_outstanding=OUTSTANDING_A num_write_outstanding=1 latency=4
   #pragma HLS INTERFACE m_axi port=matB offset=slave bundle=gmem1 max_read_burst_length=BURST_B depth=EXACT_MATB_SZ num_read_outstanding=OUTSTANDING_B num_write_outstanding=1 latency=4
   #pragma HLS INTERFACE m_axi port=matC offset=slave bundle=gmem2 max_write_burst_length=BURST_C depth=EXACT_MATC_SZ num_read_outstanding=1 num_write_outstanding=OUTSTANDING_C latency=4
   
   // Clock interface with proper association
   #pragma HLS INTERFACE ap_clk port=ap_clk
   
   // Control interface
   #pragma HLS INTERFACE s_axilite port=return bundle=control
   #pragma HLS INTERFACE s_axilite port=matA bundle=control
   #pragma HLS INTERFACE s_axilite port=matB bundle=control
   #pragma HLS INTERFACE s_axilite port=matC bundle=control
   
   // AXI-Stream interfaces with proper clock association
   #pragma HLS INTERFACE axis port=strmOut_to_A0 depth=FIFO_A_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_A1 depth=FIFO_A_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_A2 depth=FIFO_A_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_A3 depth=FIFO_A_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_A4 depth=FIFO_A_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_A5 depth=FIFO_A_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_A6 depth=FIFO_A_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_A7 depth=FIFO_A_DEPTH register
   
   #pragma HLS INTERFACE axis port=strmOut_to_B0 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B1 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B2 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B3 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B4 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B5 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B6 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B7 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B8 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B9 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B10 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B11 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B12 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B13 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B14 depth=FIFO_B_DEPTH register
   #pragma HLS INTERFACE axis port=strmOut_to_B15 depth=FIFO_B_DEPTH register
   
   #pragma HLS INTERFACE axis port=strmInp_from_C0 depth=FIFO_C_DEPTH register
   #pragma HLS INTERFACE axis port=strmInp_from_C1 depth=FIFO_C_DEPTH register
   
   // PERFECT DATAFLOW: Zero deadlock risk with explicit start propagation
   #pragma HLS DATAFLOW

       // Start with inp_A
       inp_A(matA, strmOut_to_A0, strmOut_to_A1, strmOut_to_A2, strmOut_to_A3,
             strmOut_to_A4, strmOut_to_A5, strmOut_to_A6, strmOut_to_A7);

       
       // Then inp_B
       inp_B(matB, strmOut_to_B0, strmOut_to_B1, strmOut_to_B2, strmOut_to_B3,
             strmOut_to_B4, strmOut_to_B5, strmOut_to_B6, strmOut_to_B7,
             strmOut_to_B8, strmOut_to_B9, strmOut_to_B10, strmOut_to_B11,
             strmOut_to_B12, strmOut_to_B13, strmOut_to_B14, strmOut_to_B15);

       
       // out_C: SIMPLE_OUT_C=1 writes interleaved [c0,c1,...] to matC (compare c.txt vs c_golden.txt);
       //        SIMPLE_OUT_C=0 de-tiles and writes row-major (compare c.txt vs matrix_C_golden.txt).
       out_C(strmInp_from_C0, strmInp_from_C1, matC);

}
}

