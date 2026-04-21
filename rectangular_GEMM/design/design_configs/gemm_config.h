#ifndef GEMM_CONFIG_H
#define GEMM_CONFIG_H
#define TARGET_HW 1
// Matrix dimensions (A, AB, B)
#define GEMM_SIZE_A 512
#define GEMM_SIZE_AB 512
#define GEMM_SIZE_B 512
#define DATA_TYPE 16
// Tile dimensions (A, AB, B)
// DIM is not defined here - only DIM_A, DIM_AB, DIM_B are used in code
#define DIM_A 128
#define DIM_AB 64
#define DIM_B 128
#define SPLIT_A 2
#define SPLIT_B 2
#define CASC_LN_AB 8
// Legacy names for backward compatibility
#define SPLIT 2  // For Matrix C split (uses SPLIT_B)
#define CASC_LN 8
#define WRD_LN 8
#define N_SAMPLES 1
#define ITER_CNT 1
#define GEMM_INSTS 1
#define EN_TRACE 0
#define PL_FREQ 312_5
// DDR-only mode: If 1, forces DDR-only mode (bypasses PS RAM)
#define USE_DDR_ONLY_MODE 1
// Calculated graph iteration count
// GRAPH_ITER_CNT = (GEMM_SIZE_A * GEMM_SIZE_B / SPLIT_B) / (DIM_A * DIM_B)
#define GRAPH_ITER_CNT 8
// Additional constants needed by host app and other components
#define NUM_A_FILES 8
#define NUM_B_FILES 16
#define NUM_C_FILES 2
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
