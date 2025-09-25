#!/usr/bin/env python3
"""
Complete PLIO Data Generator for AIE GEMM Simulation
==================================================

This script generates input data files for the AIE simulator.
All parameters are automatically read from config.json.

Data Ordering Summary Table:
Level                   | Matrix A | Matrix B | Matrix C
------------------------|----------|----------|----------
Elements within sub-tiles | Row-major | Row-major | Row-major
Sub-tiles within tiles    | Row-major | Row-major | Row-major
Tiles within blocks       | Row-major | Column-major | Column-major

Key Implementation Features:
- Consistent 4×4 sub-tile sizes for all data types regardless of DIM value
- Proper broadcasting patterns with correct repetition counts
- Row-major element ordering within sub-tiles
- Column-major tile processing for Matrix B, row-major for Matrix A
- Zero-padding for incomplete chunks to maintain 8 elements per line
- Sequential block processing with immediate broadcasting

Usage:
    python plioGen.py

Examples:
    python plioGen.py                    # Uses all parameters from config.json

Configuration:
    All parameters are read from config.json including:
    - GEMM_SIZE, DIM, SPLIT, CASC_LN, ITER_CNT, etc.
    - DATA_TYPE (int16, int32, float)
    - MATRIX_A_FILE, MATRIX_B_FILE (paths to input matrices)
    - RANDOM_SEED (for reproducible random matrix generation)
    - If matrix files are empty or not found, random matrices are generated
"""

import os
import sys
from plio_utils import (
    read_all_parameters_from_config,
    get_word_length,
    get_optimal_sub_tile_size,
    generate_sequential_matrix_files,
    generate_split_output_files,
    generate_cascade_input_files,
    generate_a_golden,
    generate_b_golden,
    generate_c_golden,
    compute_dim_a_b,
    list_valid_tp_dims
)


def main():
    # Read all parameters from config file
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_file = os.path.join(script_dir, '..', '..', 'design_configs', 'config.json')
    
    print(f"Reading configuration from: {config_file}")
    params = read_all_parameters_from_config(config_file)
    
    # Extract parameters
    GEMM_SIZE = params['GEMM_SIZE']
    DIM = params['DIM']
    SPLIT = params['SPLIT']
    CASC_LN = params['CASC_LN']
    DATA_TYPE = params['DATA_TYPE']
    TARGET_HW_EMU = params['TARGET_HW_EMU']
    ITER_CNT = params['ITER_CNT']
    N_SAMPLES = params['N_SAMPLES']
    GEMM_INSTS = params['GEMM_INSTS']
    EN_TRACE = params['EN_TRACE']
    
    # Get matrix file paths and random seed from config
    matrix_a_file = params['MATRIX_A_FILE'] if params['MATRIX_A_FILE'] else None
    matrix_b_file = params['MATRIX_B_FILE'] if params['MATRIX_B_FILE'] else None
    random_seed = params['RANDOM_SEED']
    
    # Display matrix file information
    if matrix_a_file and matrix_a_file.strip():
        print(f"Matrix A file to load: {matrix_a_file}")
    else:
        print(f"Matrix A: Generating random data (no input file specified)")
        
    if matrix_b_file and matrix_b_file.strip():
        print(f"Matrix B file to load: {matrix_b_file}")
    else:
        print(f"Matrix B: Generating random data (no input file specified)")
    
    # Calculate derived parameters
    WRD_LN = get_word_length(DATA_TYPE)
    DIM_A_eff, DIM_AB_eff, DIM_B_eff = compute_dim_a_b(GEMM_SIZE, DIM, SPLIT, CASC_LN, DATA_TYPE)
    GRAPH_ITER_CNT = (GEMM_SIZE * GEMM_SIZE) // (DIM * DIM) // SPLIT
    
    # Use sub-tile values from config file (already read by read_all_parameters_from_config)
    sub_tile_m = params['SUB_TILE_M']
    sub_tile_k = params['SUB_TILE_K']
    sub_tile_n = params['SUB_TILE_N']
    # Compute counts per tile for reporting
    sub_tiles_per_row = max(1, DIM // sub_tile_m)
    sub_tiles_per_col = max(1, DIM // sub_tile_n)
    
    print(f"\nConfiguration Summary:")
    print(f"GEMM_SIZE: {GEMM_SIZE}")
    print(f"DIM: {DIM}")
    print(f"SPLIT: {SPLIT}")
    print(f"CASC_LN: {CASC_LN}")
    print(f"DIM_A, DIM_AB, DIM_B: {DIM_A_eff}, {DIM_AB_eff}, {DIM_B_eff}")
    print(f"GRAPH_ITER_CNT: {GRAPH_ITER_CNT} (computed: ({GEMM_SIZE}×{GEMM_SIZE})/({DIM}×{DIM})/{SPLIT})")
    print(f"Data Type: {DATA_TYPE}")
    print(f"Sub-Tile Size (M,K,N): {sub_tile_m}x{sub_tile_k}x{sub_tile_n}")
    print(f"Sub-Tiles per Tile: {sub_tiles_per_row}x{sub_tiles_per_col}")
    print(f"Word Length (WRD_LN): {WRD_LN} elements per 128-bit PLIO (calculated: 128/{DATA_TYPE} bits)")
    print(f"Target HW EMU: {TARGET_HW_EMU}")
    print(f"Iteration Count: {ITER_CNT}")
    print(f"N Samples: {N_SAMPLES}")
    print(f"GEMM Instances: {GEMM_INSTS}")
    print(f"Enable Trace: {EN_TRACE}")
    print(f"Random Seed: {random_seed}")
    
    valid_dims = list_valid_tp_dims(GEMM_SIZE, DIM, SPLIT, CASC_LN, DATA_TYPE, sub_tile_m, sub_tile_k, sub_tile_n)
    if valid_dims:
        print("Valid (TP_DIM_A, TP_DIM_AB, TP_DIM_B) choices:")
        print(", ".join([f"({a},{ab},{b})" for (a,ab,b) in valid_dims]))
    else:
        print("Valid (TP_DIM_A, TP_DIM_AB, TP_DIM_B) choices: none under current CASC_LN/K-tile constraints")
    
    # Create output directory
    path = f"gemm_{GEMM_SIZE}x{GEMM_SIZE}x{GEMM_SIZE}_ioFiles"
    os.makedirs(path, exist_ok=True)
    
    # Generate matrices
    matrix_A, matrix_B, matrix_C = generate_sequential_matrix_files(
        GEMM_SIZE, path, 
        matrix_a_file=matrix_a_file, matrix_b_file=matrix_b_file,
        data_type=DATA_TYPE, random_seed=random_seed
    )
    
    # Generate the split output files
    generate_split_output_files(matrix_C, SPLIT, DIM, WRD_LN, path, DATA_TYPE, sub_tile_m, sub_tile_n)
    
    # Generate cascade input files
    generate_cascade_input_files(matrix_A, matrix_B, SPLIT, CASC_LN, DIM, WRD_LN, path, DATA_TYPE, sub_tile_m, sub_tile_k, sub_tile_n)
    
    # Generate consolidated golden files
    print(f"\nGenerating consolidated golden files:")
    
    # Generate a_golden.txt from all a0_casc*.txt files
    matA_file_list = [f"a0_casc{casc}.txt" for casc in range(CASC_LN)]
    generate_a_golden(path, matA_file_list)
    
    # Generate b_golden.txt from all b*_casc*.txt files
    matB_file_list = []
    for split_idx in range(SPLIT):
        for casc in range(CASC_LN):
            matB_file_list.append(f"b{split_idx}_casc{casc}.txt")
    generate_b_golden(path, matB_file_list)

    # Generate c_golden.txt from all c*_golden.txt files
    generate_c_golden(path, SPLIT)

    print(f"\nComplete data generation completed successfully!")
    print(f"Output directory: {path}")
    print(f"Matrix dimensions: A={matrix_A.shape}, B={matrix_B.shape}, C={matrix_C.shape}")
    print(f"PLIO Optimization: {DATA_TYPE} with {sub_tile_m}x{sub_tile_m} sub-tiles (AI Engine-ML instruction set)")
    
    print(f"\nGenerated files:")
    print(f"  - matrix_A_input.txt")
    print(f"  - matrix_B_input.txt") 
    print(f"  - matrix_C_golden.txt")
    for i in range(SPLIT):
        print(f"  - c{i}_golden.txt")
    for i in range(CASC_LN):
        print(f"  - a0_casc{i}.txt")
    for split_idx in range(SPLIT):
        for casc in range(CASC_LN):
            print(f"  - b{split_idx}_casc{casc}.txt")
    print(f"  - a_golden.txt")
    print(f"  - b_golden.txt")
    print(f"  - c_golden.txt")
    
    print(f"\nUsage: python plioGen.py")
    print(f"  - All parameters are read from config.json")
    print(f"  - Matrix files can be specified in config.json (MATRIX_A_FILE, MATRIX_B_FILE)")
    print(f"  - If matrix files are empty/not found, random matrices are generated")


if __name__ == "__main__":
    main()
