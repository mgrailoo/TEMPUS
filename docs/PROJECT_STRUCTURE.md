# AI Engine GEMM Project Structure

## 📁 Directory Organization

```
ssh_workflow/
├── 📁 design/                    # Source code and design files
│   ├── 📁 aie_src/              # AI Engine source code
│   ├── 📁 host_app_src/         # Host application code
│   ├── 📁 pl_src/               # PL (Programmable Logic) source
│   ├── 📁 design_configs/       # Design configuration files
│   ├── 📁 system_configs/       # System configuration files
│   ├── 📁 directives/           # Vivado directives
│   ├── 📁 exec_scripts/         # Execution scripts
│   └── 📁 profiling_configs/    # Profiling configuration
├── 📁 docs/                     # Documentation
├── 📁 logs/                     # Build and compilation logs
├── 📁 reports/                  # Generated Vivado reports
│   └── 📁 gemm_32x32x32/x1/    # Specific build reports
├── 📁 scripts/                  # Utility scripts
│   └── 📄 extract_real_metrics.ps1  # Metrics extraction (WORKING)
├── 📁 platform_edge_hwemu/      # Platform files
├── 📄 Makefile                  # Main build system
├── 📄 sync_and_run.ps1          # Remote workflow management
├── 📄 real_metrics_summary.txt  # Current metrics report
└── 📄 README.md                 # Project documentation
```

## 🔧 Key Files

- **`Makefile`** - Main build system
- **`sync_and_run.ps1`** - Remote workflow and synchronization
- **`scripts/extract_real_metrics.ps1`** - Metrics extraction (WORKING)
- **`real_metrics_summary.txt`** - Current design metrics
- **`reports/gemm_32x32x32/x1/`** - Vivado reports directory

## 📊 Current Status

- ✅ Build system working
- ✅ Metrics extraction working
- ✅ Path issues fixed
- ✅ Redundant files cleaned up
