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

Sub-tile sizes: SUB_TILE_A=4, SUB_TILE_AB=4 for both int16 and int32. Matrix C (and B):
  int16: SUB_TILE_B=4 (4×4). int32: SUB_TILE_B=2 (4×2).

C output stream (int16, 8×8 tiles, WRD_LN=8): each word = 4×2 block; sub-tiles row-major,
within each 4×4 sub-tile two words: (0,0)-(3,1) then (0,2)-(3,3), then next sub-tile.
Order within word: (0,0),(0,1),(1,0),(1,1),(2,0),(2,1),(3,0),(3,1).

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
    - GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B, DIM, SPLIT, CASC_LN, ITER_CNT, etc.
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
    # Read GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B (REQUIRED - must be explicitly provided)
    if 'GEMM_SIZE_A' not in params:
        raise ValueError("GEMM_SIZE_A is required in config.json. Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B.")
    if 'GEMM_SIZE_AB' not in params:
        raise ValueError("GEMM_SIZE_AB is required in config.json. Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B.")
    if 'GEMM_SIZE_B' not in params:
        raise ValueError("GEMM_SIZE_B is required in config.json. Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B.")
    GEMM_SIZE_A = params['GEMM_SIZE_A']
    GEMM_SIZE_AB = params['GEMM_SIZE_AB']
    GEMM_SIZE_B = params['GEMM_SIZE_B']
    # Read new parameter names (with backward compatibility)
    SPLIT_A = params.get('SPLIT_A', params.get('SPLIT', 2))
    SPLIT_B = params.get('SPLIT_B', params.get('SPLIT', 2))
    CASC_LN_AB = params.get('CASC_LN_AB', params.get('CASC_LN', 8))
    
    # For backward compatibility, also set SPLIT and CASC_LN
    SPLIT = SPLIT_B  # Legacy: SPLIT refers to SPLIT_B for Matrix C
    CASC_LN = CASC_LN_AB  # Legacy: CASC_LN refers to CASC_LN_AB
    
    # Calculate DIM_A, DIM_B as min(DIM, GEMM_SIZE_* / SPLIT); DIM_AB is always GEMM_SIZE_AB // CASC_LN_AB
    # DIM is read from config.json; default 16 if not specified
    DIM = params.get('DIM', 16)
    DIM_A = min(DIM, GEMM_SIZE_A // max(1, SPLIT_A))
    DIM_AB = GEMM_SIZE_AB // max(1, CASC_LN_AB)
    DIM_B = min(DIM, GEMM_SIZE_B // max(1, SPLIT_B))
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
    DIM_A_orig, DIM_AB_orig, DIM_B_orig = DIM_A, DIM_AB, DIM_B
    DIM_A, DIM_AB, DIM_B = compute_dim_a_b(GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B, DIM_A, DIM_AB, DIM_B, SPLIT_A, SPLIT_B, CASC_LN_AB, DATA_TYPE)
    
    # Warn if dimensions were automatically corrected
    expected_dim_a = GEMM_SIZE_A // SPLIT_A if SPLIT_A > 0 else GEMM_SIZE_A
    expected_dim_ab = GEMM_SIZE_AB // CASC_LN_AB if CASC_LN_AB > 0 else GEMM_SIZE_AB
    expected_dim_b = GEMM_SIZE_B // SPLIT_B if SPLIT_B > 0 else GEMM_SIZE_B
    
    if DIM_A_orig != DIM_A:
        print(f"\n[WARNING] DIM_A was set to {DIM_A_orig} in config, but was automatically corrected to {DIM_A}")
        print(f"         Expected DIM_A <= GEMM_SIZE_A / SPLIT_A = {GEMM_SIZE_A} / {SPLIT_A} = {expected_dim_a}")
        print(f"         Effective DIM_A = min({DIM_A_orig}, {expected_dim_a}) = {DIM_A}")
    
    if DIM_AB_orig != DIM_AB:
        print(f"\n[WARNING] DIM_AB was set to {DIM_AB_orig} in config, but is always GEMM_SIZE_AB // CASC_LN_AB = {DIM_AB}")
    
    if DIM_B_orig != DIM_B:
        print(f"\n[WARNING] DIM_B was set to {DIM_B_orig} in config, but was automatically corrected to {DIM_B}")
        print(f"         Expected DIM_B <= GEMM_SIZE_B / SPLIT_B = {GEMM_SIZE_B} / {SPLIT_B} = {expected_dim_b}")
        print(f"         Effective DIM_B = min({DIM_B_orig}, {expected_dim_b}) = {DIM_B}")
    
    # Formula: GRAPH_ITER_CNT = (GEMM_SIZE_A × (GEMM_SIZE_B / SPLIT_B)) / (DIM_A × DIM_B)
    # Each output file contains: GEMM_SIZE_A rows × (GEMM_SIZE_B / SPLIT_B) columns
    # Each iteration produces: DIM_A × DIM_B elements per file
    GRAPH_ITER_CNT = (GEMM_SIZE_A * GEMM_SIZE_B // SPLIT_B) // (DIM_A * DIM_B)
    
    # Use sub-tile values from config file (already read by read_all_parameters_from_config)
    sub_tile_a = params.get('SUB_TILE_A', 4)
    sub_tile_ab = params.get('SUB_TILE_AB', 4)
    sub_tile_b = params.get('SUB_TILE_B', 4)
    # Compute counts per tile for reporting (for each matrix type)
    sub_tiles_A_row = max(1, DIM_A // sub_tile_a)  # Matrix A (A×AB): rows use DIM_A
    sub_tiles_A_col = max(1, DIM_AB // sub_tile_ab)  # Matrix A (A×AB): cols use DIM_AB
    sub_tiles_B_row = max(1, DIM_AB // sub_tile_ab)  # Matrix B (AB×B): rows use DIM_AB
    sub_tiles_B_col = max(1, DIM_B // sub_tile_b)  # Matrix B (AB×B): cols use DIM_B
    sub_tiles_C_row = max(1, DIM_A // sub_tile_a)  # Matrix C (A×B): rows use DIM_A
    sub_tiles_C_col = max(1, DIM_B // sub_tile_b)  # Matrix C (A×B): cols use DIM_B
    
    print(f"\nConfiguration Summary:")
    print(f"GEMM_SIZE_A: {GEMM_SIZE_A}, GEMM_SIZE_AB: {GEMM_SIZE_AB}, GEMM_SIZE_B: {GEMM_SIZE_B}")
    print(f"DIM_A: {DIM_A} (min({DIM}, {GEMM_SIZE_A}/{SPLIT_A})), DIM_AB: {DIM_AB} ({GEMM_SIZE_AB}//{CASC_LN_AB}), DIM_B: {DIM_B} (min({DIM}, {GEMM_SIZE_B}/{SPLIT_B}))")
    print(f"SPLIT_A: {SPLIT_A}, SPLIT_B: {SPLIT_B}")
    print(f"CASC_LN_AB: {CASC_LN_AB}")
    print(f"GRAPH_ITER_CNT: {GRAPH_ITER_CNT} (computed: ({GEMM_SIZE_A}×{GEMM_SIZE_B}/{SPLIT_B})/({DIM_A}×{DIM_B}))")
    print(f"Data Type: {DATA_TYPE}")
    print(f"Sub-Tile Size (A,AB,B): {sub_tile_a}x{sub_tile_ab}x{sub_tile_b}")
    print(f"Sub-Tiles per Tile - Matrix A (A×AB): {sub_tiles_A_row}x{sub_tiles_A_col} (tile: {DIM_A}×{DIM_AB})")
    print(f"Sub-Tiles per Tile - Matrix B (AB×B): {sub_tiles_B_row}x{sub_tiles_B_col} (tile: {DIM_AB}×{DIM_B})")
    print(f"Sub-Tiles per Tile - Matrix C (A×B): {sub_tiles_C_row}x{sub_tiles_C_col} (tile: {DIM_A}×{DIM_B})")
    print(f"Word Length (WRD_LN): {WRD_LN} elements per 128-bit PLIO (calculated: 128/{DATA_TYPE} bits)")
    print(f"Target HW EMU: {TARGET_HW_EMU}")
    print(f"Iteration Count: {ITER_CNT}")
    print(f"N Samples: {N_SAMPLES}")
    print(f"GEMM Instances: {GEMM_INSTS}")
    print(f"Enable Trace: {EN_TRACE}")
    print(f"Random Seed: {random_seed}")
    
    valid_dims = list_valid_tp_dims(GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B, DIM_A, DIM_AB, DIM_B, SPLIT_A, SPLIT_B, CASC_LN_AB, DATA_TYPE, sub_tile_a, sub_tile_ab, sub_tile_b)
    if valid_dims:
        print("Valid (TP_DIM_A, TP_DIM_AB, TP_DIM_B) choices:")
        print(", ".join([f"({a},{ab},{b})" for (a,ab,b) in valid_dims]))
    else:
        print("Valid (TP_DIM_A, TP_DIM_AB, TP_DIM_B) choices: none under current CASC_LN/K-tile constraints")
    
    # Create output directory
    path = f"gemm_{GEMM_SIZE_A}x{GEMM_SIZE_AB}x{GEMM_SIZE_B}_ioFiles"
    os.makedirs(path, exist_ok=True)
    
    # Generate matrices
    matrix_A, matrix_B, matrix_C = generate_sequential_matrix_files(
        GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B, path, 
        matrix_a_file=matrix_a_file, matrix_b_file=matrix_b_file,
        data_type=DATA_TYPE, random_seed=random_seed
    )
    
    # Generate the split output files
    # Matrix C: files split by SPLIT_B (columns), blocks within files split by SPLIT_A (rows)
    generate_split_output_files(matrix_C, SPLIT_B, SPLIT_A, DIM_A, DIM_B, WRD_LN, path, DATA_TYPE, sub_tile_a, sub_tile_b)
    
    # Generate cascade input files
    generate_cascade_input_files(matrix_A, matrix_B, GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B, SPLIT_A, SPLIT_B, CASC_LN_AB, DIM_A, DIM_AB, DIM_B, WRD_LN, path, DATA_TYPE, sub_tile_a, sub_tile_ab, sub_tile_b)
    
    # Generate consolidated golden files
    print(f"\nGenerating consolidated golden files:")
    
    # Generate a_golden.txt from all a0_casc*.txt files (row-interleave: line r of a0_casc0..7 -> lines r*8..r*8+7 of a_golden).
    # Each line = WRD_LN elements = one 128-bit word; same element sequence as HLS inp_A streams, in chunks of 128-bit words.
    matA_file_list = [f"a0_casc{casc}.txt" for casc in range(CASC_LN_AB)]
    generate_a_golden(path, matA_file_list)
    
    # Generate b_golden.txt from all b*_casc*.txt files
    matB_file_list = []
    for split_idx in range(SPLIT_B):
        for casc in range(CASC_LN_AB):
            matB_file_list.append(f"b{split_idx}_casc{casc}.txt")
    generate_b_golden(path, matB_file_list)

    # Generate c_golden.txt from all c*_golden.txt files (row-interleave: row0 from c0, row1 from c1, ...)
    generate_c_golden(path, SPLIT_B)

    print(f"\nComplete data generation completed successfully!")
    print(f"Output directory: {path}")
    print(f"Matrix dimensions: A={matrix_A.shape}, B={matrix_B.shape}, C={matrix_C.shape}")
    print(f"PLIO Optimization: {DATA_TYPE} with {sub_tile_a}x{sub_tile_ab}x{sub_tile_b} sub-tiles (AI Engine-ML instruction set)")
    
    print(f"\nGenerated files:")
    print(f"  - matrix_A_input.txt")
    print(f"  - matrix_B_input.txt") 
    print(f"  - matrix_C_golden.txt")
    for i in range(SPLIT_B):
        print(f"  - c{i}_golden.txt")
    for i in range(CASC_LN_AB):
        print(f"  - a0_casc{i}.txt")
    for split_idx in range(SPLIT_B):
        for casc in range(CASC_LN_AB):
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
