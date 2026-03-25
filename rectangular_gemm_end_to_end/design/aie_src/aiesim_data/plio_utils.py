#!/usr/bin/env python3
"""
PLIO Data Generator Utilities
============================

Utility functions for AIE GEMM simulation data generation.
All functions extracted from plioGen.py for better code organization.

Data Ordering Summary Table:
Level                   | Matrix A | Matrix B | Matrix C
------------------------|----------|----------|----------
Elements within sub-tiles | Row-major | Row-major | Row-major
Sub-tiles within tiles    | Row-major | Row-major | Row-major
Tiles within blocks       | Row-major | Column-major | Column-major

Sub-tile sizes: SUB_TILE_A=4, SUB_TILE_AB=4 for all. For Matrix C/B use SUB_TILE_B from
config.json (defaults: int16 → 4×4×4, int32 → 4×4×4 for this project; must divide DIM_B).
"""

import os
import sys
import numpy as np
import json


def read_all_parameters_from_config(config_file_path):
    """
    Read all parameters from config.json file.
    Returns a dictionary with all configuration parameters.
    """
   
    # Initialize default values (GEMM_SIZE_A/AB/B are required and must be provided)
    params = {
        'DIM': 16,
        'DIM_A': 16,
        'DIM_AB': 4,
        'DIM_B': 16,
        'DATA_TYPE': 'int16',
        'WRD_LN': 8,
        'SUB_TILE_A': 4,
        'SUB_TILE_AB': 4,
        'SUB_TILE_B': 4,
        # GRAPH_ITER_CNT set after DIM_A/DIM_B (same formula as Makefile / gemm_config.h)
        'GRAPH_ITER_CNT': 0,
        'SPLIT': 2,
        'CASC_LN': 8,
        'TARGET_HW_EMU': 1,
        'ITER_CNT': 1,
        'N_SAMPLES': 1,
        'GEMM_INSTS': 1,
        'EN_TRACE': 0,
        'TILE_MEM_BYTES': 32768,
        'PL_FREQ': 312.5,
        'ENABLE_ML_BENCHMARKS': 0,
        'MATRIX_A_FILE': "",
        'MATRIX_B_FILE': "",
        'RANDOM_SEED': 42,
        'TARGET': "hw"
    }
    
    try:
        with open(config_file_path, 'r') as f:
            config_data = json.load(f)
        
        # Map JSON keys to our parameter names (only user-specified parameters)
        # Read GEMM_SIZE_A, AB, B (REQUIRED - must be explicitly provided)
        v = config_data.get('GEMM_SIZE_A')
        if v is None:
            raise ValueError("GEMM_SIZE_A is required in config.json. Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B.")
        params['GEMM_SIZE_A'] = int(v)
        v = config_data.get('GEMM_SIZE_AB')
        if v is None:
            raise ValueError("GEMM_SIZE_AB is required in config.json. Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B.")
        params['GEMM_SIZE_AB'] = int(v)
        v = config_data.get('GEMM_SIZE_B')
        if v is None:
            raise ValueError("GEMM_SIZE_B is required in config.json. Please provide GEMM_SIZE_A, GEMM_SIZE_AB, and GEMM_SIZE_B.")
        params['GEMM_SIZE_B'] = int(v)
        v = config_data.get('DIM');        
        if v is not None: params['DIM'] = int(v)
        
        # Read new parameter names (with backward compatibility) FIRST, before calculating DIM_A/AB/B
        v = config_data.get('SPLIT_A')
        if v is not None: params['SPLIT_A'] = int(v)
        elif config_data.get('SPLIT') is not None: params['SPLIT_A'] = int(config_data.get('SPLIT'))
        
        v = config_data.get('SPLIT_B')
        if v is not None: params['SPLIT_B'] = int(v)
        elif config_data.get('SPLIT') is not None: params['SPLIT_B'] = int(config_data.get('SPLIT'))
        
        v = config_data.get('CASC_LN_AB')
        if v is not None: params['CASC_LN_AB'] = int(v)
        elif config_data.get('CASC_LN') is not None: params['CASC_LN_AB'] = int(config_data.get('CASC_LN'))
        
        # For backward compatibility, also set SPLIT and CASC_LN
        if 'SPLIT' not in params:
            params['SPLIT'] = params.get('SPLIT_B', 2)
        if 'CASC_LN' not in params:
            params['CASC_LN'] = params.get('CASC_LN_AB', 8)
        
        # Calculate DIM_A, DIM_B as min(DIM, ...); DIM_AB is always GEMM_SIZE_AB // CASC_LN_AB
        # This ensures DIM doesn't exceed the split/cascade sizes
        # NOTE: This must happen AFTER reading SPLIT_A/B and CASC_LN_AB from config
        if params.get('DIM') is not None:
            dim_base = params['DIM']
            # DIM_A = min(DIM, GEMM_SIZE_A / SPLIT_A)
            if params.get('GEMM_SIZE_A') is not None and params.get('SPLIT_A') is not None:
                params['DIM_A'] = min(dim_base, params['GEMM_SIZE_A'] // max(1, params['SPLIT_A']))
            elif params.get('GEMM_SIZE_A') is not None and params.get('SPLIT') is not None:
                params['DIM_A'] = min(dim_base, params['GEMM_SIZE_A'] // max(1, params['SPLIT']))
            else:
                params['DIM_A'] = dim_base
            # DIM_AB is always GEMM_SIZE_AB // CASC_LN_AB
            if params.get('GEMM_SIZE_AB') is not None and params.get('CASC_LN_AB') is not None:
                params['DIM_AB'] = params['GEMM_SIZE_AB'] // max(1, params['CASC_LN_AB'])
            elif params.get('GEMM_SIZE_AB') is not None and params.get('CASC_LN') is not None:
                params['DIM_AB'] = params['GEMM_SIZE_AB'] // max(1, params['CASC_LN'])
            else:
                params['DIM_AB'] = params.get('GEMM_SIZE_AB', 64) // max(1, params.get('CASC_LN_AB', params.get('CASC_LN', 8)))
            # DIM_B = min(DIM, GEMM_SIZE_B / SPLIT_B)
            if params.get('GEMM_SIZE_B') is not None and params.get('SPLIT_B') is not None:
                params['DIM_B'] = min(dim_base, params['GEMM_SIZE_B'] // max(1, params['SPLIT_B']))
            elif params.get('GEMM_SIZE_B') is not None and params.get('SPLIT') is not None:
                params['DIM_B'] = min(dim_base, params['GEMM_SIZE_B'] // max(1, params['SPLIT']))
            else:
                params['DIM_B'] = dim_base
        v = config_data.get('ITER_CNT');   
        if v is not None: params['ITER_CNT'] = int(v)
        v = config_data.get('N_SAMPLES');  
        if v is not None: params['N_SAMPLES'] = int(v)
        v = config_data.get('GEMM_INSTS'); 
        if v is not None: params['GEMM_INSTS'] = int(v)
        v = config_data.get('EN_TRACE');   
        if v is not None: params['EN_TRACE'] = int(v)
        
        # Set TARGET and TARGET_HW_EMU based on TARGET field
        if 'TARGET' in config_data:
            params['TARGET'] = config_data['TARGET']
            if config_data['TARGET'] == 'hw_emu':
                params['TARGET_HW_EMU'] = 1
            else:
                params['TARGET_HW_EMU'] = 0
        
        # Set DATA_TYPE from config file
        if 'DATA_TYPE' in config_data and config_data['DATA_TYPE'] is not None:
            params['DATA_TYPE'] = config_data['DATA_TYPE']
        else:
            # Default to int16 if not specified
            params['DATA_TYPE'] = 'int16'

        if params['DATA_TYPE'] == 'float':
            raise ValueError("DATA_TYPE 'float' is not supported on this platform; use 'int16' or 'int32'.")

        # WRD_LN: explicit config wins (must match Makefile); else derive from DATA_TYPE
        _expected_wl = get_word_length(params['DATA_TYPE'])
        v = config_data.get('WRD_LN')
        if v is not None:
            params['WRD_LN'] = int(v)
            if params['WRD_LN'] != _expected_wl:
                print(
                    f"WARNING: WRD_LN={params['WRD_LN']} in config != type-default {_expected_wl} "
                    f"for DATA_TYPE={params['DATA_TYPE']}. Using config value (keep aligned with Makefile gemm_config.h)."
                )
        else:
            params['WRD_LN'] = _expected_wl

        # Read sub-tile values from config file, fallback to calculated values if not specified
        v = config_data.get('SUB_TILE_A')
        if v is not None: 
            params['SUB_TILE_A'] = int(v)
        else:
            sub_tile_a, _, _ = get_optimal_sub_tile_size(params['DATA_TYPE'])
            params['SUB_TILE_A'] = sub_tile_a
            
        v = config_data.get('SUB_TILE_AB')
        if v is not None: 
            params['SUB_TILE_AB'] = int(v)
        else:
            _, sub_tile_ab, _ = get_optimal_sub_tile_size(params['DATA_TYPE'])
            params['SUB_TILE_AB'] = sub_tile_ab
            
        _, _, _default_sub_b = get_optimal_sub_tile_size(params['DATA_TYPE'])
        v = config_data.get('SUB_TILE_B')
        if v is not None:
            params['SUB_TILE_B'] = int(v)
        else:
            params['SUB_TILE_B'] = _default_sub_b
        
        # Set TILE_MEM_BYTES from config
        v = config_data.get('TILE_MEM_BYTES')
        if v is not None: params['TILE_MEM_BYTES'] = int(v)
        
        # Set PL_FREQ from config
        v = config_data.get('PL_FREQ')
        if v is not None: params['PL_FREQ'] = float(v)
        
        # Set ENABLE_ML_BENCHMARKS from config
        v = config_data.get('ENABLE_ML_BENCHMARKS')
        if v is not None: params['ENABLE_ML_BENCHMARKS'] = int(v)
        
        # Set matrix file paths from config
        if 'MATRIX_A_FILE' in config_data and config_data['MATRIX_A_FILE'] is not None:
            params['MATRIX_A_FILE'] = config_data['MATRIX_A_FILE']
        if 'MATRIX_B_FILE' in config_data and config_data['MATRIX_B_FILE'] is not None:
            params['MATRIX_B_FILE'] = config_data['MATRIX_B_FILE']
        # Set random seed from config
        v = config_data.get('RANDOM_SEED')
        if v is not None: params['RANDOM_SEED'] = int(v)

        # GRAPH_ITER_CNT: identical to Makefile, gemm_config.h, plioGen, and graph.run()
        # (GEMM_SIZE_A * GEMM_SIZE_B / SPLIT_B) / (DIM_A * DIM_B); integer ops match shell $((...))
        _iter = int(params.get('ITER_CNT', 1))
        if _iter == -1:
            params['GRAPH_ITER_CNT'] = -1
        else:
            _ga = params['GEMM_SIZE_A']
            _gb = params['GEMM_SIZE_B']
            _sb = int(params.get('SPLIT_B', params.get('SPLIT', 2)))
            _da = int(params['DIM_A'])
            _db = int(params['DIM_B'])
            _computed = (_ga * _gb // max(1, _sb)) // max(1, _da * _db)
            _json_gic = config_data.get('GRAPH_ITER_CNT')
            if _json_gic is not None:
                try:
                    if int(_json_gic) != _computed:
                        print(
                            f"WARNING: config.json GRAPH_ITER_CNT={_json_gic} != computed {_computed} "
                            f"((GEMM_SIZE_A*GEMM_SIZE_B/SPLIT_B)/(DIM_A*DIM_B)); using computed value."
                        )
                except (TypeError, ValueError):
                    pass
            params['GRAPH_ITER_CNT'] = _computed

        print(f"Successfully read parameters from config file:")
        for key, value in params.items():
            print(f"  {key}: {value}")
        
        return params
        
    except FileNotFoundError:
        print(f"Warning: Config file not found: {config_file_path}, using defaults")
        return params
    except Exception as e:
        print(f"Error reading config file: {e}, using defaults")
        return params


def get_word_length(data_type="int16"):
    """
    Calculate the word length (WRD_LN) based on data type.
    WRD_LN = 128 bits / data_type_bits
    
    Equation: WRD_LN = 128 / (data_type_bit_width)
    """
    if data_type in ["int16", "uint16"]:
        return 8   # 128 / 16 = 8 elements per 128-bit PLIO stream
    elif data_type in ["int32", "uint32"]:
        return 4   # 128 / 32 = 4 elements per 128-bit PLIO stream
    else:
        return 8   # Default to int16


def get_optimal_sub_tile_size(data_type):
    """
    Default sub-tile sizes (M, K, N) when config.json omits SUB_TILE_*.
    Project standard: 4×4×4 for both int16 and int32 (must match gemm_config.h / dma_hls).
    """
    if data_type in ["int16", "uint16", "int32", "uint32"]:
        return 4, 4, 4
    return 4, 4, 4


def get_random_range(data_type="int16"):
    """
    Get safe random value range to prevent overflow.
    """
    if data_type in ["int16", "uint16"]:
        return -8, 8
    elif data_type in ["int32", "uint32"]:
        return -16, 16
    else:
        return -8, 8


def get_numpy_dtype(data_type):
    """
    Get numpy data type for the specified data type.
    """
    if data_type in ["int16", "uint16"]:
        return np.int16
    elif data_type in ["int32", "uint32"]:
        return np.int32
    else:
        return np.int16


def generate_sequential_matrix_files(size_a, size_ab, size_b, path, matrix_a_file=None, matrix_b_file=None, data_type="int16", random_seed=42):
    """Generate input matrix files and golden output with safe random values
    
    Args:
        size_a (int): Number of rows for matrix A (and result C) - A dimension
        size_ab (int): Number of columns for matrix A and rows for matrix B - AB dimension
        size_b (int): Number of columns for matrix B (and result C) - B dimension
        path (str): Output directory path
        matrix_a_file (str, optional): Path to matrix A input file
        matrix_b_file (str, optional): Path to matrix B input file
        data_type (str): Data type for matrices
        random_seed (int): Random seed for reproducible generation
    """
    
    # Set random seed for reproducible results
    np.random.seed(random_seed)
    print(f"Using random seed: {random_seed} for reproducible matrix generation")
    print(f"Matrix dimensions: A={size_a}x{size_ab}, B={size_ab}x{size_b}, C={size_a}x{size_b}")
    
    numpy_dtype = get_numpy_dtype(data_type)
    min_val, max_val = get_random_range(data_type)
    
    # Create zero-initialized matrices with correct dimensions
    # A is A×AB, B is AB×B, C is A×B
    matrix_A = np.zeros((size_a, size_ab), dtype=numpy_dtype)
    matrix_B = np.zeros((size_ab, size_b), dtype=numpy_dtype)
    
    # Check if files are provided for loading matrices
    if matrix_a_file:
        try:
            print(f"Loading Matrix A from {matrix_a_file}")
            loaded_A = np.loadtxt(matrix_a_file, dtype=numpy_dtype)
            
            # Check loaded dimensions and apply data to matrix_A
            loaded_rows, loaded_cols = loaded_A.shape
            actual_rows = min(loaded_rows, size_a)
            actual_cols = min(loaded_cols, size_ab)
            
            matrix_A[:actual_rows, :actual_cols] = loaded_A[:actual_rows, :actual_cols]
            print(f"Loaded Matrix A: {loaded_rows}x{loaded_cols}, Used: {actual_rows}x{actual_cols}")
        except Exception as e:
            print(f"Error loading Matrix A: {str(e)}")
            print("Generating random Matrix A instead")
            matrix_A[:size_a, :size_ab] = np.random.randint(min_val, max_val + 1, size=(size_a, size_ab), dtype=numpy_dtype)
    else:
        # Fill the actual data areas with random values
        matrix_A[:size_a, :size_ab] = np.random.randint(min_val, max_val + 1, size=(size_a, size_ab), dtype=numpy_dtype)
    
    if matrix_b_file:
        try:
            print(f"Loading Matrix B from {matrix_b_file}")
            loaded_B = np.loadtxt(matrix_b_file, dtype=numpy_dtype)
            
            # Check loaded dimensions and apply data to matrix_B
            loaded_rows, loaded_cols = loaded_B.shape
            actual_rows = min(loaded_rows, size_ab)
            actual_cols = min(loaded_cols, size_b)
            
            matrix_B[:actual_rows, :actual_cols] = loaded_B[:actual_rows, :actual_cols]
            print(f"Loaded Matrix B: {loaded_rows}x{loaded_cols}, Used: {actual_rows}x{actual_cols}")
        except Exception as e:
            print(f"Error loading Matrix B: {str(e)}")
            print("Generating random Matrix B instead")
            matrix_B[:size_ab, :size_b] = np.random.randint(min_val, max_val + 1, size=(size_ab, size_b), dtype=numpy_dtype)
    else:
        # Fill the actual data areas with random values
        matrix_B[:size_ab, :size_b] = np.random.randint(min_val, max_val + 1, size=(size_ab, size_b), dtype=numpy_dtype)
    
    # Calculate golden output C = A × B
    if data_type in ["int16", "uint16"]:
        # Use int64 to prevent overflow, then cast back to int16
        matrix_C = np.matmul(matrix_A.astype(np.int64), matrix_B.astype(np.int64)).astype(np.int16)
    elif data_type in ["int32", "uint32"]:
        # Use int64 to prevent overflow, then cast back to int32
        matrix_C = np.matmul(matrix_A.astype(np.int64), matrix_B.astype(np.int64)).astype(np.int32)
    else:
        # Use int64 to prevent overflow, then cast back to int32
        matrix_C = np.matmul(matrix_A.astype(np.int64), matrix_B.astype(np.int64)).astype(np.int32)

    # Save matrices with appropriate format
    fmt_a = '%d'
    fmt_b = '%d'
    fmt_c = '%d'
    
    np.savetxt(os.path.join(path, 'matrix_A_input.txt'), matrix_A, fmt=fmt_a)
    np.savetxt(os.path.join(path, 'matrix_B_input.txt'), matrix_B, fmt=fmt_b)
    np.savetxt(os.path.join(path, 'matrix_C_golden.txt'), matrix_C, fmt=fmt_c)
    
    print("\nMatrix dimensions:")
    print(f"A: {matrix_A.shape}")
    print(f"B: {matrix_B.shape}")
    print(f"C: {matrix_C.shape}")
    
    return matrix_A, matrix_B, matrix_C


def generate_split_output_files(matrix_C, split_b, split_a, dim_a, dim_b, wrd_ln, path, data_type, sub_tile_a=None, sub_tile_b=None):
    """
    Generate output files c0, c1, ..., c{split_b-1} from matrix C = A @ B.

    Split by COLUMNS: c0 = left half of C, c1 = right half (SPLIT_B divides columns).
    Within each file: blocks of size (rows_per_block × cols_per_file) with rows_per_block = GEMM_SIZE_A/SPLIT_A,
    cols_per_file = GEMM_SIZE_B/SPLIT_B. Tiles within each block are dim_a × dim_b, written in column-major order.
    Elements within sub-tiles and sub-tiles within tiles are row-major.

    Data Ordering Summary Table:
    Level                   | Matrix C
    ------------------------|----------
    Elements within sub-tiles | Row-major
    Sub-tiles within tiles    | Row-major
    Tiles within blocks       | Column-major
    Blocks within file        | Column-major (by row index)
    """
    rows, cols = matrix_C.shape
    cols_per_file = cols // split_b
    rows_per_block = rows // split_a

    if sub_tile_a is None:
        sub_tile_a, _, _ = get_optimal_sub_tile_size(data_type)
    if sub_tile_b is None:
        _, _, sub_tile_b = get_optimal_sub_tile_size(data_type)

    tiles_per_block_row = rows_per_block // dim_a
    tiles_per_block_col = cols_per_file // dim_b
    sub_tiles_per_row = max(1, dim_a // sub_tile_a)
    sub_tiles_per_col = max(1, dim_b // sub_tile_b)

    print(f"\nGenerating split output files (column-split, blocks, tiles column-major) with SPLIT_A={split_a}, SPLIT_B={split_b}, DIM_A={dim_a}, DIM_B={dim_b}")
    print(f"Matrix C shape: {rows}x{cols}")
    print(f"Files: {split_b} files (c0=left cols 0..{cols_per_file-1}, c1=right cols {cols_per_file}..{cols-1})")
    print(f"Blocks per file: {split_a} (each {rows_per_block}×{cols_per_file}), tiles per block: {tiles_per_block_row}×{tiles_per_block_col} (column-major), sub-tiles/elements row-major")

    for sb in range(split_b):
        filename = os.path.join(path, f"c{sb}_golden.txt")
        file_lines = []
        file_start_col = sb * cols_per_file
        file_end_col = (sb + 1) * cols_per_file

        for block_row in range(split_a):
            block_start_row = block_row * rows_per_block
            block_end_row = (block_row + 1) * rows_per_block

            # Tiles within block: column-major (tile_col outer, tile_row inner)
            for tile_col in range(tiles_per_block_col):
                for tile_row in range(tiles_per_block_row):
                    tile_start_row = block_start_row + tile_row * dim_a
                    tile_end_row = tile_start_row + dim_a
                    tile_start_col = file_start_col + tile_col * dim_b
                    tile_end_col = tile_start_col + dim_b
                    tile_elements = []
                    # Sub-tiles within tile: row-major; elements within sub-tile: row-major
                    for sub_row in range(sub_tiles_per_row):
                        for sub_col in range(sub_tiles_per_col):
                            sub_start_row = tile_start_row + sub_row * sub_tile_a
                            sub_end_row = min(sub_start_row + sub_tile_a, tile_end_row)
                            sub_start_col = tile_start_col + sub_col * sub_tile_b
                            sub_end_col = min(sub_start_col + sub_tile_b, tile_end_col)
                            for r in range(sub_start_row, sub_end_row):
                                for c in range(sub_start_col, sub_end_col):
                                    tile_elements.append(str(int(matrix_C[r, c])))
                    for i in range(0, len(tile_elements), wrd_ln):
                        chunk = tile_elements[i : i + wrd_ln]
                        while len(chunk) < wrd_ln:
                            chunk.append("0")
                        file_lines.append(" ".join(chunk) + "\n")

        with open(filename, "w") as f:
            f.writelines(file_lines)

        expected_lines = split_a * tiles_per_block_row * tiles_per_block_col * (dim_a * dim_b // wrd_ln)
        with open(filename, "r") as f:
            lines = f.readlines()
        for i, line in enumerate(lines, 1):
            line_stripped = line.strip()
            if not line_stripped:
                continue
            if any(keyword in line_stripped for keyword in ["Updated", "mkdir", "GRAPH_ITER_CNT=", "GEMM_SIZE", "DIM_", "SPLIT"]):
                print(f"WARNING: {filename} line {i} contains debug output: {line_stripped[:80]}")
                break
        print(f"Generated {filename}: {len(lines)} lines (expected: {expected_lines})")
        print(f"  File: {split_a} blocks, {rows_per_block}×{cols_per_file} each, tiles {dim_a}×{dim_b} column-major")


def write_matrix_A_cascade(matrix_A, casc_idx, filename, gemm_size_a, gemm_size_ab, gemm_size_b, split_a, split_b, casc_ln_ab, dim_a, dim_ab, dim_b, wrd_ln, data_type, sub_tile_a=None, sub_tile_ab=None):
    """Write Matrix A cascade strip with broadcasting.
    
    Generation is ELEMENT-BY-ELEMENT (not 8-by-8): we iterate block → row-tiles (one full cascade
    stripe wide) → sub-tiles row-major → elements row-major and append each element to a list.
    Only when writing the file
    do we chunk into lines of WRD_LN (8) elements = one 128-bit word per line. So the output file
    matches the HLS stream format (128-bit words = WRD_LN elements per line); at the end the
    element sequence is the same as inp_A produces, in chunks of 128-bit words.
    
    Data Ordering Summary Table:
    Level                   | Matrix A
    ------------------------|----------
    Elements within sub-tiles | Row-major
    Sub-tiles within tiles    | Row-major
    Tiles within blocks       | Row-major
    
    - Each cascade gets GEMM_SZ_CASC columns
    - Write in chunks of WRD_LN (8) = one 128-bit word per line
    - Each block has dimensions of GEMM_SZ_SPLIT * GEMM_SZ_CASC
    - Elements within tiles and between tiles are written in row-major order
    - After finishing writing elements of each block, we first broadcast broadcast_count times
    - Then move to the next block
    - Consistent 4×4 sub-tiling for int16 data type regardless of DIM value
    - Sequential block processing with immediate broadcasting
    """
    try:
        # Calculate dimensions (A is A×AB, cascade splits AB dimension)
        cols_per_casc = gemm_size_ab // casc_ln_ab
        start_col = casc_idx * cols_per_casc
        end_col = start_col + cols_per_casc
        
        print(f"\nWriting {filename}")
        print(f"Cascade {casc_idx}: Columns {start_col} to {end_col-1}")
        
        # Effective tile: rows limited by split; AB width = full cascade stripe (matches dma_hls dim_ab_eff = cols_per_casc).
        dim_a_eff = min(dim_a, gemm_size_a // split_a)
        dim_b_eff = cols_per_casc
        
        # Use provided sub-tile values or fallback to calculated values
        if sub_tile_a is None:
            sub_tile_a, _, _ = get_optimal_sub_tile_size(data_type)
        if sub_tile_ab is None:
            _, sub_tile_ab, _ = get_optimal_sub_tile_size(data_type)
        sub_tiles_per_row = max(1, dim_a_eff // sub_tile_a)
        sub_tiles_per_col = max(1, dim_b_eff // sub_tile_ab)
        
        print(f"  Effective tile dimensions: {dim_a_eff}x{dim_b_eff} (DIM_A={dim_a}, DIM_AB={dim_ab}, SPLIT_A_SIZE={gemm_size_a // split_a}, CASC_AB_SIZE={cols_per_casc})")
        
        # Calculate broadcast iterations - Matrix A: broadcast after each block
        # Broadcast count = number of DIM_B tiles in half of Matrix B = (GEMM_SIZE_B / SPLIT_B) / DIM_B
        # This matches the output column width per split, ensuring correct data flow
        # Matrix A is broadcast to all splits, and each split outputs (GEMM_SIZE_B / SPLIT_B) columns
        # The broadcast count must match how many DIM_B tiles fit in that output width
        cols_per_split = gemm_size_b // split_b
        dim_b_eff_for_broadcast = min(dim_b, cols_per_split)
        tiles_per_output_col = cols_per_split // dim_b_eff_for_broadcast
        broadcast_count = max(1, tiles_per_output_col)
        
        print(f"  Broadcast count: {broadcast_count} (DIM_B tiles in output width: {tiles_per_output_col})")
        
        with open(filename, 'w') as file:
            # In a cascade, process each block separately
            for block_idx in range(split_a):
                # Calculate block boundaries for this split block
                block_start_row = block_idx * (gemm_size_a // split_a)
                block_end_row = block_start_row + (gemm_size_a // split_a)
                
                # Collect all elements for this block in row-major order
                block_elements = []
                
                tiles_per_row = (gemm_size_a // split_a) // dim_a_eff
                tiles_per_col = 1  # one tile spans full cascade column range [start_col, end_col)

                print(f"  Block {block_idx}: {tiles_per_row}x{tiles_per_col} tiles of size {dim_a_eff}x{dim_b_eff}")
                print(f"  Sub-tiles: {sub_tiles_per_row}x{sub_tiles_per_col} sub-tiles of size {sub_tile_a}x{sub_tile_ab} (A uses A×AB)")

                tile_start_col = start_col
                tile_end_col = end_col
                for tile_row_idx in range(tiles_per_row):
                    tile_start_row = block_start_row + tile_row_idx * dim_a_eff
                    tile_end_row = min(tile_start_row + dim_a_eff, block_end_row)

                    for sub_row in range(sub_tiles_per_row):
                        for sub_col in range(sub_tiles_per_col):
                            sub_start_row = tile_start_row + sub_row * sub_tile_a
                            sub_end_row = min(sub_start_row + sub_tile_a, tile_end_row)
                            sub_start_col = tile_start_col + sub_col * sub_tile_ab
                            sub_end_col = min(sub_start_col + sub_tile_ab, tile_end_col)

                            for row in range(sub_start_row, sub_end_row):
                                for col in range(sub_start_col, sub_end_col):
                                    if row < gemm_size_a and col < gemm_size_ab:
                                        block_elements.append(str(matrix_A[row, col]))
                
                # Write this block's elements in chunks of exactly WRD_LN
                chunks = []
                for i in range(0, len(block_elements), wrd_ln):
                    chunk = block_elements[i:i+wrd_ln]
                    # If this is the last chunk and not complete, pad with zeros
                    while len(chunk) < wrd_ln:
                        chunk.append('0')
                    chunks.append(chunk)
                    file.write(' '.join(chunk) + '\n')
                
                # Immediately broadcast this block's data
                for _ in range(broadcast_count - 1):
                    for chunk in chunks:
                        file.write(' '.join(chunk) + '\n')
                
                print(f"  Processed and broadcast block {block_idx} ({len(block_elements)} elements)")

        # Verify file was written correctly
        with open(filename, 'r') as f:
            lines = f.readlines()
            elements_per_row = len(lines[0].strip().split())
            # Calculate expected lines based on reference formula: MATA_SZ = ((GEMM_SIZE_A * GEMM_SIZE_AB / CASC_LN_AB) / 8) * iterCnt * ((GEMM_SIZE_A/SPLIT_A) / DIM_A)
            expected_lines = ((gemm_size_a * gemm_size_ab // casc_ln_ab) // wrd_ln) * 1 * ((gemm_size_a // split_a) // dim_a_eff)
            print(f"\nFile verification for {filename}:")
            print(f"Written {len(lines)} lines")
            print(f"Expected lines: {expected_lines} (based on reference formula)")
            print(f"Elements per row: {elements_per_row} (should be {wrd_ln})")
            print(f"Broadcast count: {broadcast_count}")
            print(f"Effective tile dimensions: {dim_a_eff}x{dim_b_eff}")
            print(f"Tiles per block: {tiles_per_row}x{tiles_per_col}")
            print(f"Reference MATA_SZ calculation: {expected_lines}")
        
        return True
        
    except Exception as e:
        print(f"\nError writing to {filename}: {str(e)}")
        print("Debug info:")
        print(f"Matrix A shape: {matrix_A.shape}")
        print(f"Cascade index: {casc_idx}")
        return False


def write_matrix_B_split_cascade(matrix_B, split_idx, casc_idx, filename, gemm_size_a, gemm_size_ab, gemm_size_b, split_a, split_b, casc_ln_ab, dim_a, dim_ab, dim_b, wrd_ln, data_type, sub_tile_ab=None, sub_tile_b=None):
    """Write Matrix B split-cascade block with broadcasting:
    
    Data Ordering Summary Table:
    Level                   | Matrix B
    ------------------------|----------
    Elements within sub-tiles | Row-major
    Sub-tiles within tiles    | Row-major
    Tiles within blocks       | Column-major
    
    - Each block is processed by one AIE core
    - Tiles processed in column-major order (column-major between tiles)
    - Elements within tiles processed in row-major order
    - Elements collected as continuous stream across rows/columns
    - Written in chunks of WRD_LN (8)
    - Broadcasting happens after each column of tiles is written
        - Broadcast count per column = (GEMM_SIZE_B / split_val) / DIM_B
        - This is the number of tiles per column in the block (columns split by split_val)
    - After writing all columns, the entire file is repeated SPLIT_A times
    - Consistent 4×4 sub-tiling for int16 data type regardless of DIM value
    """
    try:
        # Calculate dimensions (B is AB×B, cascade splits AB dimension, split divides B dimension)
        rows_per_casc = gemm_size_ab // casc_ln_ab
        cols_per_split = gemm_size_b // split_b
        
        # Calculate block boundaries
        start_row = casc_idx * rows_per_casc
        end_row = start_row + rows_per_casc
        start_col = split_idx * cols_per_split
        end_col = start_col + cols_per_split
        
        print(f"\nWriting {filename}")
        print(f"Split {split_idx}, Cascade {casc_idx}")
        print(f"Block boundaries: rows {start_row}:{end_row}, cols {start_col}:{end_col}")
        
        # Calculate effective tile dimensions - limited by cascade/split
        dim_a_eff = rows_per_casc  # Tile rows limited by cascade size (uses DIM_AB for AB dimension)
        dim_b_eff = min(dim_b, cols_per_split)  # Tile cols limited by split size
        
        # Use provided sub-tile values or fallback to calculated values
        if sub_tile_ab is None:
            _, sub_tile_ab, _ = get_optimal_sub_tile_size(data_type)
        if sub_tile_b is None:
            _, _, sub_tile_b = get_optimal_sub_tile_size(data_type)
        sub_tiles_per_row = max(1, dim_a_eff // sub_tile_ab)
        sub_tiles_per_col = max(1, dim_b_eff // sub_tile_b)
        
        print(f"  Effective tile dimensions: {dim_a_eff}x{dim_b_eff} (DIM_AB={dim_ab}, DIM_B={dim_b}, CASC_SIZE={rows_per_casc}, SPLIT_SIZE={cols_per_split})")
        print(f"  Sub-tiles: {sub_tiles_per_row}x{sub_tiles_per_col} sub-tiles of size {sub_tile_ab}x{sub_tile_b} (B uses AB×B)")
        
        num_tiles_row = rows_per_casc // dim_a_eff
        num_tiles_col = cols_per_split // dim_b_eff
        
        # Calculate broadcast iterations - Matrix B: broadcast after each column within blocks
        # Broadcast count = number of DIM_A tiles in half of Matrix A = GEMM_SIZE_A / (SPLIT_A * DIM_A)
        # This matches the output row height per split, ensuring correct data flow
        # Matrix B is split by SPLIT_B, and each split processes (GEMM_SIZE_A / SPLIT_A) rows
        # The broadcast count must match how many DIM_A tiles fit in that row height
        rows_per_split = gemm_size_a // split_a
        dim_a_eff_for_broadcast = min(dim_a, rows_per_split)
        tiles_per_output_row = rows_per_split // dim_a_eff_for_broadcast
        broadcast_count = max(1, tiles_per_output_row)
        
        # Create a temporary file for the initial data
        temp_filename = filename + ".temp"
        with open(temp_filename, 'w') as file:
            # Process tiles column by column (column-major between tiles)
            for tile_col in range(num_tiles_col):
                # Collect elements for this column of tiles
                column_elements = []
                
                # Process all tiles in this column
                for tile_row in range(num_tiles_row):
                    # Process sub-tiles within each tile in row-major order (B: AB×B)
                    for sub_row in range(sub_tiles_per_row):
                        for sub_col in range(sub_tiles_per_col):
                            # Calculate sub-tile boundaries (B: AB×B)
                            sub_start_row = start_row + tile_row * dim_a_eff + sub_row * sub_tile_ab
                            sub_end_row = min(sub_start_row + sub_tile_ab, start_row + (tile_row + 1) * dim_a_eff)
                            sub_start_col = start_col + tile_col * dim_b_eff + sub_col * sub_tile_b
                            sub_end_col = min(sub_start_col + sub_tile_b, start_col + (tile_col + 1) * dim_b_eff)
                            
                            # Process elements within sub-tile in row-major order
                            for row in range(sub_start_row, sub_end_row):
                                for col in range(sub_start_col, sub_end_col):
                                    if row < gemm_size_ab and col < gemm_size_b:
                                        column_elements.append(str(matrix_B[row, col]))
                
                # Write column elements in chunks of exactly WRD_LN
                chunks = []
                for i in range(0, len(column_elements), wrd_ln):
                    chunk = column_elements[i:i+wrd_ln]
                    # If this is the last chunk and not complete, pad with zeros
                    while len(chunk) < wrd_ln:
                        chunk.append('0')
                    chunks.append(chunk)
                    file.write(' '.join(chunk) + '\n')
                
                # Broadcast this column immediately after writing it (considering sub-tiles)
                for _ in range(broadcast_count - 1):
                    for chunk in chunks:
                        file.write(' '.join(chunk) + '\n')
        
        # Now read the temporary file and repeat it SPLIT_A times into the final file
        with open(temp_filename, 'r') as temp_file:
            file_content = temp_file.read()
        with open(filename, 'w') as final_file:
            for _ in range(split_a):
                final_file.write(file_content)
        # Remove the temporary file
        os.remove(temp_filename)
        
        # Verify file was written correctly
        with open(filename, 'r') as f:
            lines = f.readlines()
            elements_per_row = len(lines[0].strip().split())
            # Calculate expected lines based on reference formula: MATB_SZ = ((GEMM_SIZE_AB * GEMM_SIZE_B / (CASC_LN*SPLIT)) / WRD_LN) * (GEMM_SIZE_AB / DIM_AB)
            expected_lines = ((gemm_size_ab * gemm_size_b // (casc_ln_ab * split_b)) // wrd_ln) * 1 * (gemm_size_ab // dim_ab)
            print(f"\nFile verification for {filename}:")
            print(f"Written {len(lines)} lines")
            print(f"Expected lines: {expected_lines} (based on reference formula)")
            print(f"Elements per row: {elements_per_row} (should be {wrd_ln})")
            print(f"Per-column broadcast count: {broadcast_count}")
            print(f"Total repeats of the whole file: {split_b}")
            print(f"Effective tile dimensions: {dim_a_eff}x{dim_b_eff}")
            print(f"Number of tiles: {num_tiles_row}x{num_tiles_col}")
            print(f"Reference MATB_SZ calculation: {expected_lines}")
        
        return True
        
    except Exception as e:
        print(f"\nError writing to {filename}: {str(e)}")
        print("Debug info:")
        print(f"Matrix B shape: {matrix_B.shape}")
        if os.path.exists(filename + ".temp"):
            os.remove(filename + ".temp")
        return False


def generate_a_golden(path, matA_file_list):
    """Create a_golden.txt by writing rows in the order: row0 of a0_casc0, row0 of a0_casc1, ..., etc."""
    try:
        # Open all cascade files for reading
        file_paths = [os.path.join(path, fname) for fname in matA_file_list]
        files = [open(fp, 'r') for fp in file_paths]
        try:
            # Read all lines from each file
            all_lines = [f.readlines() for f in files]
        finally:
            for f in files:
                f.close()

        # Ensure there is at least one file
        if not all_lines:
            print("No a0_casc* files found to generate a_golden.txt")
            return

        # Check all files have same number of rows
        num_rows = len(all_lines[0])
        for idx, lines in enumerate(all_lines):
            if len(lines) != num_rows:
                print(f"Warning: Cascade file {matA_file_list[idx]} has {len(lines)} lines; expected {num_rows}")
                num_rows = min(num_rows, len(lines))

        out_path = os.path.join(path, 'a_golden.txt')
        with open(out_path, 'w') as out:
            for row in range(num_rows):
                for casc_idx in range(len(all_lines)):
                    # Write the row as-is (already newline-terminated)
                    out.write(all_lines[casc_idx][row])

        print(f"Generated a_golden.txt with {num_rows * len(all_lines)} lines at: {out_path}")
    except Exception as e:
        print(f"Error generating a_golden.txt: {str(e)}")


def generate_b_golden(path, matB_file_list):
    """Create b_golden.txt by writing rows in the order: row0 of b*_casc*, iterating over all files, etc."""
    try:
        file_paths = [os.path.join(path, fname) for fname in matB_file_list]
        files = [open(fp, 'r') for fp in file_paths]
        try:
            all_lines = [f.readlines() for f in files]
        finally:
            for f in files:
                f.close()

        if not all_lines:
            print("No b*_casc* files found to generate b_golden.txt")
            return

        num_rows = len(all_lines[0])
        for idx, lines in enumerate(all_lines):
            if len(lines) != num_rows:
                print(f"Warning: B cascade file {matB_file_list[idx]} has {len(lines)} lines; expected {num_rows}")
                num_rows = min(num_rows, len(lines))

        out_path = os.path.join(path, 'b_golden.txt')
        with open(out_path, 'w') as out:
            for row in range(num_rows):
                for file_idx in range(len(all_lines)):
                    out.write(all_lines[file_idx][row])

        print(f"Generated b_golden.txt with {num_rows * len(all_lines)} lines at: {out_path}")
    except Exception as e:
        print(f"Error generating b_golden.txt: {str(e)}")


def generate_c_golden(path, split):
    """
    Create c_golden.txt by interleaving line-by-line: line 0 of c0, line 0 of c1, line 1 of c0, line 1 of c1, ...
    Matches buffer order when out_C writes c0 word 0, c1 word 0, c0 word 1, c1 word 1, ...
    """
    try:
        c_files = [os.path.join(path, f"c{sb}_golden.txt") for sb in range(split)]
        files = [open(fp, "r") for fp in c_files]
        try:
            all_lines = [f.readlines() for f in files]
        finally:
            for f in files:
                f.close()

        if not all_lines:
            print("No c*_golden.txt files found to generate c_golden.txt")
            return

        num_rows = len(all_lines[0])
        for idx, lines in enumerate(all_lines):
            if len(lines) != num_rows:
                print(f"Warning: C file {os.path.basename(c_files[idx])} has {len(lines)} lines; expected {num_rows}")
                num_rows = min(num_rows, len(lines))

        out_path = os.path.join(path, "c_golden.txt")
        with open(out_path, "w") as out:
            for row in range(num_rows):
                for file_idx in range(len(all_lines)):
                    if row < len(all_lines[file_idx]):
                        out.write(all_lines[file_idx][row])

        print(f"Generated c_golden.txt with {num_rows * len(all_lines)} lines (line-interleave: c0_w0, c1_w0, c0_w1, c1_w1, ...) at: {out_path}")
    except Exception as e:
        print(f"Error generating c_golden.txt: {str(e)}")


def generate_c_fake_from_split_files(
    path,
    gemm_size_a,
    gemm_size_b,
    split_a,
    split_b,
    dim_a,
    dim_b,
    wrd_ln,
    data_type,
    sub_tile_a=None,
    sub_tile_b=None,
    output_filename="c_fake.txt",
):
    """
    Read c0_golden.txt, c1_golden.txt (column-split: c0=left cols, c1=right cols; blocks, tiles column-major,
    sub-tiles/elements row-major), de-tile and merge into full matrix. Same block/tile order as generate_split_output_files.
    """
    if sub_tile_a is None or sub_tile_b is None:
        st_a, _, st_b = get_optimal_sub_tile_size(data_type)
        sub_tile_a = sub_tile_a or st_a
        sub_tile_b = sub_tile_b or st_b

    def parse_line(line):
        parts = line.strip().split()
        out = []
        for p in parts:
            try:
                out.append(int(p))
            except ValueError:
                out.append(float(p))
        return out

    cols_per_file = gemm_size_b // split_b
    rows_per_block = gemm_size_a // split_a
    tiles_per_block_row = rows_per_block // dim_a
    tiles_per_block_col = cols_per_file // dim_b
    words_per_tile = (dim_a * dim_b) // wrd_ln
    sub_tiles_per_row = max(1, dim_a // sub_tile_a)
    sub_tiles_per_col = max(1, dim_b // sub_tile_b)

    all_file_lines = []
    for sb in range(split_b):
        fp = os.path.join(path, f"c{sb}_golden.txt")
        if not os.path.isfile(fp):
            raise FileNotFoundError(f"Split file not found: {fp}")
        with open(fp, "r") as f:
            all_file_lines.append(f.readlines())

    if not all_file_lines or any(not lines for lines in all_file_lines):
        raise ValueError("One or more split files are empty")

    fmt_c = "%d"
    matrix_c_fake = np.zeros((gemm_size_a, gemm_size_b), dtype=np.int64)
    line_idx_per_file = [0] * split_b

    for block_row in range(split_a):
        block_start_row = block_row * rows_per_block
        for tile_col in range(tiles_per_block_col):
            for tile_row in range(tiles_per_block_row):
                tile_start_row = block_start_row + tile_row * dim_a
                for sb in range(split_b):
                    tile_start_col = sb * cols_per_file + tile_col * dim_b
                    lines = all_file_lines[sb]
                    tile_lines = []
                    for _ in range(words_per_tile):
                        if line_idx_per_file[sb] >= len(lines):
                            raise ValueError(
                                f"Not enough lines in c{sb}_golden.txt for block {block_row} tile ({tile_row},{tile_col})"
                            )
                        tile_lines.append(lines[line_idx_per_file[sb]])
                        line_idx_per_file[sb] += 1
                    flat = []
                    for ln in tile_lines:
                        flat.extend(parse_line(ln))
                    elem_idx = 0
                    for sub_row in range(sub_tiles_per_row):
                        for sub_col in range(sub_tiles_per_col):
                            for r in range(sub_tile_a):
                                for c in range(sub_tile_b):
                                    rr = tile_start_row + sub_row * sub_tile_a + r
                                    cc = tile_start_col + sub_col * sub_tile_b + c
                                    if rr < gemm_size_a and cc < gemm_size_b and elem_idx < len(flat):
                                        matrix_c_fake[rr, cc] = flat[elem_idx]
                                    elem_idx += 1

    out_path = os.path.join(path, output_filename)
    np.savetxt(out_path, matrix_c_fake, fmt=fmt_c)
    print(f"Generated {out_path} (raw row-major, same format as matrix_C_golden.txt)")
    return matrix_c_fake


def compare_c_fake_with_golden(path, atol=1e-5, rtol=1e-5):
    """
    Load c_fake.txt and matrix_C_golden.txt from path and compare element-wise.
    Returns (match: bool, message: str).
    """
    c_fake_path = os.path.join(path, "c_fake.txt")
    golden_path = os.path.join(path, "matrix_C_golden.txt")
    if not os.path.isfile(c_fake_path):
        return False, f"c_fake.txt not found in {path}"
    if not os.path.isfile(golden_path):
        return False, f"matrix_C_golden.txt not found in {path}"
    try:
        fake = np.loadtxt(c_fake_path)
        golden = np.loadtxt(golden_path)
    except Exception as e:
        return False, f"Failed to load files: {e}"
    if fake.shape != golden.shape:
        return False, f"Shape mismatch: c_fake {fake.shape} vs golden {golden.shape}"
    if np.allclose(fake, golden, atol=atol, rtol=rtol):
        return True, "c_fake.txt matches matrix_C_golden.txt"
    diff = np.abs(fake - golden)
    max_diff = np.max(diff)
    n_diff = np.sum(diff > atol)
    return False, f"c_fake.txt differs from matrix_C_golden.txt: max_diff={max_diff}, num_differing={n_diff}"


def generate_cascade_input_files(matrix_A, matrix_B, gemm_size_a, gemm_size_ab, gemm_size_b, split_a, split_b, casc_ln_ab, dim_a, dim_ab, dim_b, wrd_ln, path, data_type, sub_tile_a=None, sub_tile_ab=None, sub_tile_b=None):
    """
    Generate cascade input files for matrices A and B.
    
    Data Ordering Summary Table:
    Level                   | Matrix A | Matrix B
    ------------------------|----------|----------
    Elements within sub-tiles | Row-major | Row-major
    Sub-tiles within tiles    | Row-major | Row-major
    Tiles within blocks       | Row-major | Column-major
    """
    rows_A, cols_A = matrix_A.shape
    rows_B, cols_B = matrix_B.shape
    
    print(f"\nGenerating cascade input files:")
    print(f"Matrix A: {rows_A}x{cols_A}, Matrix B: {rows_B}x{cols_B}")
    print(f"SPLIT_A: {split_a}, SPLIT_B: {split_b}, CASC_LN_AB: {casc_ln_ab}, DIM_A: {dim_a}, DIM_AB: {dim_ab}, DIM_B: {dim_b}")
    
    # Use provided sub-tile values or fallback to calculated values
    if sub_tile_a is None:
        sub_tile_a, _, _ = get_optimal_sub_tile_size(data_type)
    if sub_tile_ab is None:
        _, sub_tile_ab, _ = get_optimal_sub_tile_size(data_type)
    if sub_tile_b is None:
        _, _, sub_tile_b = get_optimal_sub_tile_size(data_type)
    sub_tiles_per_row = max(1, dim_a // sub_tile_a)
    sub_tiles_per_col = max(1, dim_b // sub_tile_b)
    
    print(f"Sub-tile size (AxABxB): {sub_tile_a}x{sub_tile_ab}x{sub_tile_b}")
    print(f"Sub-tiles per tile: {sub_tiles_per_row}x{sub_tiles_per_col}")
    
    # Generate Matrix A cascade files (a0_casc0.txt through a0_casc7.txt)
    # Matrix A uses SPLIT_A for row decomposition and CASC_LN_AB for column decomposition
    print(f"\nGenerating Matrix A cascade files (a0_casc*.txt):")
    for casc in range(casc_ln_ab):
        filename = os.path.join(path, f"a0_casc{casc}.txt")
        success = write_matrix_A_cascade(matrix_A, casc, filename, gemm_size_a, gemm_size_ab, gemm_size_b, split_a, split_b, casc_ln_ab, dim_a, dim_ab, dim_b, wrd_ln, data_type, sub_tile_a, sub_tile_ab)
        if not success:
            print(f"Failed to generate {filename}")
    
    # Generate Matrix B cascade files for all splits
    # Matrix B uses CASC_LN_AB for row decomposition and SPLIT_B for column decomposition
    for s_idx in range(split_b):
        print(f"\nGenerating Matrix B cascade files for split {s_idx} (b{s_idx}_casc*.txt):")
        for casc in range(casc_ln_ab):
            filename = os.path.join(path, f"b{s_idx}_casc{casc}.txt")
            success = write_matrix_B_split_cascade(matrix_B, s_idx, casc, filename, gemm_size_a, gemm_size_ab, gemm_size_b, split_a, split_b, casc_ln_ab, dim_a, dim_ab, dim_b, wrd_ln, data_type, sub_tile_ab, sub_tile_b)
        if not success:
            print(f"Failed to generate {filename}")
    

def compute_dim_a_b(gemm_size_a, gemm_size_ab, gemm_size_b, dim_a, dim_ab, dim_b, split_a, split_b, casc_ln_ab, data_type):
    """
    Compute effective tile dimensions from provided DIM_A, DIM_AB, DIM_B values.
    These are limited by split/cascade constraints.
    
    Args:
        gemm_size_a, gemm_size_ab, gemm_size_b: Matrix dimensions (A, AB, B)
        dim_a, dim_ab, dim_b: Tile dimensions for A rows, A cols/B rows, B cols
        split_a: Split parameter for A dimension (rows of A)
        split_b: Split parameter for B dimension (columns of B)
        casc_ln_ab: Cascade length for AB dimension
        data_type: Data type
    
    Returns:
        (dim_a_effective, dim_ab_effective, dim_b_effective): Effective dimensions limited by constraints
    """
    # Effective dims limited by split/cascade; DIM_AB is always GEMM_SIZE_AB // CASC_LN_AB
    dim_a_effective = min(dim_a, gemm_size_a // max(1, split_a))  # A dimension split by SPLIT_A
    dim_ab_effective = gemm_size_ab // max(1, casc_ln_ab)  # AB: always cols per cascade
    dim_b_effective = min(dim_b, gemm_size_b // max(1, split_b))  # B dimension split by SPLIT_B
    return dim_a_effective, dim_ab_effective, dim_b_effective


def list_valid_tp_dims(gemm_size_a, gemm_size_ab, gemm_size_b, dim_a, dim_ab, dim_b, split_a, split_b, casc_ln_ab, data_type, sub_tile_a=None, sub_tile_ab=None, sub_tile_b=None):
    # Use provided sub-tile values or fallback to calculated values
    if sub_tile_a is None:
        sub_tile_a, _, _ = get_optimal_sub_tile_size(data_type)
    if sub_tile_ab is None:
        _, sub_tile_ab, _ = get_optimal_sub_tile_size(data_type)
    if sub_tile_b is None:
        _, _, sub_tile_b = get_optimal_sub_tile_size(data_type)
    abseg = gemm_size_ab // max(1, casc_ln_ab)
    if abseg % sub_tile_ab != 0:
        # If AB segment not aligned to AB sub-tile, nothing valid under current CASC_LN_AB
        return []
    valid = []
    max_a = min(dim_a, gemm_size_a // max(1, split_a))  # A dimension split by SPLIT_A
    max_b = min(dim_b, gemm_size_b // max(1, split_b))  # B dimension split by SPLIT_B
    # Enumerate multiples of A and B within limits
    a_vals = [a for a in range(sub_tile_a, max_a + 1, sub_tile_a) if (gemm_size_a // max(1, split_a)) % a == 0]
    b_vals = [b for b in range(sub_tile_b, max_b + 1, sub_tile_b) if (gemm_size_b // max(1, split_b)) % b == 0]
    for a in a_vals:
        for b in b_vals:
            valid.append((a, gemm_size_ab, b))
    return valid
