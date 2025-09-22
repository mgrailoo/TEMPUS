# Data Type Configurations Guide

## Overview
This guide provides working configurations for all supported data types in the AI Engine GEMM design.

## Supported Data Types

| Data Type | Config Value | Sub-Tile Size | DIM | GEMM_SIZE | Description |
|-----------|--------------|---------------|-----|-----------|-------------|
| `int16`   | 16           | 4×4×4         | 8   | 32        | 16-bit integer |
| `int32`   | 32           | 4×4×2         | 16  | 32        | 32-bit integer |
| `float`   | 33           | 4×4×2         | 16  | 32        | 32-bit floating point |

## Configuration Files

### int16 Configuration (Current Working)
```json
{
  "TARGET": "hw",
  "GEMM_SIZE": 32,
  "DIM": 8,
  "DATA_TYPE": "int16",
  "TILE_MEM_BYTES": 32768,
  "SPLIT": 2,
  "CASC_LN": 8,
  "ITER_CNT": 1,
  "N_SAMPLES": 1,
  "GEMM_INSTS": 1,
  "EN_TRACE": 0,
  "PL_FREQ": 312.5,
  "ENABLE_ML_BENCHMARKS": 0
}
```

### int32 Configuration (Recommended for 32-bit)
```json
{
  "TARGET": "hw",
  "GEMM_SIZE": 32,
  "DIM": 16,
  "DATA_TYPE": "int32",
  "TILE_MEM_BYTES": 32768,
  "SPLIT": 2,
  "CASC_LN": 8,
  "ITER_CNT": 1,
  "N_SAMPLES": 1,
  "GEMM_INSTS": 1,
  "EN_TRACE": 0,
  "PL_FREQ": 312.5,
  "ENABLE_ML_BENCHMARKS": 0
}
```

### float Configuration (For Floating Point)
```json
{
  "TARGET": "hw",
  "GEMM_SIZE": 32,
  "DIM": 16,
  "DATA_TYPE": "float",
  "TILE_MEM_BYTES": 32768,
  "SPLIT": 2,
  "CASC_LN": 8,
  "ITER_CNT": 1,
  "N_SAMPLES": 1,
  "GEMM_INSTS": 1,
  "EN_TRACE": 0,
  "PL_FREQ": 312.5,
  "ENABLE_ML_BENCHMARKS": 0
}
```

## Key Differences

### DIM Requirements
- **int16**: DIM must be multiple of 4 (4, 8, 12, 16, 20, 24, 28, 32...)
- **int32/float**: DIM must be multiple of 4 for M and K, multiple of 2 for N (8, 16, 24, 32...)

### Sub-Tile Sizes
- **int16**: 4×4×4 = 64 elements per sub-tile
- **int32/float**: 4×4×2 = 32 elements per sub-tile

### Memory Usage
- **int16**: 2 bytes per element
- **int32**: 4 bytes per element  
- **float**: 4 bytes per element

## Performance Characteristics

| Data Type | Memory Usage | Precision | Performance | Use Case |
|-----------|--------------|-----------|-------------|----------|
| int16     | 1x           | 16-bit    | Fastest     | High-speed integer operations |
| int32     | 2x           | 32-bit    | Medium      | High-precision integer operations |
| float     | 2x           | 32-bit FP | Medium      | Floating-point operations |

## Switching Between Data Types

1. **Update config.json** with desired DATA_TYPE and DIM
2. **Clean build** to remove old artifacts
3. **Rebuild** with new configuration
4. **Test** the new configuration

## Build Commands

```bash
# Clean and rebuild for new data type
make cleanall
make all

# Or just clean target and rebuild
make clean_tgt
make all
```

## Validation

Each configuration should:
- ✅ Compile AI Engine graph successfully
- ✅ Generate libadf.a without errors
- ✅ Compile host application successfully
- ✅ Run simulation without errors

## Troubleshooting

### Common Issues
1. **Template mismatch errors**: Check DIM is correct for data type
2. **Compilation failures**: Ensure sub-tile sizes match library requirements
3. **Memory errors**: Verify TILE_MEM_BYTES is sufficient

### Quick Fixes
- **int16 issues**: Use DIM=8, 16, 24, 32...
- **int32/float issues**: Use DIM=8, 16, 24, 32... (must be even for N dimension)
