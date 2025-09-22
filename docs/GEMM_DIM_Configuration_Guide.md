# GEMM DIM Configuration Guide

This guide provides optimal DIM sizes (powers of 2) for power-of-2 GEMM_SIZE values in AIE applications.

## Memory Constraints

- **Maximum AIE memory group size**: 32,768 bytes
- **Data type**: int16 (2 bytes per element)
- **Ping-pong buffering**: 2x memory usage
- **Memory formula**: DIM² × 2 bytes × 2 (ping-pong) ≤ 32,768 bytes
- **Maximum DIM**: 90 (√(32768/4) ≈ 90.5)

## Available DIM Sizes (Powers of 2)

| DIM | Memory Usage | Memory % | Status |
|-----|--------------|----------|--------|
|   4 |    64 bytes |   0.2% | ✅ Safe |
|   8 |   256 bytes |   0.8% | ✅ Safe |
|  16 |  1024 bytes |   3.1% | ✅ Safe |
|  32 |  4096 bytes |  12.5% | ✅ Safe |
|  64 | 16384 bytes |  50.0% | ✅ Safe |
|  90 | 32400 bytes |  98.9% | ⚠️ High |

## Power-of-2 GEMM_SIZE to DIM Mapping

| GEMM_SIZE | Optimal DIM | Memory Usage | Memory % | Performance Level | Notes |
|-----------|-------------|--------------|----------|-------------------|-------|
|   32 |  32 |  4096 bytes |  12.5% | Medium | Perfect fit, Fast compilation |
|   64 |  64 | 16384 bytes |  50.0% | High | Perfect fit, Optimal balance |
|  128 |  64 | 16384 bytes |  50.0% | High | Perfect fit, Optimal balance |
|  256 |  64 | 16384 bytes |  50.0% | High | Perfect fit, Optimal balance |
|  512 |  64 | 16384 bytes |  50.0% | High | Perfect fit, Optimal balance |
| 1024 |  64 | 16384 bytes |  50.0% | High | Perfect fit, Optimal balance |
| 2048 |  64 | 16384 bytes |  50.0% | High | Perfect fit, Optimal balance |
| 4096 |  64 | 16384 bytes |  50.0% | High | Perfect fit, Optimal balance |

## Alternative DIM Configurations

### For Faster Compilation (Lower Memory Usage):
| GEMM_SIZE | Alternative DIM | Memory Usage | Memory % | Performance Level | Notes |
|-----------|-----------------|--------------|----------|-------------------|-------|
|   32 |  16 |  1024 bytes |   3.1% | Low | Very fast compilation |
|   64 |  32 |  4096 bytes |  12.5% | Medium | Fast compilation |
|  128 |  32 |  4096 bytes |  12.5% | Medium | Fast compilation |
|  256 |  32 |  4096 bytes |  12.5% | Medium | Fast compilation |
|  512 |  32 |  4096 bytes |  12.5% | Medium | Fast compilation |
| 1024 |  32 |  4096 bytes |  12.5% | Medium | Fast compilation |
| 2048 |  32 |  4096 bytes |  12.5% | Medium | Fast compilation |
| 4096 |  32 |  4096 bytes |  12.5% | Medium | Fast compilation |

### For Maximum Performance (Higher Memory Usage):
| GEMM_SIZE | Max Performance DIM | Memory Usage | Memory % | Performance Level | Notes |
|-----------|---------------------|--------------|----------|-------------------|-------|
|   32 |  32 |  4096 bytes |  12.5% | Medium | Already optimal |
|   64 |  64 | 16384 bytes |  50.0% | High | Already optimal |
|  128 |  90 | 32400 bytes |  98.9% | Very High | Use with caution |
|  256 |  90 | 32400 bytes |  98.9% | Very High | Use with caution |
|  512 |  90 | 32400 bytes |  98.9% | Very High | Use with caution |
| 1024 |  90 | 32400 bytes |  98.9% | Very High | Use with caution |
| 2048 |  90 | 32400 bytes |  98.9% | Very High | Use with caution |
| 4096 |  90 | 32400 bytes |  98.9% | Very High | Use with caution |

## Recommendations

### For Power-of-2 GEMM_SIZE Applications:

#### **Optimal Configuration (Recommended):**
- **GEMM_SIZE=32**: Use DIM=32 (12.5% memory, perfect fit)
- **GEMM_SIZE=64**: Use DIM=64 (50% memory, perfect fit)
- **GEMM_SIZE=128-4096**: Use DIM=64 (50% memory, optimal balance)

#### **Fast Compilation Configuration:**
- **All GEMM_SIZE values**: Use DIM=32 (12.5% memory, fast compilation)

#### **Maximum Performance Configuration:**
- **GEMM_SIZE=32-64**: Already optimal with current DIM
- **GEMM_SIZE=128-4096**: Use DIM=90 (98.9% memory, maximum performance)

### Performance vs Memory Trade-offs:

| Configuration | Memory Usage | Compilation Speed | Runtime Performance | Use Case |
|---------------|--------------|-------------------|-------------------|----------|
| DIM=32 | 12.5% | Very Fast | Medium | Development, Testing |
| DIM=64 | 50.0% | Medium | High | Production (Recommended) |
| DIM=90 | 98.9% | Slow | Very High | Maximum Performance |

## Usage Example

To use this configuration in your `config.json`:

```json
{
  "GEMM_SIZE": 256,
  "DIM": 64,
  "DATA_TYPE": "int16"
}
```

## Key Insights for Power-of-2 GEMM_SIZE

### **Perfect Fits:**
- **GEMM_SIZE=32**: DIM=32 (perfect fit, no remainder)
- **GEMM_SIZE=64**: DIM=64 (perfect fit, no remainder)
- **GEMM_SIZE=128, 256, 512, 1024, 2048, 4096**: DIM=64 (perfect fit, no remainder)

### **Memory Efficiency:**
- **DIM=32**: Uses only 12.5% of available memory (4,096 bytes)
- **DIM=64**: Uses 50% of available memory (16,384 bytes) - optimal balance
- **DIM=90**: Uses 98.9% of available memory (32,400 bytes) - maximum possible

### **Performance Characteristics:**
- **Small GEMM_SIZE (32-64)**: Can use matching DIM for perfect efficiency
- **Large GEMM_SIZE (128-4096)**: DIM=64 provides optimal balance across all sizes
- **All power-of-2 GEMM_SIZE values**: Work perfectly with power-of-2 DIM values

### **Best Practices:**
- Use DIM=64 for production applications (optimal performance/memory balance)
- Use DIM=32 for development and testing (fast compilation)
- Use DIM=90 only for maximum performance requirements
- All configurations use powers of 2 for optimal AIE performance
- No remainder handling needed for power-of-2 configurations
