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
   
    # Initialize default values
    params = {
        'GEMM_SIZE': 32,
        'DIM': 16,
        'DATA_TYPE': 'int16',
        'WRD_LN': 8,
        'SUB_TILE_M': 4,
        'SUB_TILE_K': 4,
        'SUB_TILE_N': 4,
        'GRAPH_ITER_CNT': 2,
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
        v = config_data.get('GEMM_SIZE');   
        if v is not None: params['GEMM_SIZE'] = int(v)
        v = config_data.get('DIM');        
        if v is not None: params['DIM'] = int(v)
        v = config_data.get('SPLIT');       
        if v is not None: params['SPLIT'] = int(v)
        v = config_data.get('CASC_LN');    
        if v is not None: params['CASC_LN'] = int(v)
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

        # Note: SUB_TILE_M, SUB_TILE_K, SUB_TILE_N are now calculated from DATA_TYPE
        # They should not be read from config.json as they are derived parameters
        
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
    elif data_type == "float":
        return 4   # 128 / 32 = 4 elements per 128-bit PLIO stream (float is 32-bit)
    else:
        return 8   # Default to int16


def get_optimal_sub_tile_size(data_type):
    """
    Return hardware-supported sub-tile sizes (M, K, N) for the given data type.
    Values are taken from the AI Engine-ML matrix multiplication instruction set.
    """
    # DSPLIB matrix_mult_graph supported sub-tile sizes (AIE_VARIANT=1)
    if data_type in ["int16", "uint16"]:
        # 16b x 16b: 4x4x4 (primary)
        return 4, 4, 4
    elif data_type in ["int32", "uint32"]:
        # 32b x 32b: 4x4x2 (primary), 2x2x2 (alternative)
        return 4, 4, 2
    elif data_type == "float":
        # float x float: 4x4x2 (primary), 2x2x2 (alternative)
        return 4, 4, 2
    else:
        # Default fallback
        return 4, 4, 4


def get_random_range(data_type="int16"):
    """
    Get safe random value range to prevent overflow.
    """
    if data_type in ["int16", "uint16"]:
        return -8, 8
    elif data_type in ["int32", "uint32"]:
        return -16, 16
    elif data_type == "float":
        return -1.0, 1.0
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
    elif data_type == "float":
        return np.float32
    else:
        return np.int16


def generate_sequential_matrix_files(size, path, matrix_a_file=None, matrix_b_file=None, data_type="int16", random_seed=42):
    """Generate input matrix files and golden output with safe random values"""
    
    # Set random seed for reproducible results
    np.random.seed(random_seed)
    print(f"Using random seed: {random_seed} for reproducible matrix generation")
    
    numpy_dtype = get_numpy_dtype(data_type)
    min_val, max_val = get_random_range(data_type)
    
    # Create zero-initialized matrices with padded dimensions
    matrix_A = np.zeros((size, size), dtype=numpy_dtype)
    matrix_B = np.zeros((size, size), dtype=numpy_dtype)
    
    # Check if files are provided for loading matrices
    if matrix_a_file:
        try:
            print(f"Loading Matrix A from {matrix_a_file}")
            loaded_A = np.loadtxt(matrix_a_file, dtype=numpy_dtype)
            
            # Check loaded dimensions and apply data to matrix_A
            loaded_rows, loaded_cols = loaded_A.shape
            actual_rows = min(loaded_rows, size)
            actual_cols = min(loaded_cols, size)
            
            matrix_A[:actual_rows, :actual_cols] = loaded_A[:actual_rows, :actual_cols]
            print(f"Loaded Matrix A: {loaded_rows}x{loaded_cols}, Used: {actual_rows}x{actual_cols}")
        except Exception as e:
            print(f"Error loading Matrix A: {str(e)}")
            print("Generating random Matrix A instead")
            if data_type == "float":
                matrix_A[:size, :size] = np.random.uniform(min_val, max_val, size=(size, size)).astype(numpy_dtype)
            else:
                matrix_A[:size, :size] = np.random.randint(min_val, max_val + 1, size=(size, size), dtype=numpy_dtype)
    else:
        # Fill the actual data areas with random values
        if data_type == "float":
            matrix_A[:size, :size] = np.random.uniform(min_val, max_val, size=(size, size)).astype(numpy_dtype)
        else:
            matrix_A[:size, :size] = np.random.randint(min_val, max_val + 1, size=(size, size), dtype=numpy_dtype)
    
    if matrix_b_file:
        try:
            print(f"Loading Matrix B from {matrix_b_file}")
            loaded_B = np.loadtxt(matrix_b_file, dtype=numpy_dtype)
            
            # Check loaded dimensions and apply data to matrix_B
            loaded_rows, loaded_cols = loaded_B.shape
            actual_rows = min(loaded_rows, size)
            actual_cols = min(loaded_cols, size)
            
            matrix_B[:actual_rows, :actual_cols] = loaded_B[:actual_rows, :actual_cols]
            print(f"Loaded Matrix B: {loaded_rows}x{loaded_cols}, Used: {actual_rows}x{actual_cols}")
        except Exception as e:
            print(f"Error loading Matrix B: {str(e)}")
            print("Generating random Matrix B instead")
            if data_type == "float":
                matrix_B[:size, :size] = np.random.uniform(min_val, max_val, size=(size, size)).astype(numpy_dtype)
            else:
                matrix_B[:size, :size] = np.random.randint(min_val, max_val + 1, size=(size, size), dtype=numpy_dtype)
    else:
        # Fill the actual data areas with random values
        if data_type == "float":
            matrix_B[:size, :size] = np.random.uniform(min_val, max_val, size=(size, size)).astype(numpy_dtype)
        else:
            matrix_B[:size, :size] = np.random.randint(min_val, max_val + 1, size=(size, size), dtype=numpy_dtype)
    
    # Calculate golden output C = A × B using float32 precision to match host
    if data_type == "float":
        # Ensure both matrices are float32 before computation
        A_float32 = matrix_A.astype(np.float32)
        B_float32 = matrix_B.astype(np.float32)
        matrix_C = np.matmul(A_float32, B_float32).astype(np.float32)
    elif data_type in ["int16", "uint16"]:
        matrix_C = np.matmul(matrix_A.astype(np.int32), matrix_B.astype(np.int32))
    elif data_type in ["int32", "uint32"]:
        matrix_C = np.matmul(matrix_A.astype(np.int32), matrix_B.astype(np.int32))
    else:
        matrix_C = np.matmul(matrix_A.astype(np.int32), matrix_B.astype(np.int32))
      
    # Apply 4 decimal place rounding to match host application
    if data_type == "float":
        matrix_A = np.round(matrix_A * 10000.0) / 10000.0
        matrix_B = np.round(matrix_B * 10000.0) / 10000.0
        matrix_C = np.round(matrix_C * 10000.0) / 10000.0
    
    # Save matrices with appropriate format
    fmt_a = '%.4f' if data_type == "float" else '%d'
    fmt_b = '%.4f' if data_type == "float" else '%d'
    fmt_c = '%.4f' if data_type == "float" else '%d'
    
    np.savetxt(os.path.join(path, 'matrix_A_input.txt'), matrix_A, fmt=fmt_a)
    np.savetxt(os.path.join(path, 'matrix_B_input.txt'), matrix_B, fmt=fmt_b)
    np.savetxt(os.path.join(path, 'matrix_C_golden.txt'), matrix_C, fmt=fmt_c)
    
    print("\nMatrix dimensions:")
    print(f"A: {matrix_A.shape}")
    print(f"B: {matrix_B.shape}")
    print(f"C: {matrix_C.shape}")
    
    return matrix_A, matrix_B, matrix_C


def generate_split_output_files(matrix_C, split, dim, wrd_ln, path, data_type):
    """
    Generate output files c0, c1, ..., c{split-1} from matrix C.
    
    Data Ordering Summary Table:
    Level                   | Matrix C
    ------------------------|----------
    Elements within sub-tiles | Row-major
    Sub-tiles within tiles    | Row-major  
    Tiles within blocks       | Column-major
    
    Each file contains data organized as follows:
    - Matrix C is divided into split × split blocks
    - Tiles of size dim × dim within each block are written in column-major order
    - Elements within tiles are collected in proper row-major order
      (row by row, left to right within each row)
    - Elements are written in chunks of exactly WRD_LN elements
    """
    rows, cols = matrix_C.shape
    
    # Calculate block dimensions
    rows_per_split = rows // split
    cols_per_split = cols // split
    
    print(f"\nGenerating split output files with DIM={dim}")
    print(f"Matrix C shape: {rows}x{cols}")
    print(f"Split: {split}, Rows per split: {rows_per_split}, Cols per split: {cols_per_split}")
    
    # Sub-tiles for C use M×N per instruction set
    sub_tile_m, _sub_tile_k, sub_tile_n = get_optimal_sub_tile_size(data_type)
    sub_tiles_per_row = max(1, dim // sub_tile_m)
    sub_tiles_per_col = max(1, dim // sub_tile_n)
    
    print(f"Data type: {data_type}")
    print(f"Sub-tile size (C MxN): {sub_tile_m}x{sub_tile_n} (AI Engine-ML instruction set)")
    print(f"Sub-tiles per tile: {sub_tiles_per_row}x{sub_tiles_per_col}")
    
    # Create output files for each split
    for sb in range(split):
        filename = os.path.join(path, f"c{sb}_golden.txt")
        with open(filename, 'w') as f:
            # Process each block in this split column
            for sa in range(split):
                # Calculate block boundaries
                block_start_row = sa * rows_per_split
                block_end_row = (sa + 1) * rows_per_split
                block_start_col = sb * cols_per_split
                block_end_col = (sb + 1) * cols_per_split
                
                # Calculate effective tile dimensions - use original DIM but force optimal sub-tiles
                dim_a = min(dim, rows_per_split)  # Tile rows limited by split size
                dim_b = min(dim, cols_per_split)  # Tile cols limited by split size
                
                # Use M×N sub-tiles per data type
                sub_tiles_per_row = max(1, dim_a // sub_tile_m)
                sub_tiles_per_col = max(1, dim_b // sub_tile_n)
                
                # Calculate number of tiles in this block using effective dimensions
                block_tile_rows = rows_per_split // dim_a
                block_tile_cols = cols_per_split // dim_b
                
                print(f"  Block [{sa},{sb}]: {block_tile_rows}x{block_tile_cols} tiles of size {dim_a}x{dim_b}")
                print(f"  Sub-tiles: {sub_tiles_per_row}x{sub_tiles_per_col} sub-tiles of size {sub_tile_m}x{sub_tile_n}")
                
                # Process tiles in column-major order within the block (column-major between tiles)
                for tile_col in range(block_tile_cols):
                    for tile_row in range(block_tile_rows):
                        # Calculate tile boundaries using effective dimensions
                        tile_start_row = block_start_row + tile_row * dim_a
                        tile_end_row = min(tile_start_row + dim_a, block_end_row)
                        tile_start_col = block_start_col + tile_col * dim_b
                        tile_end_col = min(tile_start_col + dim_b, block_end_col)
                        
                        # Collect all elements from this tile
                        tile_elements = []
                        
                        # Process sub-tiles in row-major order between sub-tiles (MxN)
                        for sub_row in range(sub_tiles_per_row):
                            for sub_col in range(sub_tiles_per_col):
                                # Calculate sub-tile boundaries
                                sub_start_row = tile_start_row + sub_row * sub_tile_m
                                sub_end_row = min(sub_start_row + sub_tile_m, tile_end_row)
                                sub_start_col = tile_start_col + sub_col * sub_tile_n
                                sub_end_col = min(sub_start_col + sub_tile_n, tile_end_col)
                                
                                # Process elements within sub-tile in row-major order
                                for row in range(sub_start_row, sub_end_row):
                                    for col in range(sub_start_col, sub_end_col):
                                        if data_type == "float":
                                            tile_elements.append(f"{matrix_C[row, col]:.4f}")
                                        else:
                                            tile_elements.append(str(matrix_C[row, col]))
                        
                        # Write the elements in chunks of exactly WRD_LN
                        for i in range(0, len(tile_elements), wrd_ln):
                            chunk = tile_elements[i:i+wrd_ln]
                            # If this is the last chunk and not complete, pad with zeros
                            while len(chunk) < wrd_ln:
                                chunk.append('0')
                            f.write(' '.join(chunk) + '\n')
        
        # Verify the generated file
        with open(filename, 'r') as f:
            lines = f.readlines()
            expected_lines = split * (rows_per_split // dim_a) * (cols_per_split // dim_b) * (dim_a * dim_b // wrd_ln)
            print(f"Generated {filename}: {len(lines)} lines (expected: {expected_lines})")


def write_matrix_A_cascade(matrix_A, casc_idx, filename, gemm_size, split, casc_ln, dim, wrd_ln, data_type):
    """Write Matrix A cascade strip with broadcasting:
    
    Data Ordering Summary Table:
    Level                   | Matrix A
    ------------------------|----------
    Elements within sub-tiles | Row-major
    Sub-tiles within tiles    | Row-major
    Tiles within blocks       | Row-major
    
    - Each cascade gets GEMM_SZ_CASC columns
    - Write in chunks of WRD_LN (8)
    - Each block has dimensions of GEMM_SZ_SPLIT * GEMM_SZ_CASC
    - Elements within tiles and between tiles are written in row-major order
    - After finishing writing elements of each block, we first broadcast broadcast_count times
    - Then move to the next block
    - Consistent 4×4 sub-tiling for int16 data type regardless of DIM value
    - Sequential block processing with immediate broadcasting
    """
    try:
        # Calculate dimensions
        cols_per_casc = gemm_size // casc_ln
        start_col = casc_idx * cols_per_casc
        end_col = start_col + cols_per_casc
        
        print(f"\nWriting {filename}")
        print(f"Cascade {casc_idx}: Columns {start_col} to {end_col-1}")
        
        # Calculate effective tile dimensions - limited by split/cascade
        dim_a = min(dim, gemm_size // split)  # Tile rows limited by split size
        dim_b = min(dim, cols_per_casc)  # Tile cols limited by cascade size
        
        # Use data-type sub-tiles for A as M×K
        sub_tile_m, sub_tile_k, _sub_tile_n = get_optimal_sub_tile_size(data_type)
        sub_tiles_per_row = max(1, dim_a // sub_tile_m)
        sub_tiles_per_col = max(1, dim_b // sub_tile_k)
        
        print(f"  Effective tile dimensions: {dim_a}x{dim_b} (DIM={dim}, SPLIT_SIZE={gemm_size // split}, CASC_SIZE={cols_per_casc})")
        
        # Calculate broadcast iterations - cascade files must broadcast based on effective tile dimensions
        elements_per_block = dim_a * dim_b
        lines_per_block = elements_per_block // wrd_ln
        # Calculate broadcast count based on effective tile dimensions
        broadcast_count = gemm_size // dim // split
        
        print(f"  Elements per block: {elements_per_block}")
        print(f"  Lines per block: {lines_per_block}")
        print(f"  Broadcast count: {broadcast_count}")
        
        with open(filename, 'w') as file:
            # In a cascade, process each block separately
            for block_idx in range(split):
                # Calculate block boundaries for this split block
                block_start_row = block_idx * (gemm_size // split)
                block_end_row = block_start_row + (gemm_size // split)
                
                # Collect all elements for this block in row-major order
                block_elements = []
                
                # Calculate number of tiles using effective dimensions
                tiles_per_row = (gemm_size // split) // dim_a
                tiles_per_col = cols_per_casc // dim_b
                
                print(f"  Block {block_idx}: {tiles_per_row}x{tiles_per_col} tiles of size {dim_a}x{dim_b}")
                print(f"  Sub-tiles: {sub_tiles_per_row}x{sub_tiles_per_col} sub-tiles of size {sub_tile_m}x{sub_tile_k} (A uses M×K)")
                
                # Process tiles in row-major order (row-major between tiles)
                for tile_row_idx in range(tiles_per_row):
                    for tile_col_idx in range(tiles_per_col):
                        # Calculate tile boundaries using effective dimensions
                        tile_start_row = block_start_row + tile_row_idx * dim_a
                        tile_end_row = min(tile_start_row + dim_a, block_end_row)
                        tile_start_col = start_col + tile_col_idx * dim_b
                        tile_end_col = min(tile_start_col + dim_b, end_col)
                        
                        # Process sub-tiles within each tile in row-major order (A: M×K)
                        for sub_row in range(sub_tiles_per_row):
                            for sub_col in range(sub_tiles_per_col):
                                # Calculate sub-tile boundaries (A: M×K)
                                sub_start_row = tile_start_row + sub_row * sub_tile_m
                                sub_end_row = min(sub_start_row + sub_tile_m, tile_end_row)
                                sub_start_col = tile_start_col + sub_col * sub_tile_k
                                sub_end_col = min(sub_start_col + sub_tile_k, tile_end_col)
                                
                                # Process elements within sub-tile in row-major order
                                for row in range(sub_start_row, sub_end_row):
                                    for col in range(sub_start_col, sub_end_col):
                                        if row < gemm_size and col < gemm_size:
                                            if data_type == "float":
                                                block_elements.append(f"{matrix_A[row, col]:.4f}")
                                            else:
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
            # Calculate expected lines based on reference formula: MATA_SZ = ((GEMM_SIZE * GEMM_SIZE / CASC_LN) / 8) * iterCnt * ((GEMM_SIZE/SPLIT) / DIM)
            expected_lines = ((gemm_size * gemm_size // casc_ln) // wrd_ln) * 1 * ((gemm_size // split) // dim)
            print(f"\nFile verification for {filename}:")
            print(f"Written {len(lines)} lines")
            print(f"Expected lines: {expected_lines} (based on reference formula)")
            print(f"Elements per row: {elements_per_row} (should be {wrd_ln})")
            print(f"Broadcast count: {broadcast_count}")
            print(f"Effective tile dimensions: {dim_a}x{dim_b}")
            print(f"Tiles per block: {tiles_per_row}x{tiles_per_col}")
            print(f"Reference MATA_SZ calculation: {expected_lines}")
        
        return True
        
    except Exception as e:
        print(f"\nError writing to {filename}: {str(e)}")
        print("Debug info:")
        print(f"Matrix A shape: {matrix_A.shape}")
        print(f"Cascade index: {casc_idx}")
        return False


def write_matrix_B_split_cascade(matrix_B, split_idx, casc_idx, filename, gemm_size, split_val, casc_ln, dim, wrd_ln, data_type):
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
        - Broadcast count per column = (GEMM_SIZE/DIM/SPLIT)
    - After writing all columns, the entire file is repeated SPLIT times
    - Consistent 4×4 sub-tiling for int16 data type regardless of DIM value
    """
    try:
        # Calculate dimensions
        rows_per_casc = gemm_size // casc_ln
        cols_per_split = gemm_size // split_val
        
        # Calculate block boundaries
        start_row = casc_idx * rows_per_casc
        end_row = start_row + rows_per_casc
        start_col = split_idx * cols_per_split
        end_col = start_col + cols_per_split
        
        print(f"\nWriting {filename}")
        print(f"Split {split_idx}, Cascade {casc_idx}")
        print(f"Block boundaries: rows {start_row}:{end_row}, cols {start_col}:{end_col}")
        
        # Calculate effective tile dimensions - use original DIM but force 4×4 sub-tiles
        dim_a = min(dim, rows_per_casc)  # Tile rows limited by cascade size
        dim_b = min(dim, cols_per_split)  # Tile cols limited by split size
        
        # Use data-type sub-tiles for B as K×N
        sub_tile_m, sub_tile_k, sub_tile_n = get_optimal_sub_tile_size(data_type)
        sub_tiles_per_row = max(1, dim_a // sub_tile_k)
        sub_tiles_per_col = max(1, dim_b // sub_tile_n)
        
        print(f"  Effective tile dimensions: {dim_a}x{dim_b} (DIM={dim}, CASC_SIZE={rows_per_casc}, SPLIT_SIZE={cols_per_split})")
        print(f"  Sub-tiles: {sub_tiles_per_row}x{sub_tiles_per_col} sub-tiles of size {sub_tile_k}x{sub_tile_n} (B uses K×N)")
        
        num_tiles_row = rows_per_casc // dim_a
        num_tiles_col = cols_per_split // dim_b
        
        # Calculate broadcast iterations - cascade files must broadcast based on original DIM (no SPLIT)
        broadcast_count = gemm_size // dim // split_val
        
        # Create a temporary file for the initial data
        temp_filename = filename + ".temp"
        with open(temp_filename, 'w') as file:
            # Process tiles column by column (column-major between tiles)
            for tile_col in range(num_tiles_col):
                # Collect elements for this column of tiles
                column_elements = []
                
                # Process all tiles in this column
                for tile_row in range(num_tiles_row):
                    # Process sub-tiles within each tile in row-major order (B: K×N)
                    for sub_row in range(sub_tiles_per_row):
                        for sub_col in range(sub_tiles_per_col):
                            # Calculate sub-tile boundaries (B: K×N)
                            sub_start_row = start_row + tile_row * dim_a + sub_row * sub_tile_k
                            sub_end_row = min(sub_start_row + sub_tile_k, start_row + (tile_row + 1) * dim_a)
                            sub_start_col = start_col + tile_col * dim_b + sub_col * sub_tile_n
                            sub_end_col = min(sub_start_col + sub_tile_n, start_col + (tile_col + 1) * dim_b)
                            
                            # Process elements within sub-tile in row-major order
                            for row in range(sub_start_row, sub_end_row):
                                for col in range(sub_start_col, sub_end_col):
                                    if row < gemm_size and col < gemm_size:
                                        if data_type == "float":
                                            column_elements.append(f"{matrix_B[row, col]:.4f}")
                                        else:
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
        
        # Now read the temporary file and repeat it SPLIT times into the final file
        with open(temp_filename, 'r') as temp_file:
            file_content = temp_file.read()
        with open(filename, 'w') as final_file:
            for _ in range(split_val):
                final_file.write(file_content)
        # Remove the temporary file
        os.remove(temp_filename)
        
        # Verify file was written correctly
        with open(filename, 'r') as f:
            lines = f.readlines()
            elements_per_row = len(lines[0].strip().split())
            # Calculate expected lines based on reference formula: MATB_SZ = ((GEMM_SIZE * GEMM_SIZE / (CASC_LN*SPLIT)) / WRD_LN) * (GEMM_SIZE / DIM)
            expected_lines = ((gemm_size * gemm_size // (casc_ln * split_val)) // wrd_ln) * 1 * (gemm_size // dim)
            print(f"\nFile verification for {filename}:")
            print(f"Written {len(lines)} lines")
            print(f"Expected lines: {expected_lines} (based on reference formula)")
            print(f"Elements per row: {elements_per_row} (should be {wrd_ln})")
            print(f"Per-column broadcast count: {broadcast_count}")
            print(f"Total repeats of the whole file: {split_val}")
            print(f"Effective tile dimensions: {dim_a}x{dim_b}")
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
    """Create c_golden.txt by writing rows in the order: row0 of c0_golden.txt, row0 of c1_golden.txt, etc."""
    try:
        c_files = [os.path.join(path, f"c{sb}_golden.txt") for sb in range(split)]
        files = [open(fp, 'r') for fp in c_files]
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

        out_path = os.path.join(path, 'c_golden.txt')
        with open(out_path, 'w') as out:
            for row in range(num_rows):
                for file_idx in range(len(all_lines)):
                    out.write(all_lines[file_idx][row])

        print(f"Generated c_golden.txt with {num_rows * len(all_lines)} lines at: {out_path}")
    except Exception as e:
        print(f"Error generating c_golden.txt: {str(e)}")


def generate_cascade_input_files(matrix_A, matrix_B, split, casc_ln, dim, wrd_ln, path, data_type):
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
    print(f"Split: {split}, Cascade Length: {casc_ln}, DIM: {dim}")
    
    # Get optimal sub-tile configuration
    sub_tile_m, sub_tile_k, sub_tile_n = get_optimal_sub_tile_size(data_type)
    sub_tiles_per_row = max(1, dim // sub_tile_m)
    sub_tiles_per_col = max(1, dim // sub_tile_n)
    
    print(f"Sub-tile size (MxKxN): {sub_tile_m}x{sub_tile_k}x{sub_tile_n}")
    print(f"Sub-tiles per tile: {sub_tiles_per_row}x{sub_tiles_per_col}")
    
    # Generate Matrix A cascade files (a0_casc0.txt through a0_casc7.txt)
    print(f"\nGenerating Matrix A cascade files (a0_casc*.txt):")
    for casc in range(casc_ln):
        filename = os.path.join(path, f"a0_casc{casc}.txt")
        success = write_matrix_A_cascade(matrix_A, casc, filename, rows_A, split, casc_ln, dim, wrd_ln, data_type)
        if not success:
            print(f"Failed to generate {filename}")
    
    # Generate Matrix B cascade files for split 0 (b0_casc0.txt through b0_casc7.txt)
    print(f"\nGenerating Matrix B cascade files for split 0 (b0_casc*.txt):")
    for casc in range(casc_ln):
        filename = os.path.join(path, f"b0_casc{casc}.txt")
        success = write_matrix_B_split_cascade(matrix_B, 0, casc, filename, rows_B, split, casc_ln, dim, wrd_ln, data_type)
        if not success:
            print(f"Failed to generate {filename}")
    
    # Generate Matrix B cascade files for split 1 (b1_casc0.txt through b1_casc7.txt)
    print(f"\nGenerating Matrix B cascade files for split 1 (b1_casc*.txt):")
    for casc in range(casc_ln):
        filename = os.path.join(path, f"b1_casc{casc}.txt")
        success = write_matrix_B_split_cascade(matrix_B, 1, casc, filename, rows_B, split, casc_ln, dim, wrd_ln, data_type)
        if not success:
            print(f"Failed to generate {filename}")


def compute_dim_a_b(gemm_size, dim, split, casc_ln, data_type):
    # Effective dims limited by split/cascade
    dim_a = min(dim, gemm_size // max(1, split))
    dim_b = min(dim, gemm_size // max(1, casc_ln))
    return dim_a, gemm_size, dim_b


def list_valid_tp_dims(gemm_size, dim, split, casc_ln, data_type):
    # Sub-tiles per data type
    sub_m, sub_k, sub_n = get_optimal_sub_tile_size(data_type)
    kseg = gemm_size // max(1, casc_ln)
    if kseg % sub_k != 0:
        # If K segment not aligned to K sub-tile, nothing valid under current CASC_LN
        return []
    valid = []
    max_a = min(dim, gemm_size // max(1, split))
    max_b = min(dim, gemm_size // max(1, casc_ln))
    # Enumerate multiples of M and N within limits
    a_vals = [a for a in range(sub_m, max_a + 1, sub_m) if (gemm_size // max(1, split)) % a == 0]
    b_vals = [b for b in range(sub_n, max_b + 1, sub_n) if (gemm_size // max(1, casc_ln)) % b == 0]
    for a in a_vals:
        for b in b_vals:
            valid.append((a, gemm_size, b))
    return valid
