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
//   - Single DDR reader: load one SPLIT_A row-block (ping-pong), pack from block_buf,
//     then emit word index w to A0..A7 in lockstep (no eight parallel m_axi readers).
//
// Matrix B and C sizes are defined in gemm_config.h:
//   EXACT_MATB_SZ = BASE_MATB_SZ * NUM_B_FILES  (from gemm_config.h)
//   EXACT_MATC_SZ = BASE_MATC_SZ * NUM_C_FILES  (from gemm_config.h)
// Do NOT redefine them here - use the values from gemm_config.h

// Packed 128b words for one SPLIT_A block and one cascade stripe (must match inp_A_producer).
#ifndef ELEMS_A_PER_BLOCK_CASCADE
#define ELEMS_A_PER_BLOCK_CASCADE ((GEMM_SIZE_A / SPLIT_A) * (GEMM_SIZE_AB / CASC_LN_AB))
#endif
#ifndef WORDS_A_PER_BLOCK_CASCADE
#define WORDS_A_PER_BLOCK_CASCADE (((ELEMS_A_PER_BLOCK_CASCADE) + (WRD_LN) - 1) / (WRD_LN))
#endif
#ifndef WORDS_ACROSS_A_ROW
#define WORDS_ACROSS_A_ROW (GEMM_SIZE_AB / WRD_LN)
#endif
// One SPLIT_A row-block of A in packed words; loaded once per block_idx (ping-pong banks in inp_A).
#ifndef BLOCK_A_WORDS
#define BLOCK_A_WORDS ((GEMM_SIZE_A / SPLIT_A) * WORDS_ACROSS_A_ROW)
#endif
// Packed words from one sub-tile row-band stripe (all sub-tile cols for one cascade) — upper bound
#ifndef MAX_WORDS_A_SLAB
#define MAX_WORDS_A_SLAB (((SUB_TILE_A * (GEMM_SIZE_AB / CASC_LN_AB)) + WRD_LN - 1) / WRD_LN + 2)
#endif
// inp_A pack_cascades: partial UNROLL only (not 8×) to limit BRAM vs full parallel pack.
//   factor 4 + CASC_LN_AB=8 → 2 wavefronts × 4 parallel packs. factor 2 → 4×2 (lower resources).
#ifndef PACK_CASCADE_UNROLL_FACTOR
#define PACK_CASCADE_UNROLL_FACTOR 4
#endif
#if (PACK_CASCADE_UNROLL_FACTOR != 2) && (PACK_CASCADE_UNROLL_FACTOR != 4)
#error PACK_CASCADE_UNROLL_FACTOR must be 2 or 4 (override: v++ -D PACK_CASCADE_UNROLL_FACTOR=2 ...)
#endif
#if (CASC_LN_AB % PACK_CASCADE_UNROLL_FACTOR) != 0
#error CASC_LN_AB must be divisible by PACK_CASCADE_UNROLL_FACTOR
#endif

// ============================================================================
// SUB-TILE PARAMETERS
// ============================================================================
// Sub-tile dimensions control the granularity of element processing within tiles:
//   - SUB_TILE_A: Sub-tile dimension for A dimension (rows of Matrix A)
//   - SUB_TILE_AB: Sub-tile dimension for AB dimension (columns of A / rows of B)
//   - SUB_TILE_B: Sub-tile dimension for B dimension (columns of Matrix B/C)
//   - Data-type dependent values (from AI Engine-ML matrix_mult instruction set):
//     * int16 / int32 (this project): SUB_TILE_A=4, SUB_TILE_AB=4, SUB_TILE_B=4 (4×4×4 sub-tiles)
//   - inp_A / pack_cascade_from_block use only SUB_TILE_A and SUB_TILE_AB.
//   - SUB_TILE_B is for Matrix B / C paths (inp_B, out_C), not for a0_casc* stream layout.
//   - int32 WRD_LN=4 → each PLIO beat / a0_casc*.txt line holds 4 int32s; a full A row in DDR is GEMM_SIZE_AB/WRD_LN packed words.
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

// Burst/outstanding 64 regressed PL timing at 312.5 MHz on ve2302; 32 helps closure.
#define BURST_A 32        // Matrix A burst length (128b words per AXI burst)
#define BURST_B 32        // Matrix B burst length
#define BURST_C 32        // Matrix C write burst length
#define FIFO_A_DEPTH 16   // Matrix A FIFO depth (streaming buffer size)
#define FIFO_B_DEPTH 16   // Matrix B FIFO depth (streaming buffer size)
// SIMPLE_OUT_C=0: depth from gemm_config.h (FIFO_C_DEPTH_COMPLEX). Shallow FIFO risks AIE/PL deadlock.
#ifndef FIFO_C_DEPTH
#if SIMPLE_OUT_C
#define FIFO_C_DEPTH 16
#else
#ifndef FIFO_C_DEPTH_COMPLEX
/* Fallback if gemm_config.h omitted FIFO_C_DEPTH_COMPLEX: run `make gemm_config.h` with SIMPLE_OUT_C=0 for adaptive depth. */
#define FIFO_C_DEPTH_COMPLEX 8192
#endif
#define FIFO_C_DEPTH FIFO_C_DEPTH_COMPLEX
#endif
#endif

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
// MATRIX A — DUAL BLOCK BUFFERS + TILED CACHE + INTERLEAVED AXI (single m_axi reader)
// ============================================================================
/**
 * Pack one cascade stripe for matrix rows [st_start_row, st_end_row) using one SPLIT_A row-block in @p block_buf.
 * block_buf layout: row r relative to block_start_row at index (r * WORDS_ACROSS_A_ROW + wc) in [0, BLOCK_A_WORDS).
 * Cascade stripe width along AB is DIM_AB (graph tile K). Same traversal as plio_utils.write_matrix_A_cascade.
 * Returns number of 128b words written to @p out_pack.
 */
static void pack_cascade_from_block(
    const ap_int<128> block_buf[BLOCK_A_WORDS],
    int block_start_row,
    int st_start_row,
    int st_end_row,
    int casc_idx,
    ap_int<128> out_pack[MAX_WORDS_A_SLAB],
    int &n_words_out
) {
#pragma HLS INLINE off
    n_words_out = 0;
    const int cols_per_casc = DIM_AB;
    const int casc_start_col = casc_idx * cols_per_casc;
    const int casc_end_col = casc_start_col + cols_per_casc;
    const int dim_ab_eff = DIM_AB;
    const int sub_tiles_per_col = dim_ab_eff / SUB_TILE_AB;
    const int tile_start_col = casc_start_col;
    const int tile_end_col = casc_end_col;

    ap_int<128> word_buffer[WRD_LN];
#pragma HLS ARRAY_PARTITION variable=word_buffer complete
    int element_counter = 0;

    pack_cols: for (int st_col = 0; st_col < sub_tiles_per_col; st_col++) {
#pragma HLS LOOP_FLATTEN off
        const int st_start_col = tile_start_col + st_col * SUB_TILE_AB;
        const int st_end_col = (st_start_col + SUB_TILE_AB <= tile_end_col) ? (st_start_col + SUB_TILE_AB) : tile_end_col;

        for (int st_r = 0; st_r < SUB_TILE_A; st_r++) {
            const int global_row = st_start_row + st_r;
            const int linear_base = global_row * GEMM_SIZE_AB + st_start_col;
            const int src_elem_start = linear_base % WRD_LN;
            if (global_row >= st_end_row) break;
            const int wc_in_row = (linear_base / WRD_LN) - global_row * WORDS_ACROSS_A_ROW;
            const int rblk = global_row - block_start_row;
            const int slab_idx = rblk * WORDS_ACROSS_A_ROW + wc_in_row;
            ap_int<128> src_word = block_buf[slab_idx];
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
                if ((element_counter & (WRD_LN - 1)) == 0) {
                    ap_int<128> packed_word = 0;
                    for (int i = 0; i < WRD_LN; i++) {
#pragma HLS UNROLL
                        packed_word |= word_buffer[i];
                    }
                    out_pack[n_words_out++] = packed_word;
                }
            }
        }
    }

    const int remaining = element_counter % WRD_LN;
    if (remaining > 0) {
        ap_int<128> packed_word = 0;
        for (int i = 0; i < remaining; i++) {
#pragma HLS UNROLL
            packed_word |= word_buffer[i];
        }
        out_pack[n_words_out++] = packed_word;
    }
}

// ============================================================================
// MATRIX A INPUT FUNCTION - CASCADE TRANSFORMATION AND BROADCAST
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
// Same logical element order as Python; pack every WRD_LN elements into one 128-bit AXI word.
// inp_A: one block RAM; per block_idx load DDR → block_ram, then pack into tiled[] and replay
// BROADCAST_COUNT_A times. No ping-pong (SPLIT_A is typically 2; overlap gains are small vs pack cost).
// TLAST only on the last beat of strmOut_to_A7 (lockstep golden).
// Pack phase loop nest (no bc):
//   1) block_idx, 2) tile_row, 3) st_row — pack_cascade_from_block() per casc_idx from block_ram.
//
// EIGHT CASCADES vs a_golden.txt
// ------------------------------
// Each strmOut_to_Aj matches a0_casc<j>.txt line order. a_golden interleaves those files by matrix
// row of lines; our PLIO timing interleaves by packed-word index w across cascades for lockstep feed.
//
// Sub-tile bounds: clip to tile_end_row / stripe column bounds (tile_end_col within stripe), same
// as plio_utils.write_matrix_A_cascade.

/**
 * @brief Reads raw Matrix A (GEMM_SIZE_A × GEMM_SIZE_AB) and transforms to cascade format
 *
 * Single block RAM: for each row-block load from DDR, pack once into tiled[], then replay
 * BROADCAST_COUNT_A. TLAST on last A7 beat only (matches a0_casc* golden lockstep).
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
#pragma HLS INLINE off
#pragma HLS DEPENDENCE variable=matA inter false
#pragma HLS DEPENDENCE variable=matA intra false

    if (matA == nullptr) {
        return;
    }

    ap_int<128> block_ram[BLOCK_A_WORDS];
#if BLOCK_A_WORDS > 8192
#pragma HLS BIND_STORAGE variable=block_ram type=RAM_2P impl=URAM
#else
#pragma HLS BIND_STORAGE variable=block_ram type=RAM_2P impl=BRAM
#endif
#pragma HLS ARRAY_PARTITION variable=block_ram cyclic factor=4 dim=1

    ap_int<128> packbuf[CASC_LN_AB][MAX_WORDS_A_SLAB];
#pragma HLS ARRAY_PARTITION variable=packbuf complete dim=1

    ap_int<128> tiled[CASC_LN_AB][WORDS_A_PER_BLOCK_CASCADE];
#if WORDS_A_PER_BLOCK_CASCADE > 4096
#pragma HLS BIND_STORAGE variable=tiled type=RAM_2P impl=URAM
#else
#pragma HLS BIND_STORAGE variable=tiled type=RAM_2P impl=BRAM
#endif
#pragma HLS ARRAY_PARTITION variable=tiled complete dim=1

    const int rows_per_block = GEMM_SIZE_A / SPLIT_A;
    const int dim_a_eff = (DIM_A < rows_per_block) ? DIM_A : rows_per_block;
    const int tiles_per_row = rows_per_block / dim_a_eff;
    const int sub_tiles_per_row = dim_a_eff / SUB_TILE_A;
    const int split_a = SPLIT_A;

    inp_A_blocks: for (int block_idx = 0; block_idx < split_a; block_idx++) {
        const int block_start_row = block_idx * rows_per_block;
        const int block_end_row = block_start_row + rows_per_block;

        // k walks row-major within the SPLIT_A block → contiguous matA addresses → good m_axi bursts.
        load_block_a: for (int k = 0; k < BLOCK_A_WORDS; k++) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=256 max=131072
            const int r = k / WORDS_ACROSS_A_ROW;
            const int wc = k % WORDS_ACROSS_A_ROW;
            block_ram[k] = matA[(block_start_row + r) * WORDS_ACROSS_A_ROW + wc];
        }

        int beat_off = 0;
        tile_loop: for (int tile_row = 0; tile_row < tiles_per_row; tile_row++) {
#pragma HLS LOOP_FLATTEN off
            const int tile_start_row = block_start_row + tile_row * dim_a_eff;
            const int tile_end_row = (tile_start_row + dim_a_eff <= block_end_row)
                ? (tile_start_row + dim_a_eff) : block_end_row;

            st_row_loop: for (int st_row = 0; st_row < sub_tiles_per_row; st_row++) {
#pragma HLS LOOP_FLATTEN off
                const int st_start_row = tile_start_row + st_row * SUB_TILE_A;
                const int st_end_row = (st_start_row + SUB_TILE_A <= tile_end_row)
                    ? (st_start_row + SUB_TILE_A) : tile_end_row;
                const int n_load = st_end_row - st_start_row;
                if (n_load <= 0) {
                    continue;
                }

                int W = 0;
                /* Only pack_cascades benefits from UNROLL; store_beats/replay stay II=1. Default 4 → two waves of 4 parallel pack_cascade_from_block. */
#if PACK_CASCADE_UNROLL_FACTOR == 2
#pragma HLS UNROLL factor=2
#elif PACK_CASCADE_UNROLL_FACTOR == 4
#pragma HLS UNROLL factor=4
#else
#error "PACK_CASCADE_UNROLL_FACTOR must be 2 or 4 (see config.json / v++ -D)"
#endif
                pack_cascades: for (int casc_idx = 0; casc_idx < CASC_LN_AB; casc_idx++) {
                    int nw = 0;
                    pack_cascade_from_block(
                        block_ram, block_start_row, st_start_row, st_end_row, casc_idx, packbuf[casc_idx], nw);
                    if (casc_idx == 0) {
                        W = nw;
                    }
                }

                store_beats: for (int w = 0; w < W; w++) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=8 max=4096
#if CASC_LN_AB != 8
#error "store_beats: extend cascade writes if CASC_LN_AB != 8"
#endif
                    tiled[0][beat_off] = packbuf[0][w];
                    tiled[1][beat_off] = packbuf[1][w];
                    tiled[2][beat_off] = packbuf[2][w];
                    tiled[3][beat_off] = packbuf[3][w];
                    tiled[4][beat_off] = packbuf[4][w];
                    tiled[5][beat_off] = packbuf[5][w];
                    tiled[6][beat_off] = packbuf[6][w];
                    tiled[7][beat_off] = packbuf[7][w];
                    beat_off++;
                }
            }
        }

        const int total_beats = beat_off;

        broadcast_loop: for (int bc = 0; bc < BROADCAST_COUNT_A; bc++) {
            replay_beats: for (int t = 0; t < total_beats; t++) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=256 max=131072
                const bool last_beat = (block_idx == split_a - 1) && (bc == BROADCAST_COUNT_A - 1)
                    && (t == total_beats - 1);
                ap_axiu<128, 0, 0, 0> pkt;
                pkt.keep = -1;
                pkt.strb = -1;
#if CASC_LN_AB != 8
#error "replay_beats: extend explicit AXIS writes if CASC_LN_AB != 8"
#endif
                pkt.data = tiled[0][t];
                pkt.last = 0;
                strmOut_to_A0.write(pkt);
                pkt.data = tiled[1][t];
                strmOut_to_A1.write(pkt);
                pkt.data = tiled[2][t];
                strmOut_to_A2.write(pkt);
                pkt.data = tiled[3][t];
                strmOut_to_A3.write(pkt);
                pkt.data = tiled[4][t];
                strmOut_to_A4.write(pkt);
                pkt.data = tiled[5][t];
                strmOut_to_A5.write(pkt);
                pkt.data = tiled[6][t];
                strmOut_to_A6.write(pkt);
                pkt.data = tiled[7][t];
                pkt.last = last_beat ? 1 : 0;
                strmOut_to_A7.write(pkt);
            }
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
 * - SUB_TILE_B: Sub-tile dimension for B dimension (columns of Matrix B/C); 4 for int16 and int32 here
 *   * int32 WRD_LN=4 (4 elements/word) vs int16 WRD_LN=8 (8 elements/word)
 * - Per tile: PLIO words are unpacked here into elem order (sub-tiles row-major,
 *   elements row-major within sub-tiles) into a DIM_A×DIM_B tile buffer, then that
 *   full tile is scattered into the block row-major buffer (same block rows, DIM_B cols).
 * - Tile grid within the block: outer tile_col, inner tile_row → column-major tile order
 *   while advancing along columns of the block; row span of each tile is DIM_A rows.
 * - SUB_TILE_AB is unused in out_C; stream unpack uses SUB_TILE_A and SUB_TILE_B (inp_A uses SUB_TILE_A/AB).
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
 * 1. Read tiled data from streams (column-major tiles within blocks). C0 and C1 are read
 *    in lockstep for each tile (same word index from both) so both split graphs drain PLIO
 *    FIFOs together; plioGen is sequential on files, but HW runs both splits concurrently.
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

#if DATA_TYPE == 16
     typedef ap_int<16> elem_c_t;
#elif DATA_TYPE == 32
     typedef ap_int<32> elem_c_t;
#else
     #error "Unsupported DATA_TYPE for out_C (use 16 or 32)"
#endif

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

#if !defined(WORDS_PER_C_TILE) || !defined(WORDS_PER_C_BLOCK)
#error "WORDS_PER_C_TILE and WORDS_PER_C_BLOCK must be defined in gemm_config.h (regenerate via Makefile)"
#endif
     ap_int<128> block_buffer_c0[WORDS_PER_C_BLOCK];
     ap_int<128> block_buffer_c1[WORDS_PER_C_BLOCK];
#if WORDS_PER_C_BLOCK > 8192
     #pragma HLS BIND_STORAGE variable=block_buffer_c0 type=RAM_2P impl=URAM
     #pragma HLS BIND_STORAGE variable=block_buffer_c1 type=RAM_2P impl=URAM
#endif
     #pragma HLS DEPENDENCE variable=block_buffer_c0 inter false
     #pragma HLS DEPENDENCE variable=block_buffer_c1 inter false

     for (int block_row = 0; block_row < SPLIT_A; block_row++) {
         #pragma HLS LOOP_TRIPCOUNT min=SPLIT_A max=SPLIT_A
         const int block_start_row = block_row * rows_per_block;

         for (int i = 0; i < words_per_block; i++) {
             #pragma HLS PIPELINE II=1
             #pragma HLS LOOP_TRIPCOUNT min=8 max=16384
             block_buffer_c0[i] = 0;
             block_buffer_c1[i] = 0;
         }

         const int sub_tiles_per_row = DIM_A / SUB_TILE_A;
         const int sub_tiles_per_col = DIM_B / SUB_TILE_B;

         // De-tile c0 & c1: column-major tile order (match c0_golden / c1_golden).
         // Read C0 and C1 in lockstep for each (tile_col,tile_row), same word index w on both
         // streams, so parallel mmult splits backpressure together. Previously: drain entire
         // block on C0 then C1, which could force one PLIO FIFO to hold ~a full block while idle.
         for (int tile_col = 0; tile_col < tiles_per_block_col; tile_col++) {
             #pragma HLS LOOP_FLATTEN off
             #pragma HLS LOOP_TRIPCOUNT min=1 max=8
             for (int tile_row = 0; tile_row < tiles_per_block_row; tile_row++) {
                 #pragma HLS LOOP_FLATTEN off
                 #pragma HLS LOOP_TRIPCOUNT min=1 max=8
                 const int tile_start_row_in_block = tile_row * DIM_A;
                 const int tile_start_col = tile_col * DIM_B;

                 ap_int<128> tile_words_c0[WORDS_PER_C_TILE];
                 ap_int<128> tile_words_c1[WORDS_PER_C_TILE];
                 #pragma HLS ARRAY_PARTITION variable=tile_words_c0 cyclic factor=2
                 #pragma HLS ARRAY_PARTITION variable=tile_words_c1 cyclic factor=2

                 read_c01_tile: for (int w = 0; w < words_per_tile; w++) {
                     #pragma HLS PIPELINE II=1
                     #pragma HLS LOOP_TRIPCOUNT min=8 max=4096
                     ap_axiu<128, 0, 0, 0> pkt0 = strmInp_from_C0.read();
                     ap_axiu<128, 0, 0, 0> pkt1 = strmInp_from_C1.read();
                     tile_words_c0[w] = pkt0.data;
                     tile_words_c1[w] = pkt1.data;
                 }

                 const int words_per_row_in_tile = (DIM_B + WRD_LN - 1) / WRD_LN;
                 elem_c_t tile_elements[DIM_A * DIM_B];
                 elem_c_t tile_elements_c1[DIM_A * DIM_B];
                 #pragma HLS ARRAY_PARTITION variable=tile_elements cyclic factor=4
                 #pragma HLS ARRAY_PARTITION variable=tile_elements_c1 cyclic factor=4

                 int element_idx = 0;
                 for (int sub_row = 0; sub_row < sub_tiles_per_row; sub_row++) {
                     for (int sub_col = 0; sub_col < sub_tiles_per_col; sub_col++) {
                         for (int st_row = 0; st_row < SUB_TILE_A; st_row++) {
                             for (int st_col = 0; st_col < SUB_TILE_B; st_col++) {
                                 const int row_in_tile = sub_row * SUB_TILE_A + st_row;
                                 const int col_in_tile = sub_col * SUB_TILE_B + st_col;
                                 const int word_idx = element_idx / WRD_LN;
                                 const int elem_in_word = element_idx % WRD_LN;
                                 ap_int<128> word = tile_words_c0[word_idx];
                                 elem_c_t element = (DATA_TYPE == 16) ?
                                     (word.range((elem_in_word + 1) * 16 - 1, elem_in_word * 16)) :
                                     (word.range((elem_in_word + 1) * 32 - 1, elem_in_word * 32));
                                 tile_elements[row_in_tile * DIM_B + col_in_tile] = element;
                                 element_idx++;
                             }
                         }
                     }
                 }

                 int element_idx_c1 = 0;
                 for (int sub_row = 0; sub_row < sub_tiles_per_row; sub_row++) {
                     for (int sub_col = 0; sub_col < sub_tiles_per_col; sub_col++) {
                         for (int st_row = 0; st_row < SUB_TILE_A; st_row++) {
                             for (int st_col = 0; st_col < SUB_TILE_B; st_col++) {
                                 const int row_in_tile = sub_row * SUB_TILE_A + st_row;
                                 const int col_in_tile = sub_col * SUB_TILE_B + st_col;
                                 const int word_idx = element_idx_c1 / WRD_LN;
                                 const int elem_in_word = element_idx_c1 % WRD_LN;
                                 ap_int<128> word = tile_words_c1[word_idx];
                                 elem_c_t element = (DATA_TYPE == 16) ?
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
                     for (int word_in_row = 0; word_in_row < words_per_row_in_tile; word_in_row++) {
                         #pragma HLS PIPELINE II=1
                         ap_int<128> buffer_word = 0;
                         const int word_start_col = word_in_row * WRD_LN;
                         for (int elem_in_word = 0; elem_in_word < WRD_LN; elem_in_word++) {
                             #pragma HLS UNROLL
                             const int col_in_tile = word_start_col + elem_in_word;
                             if (col_in_tile < DIM_B) {
                                 elem_c_t element = tile_elements[row_in_tile * DIM_B + col_in_tile];
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
                                 elem_c_t element = tile_elements_c1[row_in_tile * DIM_B + col_in_tile];
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

