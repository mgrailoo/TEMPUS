# ML Environment Setup Guide

## 🎯 Current Working Setup

Your AI Engine GEMM project is already configured to run PyTorch and NumPy benchmarks with **zero manual setup required**.

## ✅ What's Already Working

### 1. **Automatic ML Package Installation**
- `setup_ml_environment.sh` is automatically included in your SD card image
- When `ENABLE_ML_BENCHMARKS=1` in `config.json`, ML packages install automatically
- No manual intervention needed

### 2. **Complete ML Stack**
- **NumPy**: Matrix operations and benchmarks
- **PyTorch**: CPU-only ML framework for benchmarks  
- **Additional**: scipy, matplotlib, pandas
- **Compatibility**: NumPy 1.x (compatible with PyTorch 2.1.0)

### 3. **Integrated Build Process**
- ML script is packaged via Makefile: `PKG_FLAGS += --package.sd_file $(DESIGN_REPO)/setup_ml_environment.sh`
- Host application detects and installs packages when needed
- All timing results formatted consistently

## 🚀 How to Use

### **Option 1: Automatic (Recommended)**
```bash
# 1. Set ENABLE_ML_BENCHMARKS=1 in config.json (already set)
# 2. Build and run
.\sync_and_run.ps1
# Select option 3: Run build on remote
# Select option 4: Run hardware emulator
```

### **Option 2: Manual Installation (if needed)**
```bash
# On the target board, run once:
./setup_ml_environment.sh
./gemm_aie_xrt.elf a.xclbin
```

## 📊 Expected Output

When you run your application, you'll see:

```
=== ML BENCHMARK COMPARISON (ENABLED) ===
✓ ML packages pre-installed in PetaLinux image - ready to use!
Running NumPy CPU benchmark...
Running PyTorch CPU benchmark...

AI Engine GEMM Timing: 1234 us
NumPy CPU Timing: 5678 us  
PyTorch CPU Timing: 2345 us
```

## 🔧 Configuration

### **Enable/Disable ML Benchmarks**
Edit `design/design_configs/config.json`:
```json
{
  "ENABLE_ML_BENCHMARKS": 1,  // 1 = enabled, 0 = disabled
  "GEMM_SIZE": 32,
  "DIM": 16,
  "DATA_TYPE": "int16"
}
```

### **When ENABLE_ML_BENCHMARKS=1:**
- ML packages install automatically on first run
- Benchmarks run automatically
- No user prompts

### **When ENABLE_ML_BENCHMARKS=0:**
- Host application asks: "Would you like to run PyTorch and NumPy benchmarks? (y/N)"
- If "y": installs packages and runs benchmarks
- If "n": skips ML benchmarks

## 🎯 Result

**Your setup is complete and ready to use!** 

- ✅ ML packages install automatically
- ✅ Benchmarks run with consistent timing format
- ✅ No manual setup required
- ✅ Works with your existing build process

Just run `.\sync_and_run.ps1` and select options 3 and 4 to build and test!
