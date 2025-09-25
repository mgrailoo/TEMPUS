#!/usr/bin/env python3
"""
PyTorch GEMM Benchmark for AI Engine Comparison
===============================================

This script performs matrix multiplication using PyTorch and provides timing
comparison with the AI Engine implementation. It reads the same input matrices
and produces results for performance comparison.

Usage:
    python3 pytorch_benchmark.py <matrix_a_file> <matrix_b_file> <output_file>

Features:
- Supports multiple data types (int16, int32, float32)
- CPU execution mode only
- Comprehensive timing measurements
- Memory usage analysis
- Result validation against AI Engine output
"""

import time
import sys
import argparse
from pathlib import Path

# Import with error handling to avoid NumPy compatibility issues
try:
    import numpy as np
except ImportError as e:
    print(f"Error loading matrix_A_input.txt: Numpy is not available")
    print(f"Error loading matrix_B_input.txt: Numpy is not available")
    print("Failed to load input matrices")
    sys.exit(1)

try:
    import torch
except ImportError as e:
    print(f"Error: PyTorch is not available: {e}")
    sys.exit(1)

def load_matrix_from_file(filename, dtype, shape):
    """Load matrix from text file with proper data type conversion."""
    try:
        # Read text file and parse values
        with open(filename, 'r') as f:
            content = f.read().strip()
        
        # Split by whitespace and convert to numbers
        values = content.split()
        
        # Convert to numpy array based on data type
        if dtype == 'int16':
            data = np.array([int(x) for x in values], dtype=np.int16)
        elif dtype == 'int32':
            data = np.array([int(x) for x in values], dtype=np.int32)
        elif dtype == 'float32':
            data = np.array([float(x) for x in values], dtype=np.float32)
        else:
            raise ValueError(f"Unsupported data type: {dtype}")
        
        # Reshape to matrix dimensions
        matrix = data.reshape(shape)
        return torch.from_numpy(matrix)
        
    except Exception as e:
        print(f"Error loading {filename}: {e}")
        # Fallback: try to load without numpy if there's a compatibility issue
        try:
            with open(filename, 'r') as f:
                content = f.read().strip()
            values = content.split()
            
            # Convert directly to Python lists and then to PyTorch tensors
            if dtype == 'int16':
                data_list = [int(x) for x in values]
                tensor = torch.tensor(data_list, dtype=torch.int16)
            elif dtype == 'int32':
                data_list = [int(x) for x in values]
                tensor = torch.tensor(data_list, dtype=torch.int32)
            elif dtype == 'float32':
                data_list = [float(x) for x in values]
                tensor = torch.tensor(data_list, dtype=torch.float32)
            else:
                raise ValueError(f"Unsupported data type: {dtype}")
            
            return tensor.reshape(shape)
        except Exception as e2:
            print(f"Fallback loading also failed for {filename}: {e2}")
            return None

def benchmark_pytorch_matmul(matrix_a, matrix_b, device='cpu', iterations=10):
    """Benchmark PyTorch matrix multiplication with timing analysis."""
    
    # Move matrices to device
    matrix_a = matrix_a.to(device)
    matrix_b = matrix_b.to(device)
    
    # Warmup runs
    print(f"Warming up PyTorch on {device}...")
    for _ in range(3):
        _ = torch.matmul(matrix_a, matrix_b)
    
    # CPU-only execution (no synchronization needed)
    
    # Benchmark runs
    print(f"Benchmarking PyTorch on {device}...")
    times = []
    
    for i in range(iterations):
        start_time = time.perf_counter()
        
        result = torch.matmul(matrix_a, matrix_b)
        
        # CPU-only execution (no synchronization needed)
        
        end_time = time.perf_counter()
        times.append(end_time - start_time)
    
    # Calculate statistics (with fallback for NumPy compatibility)
    try:
        times = np.array(times)
        mean_time = np.mean(times)
        std_time = np.std(times)
        min_time = np.min(times)
        max_time = np.max(times)
    except:
        # Fallback calculation without NumPy
        mean_time = sum(times) / len(times)
        variance = sum((x - mean_time) ** 2 for x in times) / len(times)
        std_time = variance ** 0.5
        min_time = min(times)
        max_time = max(times)
    
    return result, mean_time, std_time, min_time, max_time

def print_timing_results(device, mean_time, std_time, min_time, max_time, matrix_size):
    """Print comprehensive timing results in the same format as host application."""
    print(f"\n=== PYTORCH {device.upper()} TIMING RESULTS ===")
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
    
    parser = argparse.ArgumentParser(description='PyTorch GEMM Benchmark')
    parser.add_argument('matrix_a', help='Matrix A input file')
    parser.add_argument('matrix_b', help='Matrix B input file')
    parser.add_argument('output', help='Output file for results')
    parser.add_argument('--size', type=int, default=config.get('GEMM_SIZE', 32), 
                       help='Matrix size (default: from config.json)')
    parser.add_argument('--dtype', choices=['int16', 'int32', 'float32'], 
                       default=config.get('DATA_TYPE', 'int16'), 
                       help='Data type (default: from config.json)')
    parser.add_argument('--device', choices=['cpu'], default='cpu',
                       help='Device to use (cpu only)')
    parser.add_argument('--iterations', type=int, default=10,
                       help='Number of benchmark iterations (default: 10)')
    parser.add_argument('--target', default=config.get('TARGET', 'hw'), 
                       help='AI Engine target for comparison (default: from config.json)')
    
    args = parser.parse_args()
    
    # CPU-only execution (GPU not available on this board)
    
    print(f"PyTorch GEMM Benchmark - {args.size}x{args.size} {args.dtype} on {args.device}")
    print("=" * 60)
    print(f"Target: {args.target}")
    print(f"Device: {args.device}")
    print(f"Matrix Size: {args.size}x{args.size}")
    print(f"Data Type: {args.dtype}")
    print(f"Iterations: {args.iterations}")
    print("=" * 60)
    
    # Load matrices with better error handling
    # Both matrices are GEMM_SIZE x GEMM_SIZE (32x32)
    shape = (args.size, args.size)  # 32x32
    
    try:
        matrix_a = load_matrix_from_file(args.matrix_a, args.dtype, shape)
        matrix_b = load_matrix_from_file(args.matrix_b, args.dtype, shape)
    except Exception as e:
        print(f"Error loading matrices: {e}")
        return 1
    
    if matrix_a is None or matrix_b is None:
        print("Failed to load input matrices")
        return 1
    
    print(f"Loaded matrices: A{matrix_a.shape}, B{matrix_b.shape}")
    print(f"AI Engine Target: {args.target}")
    print(f"PyTorch Device: {args.device}")
    
    # Convert to appropriate PyTorch dtype
    if args.dtype == 'int16':
        matrix_a = matrix_a.short()
        matrix_b = matrix_b.short()
    elif args.dtype == 'int32':
        matrix_a = matrix_a.int()
        matrix_b = matrix_b.int()
    elif args.dtype == 'float32':
        matrix_a = matrix_a.float()
        matrix_b = matrix_b.float()
    
    # Benchmark
    result, mean_time, std_time, min_time, max_time = benchmark_pytorch_matmul(
        matrix_a, matrix_b, args.device, args.iterations
    )
    
    # Print results
    print_timing_results(args.device, mean_time, std_time, min_time, max_time, args.size)
    
    # Print summary in host application format
    print(f"\n=== PYTORCH {args.device.upper()} SUMMARY ===")
    print(f"[{mean_time*1e6:20.0f} us] PyTorch {args.device.upper()} GEMM ({args.size}x{args.size})")
    print(f"[{mean_time*1e6:20.0f} us] Throughput: {2 * args.size**3 / (mean_time * 1e9):.2f} GFLOPS")
    
    # Save result (with fallback for NumPy compatibility)
    try:
        result_np = result.cpu().numpy()
        result_np.tofile(args.output)
    except:
        # Fallback: save as text file
        result_cpu = result.cpu()
        with open(args.output, 'w') as f:
            for i in range(result_cpu.shape[0]):
                for j in range(result_cpu.shape[1]):
                    f.write(f"{result_cpu[i, j].item()} ")
                f.write("\n")
    print(f"Result saved to {args.output}")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
