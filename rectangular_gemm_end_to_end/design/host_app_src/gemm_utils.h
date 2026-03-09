/*
Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
SPDX-License-Identifier: MIT
*/

#ifndef GEMM_UTILS_H
#define GEMM_UTILS_H

#include <vector>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <new>
#include <ap_int.h>

// ============================================================================
// ALIGNED ALLOCATOR FOR MEMORY ALIGNMENT
// ============================================================================
// Custom allocator to ensure memory alignment for DMA transfers
// Aligns memory to 4096-byte boundaries for optimal DMA performance
template<typename T>
class aligned_allocator {
public:
    typedef T value_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;

    template<typename U>
    struct rebind {
        typedef aligned_allocator<U> other;
    };

    aligned_allocator() = default;
    template<typename U>
    aligned_allocator(const aligned_allocator<U>&) {}

    pointer allocate(size_type n) {
        const size_type alignment = 4096;  // 4KB alignment for DMA
        size_type size = n * sizeof(T);
        void* ptr = nullptr;
        
        // Use aligned_alloc if available (C++11), otherwise use posix_memalign
        #ifdef _WIN32
            ptr = _aligned_malloc(size, alignment);
        #else
            if (posix_memalign(&ptr, alignment, size) != 0) {
                throw std::bad_alloc();
            }
        #endif
        
        if (ptr == nullptr) {
            throw std::bad_alloc();
        }
        return static_cast<pointer>(ptr);
    }

    void deallocate(pointer p, size_type) {
        #ifdef _WIN32
            _aligned_free(p);
        #else
            std::free(p);
        #endif
    }

    template<typename U>
    bool operator==(const aligned_allocator<U>&) const { return true; }
    
    template<typename U>
    bool operator!=(const aligned_allocator<U>&) const { return false; }
};

// Forward declarations
namespace xrt {
    class device;
    class uuid;
    class bo;
    class kernel;
    class graph;
    class run;
}

// Timing functions
long long getElapsedMicroseconds(const std::chrono::high_resolution_clock::time_point& start);
void printDurationUsMs(long long elapsed_us, const char* stage);
long long logElapsedTime(const std::chrono::high_resolution_clock::time_point& start, const char* stage);
void printElapsedTime(const std::chrono::high_resolution_clock::time_point& start, const char* stage);
void printDurationSince(const std::chrono::high_resolution_clock::time_point& t0, const char* label);

// Memory and buffer utilities
void printMemoryInfo(const char* stage, size_t total_allocated);
bool verifyBufferAlignment(const void* ptr);
void verifyBufferConfiguration();
void printConfigurationInfo();
void printValidTileDimensions();
// Show detailed breakdown of how EXACT_MAT*_SZ are calculated from GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B
void printExactMatrixSizeCalculation();

// Load matrix data directly into DDR buffers (DDR-only mode).
// Syncs A and B to device inside this function so the kernel sees the data on hardware.
int loadMatrixDataDirectToDDR(ap_int<128>* inA_bomapped, ap_int<128>* inB_bomapped,
                              ap_int<128>* outC_bomapped,
                              xrt::bo& inA_bohdl, xrt::bo& inB_bohdl,
                              size_t exact_mata_sz, size_t exact_matb_sz, size_t exact_matc_sz);

// Debug printing functions
void printInputBuffers(const ap_int<128>* inA_bomapped, const ap_int<128>* inB_bomapped, 
                      size_t aligned_mata_size, size_t aligned_matb_size);
void printOutputBuffer(const ap_int<128>* outC_bomapped, size_t aligned_matc_size);

// XRT buffer validation functions
int validateBufferCreation(const xrt::bo& inA_bohdl, const xrt::bo& inB_bohdl, const xrt::bo& outC_bohdl);
int validateBufferMapping(const ap_int<128>* inA_bomapped, const ap_int<128>* inB_bomapped, const ap_int<128>* outC_bomapped);
// Verify DDR allocation matches exact memory sizes based on GEMM_SIZE_A, GEMM_SIZE_AB, GEMM_SIZE_B
int verifyDDRAllocationMatchesGEMMSizes(const xrt::bo& inA_bohdl, const xrt::bo& inB_bohdl, const xrt::bo& outC_bohdl);

// XRT device and buffer operations
int initializeDeviceAndLoadXclbin(xrt::device& device, xrt::uuid& xclbin_uuid, const char* xclbinFilename);
int createAndMapBuffers(xrt::device& device, xrt::bo& inA_bohdl, xrt::bo& inB_bohdl, xrt::bo& outC_bohdl,
                       ap_int<128>*& inA_bomapped, ap_int<128>*& inB_bomapped, ap_int<128>*& outC_bomapped);

// Optional debug: dump raw C split streams (c0/c1) from DMA kernel into DDR buffers
int createKernel(xrt::device& device, xrt::uuid& xclbin_uuid, 
                        xrt::kernel& dma_hls_khdl);

// Launch kernel with timing measurement
int launchKernel(xrt::kernel& dma_hls_khdl,
                 xrt::bo& inA_bohdl, xrt::bo& inB_bohdl, xrt::bo& outC_bohdl,
                 xrt::run& dma_hls_rhdl);
int executeComputation(xrt::graph& gemm_aie_gr, xrt::run& dma_hls_rhdl, 
                      ap_int<128>* outC_bomapped, xrt::bo& outC_bohdl);
int writeOutputToFile(ap_int<128>* outC_bomapped, xrt::bo& outC_bohdl,
                     const char* output_filename);
void printCoreComputationTiming(const std::chrono::high_resolution_clock::time_point& core_start_time,
                               long long phase4_us, long long phase5_us, long long phase6_us, long long phase7_us);

// Helper functions
std::string find_existing_file(const std::vector<std::string>& candidates);
bool load_golden_into_vector(const std::string& filepath,
                            std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& dst);
bool write_c_txt_from_bo(const ap_int<128>* src,
                         size_t num_elements,
                         const char* filename);

// Write raw split stream buffer (tiled/permuted words) as WRD_LN-elements-per-line text (matches c{i}_golden.txt format)
bool write_split_txt_from_bo(const ap_int<128>* src, size_t num_words, const char* preferred_dir, const char* out_name);

// Write c0.txt and c1.txt from interleaved matC (kernel writes [c0_w0,c1_w0,c0_w1,c1_w1,...] globally; even idx→c0, odd→c1)
bool write_split_txt_from_bo_interleaved(const ap_int<128>* src, size_t total_words, const char* preferred_dir);

// Helper function to load golden file directly into DDR buffer
bool load_golden_into_ddr_buffer(const std::string& filepath,
                                ap_int<128>* dst,
                                size_t num_elements);


// Helper function to load raw matrix file (row-major format) into DDR buffer
bool load_raw_matrix_into_ddr_buffer(const std::string& filepath,
                                    ap_int<128>* dst,
                                    size_t num_words);

#endif // GEMM_UTILS_H
