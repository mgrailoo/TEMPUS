#ifndef GEMM_CONFIG_H
#define GEMM_CONFIG_H
#define TARGET_HW 1
// Matrix dimensions (A, AB, B)
#define GEMM_SIZE_A 1024
#define GEMM_SIZE_AB 1024
#define GEMM_SIZE_B 1024
#define DATA_TYPE 32
// Tile dimensions (A, AB, B)
// DIM is not defined here - only DIM_A, DIM_AB, DIM_B are used in code
#define DIM_A 32
#define DIM_AB 128
#define DIM_B 32
#define SPLIT_A 2
#define SPLIT_B 2
#define CASC_LN_AB 8
// Legacy names for backward compatibility
#define SPLIT 2  // For Matrix C split (uses SPLIT_B)
#define CASC_LN 8
#define WRD_LN 4
#define N_SAMPLES 1
#define ITER_CNT 1
#define GEMM_INSTS 1
#define EN_TRACE 0
#define PL_FREQ 312_5
// DDR-only mode: If 1, forces DDR-only mode (bypasses PS RAM)
#define USE_DDR_ONLY_MODE 1
// out_C mode: 1=simple (c.txt vs c_golden.txt), 0=complex (c.txt vs matrix_C_golden.txt)
#define SIMPLE_OUT_C 0
// Calculated graph iteration count
// GRAPH_ITER_CNT = (GEMM_SIZE_A * GEMM_SIZE_B / SPLIT_B) / (DIM_A * DIM_B)
#define GRAPH_ITER_CNT 512
// AIE matrix_mult kernel runtime ratio (graph.h). Tune for Phase 6; typical 0.75–0.95.
#define AIE_RUNTIME_RATIO 1.0
// Additional constants needed by host app and other components
#define NUM_A_FILES 8
#define NUM_B_FILES 16
#define NUM_C_FILES 2
#define TILE_MEM_BYTES 32768
#define SUB_TILE_A 4
#define SUB_TILE_AB 4
#define SUB_TILE_B 4
// Calculate base sizes for individual matrices based on corrected plioGen.py formula
// Matrix A: BASE_MATA_SZ = GEMM_SZ_SPLIT_A * GEMM_SZ_CASC * broadcast_count_a * SPLIT_A // WRD_LN
// where: tiles_per_row_in_block_a = (GEMM_SZ_SPLIT_A) // DIM_A
//        broadcast_count_a = max(1, tiles_per_row_in_block_a)
//        GEMM_SZ_SPLIT_A = GEMM_SIZE_A // SPLIT_A
//        GEMM_SZ_CASC = GEMM_SIZE_AB // CASC_LN_AB
#define TILES_PER_ROW_IN_BLOCK_A ((GEMM_SIZE_A / SPLIT_A) / DIM_A)
#define BROADCAST_COUNT_A ((GEMM_SIZE_B / SPLIT_B) / DIM_B)
#define BASE_MATA_SZ (((GEMM_SIZE_A / SPLIT_A) * (GEMM_SIZE_AB / CASC_LN_AB) * BROADCAST_COUNT_A * SPLIT_A) / WRD_LN)
// Matrix B: BASE_MATB_SZ = GEMM_SZ_CASC * GEMM_SZ_SPLIT_B * broadcast_count_b * SPLIT_B // WRD_LN
// TILES_PER_COL_IN_BLOCK_B: number of tiles per column in block (based on column split, not row cascade)
#define TILES_PER_COL_IN_BLOCK_B ((GEMM_SIZE_B / SPLIT_B) / DIM_B)
#define BROADCAST_COUNT_B ((GEMM_SIZE_A / SPLIT_A) / DIM_A)
#define BASE_MATB_SZ (((GEMM_SIZE_AB / CASC_LN_AB) * (GEMM_SIZE_B / SPLIT_B) * BROADCAST_COUNT_B * SPLIT_B) / WRD_LN)
// Matrix C: Each split has (GEMM_SIZE_A * GEMM_SIZE_B) / SPLIT_B / WRD_LN elements
// Matrix C is split only in columns by SPLIT_B (all rows, split columns)
#define BASE_MATC_SZ ((GEMM_SIZE_A * GEMM_SIZE_B) / SPLIT_B / WRD_LN)
// SIMPLE_OUT_C=0 (dma_hls out_C): buffer sizes — hardcoded 512 caused silent overflow for large tiles/blocks
// WORDS_PER_C_TILE = (DIM_A*DIM_B)/WRD_LN; WORDS_PER_C_BLOCK = rows_per_block * ceil(cols_per_file/WRD_LN) per split
#define WORDS_PER_C_TILE ((DIM_A * DIM_B) / WRD_LN)
#define WORDS_PER_C_BLOCK (((GEMM_SIZE_A) / (SPLIT_A)) * (((GEMM_SIZE_B) / (SPLIT_B) + (WRD_LN) - 1) / (WRD_LN)))
#if !SIMPLE_OUT_C
// HLS AXIS C FIFO depth for complex out_C (integer; adaptive 32..1024 GEMM — see Makefile FIFO_C_DEPTH_COMPLEX_VAL)
#define FIFO_C_DEPTH_COMPLEX 51712
#endif
// Output C tile grid (per split block): row-major when more cols than rows, else column-major
#define TILES_PER_BLOCK_ROW_C ((GEMM_SIZE_A / SPLIT_A) / DIM_A)
#define TILES_PER_BLOCK_COL_C ((GEMM_SIZE_B / SPLIT_B) / DIM_B)
#define C_OUT_TILE_ROW_MAJOR (TILES_PER_BLOCK_ROW_C < TILES_PER_BLOCK_COL_C)
// Exact sizes: Total matrix sizes for rectangular matrices (GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B)
// These are calculated by multiplying BASE sizes by the number of files (NUM_*_FILES)
// Matrix A: BASE_MATA_SZ is per cascade file, multiply by NUM_A_FILES (= CASC_LN_AB) to get total
#define EXACT_MATA_SZ (BASE_MATA_SZ * NUM_A_FILES)
// Matrix B: BASE_MATB_SZ is per split-cascade file, multiply by NUM_B_FILES (= SPLIT_B * CASC_LN_AB) to get total
// Note: BASE_MATB_SZ is per split-cascade file (one file per split per cascade level)
#define EXACT_MATB_SZ (BASE_MATB_SZ * NUM_B_FILES)
// Matrix C: BASE_MATC_SZ is per split file, multiply by NUM_C_FILES (= SPLIT_B) to get total
#define EXACT_MATC_SZ (BASE_MATC_SZ * NUM_C_FILES)
// Aligned buffer sizes (with 4096-byte alignment padding)
// ap_int<128> is 16 bytes, so we calculate aligned sizes
#define BUFFER_ALIGNMENT 4096
#define ALIGNED_MATA_BYTES (((EXACT_MATA_SZ * 16) + BUFFER_ALIGNMENT - 1) / BUFFER_ALIGNMENT * BUFFER_ALIGNMENT)
#define ALIGNED_MATB_BYTES (((EXACT_MATB_SZ * 16) + BUFFER_ALIGNMENT - 1) / BUFFER_ALIGNMENT * BUFFER_ALIGNMENT)
#define ALIGNED_MATC_BYTES (((EXACT_MATC_SZ * 16) + BUFFER_ALIGNMENT - 1) / BUFFER_ALIGNMENT * BUFFER_ALIGNMENT)
#endif
