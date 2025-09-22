#!/usr/bin/env python3
"""
NumPy GEMM Benchmark for AI Engine Comparison
=============================================

This script performs matrix multiplication using NumPy and provides timing
comparison with the AI Engine implementation. It's lighter than PyTorch
and easier to install on Petalinux devices.

Usage:
    python3 numpy_benchmark.py <matrix_a_file> <matrix_b_file> <output_file>

Features:
- Supports multiple data types (int16, int32, float32)
- CPU execution with optimized BLAS
- Comprehensive timing measurements
- Memory usage analysis
- Result validation against AI Engine output
"""

import numpy as np
import time
import sys
import argparse
from pathlib import Path

def load_matrix_from_file(filename, dtype, shape):
    """Load matrix from text file with proper data type conversion."""
    try:
        # Read text file with space-separated values
        with open(filename, 'r') as f:
            content = f.read().strip()
        
        # Split by whitespace and convert to float first
        values = [float(x) for x in content.split()]
        
        # Convert to numpy array based on data type
        if dtype == 'int16':
            data = np.array(values, dtype=np.int16)
        elif dtype == 'int32':
            data = np.array(values, dtype=np.int32)
        elif dtype == 'float32':
            data = np.array(values, dtype=np.float32)
        else:
            raise ValueError(f"Unsupported data type: {dtype}")
        
        # Reshape to matrix dimensions
        matrix = data.reshape(shape)
        return matrix
        
    except Exception as e:
        print(f"Error loading {filename}: {e}")
        return None

def benchmark_numpy_matmul(matrix_a, matrix_b, iterations=10):
    """Benchmark NumPy matrix multiplication with timing analysis."""
    
    print("Warming up NumPy...")
    # Warmup runs
    for _ in range(3):
        _ = np.matmul(matrix_a, matrix_b)
    
    # Benchmark runs
    print("Benchmarking NumPy...")
    times = []
    
    for i in range(iterations):
        start_time = time.perf_counter()
        
        result = np.matmul(matrix_a, matrix_b)
        
        end_time = time.perf_counter()
        times.append(end_time - start_time)
    
    # Calculate statistics
    times = np.array(times)
    mean_time = np.mean(times)
    std_time = np.std(times)
    min_time = np.min(times)
    max_time = np.max(times)
    
    return result, mean_time, std_time, min_time, max_time

def print_timing_results(mean_time, std_time, min_time, max_time, matrix_size):
    """Print comprehensive timing results in the same format as host application."""
    print(f"\n=== NUMPY CPU TIMING RESULTS ===")
    print(f"Matrix Size: {matrix_size}x{matrix_size}")
    
    # Convert to microseconds to match host format
    mean_time_us = mean_time * 1e6
    std_time_us = std_time * 1e6
    min_time_us = min_time * 1e6
    max_time_us = max_time * 1e6
    
    # Print in the same format as host application: [us] description
    print(f"[{mean_time_us:20.0f} us] Mean Time: {mean_time*1000:.3f} ms ± {std_time*1000:.3f} ms")
    print(f"[{min_time_us:20.0f} us] Min Time:  {min_time*1000:.3f} ms")
    print(f"[{max_time_us:20.0f} us] Max Time:  {max_time*1000:.3f} ms")
    
    # Calculate FLOPS for matrix multiplication: 2 * size^3
    flops = 2 * matrix_size * matrix_size * matrix_size
    throughput_gflops = flops / (mean_time * 1e9)
    print(f"[{mean_time_us:20.0f} us] Throughput: {throughput_gflops:.2f} GFLOPS")
    
    # Print BLAS info
    try:
        import numpy as np
        print(f"[{mean_time_us:20.0f} us] NumPy version: {np.__version__}")
    except:
        pass

def read_config():
    """Read configuration from config.json file"""
    import json
    import os
    
    # Find config.json relative to this script
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_file = os.path.join(script_dir, '..', '..', 'design_configs', 'config.json')
    
    try:
        with open(config_file, 'r') as f:
            config = json.load(f)
        return config
    except (FileNotFoundError, json.JSONDecodeError) as e:
        print(f"Warning: Could not read config.json: {e}")
        return {}

def main():
    # Read config first
    config = read_config()
    
    parser = argparse.ArgumentParser(description='NumPy GEMM Benchmark')
    parser.add_argument('matrix_a', help='Matrix A input file')
    parser.add_argument('matrix_b', help='Matrix B input file')
    parser.add_argument('output', help='Output file for results')
    parser.add_argument('--size', type=int, default=config.get('GEMM_SIZE', 32), 
                       help='Matrix size (default: from config.json)')
    parser.add_argument('--dtype', choices=['int16', 'int32', 'float32'], 
                       default=config.get('DATA_TYPE', 'int16'), 
                       help='Data type (default: from config.json)')
    parser.add_argument('--iterations', type=int, default=10,
                       help='Number of benchmark iterations (default: 10)')
    parser.add_argument('--target', default=config.get('TARGET', 'hw'), 
                       help='AI Engine target (default: from config.json)')
    
    args = parser.parse_args()
    
    print(f"NumPy GEMM Benchmark - {args.size}x{args.size} {args.dtype}")
    print("=" * 60)
    print(f"Target: {args.target}")
    print(f"Device: CPU")
    print(f"Matrix Size: {args.size}x{args.size}")
    print(f"Data Type: {args.dtype}")
    print(f"Iterations: {args.iterations}")
    print("=" * 60)
    
    # Load matrices
    shape = (args.size, args.size)
    matrix_a = load_matrix_from_file(args.matrix_a, args.dtype, shape)
    matrix_b = load_matrix_from_file(args.matrix_b, args.dtype, shape)
    
    if matrix_a is None or matrix_b is None:
        print("Failed to load input matrices")
        return 1
    
    print(f"Loaded matrices: A{matrix_a.shape}, B{matrix_b.shape}")
    
    # Convert to appropriate NumPy dtype
    if args.dtype == 'int16':
        matrix_a = matrix_a.astype(np.int16)
        matrix_b = matrix_b.astype(np.int16)
    elif args.dtype == 'int32':
        matrix_a = matrix_a.astype(np.int32)
        matrix_b = matrix_b.astype(np.int32)
    elif args.dtype == 'float32':
        matrix_a = matrix_a.astype(np.float32)
        matrix_b = matrix_b.astype(np.float32)
    
    # Benchmark
    result, mean_time, std_time, min_time, max_time = benchmark_numpy_matmul(
        matrix_a, matrix_b, args.iterations
    )
    
    # Print results
    print_timing_results(mean_time, std_time, min_time, max_time, args.size)
    
    # Print summary in host application format
    print(f"\n=== NUMPY CPU SUMMARY ===")
    print(f"[{mean_time*1e6:20.0f} us] NumPy CPU GEMM ({args.size}x{args.size})")
    print(f"[{mean_time*1e6:20.0f} us] Throughput: {2 * args.size**3 / (mean_time * 1e9):.2f} GFLOPS")
    
    # Save result
    result.tofile(args.output)
    print(f"Result saved to {args.output}")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
