# AI Engine GEMM - Resource & Performance Summary

**Generated:** 
**Generated:** 2026-04-24 15:22:12
**Project:** GEMM 32×32×32 (int16, DIM=16, 16 cores)  
**Build Target:** hw

---

## 1. RESOURCE UTILIZATION

### PL (Programmable Logic) Resources

| Resource | Used | Available | Utilization | Status |
|----------|------|-----------|-------------|--------|
| **LUTs** | 9147 | 150,272 | **6.07%** | ✅ Very Low |
| **FFs (Registers)** | 22,310 | 300,544 | **7.42%** | ✅ Low |
| **BRAMs** | 97 | 155 | **62.58%** | ⚠️ Moderate |
| **DSPs** | 0 | 0 | **0.00%** | ✅ Not used |
| **URAM** | 0 | 155 | **0.00%** | ✅ Not used |

**Summary:** Low LUT/FF usage with moderate BRAM usage. Design has significant room for additional functionality.

---

## 2. DMA KERNEL RESOURCE BREAKDOWN

### Matrix A Processing (with Rearrangement Logic)

| Component | LUT | FF | BRAM | Purpose |
|-----------|-----|----|----|---------|
| **inp_A_producer** | 5,782 | 1,860 | 0 | Raw → Cascade transformation |
| **inp_A_consumer** | 720 | 159 | 16 | Broadcasting to 8 streams |
| **inp_A (Total)** | **428.816467** | **dma_hls_1** | **313.479614** | **Matrix A processing** |
| **inp_B** | 247 | 122 | 0 | Sequential read |
| **out_C** | 488 | 882 | 0 | Sequential write |
| **Memory Interfaces (AXI)** | 6,085 | 11,304 | 88 | DDR controllers |
| **Control Logic** | 424 | 246 | 0 | Control signals |
| **DMA Kernel Total** | **14,063** | **14,861** | **108** | **Complete DMA kernel** |

**Note:** The rearrangement logic (inp_A_producer + inp_A_consumer) uses 428.816467 LUT + 313.479614 BRAM, which is a significant portion of the DMA kernel's resources.

---

## 3. AI ENGINE RESOURCES

| Resource | Used | Available | Utilization |
|----------|------|-----------|-------------|
| **AIE Cores** | 16 | 34 | **47.06%** |
| **PLIOs** | 26 | - | - |
| **Memory Banks (AIE)** | 28 | - | - |
| **DMA Banks (DDRMC)** | 1 | 1 | 100% |

**Kernel Details:**
- **MatMul Kernels:** 84 instances
- **Kernel Placement:** Horizontal
- **Buffer Placement:** Custom (explicit MG mappings)
- **Core Utilization:** 100% (all cores active)

---

## 4. TIMING PERFORMANCE

| Metric | Value | Status |
|--------|-------|--------|
| **WNS (Worst Negative Slack)** | 0.012 ns | ✅ PASS |
| **TNS (Total Negative Slack)** | 0.000 ns | ✅ No violations |
| **WHS (Worst Hold Slack)** | 0.012 ns | ✅ PASS |
| **THS (Total Hold Slack)** | 0.000 ns | ✅ No violations |
| **Timing Status** | **✅ CLOSED** | All constraints met |

---

## 5. CLOCK FREQUENCIES

| Clock Domain | Frequency | Purpose |
|--------------|-----------|---------|
| **Base PL Clock** | 100.000 MHz | Main programmable logic |
| **PL Logic Clock** | 156.250 MHz | PL processing |
| **AI Engine Clock** | 300.003 MHz (312.5 MHz target) | AI Engine processing |
| **DDR4 Memory Clock** | 800.000 MHz | Memory interface |
| **DDR4 FIFO Clock** | 800.000 MHz | Memory FIFO |
| **Memory Controller** | 800.000 MHz | DDR controller |
| **High-Speed I/O** | 3,200.000 MHz | PHY layer |

**DMA Kernel Timing:**
- **Target Frequency:** 312.5 MHz
- **Estimated Frequency:** 428.75 MHz
- **Status:** ✅ Exceeds target by 37%

---

## 6. POWER CONSUMPTION

| Component | Power (W) | Notes |
|-----------|-----------|-------|
| **Total On-Chip Power** | **10.698** | - |
| **AI Engine Power** | **2.394** | 16 cores active |
| **Memory Power** | **3.152** | BRAM + NoC-DDRMC + XRAM |
| **PL Power** | ~5.152 | Estimated |

**Power Breakdown:**
- AI Engine: 22.3% of total power
- Memory: 29.4% of total power
- PL Logic: ~48.1% of total power

---

## 7. HLS PIPELINE PERFORMANCE

### Initiation Interval (II) Status

| Component | Loop | II | Status |
|-----------|------|----|--------|
| **inp_A_producer** | VITIS_LOOP_183_4 (inner) | **1** | ✅ Optimal |
| **inp_A_producer** | VITIS_LOOP_246_6 (remaining) | **1** | ✅ Optimal |
| **inp_A_consumer** | read_block | **1** | ✅ Optimal |
| **inp_A_consumer** | write_block | **1** | ✅ Optimal |
| **inp_B** | Main loop | **1** | ✅ Optimal |
| **out_C** | Main loop | **1** | ✅ Optimal |

**All loops achieve II=1, indicating maximum pipeline throughput.**

---

## 8. KEY INSIGHTS

✅ **Design Status:** Fully functional and optimized
- ✅ Timing closed (WNS = 0.012 ns)
- ✅ All HLS loops achieve II=1
- ✅ DMA kernel exceeds target frequency (428.75 MHz vs 312.5 MHz)
- ✅ Low resource utilization (6.07% LUTs, 7.42% FFs)
- ⚠️ Moderate BRAM usage (62.58%) - monitor for larger designs

📊 **Resource Efficiency:**
- DMA kernel uses 14,063 LUT
- Matrix A rearrangement logic: 428.816467 LUT (48% of DMA kernel)
- AI Engine: 16/34 cores (47% utilization)

⚡ **Power Efficiency:**
- Total: 10.698 W
- AI Engine: 2.394 W
- Memory: 3.152 W

🕐 **Performance:**
- All timing constraints met
- Clock frequencies operating as designed
- HLS pipelines optimized (II=1)

---

## 9. DESIGN CONFIGURATION

- **Matrix Dimensions:** A=32, AB=32, B=32
- **Tile Dimensions:** DIM_A=16, DIM_AB=4 (= GEMM_SIZE_AB / CASC_LN_AB), DIM_B=16 (config DIM=16)
- **Data Type:** int16
- **Build Target:** hw

---

**End of Summary**
