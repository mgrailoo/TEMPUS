# Rectangular GEMM (End-to-End) on Versal AI Engine ML

End-to-end **general matrix multiply (GEMM)** for Versal ACAP: PL HLS DMA moves data between DDR and the **AI Engine ML** array, with a host XRT application, optional PyTorch/NumPy benchmarks on the board, and Vivado-oriented reporting flows.

This directory is a self-contained Vitis design (Makefile + `design/` sources). It is intended as a **reference / starting point**, not a guaranteed production deliverable.

---

## Contents

- [Features](#features)
- [Repository layout](#repository-layout)
- [Prerequisites](#prerequisites)
- [Tested hardware (example)](#tested-hardware-example)
- [Quick start](#quick-start)
- [Configuration](#configuration)
- [Common Makefile targets](#common-makefile-targets)
- [Hardware vs emulation](#hardware-vs-emulation)
- [Correctness and PL/AIE data layout](#correctness-and-plaie-data-layout)
- [Architecture notes (AIEML)](#architecture-notes-aieml)
- [Performance](#performance)
- [Contributing](#contributing)
- [License](#license)
- [Acknowledgments](#acknowledgments)

---

## Features

- **Rectangular GEMM** \(C = A \times B\) with explicit **GEMM_SIZE_A × GEMM_SIZE_AB × GEMM_SIZE_B**.
- **Integer data types**: **`int16` and `int32`** are both supported end-to-end (host, HLS DMA, AI Engine graph, PLIO goldens). The main packing difference is **`WRD_LN`**: **8** elements per 128-bit PLIO word for int16, **4** for int32. The Makefile corrects a wrong `WRD_LN` for the chosen type (with a warning). Float is not supported on this path (see [Architecture notes](#architecture-notes-aieml)). **Newer Vitis / Vitis DSP Library releases may add or expose additional `matrix_mult` data types** (e.g. floating-point or other precisions on AI Engine ML); enabling those here would require graph, DMA, host, and golden-generator changes beyond this README.
- **Sub-tiles**: **`SUB_TILE_A`**, **`SUB_TILE_AB`**, **`SUB_TILE_B`** default to **4×4×4** for int16 and int32 when omitted; **`DIM_B`** (and related tile sizes) must be divisible by **`SUB_TILE_B`**. After changing type or sub-tiles, regenerate simulation I/O (see [Correctness](#correctness-and-plaie-data-layout)).
- **Tiling and decomposition**: configurable split/cascade/tile parameters via `design/design_configs/config.json`.
- **HLS DMA kernel** (`design/pl_src/dma_hls.cpp`) for streaming A/B in and C out.
- **Optional ML benchmarks**: PyTorch and NumPy scripts under `design/host_app_src/` (when enabled in config).
- **Reports**: utilization / power-style flows via Makefile targets (hardware-oriented).
- **Targets**: `hw_emu` (emulation) and `hw` (board).

---

## Repository layout

```
rectangular_gemm_end_to_end/
├── README.md
├── Makefile
├── sync_and_run.sh            # Optional: config sync, build, compare (paths may need editing)
├── Mahdieh_env_setup.sh       # Example Vitis/platform env (edit paths for your site)
├── design/
│   ├── aie_src/                 # AI Engine graph (graph.cpp, graph.h), constraints, aiesim data
│   ├── pl_src/                  # HLS DMA (dma_hls.cpp / dma_hls.h)
│   ├── host_app_src/            # Host app (gemm_aie_app.cpp, gemm_utils.*), benchmarks
│   ├── design_configs/          # config.json, generated gemm_config.h
│   ├── system_configs/          # e.g. x1.cfg connectivity
│   ├── directives/              # Compiler / HLS directives as used by the flow
│   ├── exec_scripts/            # Runtime helper scripts packaged or used with the design
│   ├── vivado_metrics_scripts/  # Reporting helpers
│   ├── ml_wheels/               # Optional offline PyTorch wheels (see ml_wheels/README.md)
│   └── setup_ml_environment.sh  # Board-side ML env setup (also copied to SD card package)
├── scripts/                     # e.g. metrics extraction (PowerShell)
├── logs/                        # Build logs (local; typically gitignored in parent repo)
├── reports/                     # Generated reports (local)
└── build/                       # Vitis build output (local; do not commit large artifacts)
```

If you publish **only** this folder on GitHub, clone it and work from the repository root (the directory that contains this `README.md` and `Makefile`).

---

## Prerequisites

- **AMD Vitis** (tested with **2024.1** or compatible; align Vivado/Vitis versions).
- **AMD Vivado** (matching Vitis release).
- A **Versal** board and **platform** (`.xpfm`) supported by your tool version — you must set **`PLATFORM`** to that platform path for the Makefile (see [Quick start](#quick-start)).
- **Python 3.8+** (JSON parsing in the Makefile, benchmarks, utilities).
- **Xilinx/AMD environment**: `source <Vitis_install>/settings64.sh` (and any board-specific setup your organization uses).

---

## Tested hardware (example)

Bring-up for this tree has been done on **AMD Versal AI Edge** silicon (**VE2302**), using **Vitis 2024.1** with a **custom edge platform** (`.xpfm`), e.g. the path shown in **`Mahdieh_env_setup.sh`** (`platform_edge_hwemu/.../platform_edge_hwemu.xpfm`). Linked `xclbin`/XSA names in the build flow may include **`ve2302`** as the part shorthand.

That platform path is **machine-specific**; fork/clone users must point **`PLATFORM`** at **their** installed or rebuilt `.xpfm`. Other **Versal** parts (AI Core, AI Edge, premium) are not validated here but may work if you provide a matching platform and satisfy AI Engine / PL resource and timing.

---

## Quick start

### 1. Clone

```bash
git clone https://github.com/<your-org>/<your-repo>.git
cd <your-repo>/Versal_AI_ML_Engines_GEMM/rectangular_gemm_end_to_end   # adjust path if your layout differs
```

If this repo is the **root** of your GitHub project, `cd` into that root instead.

### 2. Tool environment and platform

```bash
source /tools/Xilinx/Vitis/2024.1/settings64.sh   # example path

# Required: path to your Versal base platform (.xpfm)
export PLATFORM=/path/to/your_platform.xpfm
```

Without `PLATFORM`, the Makefile cannot invoke the AI Engine compiler or `v++` linking.

### 3. Configure the design

Edit **`design/design_configs/config.json`**. The Makefile **requires** explicit **`GEMM_SIZE_A`**, **`GEMM_SIZE_AB`**, and **`GEMM_SIZE_B`** (no silent defaults for those three).

The checked-in file is the authoritative example; it includes fields such as `TARGET`, `DATA_TYPE`, `DIM`, `SPLIT_A` / `SPLIT_B`, `CASC_LN_AB`, `SUB_TILE_*`, `GRAPH_ITER_CNT`, `PL_FREQ`, `AIE_RUNTIME_RATIO`, `USE_DDR_ONLY_MODE`, `SIMPLE_OUT_C`, and `ENABLE_ML_BENCHMARKS`. After editing sizes, run a build so **`GRAPH_ITER_CNT`** and **`gemm_config.h`** stay consistent (the Makefile updates `GRAPH_ITER_CNT` in `config.json` when generating the header).

Validate tiling before long builds:

```bash
python3 design/design_configs/validate_gemm_config.py design/design_configs/config.json
```

For cubic sizes and valid `(N, DIM)` pairs: `python3 design/design_configs/validate_gemm_config.py --suggest-cubic`.

### 4. Build and run

```bash
make help              # discover targets

# Emulation (faster iteration)
make run TARGET=hw_emu

# Hardware SD card package (long build)
make run TARGET=hw
# or: make sd_card
```

### 5. PyTorch on the board (optional)

On the **SD card root** (as packaged), run:

```bash
./setup_ml_environment.sh --offline
```

**`--offline`** installs PyTorch (and dependencies) from the wheel tree under **`design/ml_wheels/`** only—**no Internet on the board is required**—which enables the optional **on-board PyTorch GEMM benchmarking** path (same shapes as the AIE run when **`ENABLE_ML_BENCHMARKS`** is on in **`config.json`**; see **`design/host_app_src/pytorch_benchmark.py`**). Run this once **before** **`gemm_aie_xrt.elf`** if you want that comparison. For details on the wheel layout, see **`design/ml_wheels/README.md`**. In the source tree, the script lives at **`design/setup_ml_environment.sh`**.

---

## Configuration

| Area | Key ideas |
|------|-----------|
| **Sizes** | `GEMM_SIZE_A`, `GEMM_SIZE_AB`, `GEMM_SIZE_B` define \(A_{M\times K}\), \(B_{K\times N}\). |
| **Tiling** | `DIM`, `SPLIT_A`, `SPLIT_B`, `CASC_LN_AB` drive tile dimensions `DIM_A`, `DIM_AB`, `DIM_B` (computed in the Makefile). |
| **Data type** | `DATA_TYPE`: `int16` or `int32`. **`WRD_LN`**: 8 (int16) or 4 (int32); Makefile can override a mismatch with a warning. Host and `dma_hls` use `DATA_TYPE` for element width in `ap_int<128>` words. |
| **Sub-tiles** | `SUB_TILE_A`, `SUB_TILE_AB`, `SUB_TILE_B` (and legacy `SUB_TILE_M/K/N` in JSON): use **4×4×4** for both int16 and int32 in this project unless you know your flow needs otherwise. **`SIMPLE_OUT_C=0`** uses deeper AXIS FIFO depths derived from geometry. |
| **Output layout** | `SIMPLE_OUT_C`: `1` = compare `c.txt` to `c_golden.txt`; `0` = full de-tile to row-major `matrix_C_golden.txt` semantics. |
| **Runtime** | `AIE_RUNTIME_RATIO` adjusts AI Engine kernel `adf::runtime<ratio>` (see comment in `config.json`). |
| **DDR** | `USE_DDR_ONLY_MODE` forces DDR-only buffering behavior for experiments. |

**Build target** is `TARGET`: `hw` or `hw_emu` in `config.json` (you can also pass `TARGET=` on the `make` command line where supported).

---

## Common Makefile targets

```bash
make help           # All targets and short descriptions
make sd_card        # Full flow: kernels, AIE graph, link, host app, package
make run            # Build and run (per TARGET)
make kernels        # HLS kernels only
make graph          # AI Engine graph only
make application    # Host application only
make report_metrics # Vivado-style utilization reports (hardware-oriented)
make vcd            # VCD / XPE-oriented flows where applicable
make cleanall       # Remove build artifacts
```

---

## Hardware vs emulation

| Mode | Typical use |
|------|-------------|
| **`hw_emu`** | QEMU-based hardware emulation; quicker feedback, not final performance. |
| **`hw`** | Bitstream + SD image for a real Versal board; use your organization’s programming and boot procedure. |

---

## Correctness and PL/AIE data layout

The end-to-end path (host-loaded files through **`c.txt`**) must match the Python golden flow in **`design/aie_src/aiesim_data/`** (`plioGen.py`, `plio_utils.py`, `compare_outputs.py`) and **`design/pl_src/dma_hls.cpp`**.

- **Matrix A**: the host loads **`matrix_A_input.txt`** as row-major \( \text{GEMM\_SIZE\_A} \times \text{GEMM\_SIZE\_AB} \) packed into 128-bit words; **`inp_A`** streams cascade order consistent with **`a0_casc*.txt`** from `plioGen.py`.
- **Matrix B**: the host loads **`b_golden.txt`** (interleaved cascade lines); **`inp_B`** matches the **`b*_casc*.txt`** set used to build that file.
- **Matrix C**: the AI Engine produces **C0/C1** streams. **`SIMPLE_OUT_C=1`**: simple **`out_C`** passes stream words through; compare **`c.txt`** to **`c_golden.txt`** (interleaved c0/c1 word order). **`SIMPLE_OUT_C=0`**: complex **`out_C`** de-tiles into DDR **`matC`**; the host writes row-major **`c.txt`** comparable to **`matrix_C_golden.txt`**.

Regenerate golden files whenever you change **`GEMM_SIZE_*`**, **`DATA_TYPE`**, **`SUB_TILE_*`**, splits, cascade, or effective **`DIM_*`**:

```bash
make create_ioFiles
# or: (cd design/aie_src/aiesim_data && python3 plioGen.py)
```

Use the same **`config.json`** for **`make package`** so the SD card carries matching **`matrix_A_input.txt`**, **`b_golden.txt`**, **`c_golden.txt`** / **`matrix_C_golden.txt`**, and **`compare_outputs.py`**. **`SIMPLE_OUT_C`** in the **built** `gemm_config.h` / bitstream must match what you assume when comparing (changing it requires rebuilding **`dma_hls`**, link, and host).

**Compare on the board:** After the host app has written **`c.txt`**, you can run the same Python checker on the board as in simulation—no need to copy **`c.txt`** back to a PC for a basic pass/fail. From the SD package directory that contains **`compare_outputs.py`** and **`config.json`**, run:

```bash
python3 compare_outputs.py
```

The script reads **`SIMPLE_OUT_C`** from **`config.json`** and compares **`c.txt`** to **`c_golden.txt`** or **`matrix_C_golden.txt`** accordingly. Default paths are tuned for a typical embedded layout (e.g. **`c.txt`** under **`/media/output_files/`**, goldens under **`/media`**); use **`python3 compare_outputs.py --help`** and **`--c-txt`** / **`--golden-dir`** if your paths differ.

---

## Architecture notes (AIEML)

This design targets **AI Engine ML (AIEML)** generation (**`__AIE_ARCH__ == 20`** and related), with **ACC32/ACC64** support in that family. The DSPLIB **`matrix_mult_graph`** path used in **`graph.h`** supports **integer** GEMM (`int16`, `int32` here; the library also lists other integer complex types). **`float`** / **`cfloat`** are rejected in the Makefile for this project because the host and graph are wired for int16/int32 only.

**Future tooling:** This project is **developed and tested with AMD Vitis 2024.1** (match Vivado to the same release). Newer **Vitis** / **Vitis Libraries** (DSP) versions may expose **additional `matrix_mult` data types or precisions** beyond the int16/int32 path here; see **release notes** for your installed version. Porting to another type still requires updating **`graph.h`**, **`dma_hls.cpp`**, **`gemm_utils.cpp` / `gemm_aie_app.cpp`**, **`plioGen.py` / `plio_utils.py`**, Makefile `DATA_TYPE` handling, and validation scripts.

Board-specific AI Engine placement may be described in **`aie_primitive.json`** (if present in your platform or flow outputs).

**Optional workflow**: `sync_and_run.sh` (with `Mahdieh_env_setup.sh` for local paths) can align derived fields in `config.json`, drive builds, and run compare steps; adjust paths inside those scripts for your machine before committing forks.

---

## Performance

Fill in numbers for **your** board, clock, and `config.json` — the table below is a placeholder for your README on GitHub.

| Matrix (A × AB × B) | Notes | AIE / system time | Host reference (e.g. PyTorch CPU) |
|---------------------|-------|-------------------|-----------------------------------|
| *example*           | *device, INT16/32, core count* | | |

---

## Contributing

1. Fork the repository and create a branch for your change.
2. Keep commits focused; match existing style in C++/HLS/AIE sources.
3. Do not commit large build artifacts (`build/`, generated `xclbin`, Vivado runs); use `.gitignore` (see below).
4. If you change `config.json` geometry or datatype, run **`validate_gemm_config.py`** and **`make create_ioFiles`** (or `plioGen.py`) so checked-in **`gemm_*_ioFiles`** stay consistent, or document that goldens are intentionally not updated.
5. Open a pull request with a short description of behavior, risk, and how you tested (e.g. `hw_emu` vs `hw`, int16 vs int32).

---

## License

SPDX: **MIT** — see the header in **`Makefile`** and per-file notices (e.g. `SPDX-License-Identifier: MIT`). If you want GitHub’s license badge to resolve, add a top-level **`LICENSE`** file with the same MIT text.

---

## Acknowledgments

- **AMD** (formerly Xilinx) for Versal, Vitis, and AI Engine tooling.
- **Vitis DSP Library** components used in the AI Engine graph.
- Open-source Python ecosystem (PyTorch, NumPy) for optional benchmarking.

---

## Support

Use **GitHub Issues** in your published repository for questions and bug reports. For tool and silicon defects, use **AMD support channels** and official documentation for your device and Vitis release.
