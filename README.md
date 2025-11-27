# Versal AI-ML Edge Engine GEMM Implementation

Animplementation of General Matrix Multiply (GEMM) operations using Xilinx AI/ML Engine on Versal ACAP platforms. This project demonstrates high-performance matrix multiplication with configurable dimensions, data types, and optimization strategies.

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

## 🚀 Features

- **Multiple Matrix Sizes**: Support for 32×32 to 1024×1024 matrices
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
  "GEMM_SIZE": 32,
  "DIM": 16,
  "_comment_DIM": "Changed from 4 to GEMM_SIZE/2 if it fits output matrix in 32kB memory",
  "DATA_TYPE": "int16",
  "_comment_DATA_TYPE": "int32 or int16",
  "TILE_MEM_BYTES": 32768,
  "SPLIT": 2,
  "CASC_LN": 8,
  "ITER_CNT": 1,
  "N_SAMPLES": 1,
  "GEMM_INSTS": 1,
  "EN_TRACE": 0,
  "PL_FREQ": 312.5,
  "ENABLE_ML_BENCHMARKS": 1,
  "WRD_LN": 8,
  "_comment_WRD_LN": "8 for int16 and 4 for int32 elements per 128-bit PLIO",
  "SUB_TILE_M": 4,
  "SUB_TILE_K": 4,
  "SUB_TILE_N": 4,
  "_comment_SUB_TILE_N": "2 for int32 and 4 for int16",
  "GRAPH_ITER_CNT": 2,
  "_comment_GRAPH_ITER_CNT": "Calculated automatically based on DIM and GEMM_SIZE"
}
```

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

## 🔧 Configuration Options

### Matrix Dimensions
- **GEMM_SIZE**: 32, 64, 128, 256, 512, 1024
- **SPLIT**: Number of AI Engine splits (2)
- **CASC_LN**: Cascade levels (8)

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

