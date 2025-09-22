/*
Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
SPDX-License-Identifier: MIT
*/

#include "gemm_utils.h"
#include "graph.h"
#include "../design_configs/gemm_config.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_graph.h"
#include "xrt/xrt_bo.h"

// ============================================================================
// TIMING AND DURATION FUNCTIONS
// ============================================================================

// Function to print elapsed time since start
void printElapsedTime(const std::chrono::high_resolution_clock::time_point& start, const char* stage) {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
    printf("[%20lld us] %s\n", static_cast<long long>(elapsed), stage);
}

// Print duration since t0 with a label (microseconds)
void printDurationSince(const std::chrono::high_resolution_clock::time_point& t0, const char* label) {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - t0).count();
    printf("[%20lld us] %s\n", static_cast<long long>(elapsed), label);
}

// ============================================================================
// MEMORY AND BUFFER UTILITIES
// ============================================================================

// Function to print memory allocation info
void printMemoryInfo(const char* stage, size_t total_allocated) {
    printf("Memory status at %s: %zu KB allocated\n", stage, total_allocated / 1024);
}

// Function to verify buffer alignment
bool verifyBufferAlignment(const void* ptr) {
    return (reinterpret_cast<uintptr_t>(ptr) % BUFFER_ALIGNMENT) == 0;
}

// Function to verify buffer configuration and parameters
void verifyBufferConfiguration() {
    printf("Verifying buffer configuration and parameters...\n");
    
    // Verify GEMM_SIZE is power of 2
    if ((GEMM_SIZE & (GEMM_SIZE - 1)) != 0) {
        printf("WARNING: GEMM_SIZE (%d) is not a power of 2\n", GEMM_SIZE);
    }
    
    // Verify DIM_A and DIM_B are compatible with GEMM_SIZE
    if (GEMM_SIZE % DIM_A != 0 || GEMM_SIZE % DIM_B != 0) {
        printf("WARNING: GEMM_SIZE (%d) is not divisible by DIM_A (%d) or DIM_B (%d)\n", 
               GEMM_SIZE, DIM_A, DIM_B);
    }
    
    // Verify GRAPH_ITER_CNT calculation
    int expected_iter_cnt = (GEMM_SIZE * GEMM_SIZE) / (DIM * DIM) / SPLIT;
    printf("Expected GRAPH_ITER_CNT: %d\n", expected_iter_cnt);
    printf("Actual GRAPH_ITER_CNT: %d\n", GRAPH_ITER_CNT);
}

// ============================================================================
// CONFIGURATION AND MATRIX INFORMATION PRINTING
// ============================================================================

// Print configuration and matrix size information
void printConfigurationInfo() {
    printf("\nBuffer size calculations:\n");
    printf("GEMM_SIZE = %d\n", GEMM_SIZE);
    printf("SPLIT = %d\n", SPLIT);
    printf("CASC_LN = %d\n", CASC_LN);
    printf("WRD_LN = %d\n", WRD_LN);
    printf("ITER_CNT = %d\n", ITER_CNT);
    printf("DIM_A = %d\n", DIM_A);
    printf("DIM_B = %d\n", DIM_B);
    printf("\nBase sizes:\n");
    printf("BASE_MATA_SZ = %d\n", BASE_MATA_SZ);
    printf("BASE_MATB_SZ = %d\n", BASE_MATB_SZ);
    printf("BASE_MATC_SZ = %d\n", BASE_MATC_SZ);
    printf("\nMatrix sizes (elements vs bytes):\n");
    printf("Matrix A: %d elements (%zu bytes exact, %zu bytes aligned)\n", 
           EXACT_MATA_SZ, EXACT_MATA_SZ * sizeof(ap_int<128>), ALIGNED_MATA_BYTES);
    printf("Matrix B: %d elements (%zu bytes exact, %zu bytes aligned)\n", 
           EXACT_MATB_SZ, EXACT_MATB_SZ * sizeof(ap_int<128>), ALIGNED_MATB_BYTES);
    printf("Matrix C: %d elements (%zu bytes exact, %zu bytes aligned)\n", 
           EXACT_MATC_SZ, EXACT_MATC_SZ * sizeof(ap_int<128>), ALIGNED_MATC_BYTES);
    printf("GRAPH_ITER_CNT = %d\n", GRAPH_ITER_CNT);
}

// Print valid tile dimension combinations
void printValidTileDimensions() {
    // List valid (TP_DIM_A, TP_DIM_AB, TP_DIM_B) at runtime for visibility
    int sub_m = 4, sub_k = 4, sub_n = 4;
    if (DATA_TYPE == 16) { sub_m=4; sub_k=4; sub_n=4; }
    else if (DATA_TYPE == 32) { sub_m=4; sub_k=4; sub_n=2; }
    else if (DATA_TYPE == 33) { sub_m=4; sub_k=4; sub_n=2; }
    int kseg = GEMM_SIZE / CASC_LN;
    printf("Valid (TP_DIM_A, TP_DIM_AB, TP_DIM_B): ");
    if ((GEMM_SIZE % sub_k)==0 && (kseg % sub_k)==0) {
        int max_a = (DIM_A < (GEMM_SIZE / SPLIT)) ? DIM_A : (GEMM_SIZE / SPLIT);
        int max_b = (DIM_B < (GEMM_SIZE / CASC_LN)) ? DIM_B : (GEMM_SIZE / CASC_LN);
        bool first=true;
        for (int a=sub_m; a<=max_a; a+=sub_m) {
            if (((GEMM_SIZE / SPLIT) % a) != 0) continue;
            for (int b=sub_n; b<=max_b; b+=sub_n) {
                if (((GEMM_SIZE / CASC_LN) % b) != 0) continue;
                if (!first) printf(", ");
                printf("(%d,%d,%d)", a, GEMM_SIZE, b);
                first=false;
            }
        }
        if (first) printf("none");
    } else {
        printf("none");
    }
    printf("\n");
}

// ============================================================================
// MEMORY ALLOCATION AND DATA LOADING
// ============================================================================

// Allocate and initialize host memory
int allocateHostMemory(std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_A,
                      std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_B,
                      std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_C,
                      int exact_mata_sz, int exact_matb_sz, int exact_matc_sz) {
    // ============================================================================
    // MEMORY ALLOCATION: Resize vectors to exact matrix sizes
    // ============================================================================
    // Allocate memory for exact matrix sizes (not aligned sizes)
    // host_mem_A.size() = exact_mata_sz elements
    // host_mem_A memory = exact_mata_sz * sizeof(ap_int<128>) bytes
    // This must be <= ALIGNED_MATA_BYTES (which includes alignment padding)
    try {
        host_mem_A.resize(exact_mata_sz);  // Allocate exact number of elements
        host_mem_B.resize(exact_matb_sz);  // Allocate exact number of elements  
        host_mem_C.resize(exact_matc_sz);  // Allocate exact number of elements

        printf("Host memory allocated successfully\n");
        printf("Matrix A: %zu elements (%zu bytes)\n", host_mem_A.size(), host_mem_A.size() * sizeof(ap_int<128>));
        printf("Matrix B: %zu elements (%zu bytes)\n", host_mem_B.size(), host_mem_B.size() * sizeof(ap_int<128>));
        printf("Matrix C: %zu elements (%zu bytes)\n", host_mem_C.size(), host_mem_C.size() * sizeof(ap_int<128>));

        // Verify that exact sizes are <= aligned sizes
        if (exact_mata_sz * sizeof(ap_int<128>) > ALIGNED_MATA_BYTES) {
            printf("ERROR: Matrix A exact size (%d) exceeds aligned size (%zu)\n", 
                   exact_mata_sz, ALIGNED_MATA_BYTES);
            return EXIT_FAILURE;
        }
        if (exact_matb_sz * sizeof(ap_int<128>) > ALIGNED_MATB_BYTES) {
            printf("ERROR: Matrix B exact size (%d) exceeds aligned size (%zu)\n", 
                   exact_matb_sz, ALIGNED_MATB_BYTES);
            return EXIT_FAILURE;
        }
        if (exact_matc_sz * sizeof(ap_int<128>) > ALIGNED_MATC_BYTES) {
            printf("ERROR: Matrix C exact size (%d) exceeds aligned size (%zu)\n", 
                   exact_matc_sz, ALIGNED_MATC_BYTES);
            return EXIT_FAILURE;
        }
        
    } catch (const std::bad_alloc& e) {
        printf("ERROR: Memory allocation failed: %s\n", e.what());
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        printf("ERROR: Unexpected error during memory allocation: %s\n", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// Load matrix data from files or use default patterns
int loadMatrixData(std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_A,
                   std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_B,
                   std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_C) {
    // Initialize matrices with proper alignment and bounds checking
    try {
        // Attempt to load golden files for A and B
        std::string io_dir_name = std::string("gemm_") + std::to_string(GEMM_SIZE) + "x" + std::to_string(GEMM_SIZE) + "x" + std::to_string(GEMM_SIZE) + "_ioFiles/";
        std::vector<std::string> a_candidates = {
            "a_golden.txt",
            std::string("./aie_src/aiesim_data/") + io_dir_name + "a_golden.txt",
            std::string("/sd_card/") + io_dir_name + "a_golden.txt",
            std::string("/sd_card/") + "a_golden.txt",
            std::string("/mnt/") + io_dir_name + "a_golden.txt",
            std::string("/mnt/") + "a_golden.txt"
        };
        std::vector<std::string> b_candidates = {
            "b_golden.txt",
            std::string("./") + io_dir_name + "b_golden.txt",
            std::string("/sd_card/") + io_dir_name + "b_golden.txt",
            std::string("/sd_card/") + "b_golden.txt",
            std::string("/mnt/") + io_dir_name + "b_golden.txt",
            std::string("/mnt/") + "b_golden.txt"
        };

        std::string a_path = find_existing_file(a_candidates);
        std::string b_path = find_existing_file(b_candidates);

        if (a_path.empty() || b_path.empty()) {
            printf("WARNING: golden inputs not found (A:%s B:%s). Using default patterns.\n",
                   a_path.empty() ? "missing" : a_path.c_str(),
                   b_path.empty() ? "missing" : b_path.c_str());
            std::fill(host_mem_A.begin(), host_mem_A.end(), ap_int<128>("0x00010001000100010001000100010001", 16));
            std::fill(host_mem_B.begin(), host_mem_B.end(), ap_int<128>("0x00020002000200020002000200020002", 16));
        } else {
            printf("Loading A from %s\n", a_path.c_str());
            if (!load_golden_into_vector(a_path, host_mem_A)) {
                printf("Failed reading %s, falling back to default A pattern.\n", a_path.c_str());
                std::fill(host_mem_A.begin(), host_mem_A.end(), ap_int<128>("0x00010001000100010001000100010001", 16));
            }
            printf("Loading B from %s\n", b_path.c_str());
            if (!load_golden_into_vector(b_path, host_mem_B)) {
                printf("Failed reading %s, falling back to default B pattern.\n", b_path.c_str());
                std::fill(host_mem_B.begin(), host_mem_B.end(), ap_int<128>("0x00020002000200020002000200020002", 16));
            }
        }

        std::fill(host_mem_C.begin(), host_mem_C.end(), 0);
        printf("Matrices initialized successfully\n");
        
    } catch (const std::exception& e) {
        printf("ERROR: Matrix initialization failed: %s\n", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

// ============================================================================
// DEBUG PRINTING FUNCTIONS
// ============================================================================

// Debug printing functions
void printInputBuffers(const ap_int<128>* inA_bomapped, const ap_int<128>* inB_bomapped, 
                      size_t aligned_mata_size, size_t aligned_matb_size) {
    // ============================================================================
    // INPUT BUFFER DEBUG PRINTING: Display first 16 elements of A and B matrices
    // ============================================================================
    printf("\n=== Input Buffer A (first 16 elements after sync to device) ===\n");
    for (int i = 0; i < std::min(16, (int)(aligned_mata_size / sizeof(ap_int<128>))); ++i) {
        printf("A[%2d]: ", i);
        for (int j = 0; j < WRD_LN; ++j) {
            if (DATA_TYPE == 16) {  // int16
                ap_int<16> val = (inA_bomapped[i] >> (j * 16)) & 0xFFFF;
                printf("%6d ", (int16_t)val);
            } else if (DATA_TYPE == 32) {  // int32
                ap_int<32> val = (inA_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                printf("%8d ", (int32_t)val);
            } else if (DATA_TYPE == 33) {  // float
                uint32_t float_bits = (inA_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                float val = *reinterpret_cast<float*>(&float_bits);
                printf("%8.4f ", val);
            }
        }
        printf("\n");
    }
    
    printf("\n=== Input Buffer B (first 16 elements after sync to device) ===\n");
    for (int i = 0; i < std::min(16, (int)(aligned_matb_size / sizeof(ap_int<128>))); ++i) {
        printf("B[%2d]: ", i);
        for (int j = 0; j < WRD_LN; ++j) {
            if (DATA_TYPE == 16) {  // int16
                ap_int<16> val = (inB_bomapped[i] >> (j * 16)) & 0xFFFF;
                printf("%6d ", (int16_t)val);
            } else if (DATA_TYPE == 32) {  // int32
                ap_int<32> val = (inB_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                printf("%8d ", (int32_t)val);
            } else if (DATA_TYPE == 33) {  // float
                uint32_t float_bits = (inB_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                float val = *reinterpret_cast<float*>(&float_bits);
                printf("%8.4f ", val);
            }
        }
        printf("\n");
    }
    printf("\n");
}

void printOutputBuffer(const ap_int<128>* outC_bomapped, size_t aligned_matc_size) {
    // ============================================================================
    // OUTPUT BUFFER DEBUG PRINTING: Display first 16 elements of C matrix
    // ============================================================================
    printf("\n=== Output Buffer C (first 16 elements after sync) ===\n");
    for (int i = 0; i < std::min(16, (int)(aligned_matc_size / sizeof(ap_int<128>))); ++i) {
        printf("C[%2d]: ", i);
        for (int j = 0; j < WRD_LN; ++j) {
            if (DATA_TYPE == 33) {  // float
                uint32_t float_bits = (outC_bomapped[i] >> (j * 32)) & 0xFFFFFFFF;
                float val = *reinterpret_cast<float*>(&float_bits);
                printf("%8.4f ", val);
            } else if (DATA_TYPE == 4) {  // int4
                ap_int<4> val = (outC_bomapped[i] >> (j * 4)) & 0xF;
                printf("%2d ", (int8_t)val);
            } else if (DATA_TYPE == 8) {  // int8
                ap_int<8> val = (outC_bomapped[i] >> (j * 8)) & 0xFF;
                printf("%4d ", (int8_t)val);
            } else if (DATA_TYPE == 16) {  // int16
                ap_int<16> val = (outC_bomapped[i] >> (j * 16)) & 0xFFFF;
                printf("%6d ", (int16_t)val);
            } else if (DATA_TYPE == 34) {  // bfloat16
                ap_uint<16> val = (outC_bomapped[i] >> (j * 16)) & 0xFFFF;
                printf("%6d ", (uint16_t)val);
            }
        }
        printf("\n");
    }
    printf("\n");
}

// ============================================================================
// XRT BUFFER VALIDATION FUNCTIONS
// ============================================================================

// Validate XRT buffer creation
int validateBufferCreation(const xrt::bo& inA_bohdl, const xrt::bo& inB_bohdl, const xrt::bo& outC_bohdl) {
    if (!inA_bohdl) {
        printf("ERROR: Failed to create buffer A\n");
        return EXIT_FAILURE;
    }
    if (!inB_bohdl) {
        printf("ERROR: Failed to create buffer B\n");
        return EXIT_FAILURE;
    }
    if (!outC_bohdl) {
        printf("ERROR: Failed to create buffer C\n");
        return EXIT_FAILURE;
    }
    printf("XRT buffers created successfully\n");
    return EXIT_SUCCESS;
}

// Validate buffer mapping
int validateBufferMapping(const ap_int<128>* inA_bomapped, const ap_int<128>* inB_bomapped, const ap_int<128>* outC_bomapped) {
    if (!inA_bomapped) {
        printf("ERROR: Failed to map buffer A\n");
        return EXIT_FAILURE;
    }
    if (!inB_bomapped) {
        printf("ERROR: Failed to map buffer B\n");
        return EXIT_FAILURE;
    }
    if (!outC_bomapped) {
        printf("ERROR: Failed to map buffer C\n");
        return EXIT_FAILURE;
    }
    printf("Buffers mapped successfully\n");
    return EXIT_SUCCESS;
}

// ============================================================================
// XRT DEVICE AND BUFFER OPERATIONS
// ============================================================================

// Initialize device and load XCLBIN
int initializeDeviceAndLoadXclbin(xrt::device& device, xrt::uuid& xclbin_uuid, const char* xclbinFilename) {
    // Device initialization
    auto t_device_init = std::chrono::high_resolution_clock::now();
    device = xrt::device(0);
    printf("Device initialized successfully\n");
    printDurationSince(t_device_init, "Device initialization");

    // XCLBIN file validation
    auto t_xclbin_checks = std::chrono::high_resolution_clock::now();
    FILE* file = fopen(xclbinFilename, "r");
    if (!file) {
        printf("ERROR: Cannot open XCLBIN file: %s\n", xclbinFilename);
        printf("Error details: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    fclose(file);
    
    // Check file size to ensure it's not empty
    struct stat file_stat;
    if (stat(xclbinFilename, &file_stat) != 0) {
        printf("ERROR: Cannot get file stats for: %s\n", xclbinFilename);
        return EXIT_FAILURE;
    }
    if (file_stat.st_size == 0) {
        printf("ERROR: XCLBIN file is empty: %s\n", xclbinFilename);
        return EXIT_FAILURE;
    }
    printDurationSince(t_xclbin_checks, "XCLBIN file checks (existence, size)");
    
    // Load XCLBIN
    auto t_xclbin_load = std::chrono::high_resolution_clock::now();
    xclbin_uuid = device.load_xclbin(xclbinFilename);
    printf("XCLBIN loaded successfully, UUID: %s\n", xclbin_uuid.to_string().c_str());
    printDurationSince(t_xclbin_load, "XCLBIN load");
    
    return EXIT_SUCCESS;
}

// Create and map XRT buffers
int createAndMapBuffers(xrt::device& device, xrt::bo& inA_bohdl, xrt::bo& inB_bohdl, xrt::bo& outC_bohdl,
                       ap_int<128>*& inA_bomapped, ap_int<128>*& inB_bomapped, ap_int<128>*& outC_bomapped) {
    // Buffer creation
    auto t_bo_create = std::chrono::high_resolution_clock::now();
    inA_bohdl = xrt::bo(device, ALIGNED_MATA_BYTES, 0, 0);
    inB_bohdl = xrt::bo(device, ALIGNED_MATB_BYTES, 0, 0);
    outC_bohdl = xrt::bo(device, ALIGNED_MATC_BYTES, 0, 0);
    
    if (validateBufferCreation(inA_bohdl, inB_bohdl, outC_bohdl) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    printDurationSince(t_bo_create, "BO create (A, B, C)");
    
    // Buffer mapping
    auto t_map = std::chrono::high_resolution_clock::now();
    inA_bomapped = inA_bohdl.map<ap_int<128>*>();
    inB_bomapped = inB_bohdl.map<ap_int<128>*>();
    outC_bomapped = outC_bohdl.map<ap_int<128>*>();
    
    if (validateBufferMapping(inA_bomapped, inB_bomapped, outC_bomapped) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    printDurationSince(t_map, "BO map (A, B, C)");
    
    return EXIT_SUCCESS;
}

// Transfer data to device buffers
int transferDataToDevice(const std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_A,
                        const std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_B,
                        ap_int<128>* inA_bomapped, ap_int<128>* inB_bomapped, ap_int<128>* outC_bomapped,
                        xrt::bo& inA_bohdl, xrt::bo& inB_bohdl) {
    // Initialize output buffer
    printf("Initializing output buffer...\n");
    auto t_zero_out = std::chrono::high_resolution_clock::now();
    memset(outC_bomapped, 0, ALIGNED_MATC_BYTES);
    printDurationSince(t_zero_out, "Zero initialize C buffer");
    
    // Data transfer validation
    size_t actual_mata_bytes = EXACT_MATA_SZ * sizeof(ap_int<128>);
    size_t actual_matb_bytes = EXACT_MATB_SZ * sizeof(ap_int<128>);
    
    if (actual_mata_bytes > ALIGNED_MATA_BYTES) {
        printf("ERROR: Buffer A size mismatch for copy operation\n");
        return EXIT_FAILURE;
    }
    if (actual_matb_bytes > ALIGNED_MATB_BYTES) {
        printf("ERROR: Buffer B size mismatch for copy operation\n");
        return EXIT_FAILURE;
    }
    
    // Copy data to device buffers
    auto t_memcpy = std::chrono::high_resolution_clock::now();
    memcpy(inA_bomapped, host_mem_A.data(), actual_mata_bytes);
    memcpy(inB_bomapped, host_mem_B.data(), actual_matb_bytes);
    printf("Data copied successfully\n");
    printDurationSince(t_memcpy, "Memcpy host->BO (A, B)");
    
    // Sync to device
    auto t_sync_to_dev = std::chrono::high_resolution_clock::now();
    inA_bohdl.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    inB_bohdl.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    printf("Buffer synchronization completed\n");
    printDurationSince(t_sync_to_dev, "Sync to device (A, B)");
    
    return EXIT_SUCCESS;
}

// Create kernel and graph objects
int createKernelAndGraph(xrt::device& device, xrt::uuid& xclbin_uuid, 
                        xrt::kernel& dma_hls_khdl, xrt::graph& gemm_aie_gr) {
    printf("Creating kernel object with optimized settings...\n");
    
    // Create DMA kernel
    auto t_kernel_create = std::chrono::high_resolution_clock::now();
    dma_hls_khdl = xrt::kernel(device, xclbin_uuid, "dma_hls");
    printf("DMA kernel created successfully\n");
    printDurationSince(t_kernel_create, "Kernel create (dma_hls)");

    // Graph is already created in main, just verify it's valid
    printf("Graph object already created and ready\n");
    
    return EXIT_SUCCESS;
}

// Launch kernel with timing measurement
int launchKernel(xrt::kernel& dma_hls_khdl, xrt::bo& inA_bohdl, xrt::bo& inB_bohdl, xrt::bo& outC_bohdl, xrt::run& dma_hls_rhdl) {
    printf("Launching DMA kernel with optimized configuration...\n");
    auto t_kernel_launch = std::chrono::high_resolution_clock::now();
    dma_hls_rhdl = dma_hls_khdl(
        inA_bohdl, inB_bohdl, outC_bohdl,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr);
    printf("DMA kernel launched successfully\n");
    printDurationSince(t_kernel_launch, "Kernel launch (dma_hls)");
    
    return EXIT_SUCCESS;
}

// Execute computation (graph run + DMA wait + Output sync)
int executeComputation(xrt::graph& gemm_aie_gr, xrt::run& dma_hls_rhdl, 
                      ap_int<128>* outC_bomapped, xrt::bo& outC_bohdl) {
    // Run graph
    auto graph_start_time = std::chrono::high_resolution_clock::now();
    printf("Running graph for %d iterations\n", GRAPH_ITER_CNT);
    gemm_aie_gr.run(GRAPH_ITER_CNT);
    printElapsedTime(graph_start_time, "Graph run");
    
    // Wait for DMA completion
    auto dma_wait_time = std::chrono::high_resolution_clock::now();
    dma_hls_rhdl.wait();
    printf("DMA kernel execution completed successfully\n");
    printElapsedTime(dma_wait_time, "DMA kernel wait");
    
    // Sync output buffer from device
    auto buffer_sync_time = std::chrono::high_resolution_clock::now();
    printf("Syncing output buffer with optimized transfer: %d , %d\n", 
            (int16_t)outC_bomapped[0], (int16_t)outC_bomapped[1]);
    outC_bohdl.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    printf("Output buffer synced successfully\n");
    printElapsedTime(buffer_sync_time, "Buffer sync");
    
    return EXIT_SUCCESS;
}

// Write output results to file
int writeOutputToFile(ap_int<128>* outC_bomapped, xrt::bo& outC_bohdl,
                     const std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& host_mem_C) {
    
    // Write output file
    write_c_txt_from_bo(outC_bomapped, host_mem_C.size(), std::string("/sd_card/output_files/"));
    
    return EXIT_SUCCESS;
}

// Calculate and print core computation timing (Graph run + DMA wait + Output sync)
void printCoreComputationTiming(const std::chrono::high_resolution_clock::time_point& core_start_time,
                               const std::chrono::high_resolution_clock::time_point& graph_start_time,
                               const std::chrono::high_resolution_clock::time_point& dma_start_time,
                               const std::chrono::high_resolution_clock::time_point& sync_start_time) {
    
    // Calculate individual phase timings
    auto graph_duration = std::chrono::duration_cast<std::chrono::microseconds>(dma_start_time - graph_start_time).count();
    auto dma_duration = std::chrono::duration_cast<std::chrono::microseconds>(sync_start_time - dma_start_time).count();
    auto sync_duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - sync_start_time).count();
    
    // Calculate total core computation time
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - core_start_time).count();
    
    printf("*** CORE COMPUTATION BREAKDOWN ***\n");
    printf("  Graph run:        %8ld us\n", graph_duration);
    printf("  DMA wait:         %8ld us\n", dma_duration);
    printf("  Output sync:      %8ld us\n", sync_duration);
    printf("  TOTAL CORE:       %8ld us (%.3f ms)\n", total_duration, total_duration / 1000.0);
}

// ============================================================================
// FILE I/O UTILITIES
// ============================================================================

// Try to locate a file among candidate paths
std::string find_existing_file(const std::vector<std::string>& candidates) {
    for (const auto& p : candidates) {
        FILE* f = fopen(p.c_str(), "r");
        if (f) { fclose(f); return p; }
    }
    return std::string();
}

// Load golden file lines with dynamic word length based on data type
bool load_golden_into_vector(const std::string& filepath,
                            std::vector<ap_int<128>, aligned_allocator<ap_int<128>>>& dst) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        printf("ERROR: Failed to open %s\n", filepath.c_str());
        return false;
    }
    std::string line;
    size_t idx = 0;
    while (std::getline(in, line) && idx < dst.size()) {
        std::istringstream iss(line);
        ap_int<128> packed = 0;
        
        // Dynamic word length based on data type
        for (int k = 0; k < WRD_LN; ++k) {
            if (DATA_TYPE == 33) {  // float
                float v = 0.0f;
                if (!(iss >> v)) v = 0.0f;
                // Pack float as 32-bit integer representation
                uint32_t float_bits = *reinterpret_cast<uint32_t*>(&v);
                packed.range((k+1)*32-1, k*32) = float_bits;
            } else {
                int v = 0;
                if (!(iss >> v)) v = 0;
                
                // Pack based on data type
                if (DATA_TYPE == 16) {  // int16
                    packed.range((k+1)*16-1, k*16) = static_cast<int16_t>(v);
                } else if (DATA_TYPE == 32) {  // int32
                    packed.range((k+1)*32-1, k*32) = static_cast<int32_t>(v);
                }
            }
        }
        dst[idx++] = packed;
    }
    // If file shorter than needed, pad remaining with zeros
    for (; idx < dst.size(); ++idx) dst[idx] = 0;
    return true;
}

// Write mapped BO contents to c.txt with dynamic word length based on data type
bool write_c_txt_from_bo(const ap_int<128>* src,
                        size_t elements,
                        const std::string& preferred_dir) {
    // Try preferred dir, then /mnt/output_files/, then ./output_files/
    std::vector<std::string> dirs = {
        preferred_dir,
        std::string("/mnt/output_files/"),
        std::string("./output_files/")
    };

    std::string out_dir;
    for (const auto& d : dirs) {
        struct stat st;
        if (stat(d.c_str(), &st) == 0) { out_dir = d; break; }
        if (mkdir(d.c_str(), 0755) == 0 || errno == EEXIST) { out_dir = d; break; }
    }
    if (out_dir.empty()) {
        fprintf(stderr, "Error creating any output directory (tried preferred, /mnt/output_files/, ./output_files/)\n");
        return false;
    }

    std::string out_path = out_dir + "c.txt";
    std::ofstream out(out_path);
    if (!out.is_open()) {
        fprintf(stderr, "Error opening %s for write: %s\n", out_path.c_str(), strerror(errno));
        return false;
    }
    for (size_t i = 0; i < elements; ++i) {
        for (int j = 0; j < WRD_LN; ++j) {
            // Extract based on data type
            if (DATA_TYPE == 33) {  // float
                uint32_t float_bits = (src[i] >> (j * 32)) & 0xFFFFFFFF;
                float val = *reinterpret_cast<float*>(&float_bits);
                // Round to 4 decimal places before writing
                // Use printf-style formatting to match golden file precision
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%.4f", val);
                out << buffer;
            } else if (DATA_TYPE == 16) {  // int16
                ap_int<16> current_val = (src[i] >> (j * 16)) & 0xFFFF;
                out << (int16_t)current_val;
            } else if (DATA_TYPE == 32) {  // int32
                ap_int<32> current_val = (src[i] >> (j * 32)) & 0xFFFFFFFF;
                out << (int32_t)current_val;
            }
            if (j + 1 < WRD_LN) out << ' ';
        }
        out << '\n';
    }
    out.close();
    printf("Wrote C buffer to %s\n", out_path.c_str());
    return true;
}
