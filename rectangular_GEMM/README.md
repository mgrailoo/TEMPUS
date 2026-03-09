# Versal AI-ML Edge Engine GEMM Implementation

An implementation of General Matrix Multiply (GEMM) operations using Xilinx AI/ML Engine on Versal ACAP platforms. This project demonstrates high-performance matrix multiplication with configurable dimensions, data types, and optimization strategies.


## 🚀 Features

- **Rectangular Matrix Support**: Support for rectangular GEMM operations (A×AB×B format)
- **Multiple Matrix Sizes**: Support for square matrices (32×32 to 1024×1024) and rectangular matrices (e.g., 2048×64×8)
- **Multiple Data Types**: int16, and int32 support
- **AI/ML Engine Optimization**: Leverages Versal AI/ML Engine array for maximum performance
- **HLS Kernels**: Custom DMA kernels for efficient data movement
- **ML Benchmarking**: Built-in PyTorch and NumPy performance comparisons
- **Comprehensive Reporting**: Vivado utilization and power analysis
- **Cross-Platform**: Works on both hardware emulation and real hardware

## 📁 Project Structure

```
Versal_AIE_GEMM/
├── README.md                    # This file
├── .gitignore                  # Git ignore rules
├── Makefile                    # Build system
├── design/                     # Source code
│   ├── aie_src/               # AI Engine sources
│   │   ├── graph.cpp          # Main AI Engine graph
│   │   ├── graph.h            # Graph header
│   │   └── aiesim_data/       # Simulation data
│   ├── pl_src/                # HLS kernels
│   │   ├── dma_hls.cpp        # DMA kernel implementation
│   │   └── dma_hls.h          # DMA kernel header
│   ├── host_app_src/          # Host application
│   │   ├── gemm_aie_app.cpp   # Main host application
│   │   ├── gemm_utils.cpp     # Utility functions
│   │   ├── pytorch_benchmark.py
│   │   └── numpy_benchmark.py
│   ├── design_configs/        # Configuration files
│   ├── system_configs/        # System configuration
│   └── vivado_metrics_scripts/ # Vivado reporting
├── scripts/                   # Utility scripts
│   ├── setup_platform_ml.sh
│   ├── sync_and_run.ps1
│   ├── fix_aie_checksum.tcl
│   └── ...
├── examples/                  # Example configurations
│   ├── configs/              # Configuration examples
│   └── Commands              # Command examples
└── platform_edge_hwemu/      # Platform files
```

## 🛠️ Prerequisites

- **Xilinx Vitis 2024.1** or later
- **Vivado 2024.1** or later
- **Versal ACAP Development Board** (for hardware testing)
- **Python 3.8+** (for ML benchmarking)
- **Git** (for version control)

## 🚀 Quick Start

### 1. Clone and Setup
```bash
git clone <your-repo-url>
cd Versal_AIE_GEMM
```

### 2. Configure Your Design
Edit `design/design_configs/config.json`:
```json
{
  "TARGET": "hw",
  "GEMM_SIZE_A": 2048,
  "GEMM_SIZE_AB": 64,
  "GEMM_SIZE_B": 8,
  "DIM": 16,
  "DATA_TYPE": "int16",
  "_comment_DATA_TYPE": "int32 or int16",
  "TILE_MEM_BYTES": 32768,
  "SPLIT_A": 2,
  "SPLIT_B": 2,
  "CASC_LN_AB": 8,
  "_comment_SPLIT_A": "Decomposition factor for rows of Matrix A",
  "_comment_SPLIT_B": "Decomposition factor for columns of Matrix B",
  "_comment_CASC_LN_AB": "Decomposition factor for AB dimension (columns of A, rows of B)",
  "ITER_CNT": 1,
  "N_SAMPLES": 1,
  "GEMM_INSTS": 1,
  "EN_TRACE": 0,
  "PL_FREQ": 312.5,
  "ENABLE_ML_BENCHMARKS": 1,
  "WRD_LN": 8,
  "_comment_WRD_LN": "8 for int16 and 4 for int32 elements per 128-bit PLIO",
  "SUB_TILE_A": 4,
  "SUB_TILE_AB": 4,
  "SUB_TILE_B": 4,
  "_comment_SUB_TILE": "Sub-tile dimensions: A (rows of Matrix A), AB (cols of A / rows of B), B (cols of Matrix B)",
  "GRAPH_ITER_CNT": 128,
  "_comment_GRAPH_ITER_CNT": "Calculated automatically: (GEMM_SIZE_A * GEMM_SIZE_B / SPLIT_B) / (DIM_A * DIM_B)"
}
```

**Important**: All three `GEMM_SIZE_A`, `GEMM_SIZE_AB`, and `GEMM_SIZE_B` values are **REQUIRED** and must be explicitly provided. No default values are used.

### 3. Build and Run
For having the pytorch matmult result on the board, run ./setup_ml_environment.sh --offline before running gemm_aie_xrt.elf ...


```bash
# Build for hardware emulation
make run

# Build for hardware
make run TARGET=hw

# Generate reports
make report_metrics
```

## 📊 Performance Results

### Rectangular Matrix Performance (mut be revised!)

| Matrix Size (A×AB×B)  | AIE engine time (16 AIE cores, INT16) |Pytorch CPU (ARM A72, dual-core, float)                      |
|-----------------------|---------------------------------------|-------------------------------------------------------------|


## 🔧 Configuration Options

### Matrix Dimensions
- **GEMM_SIZE_A**: Rows of Matrix A (e.g., 2048) - **REQUIRED**
- **GEMM_SIZE_AB**: Columns of A / Rows of B (e.g., 64) - **REQUIRED**
- **GEMM_SIZE_B**: Columns of Matrix B (e.g., 8) - **REQUIRED**
- **DIM_A**: Tile dimension for A dimension (calculated as min(DIM, GEMM_SIZE_A/SPLIT_A))
- **DIM_AB**: Tile dimension for AB dimension (always GEMM_SIZE_AB // CASC_LN_AB)
- **DIM_B**: Tile dimension for B dimension (calculated as min(DIM, GEMM_SIZE_B/SPLIT_B))
- **DIM**: Base tile dimension used for calculations (default: 16)
- **SPLIT_A**: Decomposition factor for rows of Matrix A (default: 2)
- **SPLIT_B**: Decomposition factor for columns of Matrix B (default: 2)
- **CASC_LN_AB**: Cascade levels for AB dimension (default: 8)

**Note**: All three GEMM_SIZE values (A, AB, B) must be explicitly provided in `config.json`. No default values are used.

### Data Types
- **int16**: 16-bit integer (best performance)
- **int32**: 32-bit integer (higher precision)

### Build Targets
- **hw_emu**: Hardware emulation (faster iteration)
- **hw**: Real hardware (final deployment)

## 📈 Available Targets

```bash
# Build targets
make help                    # Show all available targets
make run                     # Build and run emulation
make sd_card                 # Build complete design
make kernels                 # Build HLS kernels only
make graph                   # Build AI Engine graph only
make application             # Build host application only

# Analysis targets
make report_metrics          # Generate Vivado reports
make vcd                     # Generate VCD and XPE files


# Cleanup targets
make clean_tgt              # Clean specific target
make cleanall               # Clean all build artifacts
```

## 🧪 Testing and Validation

### Hardware Emulation
```bash
make run TARGET=hw_emu
# Runs in QEMU virtual environment
# Includes ML benchmarking
# Generates performance reports
```

### Hardware Testing
```bash
make run TARGET=hw
# Generates SD card image
# Deploy to Versal board
# Run on actual hardware
```


## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- Xilinx for the Versal ACAP platform and AI Engine
- Vitis Libraries for DSP functions
- Open source community for benchmarking tools

## 📞 Support

For questions and support:
- Create an issue in this repository


**Built for high-performance AI/ML acceleration on Versal ACAP**


## 🏗️ Architecture Support

This implementation targets **AIEML (AI Engine ML)** hardware generation, which maps to AIE‑ML (v1) `__AIE_ARCH__ == 20` and thus:
- **Enables 64-bit accumulators** (`__SUPPORTS_ACC64__`) and 32-bit accumulators (`__SUPPORTS_ACC32__`)
- **Provides ML features including BF16 support** (`_SUPPORTS_BFLOAT16_`)
- **Versal ACAP Platform**: Optimized for Versal edge and premium series devices

For reference, on AIEML and related architectures the accumulator support is defined as:

```c
#if (__AIE_ARCH__ == 20) || (__AIE_ARCH__ == 21) || (__AIE_ARCH__ == 22) || \
    (__AIEARCH__ == 20) || (__AIEARCH__ == 21) || (__AIEARCH__ == 22)
#define __SUPPORTS_ACC32__
#define __SUPPORTS_ACC64__
#endif
```

### Data type support on AIE‑MLv1 / AIE‑MLv2

- On AIE‑ML generations (ACC64/ACC32 enabled), the BF16 scalar type exists and is usable at the application boundary; however, with the DSPLIB L1/L2 `matrix_mult` we are using, native GEMM for float×float or bf16×bf16 on ACC64 is not implemented. This is why float/bf16 selections fail in compilation with “no supported modes.”

- For `matrix_mult.hpp` ACC64 path (AIE‑ML, AIE‑MLv2):
  - Supported: integer types (`int16`, `cint16`, `int32`, `cint32`).
  - Not supported in this kernel path: floating‑point types (`float`, `cfloat`).

- General rule (from DSPLIB docs for this kernel family):
  - Integer×Integer is supported (e.g., `int16×int16`, `int32×int32`, and complex integer variants).
  - Float×Float is supported only on older ACC48 paths (not ACC64 in this header).
  - Mixed Integer×Float is not supported.


### Hardware Configuration
The `aie_primitive.json` file contains the board-specific AI Engine configuration:
- **AI Engine Array**: 17 columns with compute tiles in row 2
- **Memory Tiles**: Located in row 1 for data storage
- **Shim Tiles**: Rows 0-1 for data movement between PL and AI Engine
- **Hardware Generation**: AIEML (v1) (`__AIE_ARCH__ == 20`, ACC64/BF16 capable)

