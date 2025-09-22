# AI Engine GEMM Project Documentation

Welcome to the comprehensive documentation for the **Versal AI Engine GEMM** project. This documentation covers all aspects of the project from setup and configuration to optimization and analysis.

## 📚 **Documentation Index**

### **🚀 Setup & Configuration**
- **[ML Benchmarks Configuration](README_ML_BENCHMARKS.md)** - Configure PyTorch and NumPy benchmarks
- **[Data Type Support Guide](DATA_TYPE_SUPPORT_GUIDE.md)** - Dynamic data type configuration (int4, int8, int16, bfloat16)
- **[GEMM DIM Configuration Guide](GEMM_DIM_Configuration_Guide.md)** - Optimal DIM parameter selection for power-of-2 GEMM sizes

### **📖 Reference Documentation**
- **[AIE GEMM Tile Sizes Reference](AIE_GEMM_Tile_Sizes_Reference.md)** - Hardware-supported atomic block sizes for AI Engine operations
- **[HLS Interface Optimization](README_Interface_Parameters.md)** - AXI memory interface parameter optimization for dma_hls.cpp

### **🔧 Tools & Analysis**
- **[Metrics Extraction Scripts](METRICS_EXTRACTION_README.md)** - Extract power, performance, and resource utilization metrics from Vivado reports
- **[Project Structure](PROJECT_STRUCTURE.md)** - Complete project organization and file structure overview

## 🎯 **Quick Start Guide**

### **For New Users:**
1. Start with [Project Structure](PROJECT_STRUCTURE.md) to understand the project layout
2. Read [Data Type Support Guide](DATA_TYPE_SUPPORT_GUIDE.md) for configuration options
3. Check [GEMM DIM Configuration Guide](GEMM_DIM_Configuration_Guide.md) for optimal parameters

### **For Developers:**
1. Review [HLS Interface Optimization](README_Interface_Parameters.md) for performance tuning
2. Use [AIE GEMM Tile Sizes Reference](AIE_GEMM_Tile_Sizes_Reference.md) for hardware constraints
3. Follow [Metrics Extraction Scripts](METRICS_EXTRACTION_README.md) for analysis

### **For Benchmarking:**
1. Configure [ML Benchmarks](README_ML_BENCHMARKS.md) for performance comparison
2. Extract metrics using [Metrics Extraction Scripts](METRICS_EXTRACTION_README.md)
3. Analyze results for optimization opportunities

## 📊 **Project Overview**

This project implements a **high-performance GEMM (General Matrix Multiply)** operation using **Xilinx Versal AI Engine** technology. It supports:

- **Multiple Data Types**: int4, int8, int16, bfloat16
- **Scalable Matrix Sizes**: 32×32 to 4096×4096
- **Optimized Performance**: AI Engine acceleration with HLS kernels
- **Comprehensive Analysis**: Power, timing, and resource utilization metrics

## 🔗 **Related Files**

- **Main Project**: [../README.md](../README.md) - Project overview and getting started
- **Build System**: [../Makefile](../Makefile) - Build configuration and targets
- **Workflow Management**: [../sync_and_run.ps1](../sync_and_run.ps1) - Remote workflow automation

## 📝 **Documentation Standards**

- **File Naming**: Descriptive names with underscores for spaces
- **Format**: Markdown with clear headings and code blocks
- **Structure**: Consistent organization with overview, details, and examples
- **Cross-References**: Links between related documentation files

## 🤝 **Contributing**

When adding new documentation:
1. Follow the existing naming conventions
2. Include clear headings and structure
3. Add cross-references to related files
4. Update this index file
5. Test all links and examples

---

**Last Updated**: December 2024  
**Project Version**: 1.0  
**Documentation Status**: Complete and Current
