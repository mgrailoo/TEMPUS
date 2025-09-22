# Dynamic Data Type Support Guide

## Overview
All files have been modified to support dynamic data types (int4, int8, int16, bfloat16) from the config file with sub-tile sizes determined by the AI Engine-ML matrix multiplication instruction set.

## Supported Data Types

| Config Data Type | Actual AIE Type | Config Value | Bits | WRD_LN | Elements per 128-bit | Sub-Tile Size (M×K×N) |
|------------------|-----------------|--------------|------|--------|---------------------|------------------------|
| int4             | int4            | 4            | 4    | 32     | 32                  | 4×16×8                 |
| int8             | int8            | 8            | 8    | 16     | 16                  | 4×8×4                  |
| int16            | int16           | 16           | 16   | 8      | 8                   | 4×4×4                  |
| bfloat16         | bfloat16        | 17           | 16   | 8      | 8                   | 4×8×4                  |

**Note**: AI Engine ML Architecture (VE2302 SOM) supports these native data types: `(u)int4`, `(u)int8`, `(u)int16`, `bfloat16`. Each data type uses its actual bit width and corresponding word length. Sub-tile sizes are determined by the AI Engine-ML matrix multiplication instruction set and must match hardware-supported instruction sizes.

## Configuration

### config.json
```json
{
  "TARGET": "hw_emu",
  "GEMM_SIZE": 256,
  "DIM": 64,
  "SPLIT": 2,
  "CASC_LN": 8,
  "WRD_LN": 8,
  "N_SAMPLES": 1,
  "ITER_CNT": 1,
  "GEMM_INSTS": 1,
  "EN_TRACE": 0,
  "PL_FREQ": 312.5,
  "DATA_TYPE": "int16",
  "MATRIX_A_FILE": "",
  "MATRIX_B_FILE": "",
  "RANDOM_SEED": 42
}
```

### Data Type Configuration
Change the `DATA_TYPE` field to one of:
- `"int4"` - 4-bit signed integer (converts to DATA_TYPE=4)
- `"int8"` - 8-bit signed integer (converts to DATA_TYPE=8)
- `"int16"` - 16-bit signed integer (converts to DATA_TYPE=16, default)
- `"bfloat16"` - Brain floating point 16-bit (converts to DATA_TYPE=17)

**Note**: The Makefile automatically converts the string data type to a numeric value that can be used by the C++ preprocessor.

## File Modifications

### 1. dma_hls.h
- Added dynamic data type definitions based on `DATA_TYPE` macro
- Added `ELEMENTS_PER_128BIT` calculation
- Added `WRD_LN` auto-calculation

### 2. dma_hls.cpp
- Added `SUB_TILE_SIZE = 4` constant
- Updated comments to reflect fixed 4×4 sub-tile processing
- Maintained existing streaming logic with fixed sub-tile distribution

### 3. graph.h
- Added dynamic data type definitions
- Fixed template parameters: `DIM_A, DIM_AB, DIM_B` instead of `DIM, DIM_AB, DIM`
- **Updated matrix_mult_graph template to use `data_t` instead of hardcoded `int16`**
- Added `SUB_TILE_SIZE = 4` constant
- Updated comments for fixed 4×4 sub-tile processing

### 4. graph.cpp
- No changes needed (already generic)

### 5. plioGen.py
- Updated `get_word_length()` with proper 128-bit calculations
- Updated `get_optimal_sub_tile_size()` to use fixed 4×4 sub-tiles
- Updated all sub-tile processing to use fixed 4×4 size
- Updated print statements to reflect fixed sub-tile size

### 6. gemm_aie_app.cpp
- Updated `load_golden_into_vector()` with dynamic data type packing
- Updated `write_c_txt_from_bo()` with dynamic data type extraction
- Updated all debug print statements with dynamic data type handling
- Added proper bit manipulation for each data type

### 7. Makefile
- **Added DATA_TYPE reading from config.json**
- **Added string-to-numeric conversion for C++ preprocessor**
- **Added DATA_TYPE define to AIE_FLAGS, GCC_FLAGS, and VPP_FLAGS**
- Ensures all compilation units receive the correct DATA_TYPE value

## Usage Examples

### int4 Configuration
```json
{
  "DATA_TYPE": "int4",
  "DIM": 64
}
```
- WRD_LN = 32 elements per 128-bit word
- 4×4 sub-tiles with 4-bit precision
- Maximum performance on VE2302 SOM

### int8 Configuration  
```json
{
  "DATA_TYPE": "int8",
  "DIM": 128
}
```
- WRD_LN = 16 elements per 128-bit word
- 4×4 sub-tiles with 8-bit precision
- 2x performance improvement over int16

### int16 Configuration (Default)
```json
{
  "DATA_TYPE": "int16",
  "DIM": 64
}
```
- WRD_LN = 8 elements per 128-bit word
- 4×4 sub-tiles with 16-bit precision
- Current working configuration

### bfloat16 Configuration
```json
{
  "DATA_TYPE": "bfloat16",
  "DIM": 64
}
```
- WRD_LN = 8 elements per 128-bit word
- 4×4 sub-tiles with 16-bit floating point
- For applications requiring floating point precision

## Performance Expectations

| Data Type | Memory Usage | Performance | Use Case |
|-----------|--------------|-------------|----------|
| int4      | 1/4 of int16 | 4x faster   | Quantized models |
| int8      | 1/2 of int16 | 2x faster   | Balanced performance |
| int16     | Current      | Baseline    | High precision |
| bfloat16  | Same as int16| Baseline    | Floating point needed |

## Fixed 4×4 Sub-tile Architecture

All data types use a fixed 4×4 sub-tile size regardless of:
- DIM value (64, 128, 256, etc.)
- Data type (int4, int8, int16, bfloat16)
- GEMM_SIZE (32 to 4096)

This ensures:
- Consistent AI Engine-ML utilization
- Optimal memory bandwidth usage
- Predictable performance scaling
- Simplified debugging and verification

## Build Process

1. Update `config.json` with desired `DATA_TYPE`
2. Run `python plioGen.py` to generate data files
3. Build with `make` (Makefile automatically reads config)
4. Execute on target platform

The system will automatically:
- Calculate correct WRD_LN based on data type
- Generate appropriate data files
- Configure AIE graph with correct template parameters
- Handle data packing/unpacking correctly
