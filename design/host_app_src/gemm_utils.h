/*
Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
SPDX-License-Identifier: MIT
*/

#ifndef GEMM_UTILS_H
#define GEMM_UTILS_H

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <ap_int.h>
#include <thread>
#include <limits>
#include <sys/stat.h>
#include <errno.h>
#include <cstring>
#include <unistd.h>
#include <memory>
#include <random>
#include <sstream>

// Forward declarations for XRT types
namespace xrt {
    class device;
    class bo;
    class kernel;
    class graph;
    class uuid;
    class run;
}

// Forward declaration for aligned_allocator
template<typename T>
struct aligned_allocator;

// ============================================================================
// MEMORY ALIGNMENT CONSTANTS
// ============================================================================
#ifndef BUFFER_ALIGNMENT
#define BUFFER_ALIGNMENT 4096  // Page size alignment for optimal memory access
#endif

// ============================================================================
// SIZE ALIGNMENT: ALIGN_UP macro for buffer size calculations
// ============================================================================
// Purpose: Ensures buffer sizes are multiples of BUFFER_ALIGNMENT
// When: Used at COMPILE TIME to calculate aligned buffer sizes
// Why: XRT buffers work most efficiently with page-aligned sizes
// Example: ALIGN_UP(2048, 4096) = 4096, ALIGN_UP(5000, 4096) = 8192
#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

// ============================================================================
// BASE MATRIX SIZES: Individual matrix sizes per file
// ============================================================================
// Calculate base sizes for individual matrices (number of 128-bit words needed per matrix)
#define TILES_PER_ROW_IN_BLOCK_A ((GEMM_SIZE / SPLIT) / DIM_A)
#define BROADCAST_COUNT_A ((TILES_PER_ROW_IN_BLOCK_A > 1) ? TILES_PER_ROW_IN_BLOCK_A : 1)
// Matrix A: Base size calculation for input matrix A
#define BASE_MATA_SZ (((GEMM_SIZE / SPLIT) * (GEMM_SIZE / CASC_LN) * BROADCAST_COUNT_A * SPLIT) / WRD_LN)
// Matrix B: Base size is the same as Matrix A
#define BASE_MATB_SZ BASE_MATA_SZ
// Matrix C: Base size calculation for output matrix C
#define BASE_MATC_SZ ((GEMM_SIZE * GEMM_SIZE) / SPLIT / WRD_LN)  // Each split contains (GEMM_SIZE * GEMM_SIZE) / SPLIT elements, packed into 128-bit words

// ============================================================================
// EXACT MATRIX SIZES: Unaligned sizes for actual data storage
// ============================================================================
// These represent the exact number of elements needed for each matrix
// Used for: Host memory allocation, data validation, and actual data operations
// Note: These are NOT aligned - they represent the true data size
//
// SIZE HIERARCHY (with units):
// BASE_*_SZ    → Elements per file (count, e.g., 16 elements per file)
// EXACT_*_SZ   → Total elements across all files (count, e.g., 8 files × 16 = 128 elements)
// actual_*_bytes → Exact bytes for data (bytes, e.g., 128 elements × 16 bytes = 2048 bytes)
// ALIGNED_*_BYTES → Aligned bytes for XRT buffers (bytes, e.g., 2048 bytes → 4096 bytes aligned)
#define EXACT_MATA_SZ (NUM_A_FILES * BASE_MATA_SZ)
#define EXACT_MATB_SZ (NUM_B_FILES * BASE_MATB_SZ)
#define EXACT_MATC_SZ (NUM_C_FILES * BASE_MATC_SZ)

// ============================================================================
// ALIGNED BUFFER SIZES: Size-aligned for XRT buffers and DMA transfers
// ============================================================================
// These represent the aligned buffer sizes in BYTES for optimal DMA performance
// Used for: XRT buffer creation, DMA transfers, and hardware operations
// Note: These ARE aligned - they include padding for optimal performance
#define ALIGNED_MATA_BYTES ALIGN_UP(EXACT_MATA_SZ * sizeof(ap_int<128>), BUFFER_ALIGNMENT)
#define ALIGNED_MATB_BYTES ALIGN_UP(EXACT_MATB_SZ * sizeof(ap_int<128>), BUFFER_ALIGNMENT)
#define ALIGNED_MATC_BYTES ALIGN_UP(EXACT_MATC_SZ * sizeof(ap_int<128>), BUFFER_ALIGNMENT)

// ============================================================================
// ALIGNED ALLOCATOR TEMPLATE CLASS
// ============================================================================
// Purpose: Ensures memory addresses are aligned to BUFFER_ALIGNMENT boundaries
// When: Used at RUNTIME when allocating memory for host vectors
// Why: DMA engines and AI Engine work most efficiently with aligned addresses
// Difference from ALIGN_UP: This aligns ADDRESSES, ALIGN_UP aligns SIZES
//
// Example:
// - ALIGN_UP: Ensures size is multiple of 4096 (e.g., 2048 → 4096 bytes)
// - aligned_allocator: Ensures address is multiple of 4096 (e.g., 0x12345678 → 0x12345000)
template<typename T>
struct aligned_allocator {
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    aligned_allocator() = default;
    template<typename U>
    aligned_allocator(const aligned_allocator<U>&) {}

    // Allocate memory with address alignment using posix_memalign
    // This ensures the returned pointer is aligned to BUFFER_ALIGNMENT
    pointer allocate(size_type n) {
        if (n == 0) return nullptr;
        void* ptr = nullptr;
        if (posix_memalign(&ptr, BUFFER_ALIGNMENT, n * sizeof(T)) != 0) {
            throw std::bad_alloc();
        }
        return static_cast<pointer>(ptr);
    }

    void deallocate(pointer p, size_type) {
        free(p);
    }

    template<typename U>
    bool operator==(const aligned_allocator<U>&) const { return true; }
    template<typename U>
    bool operator!=(const aligned_allocator<U>&) const { return false; }
};

// ============================================================================
// UTILITY FUNCTION DECLARATIONS
// ============================================================================

// Timing and duration functions
long long getElapsedMicroseconds(const std::chrono::high_resolution_clock::time_point& start);
void printDurationUsMs(long long elapsed_us, const char* stage);
long long logElapsedTime(const std::chrono::high_resolution_clock::time_point& start, const char* stage);
void printElapsedTime(const std::chrono::high_resolution_clock::time_point& start, const char* stage);
void printDurationSince(const std::chrono::high_resolution_clock::time_point& t0, const char* label);

// Memory and buffer utilities
void printMemoryInfo(const char* stage, size_t total_allocated);
bool verifyBufferAlignment(const void* ptr);
void verifyBufferConfiguration();

// Configuration and matrix information printing
void printConfigurationInfo();
void printValidTileDimensions();

// Memory allocation and data loading
int allocateHostMemory(std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_A,
                      std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_B,
                      std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_C,
                      int exact_mata_sz, int exact_matb_sz, int exact_matc_sz);

int loadMatrixData(std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_A,
                   std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_B,
                   std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_C);

// Debug printing functions
void printInputBuffers(const ap_int<128>* inA_bomapped, const ap_int<128>* inB_bomapped, 
                      size_t aligned_mata_size, size_t aligned_matb_size);
void printOutputBuffer(const ap_int<128>* outC_bomapped, size_t aligned_matc_size);

// XRT buffer validation functions
int validateBufferCreation(const xrt::bo& inA_bohdl, const xrt::bo& inB_bohdl, const xrt::bo& outC_bohdl);
int validateBufferMapping(const ap_int<128>* inA_bomapped, const ap_int<128>* inB_bomapped, const ap_int<128>* outC_bomapped);

// XRT device and buffer operations
int initializeDeviceAndLoadXclbin(xrt::device& device, xrt::uuid& xclbin_uuid, const char* xclbinFilename);
int createAndMapBuffers(xrt::device& device, xrt::bo& inA_bohdl, xrt::bo& inB_bohdl, xrt::bo& outC_bohdl,
                       ap_int<128>*& inA_bomapped, ap_int<128>*& inB_bomapped, ap_int<128>*& outC_bomapped);
int transferDataToDevice(const std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_A,
                        const std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_B,
                        ap_int<128>* inA_bomapped, ap_int<128>* inB_bomapped, ap_int<128>* outC_bomapped,
                        xrt::bo& inA_bohdl, xrt::bo& inB_bohdl);
int createKernel(xrt::device& device, xrt::uuid& xclbin_uuid, 
                        xrt::kernel& dma_hls_khdl);

// Launch kernel with timing measurement
int launchKernel(xrt::kernel& dma_hls_khdl, xrt::bo& inA_bohdl, xrt::bo& inB_bohdl, xrt::bo& outC_bohdl, xrt::run& dma_hls_rhdl);

// Execute computation (graph run + DMA wait + Output sync)
int executeComputation(xrt::graph& gemm_aie_gr, xrt::run& dma_hls_rhdl, 
                      ap_int<128>* outC_bomapped, xrt::bo& outC_bohdl);
int writeOutputToFile(ap_int<128>* outC_bomapped, xrt::bo& outC_bohdl,
                     const std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_C);

// Core computation timing calculation
void printCoreComputationTiming(const std::chrono::high_resolution_clock::time_point& core_start_time,
                               const std::chrono::high_resolution_clock::time_point& graph_start_time,
                               const std::chrono::high_resolution_clock::time_point& dma_start_time,
                               const std::chrono::high_resolution_clock::time_point& sync_start_time);

// File I/O utilities
std::string find_existing_file(const std::vector<std::string>& candidates);
bool load_golden_into_vector(const std::string& filepath,
                            std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& dst);
bool write_c_txt_from_bo(const ap_int<128>* src,
                        size_t elements,
                        const std::string& preferred_dir);

#endif // GEMM_UTILS_H
