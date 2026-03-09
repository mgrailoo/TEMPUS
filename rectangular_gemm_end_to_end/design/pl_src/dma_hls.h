/*
Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
SPDX-License-Identifier: MIT
*/

#pragma once

#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

////////////////////////////////////////////////////////////
// Data Type Support for Dynamic Configuration
////////////////////////////////////////////////////////////

// Data type definitions based on config
#if DATA_TYPE == 16  // int16
    typedef ap_int<16> data_t;
    #define DATA_BITS 16
    #define ELEMENTS_PER_128BIT 8
#elif DATA_TYPE == 32  // int32
    typedef ap_int<32> data_t;
    #define DATA_BITS 32
    #define ELEMENTS_PER_128BIT 4
#elif DATA_TYPE == 33  // float
    typedef float data_t;
    #define DATA_BITS 32
    #define ELEMENTS_PER_128BIT 4
#else
    #error "Unsupported DATA_TYPE. Use 16 (int16), 32 (int32), or 33 (float)"
#endif

// Ensure WRD_LN matches ELEMENTS_PER_128BIT
#ifndef WRD_LN
#define WRD_LN ELEMENTS_PER_128BIT
#endif

////////////////////////////////////////////////////////////
// Top Function of Final Datamover unit for design
////////////////////////////////////////////////////////////
extern "C" {
void dma_hls(
   ap_int<128> *matA,  // Input matrix A
   ap_int<128> *matB,  // Input matrix B
   ap_int<128> *matC,  // Output matrix C
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A0,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A1,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A2,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A3,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A4,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A5,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A6,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_A7,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B0,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B1,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B2,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B3,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B4,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B5,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B6,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B7,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B8,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B9,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B10,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B11,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B12,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B13,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B14,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmOut_to_B15,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmInp_from_C0,
   hls::stream<ap_axiu<128, 0, 0, 0>> &strmInp_from_C1
);
}