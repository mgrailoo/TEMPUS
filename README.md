<<<<<<< HEAD
# Versal_AIE_GEMM
=======
# Versal AI Engine GEMM Implementation

A comprehensive implementation of General Matrix Multiply (GEMM) operations using Xilinx AI Engine on Versal ACAP platforms. This project demonstrates high-performance matrix multiplication with configurable dimensions, data types, and optimization strategies.

## 🚀 Features

- **Multiple Matrix Sizes**: Support for 32×32 to 1024×1024 matrices
- **Multiple Data Types**: int16, int32, and float32 support
- **AI Engine Optimization**: Leverages Versal AI Engine array for maximum performance
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
├── docs/                      # Documentation
│   ├── AIE_GEMM_Tile_Sizes_Reference.md
│   ├── DATA_TYPE_SUPPORT_GUIDE.md
│   ├── ML_BOARD_SETUP_GUIDE.md
│   └── ...
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
  "TARGET": "hw_emu",
  "GEMM_SIZE": 1024,
  "DATA_TYPE": "int16",
  "SPLIT": 2,
  "CASC_LN": 8,
  "ENABLE_ML_BENCHMARKS": 1
}
```

### 3. Build and Run
```bash
# Build for hardware emulation
make run

# Build for hardware
make run TARGET=hw

# Generate reports
make report_metrics

# Run with ML benchmarking
make run ENABLE_ML_BENCHMARKS=1
```

## 📊 Performance Results

| Matrix Size | Data Type | AI Engine Time | PyTorch CPU | Speedup |
|-------------|-----------|----------------|-------------|---------|
| 32×32       | int16     | ~0.1ms        | ~0.5ms      | 5x      |
| 256×256     | int16     | ~2.5ms        | ~15ms       | 6x      |
| 1024×1024   | int16     | ~45ms         | ~280ms      | 6.2x    |

## 🔧 Configuration Options

### Matrix Dimensions
- **GEMM_SIZE**: 32, 64, 128, 256, 512, 1024
- **SPLIT**: Number of AI Engine splits (1-8)
- **CASC_LN**: Cascade levels (1-16)

### Data Types
- **int16**: 16-bit integer (best performance)
- **int32**: 32-bit integer (higher precision)
- **float**: 32-bit floating point

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
make pytorch_benchmark       # Run PyTorch comparison
make numpy_benchmark         # Run NumPy comparison

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

## 📚 Documentation

- **[AI Engine Architecture Guide](docs/versal_acap_ai_engine_programming_cheat_sheet.md)**
- **[Data Type Support](docs/DATA_TYPE_SUPPORT_GUIDE.md)**
- **[ML Setup Guide](docs/ML_BOARD_SETUP_GUIDE.md)**
- **[Tile Sizes Reference](docs/AIE_GEMM_Tile_Sizes_Reference.md)**

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
- Check the documentation in `docs/`
- Review the example configurations in `examples/`

---

**Built with ❤️ for high-performance AI/ML acceleration on Versal ACAP**
>>>>>>> master
