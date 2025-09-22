# AIE GEMM Tile Sizes Reference

This document provides a comprehensive reference for hardware-supported atomic block (sub-tile) sizes for AI Engine GEMM operations based on the DSPLIB matrix multiplication library.

## Overview

The AI Engine supports different sub-tile sizes depending on:
- **Data types** of matrices A and B
- **AIE_VARIANT** (1 = AIE, 2 = AIE-ML)
- **Architecture** (AIE vs AIE-ML)

## Current Configuration

- **AIE_VARIANT**: 1 (AIE architecture)
- **Architecture**: AIE (not AIE-ML)
- **Platform**: VE2302 SOM

## Supported Data Types

The DSPLIB `matrix_mult_graph` supports the following data types:
- `int16`, `cint16`
- `int32`, `cint32` 
- `float`, `cfloat`

**Note**: `int8` and `bfloat16` are **NOT supported** by DSPLIB matrix multiplication library.

## Sub-Tile Sizes (AIE_VARIANT = 1)

### Primary Sub-Tile Sizes

| Data Type A | Data Type B | Sub-Tile (M×K×N) | Description |
|-------------|-------------|------------------|-------------|
| `int16` | `int16` | **4×4×4** | 16b × 16b multiplication |
| `int32` | `int32` | **4×4×2** | 32b × 32b multiplication |
| `float` | `float` | **4×4×2** | 32b × 32b floating point |
| `cint16` | `int16` | **4×4×2** | Complex 16b × 16b |
| `int16` | `cint16` | **4×2×2** | 16b × Complex 16b |
| `cint16` | `cint16` | **4×4×2** | Complex 16b × Complex 16b |
| `cint32` | `int16` | **2×4×2** | Complex 32b × 16b |
| `int16` | `cint32` | **2×4×2** | 16b × Complex 32b |
| `cint32` | `cint16` | **2×2×2** | Complex 32b × Complex 16b |
| `cint16` | `cint32` | **2×2×2** | Complex 16b × Complex 32b |
| `cint32` | `cint32` | **2×2×2** | Complex 32b × Complex 32b |
| `cfloat` | `float` | **2×4×2** | Complex float × float |
| `float` | `cfloat` | **2×4×2** | float × Complex float |
| `cfloat` | `cfloat` | **4×2×2** | Complex float × Complex float |

### Alternative Sub-Tile Sizes

Some combinations support alternative sizes:

| Data Type A | Data Type B | Primary | Alternative |
|-------------|-------------|---------|-------------|
| `int16` | `cint32` | 2×4×2 | **4×4×2** |
| `cint32` | `cint16` | 2×2×2 | **2×4×2** |
| `cint32` | `int32` | 2×2×2 | **2×4×2** |
| `cfloat` | `float` | 2×4×2 | **2×2×2** |
| `float` | `cfloat` | 2×4×2 | **2×2×2** |

## AIE-ML Sub-Tile Sizes (AIE_VARIANT = 2)

For AIE-ML architecture (not used in current configuration):

| Data Type A | Data Type B | Sub-Tile (M×K×N) |
|-------------|-------------|------------------|
| `int16` | `int16` | 4×4×4 |
| `int32` | `int32` | 4×4×4 |
| `cint16` | `int16` | 4×4×4 |
| `cint16` | `cint16` | 1×4×8 |
| `cint32` | `cint16` | 2×4×8 |
| `cint32` | `cint32` | 1×2×8 |

**Note**: AIE-ML does not support floating-point data types.

## Memory Requirements

### Word Length (WRD_LN) Calculation
- **WRD_LN** = 128 bits / data_type_bit_width
- **int16**: 128/16 = 8 elements per 128-bit word
- **int32**: 128/32 = 4 elements per 128-bit word  
- **float**: 128/32 = 4 elements per 128-bit word

### Sub-Tile Memory Usage
- **4×4×4**: 64 elements per sub-tile
- **4×4×2**: 32 elements per sub-tile
- **2×2×2**: 8 elements per sub-tile

## Current Implementation

The system automatically selects the **primary** sub-tile size for each data type:

```python
# From plioGen.py
def get_optimal_sub_tile_size(data_type, dim):
    if data_type in ["int16", "uint16"]:
        return 4, 4, 4  # Primary for int16
    elif data_type in ["int32", "uint32"]:
        return 4, 4, 2  # Primary for int32
    elif data_type == "float":
        return 4, 4, 2  # Primary for float
```

## Performance Considerations

1. **Larger sub-tiles** (4×4×4) generally provide better performance for `int16`
2. **Medium sub-tiles** (4×4×2) are optimal for `int32` and `float`
3. **Smaller sub-tiles** (2×2×2) are used for complex data types or as alternatives
4. **Memory constraints** may limit the maximum sub-tile size based on available tile memory

## Validation

All sub-tile sizes are validated against the DSPLIB library constraints:
- Source: `/home/mgrailoo/Vitis_Libraries/dsp/L2/meta/matrix_mult.py`
- Function: `getTilingScheme(typeA, typeB, aie_variant)`
- AIE_VARIANT: 1 (AIE architecture)

## References

- **DSPLIB Documentation**: `/home/mgrailoo/Vitis_Libraries/dsp/L2/meta/matrix_mult.json`
- **Validation Script**: `/home/mgrailoo/Vitis_Libraries/dsp/L2/meta/matrix_mult.py`
- **AIE Architecture**: VE2302 SOM with AIE (not AIE-ML)
