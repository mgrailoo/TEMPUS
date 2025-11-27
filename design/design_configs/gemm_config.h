#ifndef GEMM_CONFIG_H
#define GEMM_CONFIG_H
#define TARGET_HW 1
#define GEMM_SIZE 32
#define DATA_TYPE 16
#define DIM 16
#define SPLIT 2
#define CASC_LN 8
#define WRD_LN 8
#define N_SAMPLES 1
#define ITER_CNT 1
#define GEMM_INSTS 1
#define EN_TRACE 0
#define PL_FREQ 312_5
// Calculated tiling dimensions
#define DIM_A 16
#define DIM_B 4
// Calculated graph iteration count
// GRAPH_ITER_CNT = (GEMM_SIZE * GEMM_SIZE) / (DIM * DIM) / SPLIT
#define GRAPH_ITER_CNT 2
// Additional constants needed by host app and other components
#define NUM_A_FILES 8
#define NUM_B_FILES 16
#define NUM_C_FILES 2
// Matrix dimensions
#define DIM_AB 32
#define SUB_TILE_M 4
#define SUB_TILE_K 4
#define SUB_TILE_N 4
// Calculate base sizes for individual matrices based on corrected plioGen.py formula
// Matrix A: BASE_MATA_SZ = GEMM_SZ_SPLIT * GEMM_SZ_CASC * broadcast_count_a * SPLIT // WRD_LN
// where: tiles_per_row_in_block_a = (GEMM_SZ_SPLIT) // DIM_A
//        broadcast_count_a = max(1, tiles_per_row_in_block_a)
//        GEMM_SZ_SPLIT = GEMM_SIZE // SPLIT
//        GEMM_SZ_CASC = GEMM_SIZE // CASC_LN
#define TILES_PER_ROW_IN_BLOCK_A ((GEMM_SIZE / SPLIT) / DIM_A)
#define BROADCAST_COUNT_A ((TILES_PER_ROW_IN_BLOCK_A > 1) ? TILES_PER_ROW_IN_BLOCK_A : 1)
#define BASE_MATA_SZ (((GEMM_SIZE / SPLIT) * (GEMM_SIZE / CASC_LN) * BROADCAST_COUNT_A * SPLIT) / WRD_LN)
// Matrix B: Base size is the same as Matrix A
#define BASE_MATB_SZ BASE_MATA_SZ
// Matrix C: Each split has (GEMM_SIZE * GEMM_SIZE) / SPLIT / WRD_LN elements
#define BASE_MATC_SZ ((GEMM_SIZE * GEMM_SIZE) / SPLIT / WRD_LN)
#endif
