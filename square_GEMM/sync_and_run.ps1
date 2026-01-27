# ============================================================================
# AI Engine GEMM Remote Workflow Management Script
# ============================================================================
# Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: MIT
#
# This PowerShell script provides a comprehensive remote workflow management
# system for AI Engine GEMM development on Versal ACAP platforms. It enables
# seamless synchronization, building, and execution of AI Engine applications
# across local development environments and remote Linux servers.
#
# Key Features:
# - Centralized configuration management via config.json
# - Automated file synchronization between local and remote environments
# - Remote build execution with comprehensive logging
# - Hardware emulation and execution management
# - Output comparison and validation
# - Comprehensive reporting and analysis tools
# - Guided configuration and optimization
#
# Architecture:
# - Local Development: Windows PowerShell environment with local design files
# - Remote Server: Linux environment with Xilinx toolchain and build system
# - Synchronization: Secure file transfer using PSCP and PLINK
# - Configuration: Centralized JSON-based configuration management
# - Logging: Comprehensive logging and error tracking
#
# Prerequisites:
# - PuTTY suite (plink.exe, pscp.exe) installed and in PATH
# - Remote Linux server with Xilinx Vitis 2024.1 installed
# - SSH access to remote server
# - Local design files and configuration
#
# Usage:
#   .\sync_and_run.ps1
#
# Configuration:
#   All settings are managed through design/design_configs/config.json
#   Key parameters: GEMM_SIZE, DIM, DATA_TYPE, TARGET, SPLIT, CASC_LN
#
# Remote Server Details:
#   Server: 10.240.33.35
#   User: mgrailoo
#   Path: /media/josnu02/large_SDD/mgrailoo/
#
# Example Commands:
#   ssh mgrailoo@10.240.33.35
#   cd /media/josnu02/large_SDD/mgrailoo/
#   source Mahdieh_env_setup.sh && make run TARGET=hw_emu ITER_CNT=1 EN_TRACE=1 GEMM_SIZE=32
#   source Mahdieh_env_setup.sh && cd /media/josnu02/large_SDD/mgrailoo/build/gemm_32x32x32/x1/hw_emu/package && ./launch_hw_emu.sh -run-app ./gemm_aie_xrt.elf a.xclbin
# ============================================================================

# ============================================================================
# SSH CONNECTION CONFIGURATION
# ============================================================================
# Remote server connection details for SSH operations
$sshServer = "10.240.33.35"
$sshUser = "mgrailoo"
$sshPassword = "******"
$remotePath = "/media/josnu02/large_SDD/mgrailoo/"
$localPath = ".\design"  # Changed to match remote folder name
$designPath = "$remotePath/design"

# Optional: Board connection details for installing runtime libs
$boardServer = ""
$boardUser = "root"
$boardPassword = ""

# ============================================================================
# LOGGING AND CONFIGURATION SETUP
# ============================================================================
# Define log file path for comprehensive operation logging
$logFilePath = ".\build_log.txt"  # Set a valid log file path

# Initialize TARGET with a default value
$script:TARGET = "hw"  # Default value
$script:configPath = ".\design\design_configs\config.json"

# ============================================================================
# CONFIGURATION MANAGEMENT FUNCTIONS
# ============================================================================
# Centralized configuration loading and management for AI Engine GEMM parameters
function Load-Config {
    <#
    .SYNOPSIS
    Loads configuration parameters from the centralized config.json file.
    
    .DESCRIPTION
    This function reads all AI Engine GEMM configuration parameters from the
    centralized config.json file and populates the script variables. It provides
    default values for all parameters to ensure robust operation even if the
    configuration file is missing or incomplete.
    
    .PARAMETER None
    No parameters required - uses global script variables.
    
           .NOTES
           Configuration parameters loaded:
           - GEMM_SIZE: Matrix dimension (default: 32)
           - DIM: Tile dimension (default: 16)
           - DATA_TYPE: Data type (default: int16)
           - TARGET: Build target (default: hw)
           - SPLIT: Number of AI Engine splits (default: 2)
           - CASC_LN: Number of cascade levels (default: 8)
           - ITER_CNT: Iteration count (default: 1)
           - N_SAMPLES: Number of samples (default: 1)
           - GEMM_INSTS: Number of GEMM instances (default: 1)
           - EN_TRACE: Enable tracing flag (default: 0)
           - PL_FREQ: HLS kernel frequency in MHz (default: 312.5)
           - TILE_MEM_BYTES: Tile memory constraint (default: 32768)
           - WRD_LN: Word length for PLIO transfers
           - SUB_TILE_M/K/N: Sub-tile dimensions
           - GRAPH_ITER_CNT: Graph iteration count
           
           Platform Requirements:
           - platform_edge_hwemu folder must be synced to remote server
           - Use option 1.5 to sync platform folder from local to remote
    
    .EXAMPLE
    Load-Config
    Loads all configuration parameters from config.json
    #>
    if (Test-Path $script:configPath) {
        try {
            $json = Get-Content $script:configPath -Raw | ConvertFrom-Json
            if ($null -ne $json.GEMM_SIZE) { $script:gemmSize = [int]$json.GEMM_SIZE }
            if ($null -ne $json.TARGET) { $script:TARGET = [string]$json.TARGET }
            if ($null -ne $json.DIM) { $script:DIM = [int]$json.DIM }
            if ($null -ne $json.DATA_TYPE) { $script:dataType = [string]$json.DATA_TYPE }
            if ($null -ne $json.TILE_MEM_BYTES) { $script:tileMemBytes = [int]$json.TILE_MEM_BYTES }
            # Read WRD_LN and SUB_TILE_* from config.json if present, otherwise calculate
            if ($null -ne $json.WRD_LN) { $script:WRD_LN = [int]$json.WRD_LN }
            if ($null -ne $json.SUB_TILE_M) { $script:SUB_TILE_M = [int]$json.SUB_TILE_M }
            if ($null -ne $json.SUB_TILE_K) { $script:SUB_TILE_K = [int]$json.SUB_TILE_K }
            if ($null -ne $json.SUB_TILE_N) { $script:SUB_TILE_N = [int]$json.SUB_TILE_N }
            if ($null -ne $json.SPLIT) { $script:SPLIT = [int]$json.SPLIT } else { $script:SPLIT = 2 }
            if ($null -ne $json.CASC_LN) { $script:CASC_LN = [int]$json.CASC_LN } else { $script:CASC_LN = 8 }
            if ($null -ne $json.ITER_CNT) { $script:ITER_CNT = [int]$json.ITER_CNT } else { $script:ITER_CNT = 1 }
            if ($null -ne $json.N_SAMPLES) { $script:N_SAMPLES = [int]$json.N_SAMPLES } else { $script:N_SAMPLES = 1 }
            if ($null -ne $json.GEMM_INSTS) { $script:GEMM_INSTS = [int]$json.GEMM_INSTS } else { $script:GEMM_INSTS = 1 }
            if ($null -ne $json.EN_TRACE) { $script:EN_TRACE = [int]$json.EN_TRACE } else { $script:EN_TRACE = 0 }
            if ($null -ne $json.PL_FREQ) { $script:PL_FREQ = [double]$json.PL_FREQ } else { $script:PL_FREQ = 312.5 }
            if ($null -ne $json.ENABLE_ML_BENCHMARKS) { $script:ENABLE_ML_BENCHMARKS = [int]$json.ENABLE_ML_BENCHMARKS } else { $script:ENABLE_ML_BENCHMARKS = 1 }
            # Note: GRAPH_ITER_CNT is now calculated from GEMM_SIZE, DIM, SPLIT
            # It should not be read from config.json as it is a derived parameter
        } catch {
            Write-Host "Warning: Failed to parse $script:configPath; using defaults" 
        }
    }
}

# Save centralized config
function Save-Config {
    $obj = [ordered]@{
        TARGET = $script:TARGET
        GEMM_SIZE = $script:gemmSize
        DIM = $script:DIM
        DATA_TYPE = $script:dataType
        TILE_MEM_BYTES = $script:tileMemBytes
        SPLIT = $script:SPLIT
        CASC_LN = $script:CASC_LN
        ITER_CNT = $script:ITER_CNT
        N_SAMPLES = $script:N_SAMPLES
        GEMM_INSTS = $script:GEMM_INSTS
        EN_TRACE = $script:EN_TRACE
        PL_FREQ = $script:PL_FREQ
        ENABLE_ML_BENCHMARKS = $script:ENABLE_ML_BENCHMARKS
        # Include WRD_LN and SUB_TILE_* for compatibility with Makefile
        WRD_LN = $script:WRD_LN
        SUB_TILE_M = $script:SUB_TILE_M
        SUB_TILE_K = $script:SUB_TILE_K
        SUB_TILE_N = $script:SUB_TILE_N
    }
    $obj | ConvertTo-Json | Out-File -FilePath $script:configPath -Encoding UTF8
    Write-Host "Saved config to $script:configPath"
}

# Function to log messages
function Log-Message {
    param (
        [string]$message
    )
    $timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    "$timestamp - $message" | Out-File -FilePath $logFilePath -Append
}

# Function to set GEMM size
function Set-GEMMSize {
    $newSize = Read-Host "Enter GEMM size (default: $script:gemmSize)"
    if (-not [string]::IsNullOrWhiteSpace($newSize)) { $script:gemmSize = [int]$newSize }
    $script:hwEmuPath = "$remotePath/build/gemm_${gemmSize}x${gemmSize}x${gemmSize}/x1/hw_emu/package"
    Write-Host "GEMM size set to $gemmSize"
    Log-Message "GEMM size set to $gemmSize"
    Save-Config
}

function Set-Dimensions {
    $newDim = Read-Host "Enter DIM (default: $script:DIM)"
    if (-not [string]::IsNullOrWhiteSpace($newDim)) { $script:DIM = [int]$newDim }
    Write-Host "DIM set to $script:DIM"
    Log-Message "DIM set: $script:DIM"
    Save-Config
}

function Set-DataType {
    Write-Host "Current DATA_TYPE: $script:dataType"
    Write-Host "Available data types:"
    Write-Host "  int16    - 16-bit signed integer (8 elements per 128-bit word)"
    Write-Host "  int32    - 32-bit signed integer (4 elements per 128-bit word)"
    Write-Host "  float    - 32-bit floating point (4 elements per 128-bit word)"
    
    $newDataType = Read-Host "Enter DATA_TYPE (default: $script:dataType)"
    if (-not [string]::IsNullOrWhiteSpace($newDataType)) { 
        if ($newDataType -in @("int16", "int32", "float")) {
            $script:dataType = $newDataType
            Write-Host "DATA_TYPE set to $script:dataType"
            Log-Message "DATA_TYPE set: $script:dataType"
            Save-Config
            # Immediately refresh derived parameters and persist
            Ensure-DerivedParams
            Write-Host "Derived parameters updated: WRD_LN=$script:WRD_LN, SUB_TILE=($script:SUB_TILE_M,$script:SUB_TILE_K,$script:SUB_TILE_N)"
        } else {
            Write-Host "Invalid data type. Please use: int16, int32, or float"
        }
    }
}

# Compute WRD_LN from DATA_TYPE (elements per 128-bit word)
function Get-WrdLnFromDataType {
    param(
        [string]$dt
    )
    switch ($dt) {
        "int16" { return 8 }
        "int32" { return 4 }
        "float" { return 4 }
        default { return 8 }
    }
}

# Bytes per logical element for memory sizing (int4 stored as int8 in generators)
function Get-BytesPerElement {
    param(
        [string]$dt
    )
    switch ($dt) {
        "int16" { return 2 }
        "int32" { return 4 }
        "float" { return 4 }
        default { return 2 }
    }
}

# Sub-tile sizes (M,K,N) from the AI Engine-ML instruction set guidance
function Get-SubTileSizes {
    param(
        [string]$dt
    )
    switch ($dt) {
        "int16" { return @{ M = 4; K = 4;  N = 4 } }
        "int32" { return @{ M = 4; K = 4;  N = 2 } }
        "float" { return @{ M = 4; K = 4; N = 2 } }
        default { return @{ M = 4; K = 4; N = 4 } }
    }
}

# Ensure WRD_LN and sub-tile sizes exist in config; compute from DATA_TYPE if missing
function Ensure-DerivedParams {
    Load-Config
    if (-not $script:dataType) { $script:dataType = "int16" }
    $needSave = $false
    # Only calculate if not already set from config.json
    if (-not $script:WRD_LN) {
        $newWRD = Get-WrdLnFromDataType -dt $script:dataType
        $script:WRD_LN = $newWRD; $needSave = $true
    }
    if (-not $script:SUB_TILE_M -or -not $script:SUB_TILE_K -or -not $script:SUB_TILE_N) {
        $st = Get-SubTileSizes -dt $script:dataType
        if (-not $script:SUB_TILE_M) { $script:SUB_TILE_M = $st.M; $needSave = $true }
        if (-not $script:SUB_TILE_K) { $script:SUB_TILE_K = $st.K; $needSave = $true }
        if (-not $script:SUB_TILE_N) { $script:SUB_TILE_N = $st.N; $needSave = $true }
    }
    # Compute GRAPH_ITER_CNT consistent with Makefile and C++ definitions
    if (-not $script:gemmSize) { $script:gemmSize = 32 }
    if (-not $script:DIM) { $script:DIM = 16 }
    if (-not $script:SPLIT) { $script:SPLIT = 2 }
    if (-not $script:CASC_LN) { $script:CASC_LN = 8 }
    $newGraphIter = $script:GRAPH_ITER_CNT
    if ($script:ITER_CNT -eq -1) {
        $newGraphIter = -1
    } else {
        # Use DIM×DIM per request instead of DIM_A×DIM_B
        $den = [int]([Math]::Max(1, $script:DIM * $script:DIM * $script:SPLIT))
        $num = [int]($script:gemmSize * $script:gemmSize)
        $newGraphIter = [int]([Math]::Floor($num / $den))
    }
    if ($script:GRAPH_ITER_CNT -ne $newGraphIter) { $script:GRAPH_ITER_CNT = $newGraphIter; $needSave = $true }
    if ($needSave) { Save-Config }
}

# Sub-tile granularity per data type (M and N multiples)
function Get-SubTileMultiple {
    param(
        [string]$dt
    )
    switch ($dt) {
        "int16" { return 4 }
        "int32" { return 4 }  # Sub-tile 4x4x2, max is 4
        "float" { return 4 }  # Sub-tile 4x4x2, max is 4
        default { return 4 }
    }
}

# List valid DIM options based on GEMM_SIZE, DATA_TYPE, 32KB tile memory, and sub-tile multiples
function Get-ValidDims {
    param(
        [int]$gemmSize,
        [string]$dataType
    )
    $bytesPerEl = Get-BytesPerElement -dt $dataType
    $subMul = Get-SubTileMultiple -dt $dataType
    $candidates = @()
    for ($d = 1; $d -le [Math]::Min($gemmSize, 1024); $d += 1) {
        if (($gemmSize % $d) -ne 0) { continue }
        if (($d % $subMul) -ne 0) { continue }
        # Respect split mapping limit for rows; DIM_B will be derived as min(DIM, GEMM_SIZE/CASC_LN)
        $rowsPerSplit = [Math]::Floor($gemmSize / $script:SPLIT)
        if ($d -gt $rowsPerSplit) { continue }
        # Include ping-pong buffering (2x)
        $memBytes = $d * $d * $bytesPerEl * 2
        if ($memBytes -le $script:tileMemBytes) {
            $candidates += $d
        }
    }
    return $candidates
}

# Guided DIM selection and build
function Guided-SelectDim-And-Build {
    # Ensure config is loaded
    Load-Config
    if (-not $script:gemmSize) { $script:gemmSize = 32 }
    if (-not $script:dataType) { $script:dataType = "int16" }
    if (-not $script:tileMemBytes) { $script:tileMemBytes = 32768 }

    $wrdLn = Get-WrdLnFromDataType -dt $script:dataType
    $st = Get-SubTileSizes -dt $script:dataType
    $validDims = Get-ValidDims -gemmSize $script:gemmSize -dataType $script:dataType
    if (-not $validDims -or $validDims.Count -eq 0) {
        Write-Host "No valid DIM options found for GEMM_SIZE=$($script:gemmSize), DATA_TYPE=$($script:dataType)."
        return
    }

    Write-Host "\nComputed parameters:"
    Write-Host "  DATA_TYPE: $($script:dataType)  -> WRD_LN: $wrdLn elements per 128-bit"
    Write-Host "  GEMM_SIZE: $($script:gemmSize)"
    Write-Host "  TILE_MEM_BYTES: $($script:tileMemBytes) (per tile memory constraint)"
    Write-Host ("  Sub-tile (M,K,N): {0}x{1}x{2}" -f $st.M, $st.K, $st.N)
    Write-Host "Valid DIM choices (divisors, fit tile memory, multiples of sub-tile):"
    Write-Host "  $($validDims -join ', ')"

    $choice = Read-Host "Enter DIM from the list above"
    if (-not [int]::TryParse($choice, [ref]([int]$null))) {
        Write-Host "Invalid input. Aborting."
        return
    }
    $choiceInt = [int]$choice
    if (-not ($validDims -contains $choiceInt)) {
        Write-Host "DIM $choiceInt is not in the allowed list. Aborting."
        return
    }

    $script:DIM = $choiceInt
    $script:WRD_LN = $wrdLn
    $script:SUB_TILE_M = $st.M
    $script:SUB_TILE_K = $st.K
    $script:SUB_TILE_N = $st.N
    Write-Host "Selected DIM: $script:DIM"
    Save-Config

    # Trigger build
    Run-RemoteBuild
}

# Function to set TARGET
function Set-Target {
    $newTarget = Read-Host "Enter the target (default: hw)"
    if (-not [string]::IsNullOrWhiteSpace($newTarget)) {
        $script:TARGET = $newTarget
        Write-Host "TARGET set to: $script:TARGET"
    } else {
        Write-Host "Using default TARGET: $script:TARGET"
    }
}

# Consolidated settings submenu
function Configure-Settings {
    while ($true) {
        Load-Config
        Write-Host ""
        Write-Host "=== CONFIGURE SETTINGS ==="
        Write-Host ("GEMM_SIZE: {0}    DATA_TYPE: {1}    DIM: {2}" -f $script:gemmSize, $script:dataType, $script:DIM)
        Write-Host ("SPLIT: {0}        CASC_LN: {1}       TILE_MEM_BYTES: {2}" -f $script:SPLIT, $script:CASC_LN, $script:tileMemBytes)
        Write-Host ("WRD_LN: {0}       SUB_TILE (M,K,N): {1},{2},{3}" -f $script:WRD_LN, $script:SUB_TILE_M, $script:SUB_TILE_K, $script:SUB_TILE_N)
        Write-Host ("ITER_CNT: {0}     N_SAMPLES: {1}     GEMM_INSTS: {2}" -f $script:ITER_CNT, $script:N_SAMPLES, $script:GEMM_INSTS)
        Write-Host ("EN_TRACE: {0}     PL_FREQ: {1}       ENABLE_ML_BENCHMARKS: {2}" -f $script:EN_TRACE, $script:PL_FREQ, $script:ENABLE_ML_BENCHMARKS)
        Write-Host ""
        Write-Host "1) Set GEMM_SIZE"
        Write-Host "2) Set DATA_TYPE"
        Write-Host "3) Set DIM"
        Write-Host "4) Set SPLIT"
        Write-Host "5) Set CASC_LN"
        Write-Host "6) Set TILE_MEM_BYTES"
        Write-Host "7) Set ITER_CNT"
        Write-Host "8) Set N_SAMPLES"
        Write-Host "9) Set GEMM_INSTS"
        Write-Host "10) Set EN_TRACE (0/1)"
        Write-Host "11) Set PL_FREQ (MHz)"
        Write-Host "12) Set ENABLE_ML_BENCHMARKS (0/1)"
        Write-Host "A) Auto-derive WRD_LN and sub-tiles"
        Write-Host "T) Set TARGET"
        Write-Host "Q) Back"
        $opt = Read-Host "Select option"
        switch ($opt.ToUpper()) {
            "1" { Set-GEMMSize }
            "2" { Set-DataType }
            "3" { Set-Dimensions }
            "4" {
                $v = Read-Host "Enter SPLIT (current: $script:SPLIT)"
                if ($v -match '^[0-9]+$') { $script:SPLIT = [int]$v; Save-Config }
            }
            "5" {
                $v = Read-Host "Enter CASC_LN (current: $script:CASC_LN)"
                if ($v -match '^[0-9]+$') { $script:CASC_LN = [int]$v; Save-Config }
            }
            "6" {
                $v = Read-Host "Enter TILE_MEM_BYTES (current: $script:tileMemBytes)"
                if ($v -match '^[0-9]+$') { $script:tileMemBytes = [int]$v; Save-Config }
            }
            "7" {
                $v = Read-Host "Enter ITER_CNT (current: $script:ITER_CNT)"
                if ($v -match '^-?[0-9]+$') { $script:ITER_CNT = [int]$v; Save-Config }
            }
            "8" {
                $v = Read-Host "Enter N_SAMPLES (current: $script:N_SAMPLES)"
                if ($v -match '^[0-9]+$') { $script:N_SAMPLES = [int]$v; Save-Config }
            }
            "9" {
                $v = Read-Host "Enter GEMM_INSTS (current: $script:GEMM_INSTS)"
                if ($v -match '^[0-9]+$') { $script:GEMM_INSTS = [int]$v; Save-Config }
            }
            "10" {
                $v = Read-Host "Enter EN_TRACE (0/1) (current: $script:EN_TRACE)"
                if ($v -match '^[01]$') { $script:EN_TRACE = [int]$v; Save-Config }
            }
            "11" {
                $v = Read-Host "Enter PL_FREQ in MHz (current: $script:PL_FREQ)"
                if ($v -match '^[0-9]+(\.[0-9]+)?$') { $script:PL_FREQ = [double]$v; Save-Config }
            }
            "12" {
                $v = Read-Host "Enter ENABLE_ML_BENCHMARKS (0/1) (current: $script:ENABLE_ML_BENCHMARKS)"
                if ($v -match '^[01]$') { $script:ENABLE_ML_BENCHMARKS = [int]$v; Save-Config }
            }
            "A" { Ensure-DerivedParams }
            "T" { Set-Target }
            "Q" { break }
            default { Write-Host "Invalid option." }
        }
    }
}

# Initialize defaults then load config
$script:gemmSize = 1024
$script:DIM = 16
$script:dataType = "int16"
$script:ENABLE_ML_BENCHMARKS = 1
Load-Config
# Populate derived fields (WRD_LN, SUB_TILE_M/K/N) from current config on startup
Ensure-DerivedParams
$script:hwEmuPath = "$remotePath/build/gemm_${gemmSize}x${gemmSize}x${gemmSize}/x1/hw_emu/package"

# Function to test SSH connection
function Test-SSHConnection {
    Write-Host "Testing SSH connection..."
    $testCommand = "echo 'Connection test successful'"
    try {
        $result = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch $testCommand
        if ($LASTEXITCODE -eq 0) {
            Write-Host "SSH connection successful"
            Log-Message "SSH connection successful"
            return $true
        } else {
            Write-Host "SSH connection failed with exit code $LASTEXITCODE"
            Log-Message "SSH connection failed with exit code $LASTEXITCODE"
            return $false
        }
    } catch {
        Write-Host "SSH connection error: $_"
        Log-Message "SSH connection error: $_"
        return $false
    }
}

# Function to sync build output log from remote to local
function Sync-BuildOutputLog {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing build output log from remote to local..."
    Log-Message "Syncing build output log from remote to local..."
    
    # Define local log file path
    $localLogPath = ".\logs\build_output.log"
    
    # Remove old log file if it exists
    if (Test-Path $localLogPath) {
        Remove-Item $localLogPath -Force
        Write-Host "Removed old build_output.log from local."
    }
    
    # Check if log file exists in main path on remote
    $checkLog = @"
if [ -f "$remotePath/build_output.log" ]; then
    echo "Found build_output.log in main directory"
    ls -l "$remotePath/build_output.log"
    exit 0
else
    echo "No build_output.log found in $remotePath"
    exit 1
fi
"@
    $checkLog | Out-File -FilePath "check_log.sh" -Encoding ASCII
    $result = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m check_log.sh
    Remove-Item check_log.sh
    
    if ($result -match "No build_output.log found") {
        Write-Host "No build output log found on remote server"
        Log-Message "No build output log found on remote server"
        return
    }
    
    # Create local logs directory if it doesn't exist
    $localLogDir = ".\logs"
    if (-not (Test-Path $localLogDir)) {
        New-Item -ItemType Directory -Path $localLogDir -Force | Out-Null
    }
    
    $remoteLogPath = "$($sshUser)@$($sshServer):$($remotePath)/build_output.log"
    
    Write-Host "Copying from: $remoteLogPath"
    Write-Host "To: $localLogPath"
    
    try {
        pscp -pw $sshPassword "$remoteLogPath" "$localLogPath"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Successfully copied build_output.log"
            Log-Message "Successfully copied build_output.log"
            
            # Display the first few lines of the log
            if (Test-Path $localLogPath) {
                Write-Host "`nFirst few lines of the log:"
                Get-Content $localLogPath -TotalCount 10
                
                # Also show the last few lines to see build completion status
                Write-Host "`nLast few lines of the log:"
                Get-Content $localLogPath -Tail 10
            }
        } else {
            Write-Host "Failed to copy build_output.log"
            Log-Message "Failed to copy build_output.log"
        }
    } catch {
        Write-Host "Error copying build_output.log: $_"
        Log-Message "Error copying build_output.log: $_"
    }
}

# Function to run hardware emulator
function Run-HardwareEmulator {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Running hardware emulator (press Ctrl+C to stop)..."
    Log-Message "Running hardware emulator..."
    
    Write-Host "The emulator output will be shown in real-time below:"
    Write-Host "----------------------------------------"
    Write-Host "Once the emulator starts, you can run:"
    Write-Host "./gemm_aie_xrt.elf a.xclbin"
    Write-Host "----------------------------------------"
    
    # First check if the directory exists and run emulator with proper environment
    $checkDir = @"
if [ -d "$hwEmuPath" ]; then
    # Source Vitis environment
    source /opt/Xilinx/Vitis/2024.1/settings64.sh
    cd $hwEmuPath
    
    # Run the emulator
    ./launch_hw_emu.sh
else
    echo "Error: Directory $hwEmuPath does not exist"
    echo "Please run the build first (option 2)"
fi
"@
    $checkDir | Out-File -FilePath "check_dir.sh" -Encoding ASCII
    plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -t -m check_dir.sh
    Remove-Item check_dir.sh
    
    Write-Host "----------------------------------------"
    Write-Host "Emulation completed or stopped by user"
}

# Function to view emulation results
function View-EmulationResults {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Viewing emulation results..."
    Log-Message "Viewing emulation results..."
    
    # Run the emulator and then execute the program in the emulator shell
    $runProgram = @"
if [ -d "$hwEmuPath" ]; then
    # Source Vitis environment
    source /opt/Xilinx/Vitis/2024.1/settings64.sh
    cd $hwEmuPath
    
    echo "Starting emulator and running program..."
    echo "----------------------------------------"
    
    # Clean up any existing log files
    rm -f qemu_output.log program_output.log simulate.log
    
    # Run the emulator in the background
    ./launch_hw_emu.sh -run-app ./gemm_aie_xrt.elf a.xclbin &
    EMULATOR_PID=$!
    
    # Wait for the emulator to start and show the prompt
    while true; do
        if [ -f qemu_output.log ] && grep -q "versal-rootfs-common-20241:/mnt#" qemu_output.log; then
            echo "Emulator is ready"
            break
        fi
        sleep 2
    done
    
    # Check if emulator is still running
    if ps -p $EMULATOR_PID > /dev/null; then
        echo "Emulator is running with PID $EMULATOR_PID"
        echo "----------------------------------------"
        echo "Running program..."
        echo "----------------------------------------"
        
        # Run the program and capture output with proper encoding handling
        ./gemm_aie_xrt.elf a.xclbin 2>&1 | iconv -f ASCII -t UTF-8//IGNORE | tee program_output.log
        
        echo "----------------------------------------"
        echo "Program execution completed"
        
        # Show the program output with proper encoding handling
        if [ -f program_output.log ]; then
            iconv -f ASCII -t UTF-8//IGNORE program_output.log
        else
            echo "No program output log found"
        fi
        
        # Clean up
        rm -f program_output.log qemu_output.log simulate.log
    else
        echo "Emulator failed to start"
        if [ -f simulate.log ]; then
            echo "Simulation log contents:"
            iconv -f ASCII -t UTF-8//IGNORE simulate.log
        fi
    fi
    
    echo "----------------------------------------"
else
    echo "Directory $hwEmuPath does not exist"
    echo "Please run the build first (option 8)"
fi
"@
    
    $runProgram | Out-File -FilePath "run_program.sh" -Encoding ASCII
    plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -t -m run_program.sh
    Remove-Item run_program.sh
}

# Function to clean local generated files
function Clean-LocalFiles {
    Write-Host "Cleaning local generated files..."
    Log-Message "Cleaning local generated files..."
    
    $localPaths = @(
        "design\aie_src\aiesim_data\gemm_${gemmSize}x${gemmSize}x${gemmSize}_ioFiles",
        "design\aie_src\aiesim_data\c.txt",
        "design\aie_src\aiesim_data\log.txt",
        "design\aie_src\aiesim_data\plioGen.log",
        "design\aie_src\aiesim_data\build_log.txt",
        "design\aie_src\aiesim_data\compare_outputs.log",
        "build_log.txt"
    )
    
    foreach ($path in $localPaths) {
        if (Test-Path $path) {
            if ((Get-Item $path) -is [System.IO.DirectoryInfo]) {
                Remove-Item -Path $path -Recurse -Force
                Write-Host "Removed directory: $path"
            } else {
                Remove-Item -Path $path -Force
                Write-Host "Removed file: $path"
            }
        }
    }
    
    Write-Host "Local cleanup completed"
    Log-Message "Local cleanup completed"
}

# Function to clean build on remote
function Clean-RemoteBuild {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Cleaning build folder and generated IO files on remote server..."
    Log-Message "Cleaning build folder and generated IO files on remote server..."
    
    $remoteCommands = @"
cd $remotePath
. ./Mahdieh_env_setup.sh
# Run make cleanall for thorough cleanup
echo "Running make cleanall..."
make cleanall 2>/dev/null || echo "make cleanall completed (some warnings expected)"
rm -f build_output.log  # Remove the build output log
rm -f *.xclbin         # Remove old XCLBIN files
rm -rf build            # Remove the build directory
rm -f c.txt            # Remove the generated output file
rm -f *.txt            # Remove any other generated text files
rm -f output_files/*   # Remove output_files directory contents
rm -rf output_files    # Remove output_files directory
# Clean only generated files in the specific GEMM directory, not the source code
rm -f aiesim_data/gemm_${gemmSize}x${gemmSize}x${gemmSize}_ioFiles/*.txt
rm -rf aiesim_data/gemm_${gemmSize}x${gemmSize}x${gemmSize}_ioFiles
echo "Cleaned build directory, output files, and generated IO files for GEMM size $gemmSize"
"@
    $remoteCommands | Out-File -FilePath "remote_clean.sh" -Encoding ASCII
    plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m remote_clean.sh
    Remove-Item remote_clean.sh
    
    Write-Host "Build cleanup completed - removed build directory and generated IO files for GEMM size $gemmSize"
    Log-Message "Build cleanup completed - removed build directory and generated IO files for GEMM size $gemmSize"
}

# Function to list XCLBIN contents
function List-XCLBINContents {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Listing XCLBIN contents..."
    Log-Message "Listing XCLBIN contents..."
    
    $remoteCommands = @"
cd $hwEmuPath
if [ -f "a.xclbin" ]; then
    echo "XCLBIN file exists. Contents:"
    echo "----------------------------------------"
    echo "Kernels and Graphs in XCLBIN:"
    xclbinutil --info --input a.xclbin | grep -E "Kernel:|Graph:"
    echo "----------------------------------------"
    echo "Full XCLBIN info:"
    xclbinutil --info --input a.xclbin
else
    echo "Error: a.xclbin not found in $hwEmuPath"
fi
"@
    $remoteCommands | Out-File -FilePath "list_xclbin.sh" -Encoding ASCII
    plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m list_xclbin.sh
    Remove-Item list_xclbin.sh
}

# Function to open server terminal and navigate
function Open-ServerTerminal {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Opening server terminal..."
    Log-Message "Opening server terminal..."
    
    Write-Host "You can use the following commands:"
    Write-Host "- cd <path> : Change directory"
    Write-Host "- ls : List files"
    Write-Host "- pwd : Show current directory"
    Write-Host "- cat <file> : View file contents"
    Write-Host "- exit : Return to menu"
    Write-Host "----------------------------------------"
    
    $startPath = Read-Host "Enter starting path (default: $remotePath)"
    if (-not $startPath) { $startPath = $remotePath }
    
    # Create a simple bash command to start the session
    $bashCommand = @"
cd $startPath
echo 'Current directory: \$(pwd)'
echo 'Available commands: cd, ls, pwd, cat, exit'
echo '----------------------------------------'
bash --norc
"@
    
    # Run plink directly with the bash command
    plink -ssh $sshUser@$sshServer -pw $sshPassword -t "$bashCommand"
}

# Remote to Local Sync Functions
function Sync-FromRemote {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing design folder from remote to local..."
    if (-not (Test-Path $localPath)) {
        New-Item -ItemType Directory -Path $localPath -Force
    }
    
    # Use pscp to copy only the design folder
    $remoteSource = "$($sshUser)@$($sshServer):$($designPath)/*"
    Write-Host "Copying from: $remoteSource"
    Write-Host "To: $localPath"
    pscp -r -pw $sshPassword "$remoteSource" "$localPath"
}

function Sync-MakefileFromRemote {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing Makefile from remote to local..."
    $remoteSource = "$($sshUser)@$($sshServer):$($remotePath)/Makefile"
    Write-Host "Copying from: $remoteSource"
    Write-Host "To: .\Makefile"
    pscp -pw $sshPassword "$remoteSource" ".\Makefile"
}

function Sync-EnvSetupFromRemote {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing Mahdieh_env_setup.sh from remote to local..."
    $remoteSource = "$($sshUser)@$($sshServer):$($remotePath)/Mahdieh_env_setup.sh"
    Write-Host "Copying from: $remoteSource"
    Write-Host "To: .\Mahdieh_env_setup.sh"
    pscp -pw $sshPassword "$remoteSource" ".\Mahdieh_env_setup.sh"
}

# Local to Remote Sync Functions
function Sync-ToRemote {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing design folder from local to remote..."
    $remoteDest = "${sshUser}@${sshServer}:${designPath}/"
    Write-Host "Copying from: $localPath"
    Write-Host "To: $remoteDest"
    
    # First remove existing files in remote design folder
    $cleanRemote = @"
cd $designPath && rm -rf *
"@
    $cleanRemote | Out-File -FilePath "clean_remote.sh" -Encoding ASCII
    plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m clean_remote.sh
    Remove-Item clean_remote.sh
    
    # Check if local files exist before syncing
    if (-not (Test-Path -Path "$localPath\*")) {
        Write-Host "No files found in local path: $localPath"
        Log-Message "No files found in local path: $localPath"
        return
    }

    # Use pscp to copy only the design folder
    $remoteSource = "$($sshUser)@$($sshServer):$($designPath)/*"
    Write-Host "Copying files to remote..."
    
    try {
        pscp -r -pw $sshPassword "$($localPath)\*" "$remoteDest"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Files successfully synced to remote."
            Log-Message "Files successfully synced to remote."
        } else {
            Write-Host "Failed to sync files to remote."
            Log-Message "Failed to sync files to remote."
        }
    } catch {
        Write-Host "Error during file sync: $_"
        Log-Message "Error during file sync: $_"
    }
}

function Sync-MakefileToRemote {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing Makefile from local to remote..."
    $remoteDest = "$($sshUser)@$($sshServer):$($remotePath)/"
    Write-Host "Copying from: .\Makefile"
    Write-Host "To: $remoteDest"
    pscp -pw $sshPassword ".\Makefile" "$remoteDest"
}

function Sync-EnvSetupToRemote {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing Mahdieh_env_setup.sh from local to remote..."
    $remoteDest = "$($sshUser)@$($sshServer):$($remotePath)/"
    Write-Host "Copying from: .\Mahdieh_env_setup.sh"
    Write-Host "To: $remoteDest"
    pscp -pw $sshPassword ".\Mahdieh_env_setup.sh" "$remoteDest"
}

# Function to run the build on remote
function Run-RemoteBuild {
    if (-not (Test-SSHConnection)) { return }
    # Ensure derived parameters are populated in config
    Ensure-DerivedParams
    
    Write-Host "Running build on remote server..."
    Log-Message "Running build on remote server..."
    
    # Check if streaming is enabled in config
    $streamingEnabled = $false
    if (Test-Path $script:configPath) {
        try {
            $config = Get-Content $script:configPath -Raw | ConvertFrom-Json
            if ($null -ne $config.STREAMING_ENABLED) {
                $streamingEnabled = $config.STREAMING_ENABLED
            }
        } catch {
            Write-Host "Warning: Could not read streaming config, using default"
        }
    }
    
    if ($streamingEnabled) {
        Write-Host "🚀 Streaming architecture is ENABLED - Using dma_hls_streaming.cpp"
        Log-Message "Streaming architecture is ENABLED - Using dma_hls_streaming.cpp"
    } else {
        Write-Host "📦 Standard architecture - Using dma_hls.cpp"
        Log-Message "Standard architecture - Using dma_hls.cpp"
    }
    
    $remoteCommands = @"
cd $remotePath
. ./Mahdieh_env_setup.sh
# Redirect build output to log file
if [ "$($script:TARGET)" = "hw" ]; then
    make all TARGET=$($script:TARGET) ITER_CNT=1 EN_TRACE=0 GEMM_SIZE=$($script:gemmSize) DIM=$($script:DIM) ENABLE_ML_BENCHMARKS=$($script:ENABLE_ML_BENCHMARKS) 2>&1 | tee build_output.log
else
    make run TARGET=$($script:TARGET) ITER_CNT=1 EN_TRACE=0 GEMM_SIZE=$($script:gemmSize) DIM=$($script:DIM) ENABLE_ML_BENCHMARKS=$($script:ENABLE_ML_BENCHMARKS) 2>&1 | tee build_output.log
fi
# Ensure log file is readable
chmod 644 build_output.log
"@
    $remoteCommands | Out-File -FilePath "remote_commands.sh" -Encoding ASCII
    plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m remote_commands.sh
    Remove-Item remote_commands.sh
}

# Function to run Vivado report generation on remote
function Run-VivadoReports {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Running Vivado report generation on remote server..."
    Log-Message "Running Vivado report generation on remote server..."
    
    $gemmSize = $script:gemmSize
    $target = $script:TARGET
    $remoteCommands = @"
cd $remotePath
. ./Mahdieh_env_setup.sh
# Run Vivado report generation
echo "Starting Vivado report generation..."
echo "Project: gemm_${gemmSize}x${gemmSize}x${gemmSize}"
echo "Target: ${target}"
echo "=================================="

# Check if project file exists
PROJECT_FILE="$remotePath/build/gemm_${gemmSize}x${gemmSize}x${gemmSize}/x1/${target}/_x/link/vivado/vpl/prj/prj.xpr"
if [ ! -f "$PROJECT_FILE" ]; then
    echo "ERROR: Project file not found: $PROJECT_FILE"
    echo "Please run the build first (option 3)"
    echo "Available files in build directory:"
    ls -la "$remotePath/build/gemm_${gemmSize}x${gemmSize}x${gemmSize}/x1/${target}/_x/link/vivado/vpl/prj/" 2>/dev/null || echo "Directory not found"
    exit 1
fi

# Create reports directory
REPORTS_DIR="$remotePath/reports_dir/gemm_${gemmSize}x${gemmSize}x${gemmSize}/x1"
mkdir -p "$REPORTS_DIR"

# Run Vivado report generation
cd "$REPORTS_DIR"
echo "Running Vivado with project file: $PROJECT_FILE"
echo "Reports directory: $REPORTS_DIR"
VIVADO_PROJECT_FILE="$PROJECT_FILE" VIVADO_REPORTS_DIR="$REPORTS_DIR" vivado -mode batch -source "$remotePath/design/vivado_metrics_scripts/report_metrics.tcl" 2>&1 | tee vivado_reports.log

echo "Vivado report generation completed!"
echo "Reports generated in: $REPORTS_DIR"
ls -la "$REPORTS_DIR"
"@
    
    $remoteCommands | Out-File -FilePath "vivado_reports.sh" -Encoding ASCII
    plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -t -m vivado_reports.sh
    Remove-Item vivado_reports.sh
}

# Function to sync Vivado reports from remote to local
function Sync-VivadoReports {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing Vivado reports from remote to local..."
    Log-Message "Syncing Vivado reports from remote to local..."
    
    # Create local reports directory
    $localReportsDir = ".\reports\vivado_reports"
    if (-not (Test-Path $localReportsDir)) {
        New-Item -ItemType Directory -Path $localReportsDir -Force | Out-Null
    }
    
    # Define remote reports path
    $remoteReportsPath = "$($sshUser)@$($sshServer):$($remotePath)/reports/gemm_$($script:gemmSize)x$($script:gemmSize)x$($script:gemmSize)/x1/*"
    
    Write-Host "Copying Vivado reports from: $remoteReportsPath"
    Write-Host "To: $localReportsDir"
    
    try {
        pscp -pw $sshPassword "$remoteReportsPath" "$localReportsDir\"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Successfully synced Vivado reports"
            Log-Message "Successfully synced Vivado reports"
            
            # List the synced reports
            if (Test-Path $localReportsDir) {
                Write-Host "`n=== VIVADO REPORTS SYNCED ==="
                Get-ChildItem $localReportsDir | ForEach-Object { Write-Host "  - $($_.Name)" }
                Write-Host "==============================="
            }
        } else {
            Write-Host "Failed to sync Vivado reports"
            Log-Message "Failed to sync Vivado reports"
        }
    } catch {
        Write-Host "Error syncing Vivado reports: $_"
        Log-Message "Error syncing Vivado reports: $_"
    }
}


# Run program on target/emulator with CPU GEMM benchmarking switch
function Run-ProgramWithCpuBench {
    if (-not (Test-SSHConnection)) { return }

    $remoteCommands = @"
cd $remotePath/build/gemm_${script:gemmSize}x${script:gemmSize}x${script:gemmSize}/x1/$($script:TARGET)/package
if [ -f ./gemm_aie_xrt.elf ] && [ -f a.xclbin ]; then
  echo "Running gemm_aie_xrt.elf with CPU GEMM comparisons (all variants)"
  ./gemm_aie_xrt.elf a.xclbin | tee -a program_output.log
else
  echo "Executable or xclbin not found. Please run build first."
fi
"@
    $remoteCommands | Out-File -FilePath "remote_run_app.sh" -Encoding ASCII
    plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m remote_run_app.sh
    Remove-Item remote_run_app.sh
}

# Function to export QEMU output
function Export-QEMUOutput {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Exporting QEMU output..."
    Log-Message "Exporting QEMU output..."
    
    $remoteQEMUOutputPath = "$hwEmuPath/qemu_output.log"
    $localQEMUOutputPath = ".\logs\qemu_output.log"
    
    # Create local logs directory if it doesn't exist
    if (-not (Test-Path ".\logs")) {
        New-Item -ItemType Directory -Path ".\logs" -Force | Out-Null
    }
    
    # Check if the QEMU output file exists on the remote server
    $checkQEMUOutput = @"
if [ -f "$remoteQEMUOutputPath" ]; then
    echo "QEMU output file exists."
    exit 0
else
    echo "QEMU output file not found."
    exit 1
fi
"@
    $checkQEMUOutput | Out-File -FilePath "check_qemu_output.sh" -Encoding ASCII
    $result = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m check_qemu_output.sh
    Remove-Item check_qemu_output.sh
    
    if ($result -match "QEMU output file exists") {
        # Copy the QEMU output file to local
        $remoteQEMUPath = "$($sshUser)@$($sshServer):$($remoteQEMUOutputPath)"
        pscp -pw $sshPassword "$remoteQEMUPath" "$localQEMUOutputPath"
        Write-Host "Successfully exported QEMU output to $localQEMUOutputPath"
        Log-Message "Successfully exported QEMU output to $localQEMUOutputPath"
    } else {
        Write-Host "QEMU output file not found on remote server."
        Log-Message "QEMU output file not found on remote server."
    }
}

# Function to download optimization reports
function Download-OptimizationReports {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Downloading optimization reports..."
    Log-Message "Downloading optimization reports..."
    
    # Create local reports directory if it doesn't exist
    $localReportsDir = ".\reports"
    if (-not (Test-Path $localReportsDir)) {
        New-Item -ItemType Directory -Path $localReportsDir -Force | Out-Null
    } else {
        # Remove old reports if they exist
        Remove-Item "$localReportsDir\*" -Recurse -Force
        Write-Host "Removed old optimization reports from local."
    }
    
    # Define remote report paths based on current GEMM size
    $remoteBuildPath = "$remotePath/build/gemm_${gemmSize}x${gemmSize}x${gemmSize}/x1/hw_emu"
    $remoteReportsPath = "$remoteBuildPath/_x/reports/dma_hls.hw_emu"
    $remoteHLSLogPath = "$remoteBuildPath/_x/dma_hls.hw_emu/dma_hls/vitis_hls.log"
    $remoteCompileSummaryPath = "$remoteBuildPath/dma_hls.hw_emu.xo.compile_summary"
    
    Write-Host "Remote reports path: $remoteReportsPath"
    
    # Check if reports directory exists
    $checkReports = @"
if [ -d "$remoteReportsPath" ]; then
    echo "Reports directory exists"
    ls -la "$remoteReportsPath"
    exit 0
else
    echo "Reports directory not found at $remoteReportsPath"
    exit 1
fi
"@
    $checkReports | Out-File -FilePath "check_reports.sh" -Encoding ASCII
    $result = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m check_reports.sh
    Remove-Item check_reports.sh
    
    if ($result -match "Reports directory exists") {
        Write-Host "Found reports directory. Downloading reports..."
        
        # Download system estimate report
        $systemEstimateRemote = "$($sshUser)@$($sshServer):$($remoteReportsPath)/system_estimate_dma_hls.hw_emu.xtxt"
        $systemEstimateLocal = "$localReportsDir\system_estimate_dma_hls.hw_emu.xtxt"
        pscp -pw $sshPassword "$systemEstimateRemote" "$systemEstimateLocal" 2>$null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✓ Downloaded: system_estimate_dma_hls.hw_emu.xtxt"
        }
        
        # Download guidance report
        $guidanceRemote = "$($sshUser)@$($sshServer):$($remoteReportsPath)/v++_compile_dma_hls.hw_emu_guidance.html"
        $guidanceLocal = "$localReportsDir\v++_compile_dma_hls.hw_emu_guidance.html"
        pscp -pw $sshPassword "$guidanceRemote" "$guidanceLocal" 2>$null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✓ Downloaded: v++_compile_dma_hls.hw_emu_guidance.html"
        }
        
        # Download HLS log
        $hlsLogRemote = "$($sshUser)@$($sshServer):$($remoteHLSLogPath)"
        $hlsLogLocal = "$localReportsDir\vitis_hls.log"
        pscp -pw $sshPassword "$hlsLogRemote" "$hlsLogLocal" 2>$null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✓ Downloaded: vitis_hls.log"
        }
        
        # Download compilation summary
        $compileSummaryRemote = "$($sshUser)@$($sshServer):$($remoteCompileSummaryPath)"
        $compileSummaryLocal = "$localReportsDir\dma_hls.hw_emu.xo.compile_summary"
        pscp -pw $sshPassword "$compileSummaryRemote" "$compileSummaryLocal" 2>$null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✓ Downloaded: dma_hls.hw_emu.xo.compile_summary"
        }
        
        # Download entire reports directory (for any additional reports)
        $allReportsRemote = "$($sshUser)@$($sshServer):$($remoteReportsPath)/*"
        pscp -r -pw $sshPassword "$allReportsRemote" "$localReportsDir\" 2>$null
        
        Write-Host ""
        Write-Host "=== OPTIMIZATION REPORTS DOWNLOADED ==="
        Write-Host "1. System Estimate: $localReportsDir\system_estimate_dma_hls.hw_emu.xtxt"
        Write-Host "2. Guidance Report: $localReportsDir\v++_compile_dma_hls.hw_emu_guidance.html"
        Write-Host "3. HLS Log: $localReportsDir\vitis_hls.log"
        Write-Host "4. Compile Summary: $localReportsDir\dma_hls.hw_emu.xo.compile_summary"
        Write-Host ""
        Write-Host "🔍 RECOMMENDED ANALYSIS ORDER:"
        Write-Host "1. Open guidance report in browser for optimization suggestions"
        Write-Host "2. Check system estimate for resource utilization"
        Write-Host "3. Review HLS log for detailed loop analysis"
        Write-Host "========================================="
        
        Log-Message "Successfully downloaded optimization reports to $localReportsDir"
    } else {
        Write-Host "Reports directory not found on remote server."
        Write-Host "Make sure the build completed successfully before downloading reports."
        Log-Message "Reports directory not found on remote server."
    }
}

# Function to show optimization reports help
function Show-ReportsHelp {
    Write-Host ""
    Write-Host "==============================================="
    Write-Host "         OPTIMIZATION REPORTS GUIDE"
    Write-Host "==============================================="
    Write-Host ""
    Write-Host "📊 SYSTEM ESTIMATE REPORT (system_estimate_dma_hls.hw_emu.xtxt):"
    Write-Host "   • Resource utilization"
    Write-Host "   • Memory bandwidth usage" 
    Write-Host "   • Performance estimates"
    Write-Host "   • Burst efficiency metrics"
    Write-Host ""
    Write-Host "🎯 GUIDANCE REPORT (v++_compile_dma_hls.hw_emu_guidance.html):"
    Write-Host "   • Specific optimization suggestions"
    Write-Host "   • Memory access pattern recommendations"
    Write-Host "   • Burst optimization tips"
    Write-Host "   • II improvement suggestions"
    Write-Host ""
    Write-Host "🔍 HLS LOG (vitis_hls.log):"
    Write-Host "   • Detailed loop analysis"
    Write-Host "   • Memory interface information"
    Write-Host "   • Why loops didn't achieve target II"
    Write-Host "   • Burst length utilization details"
    Write-Host ""
    Write-Host "📋 COMPILATION SUMMARY (dma_hls.hw_emu.xo.compile_summary):"
    Write-Host "   • Overall design metrics"
    Write-Host "   • Can be opened with Vitis IDE for visual analysis"
    Write-Host ""
    Write-Host "🚀 RECOMMENDED WORKFLOW:"
    Write-Host "   1. Download reports (option 10)"
    Write-Host "   2. Open guidance report in browser first"
    Write-Host "   3. Check system estimate for bottlenecks"
    Write-Host "   4. Review HLS log for detailed loop issues"
    Write-Host "   5. Use compilation summary for Vitis IDE analysis"
    Write-Host ""
    Write-Host "💡 KEY METRICS TO LOOK FOR:"
    Write-Host "   • II (Initiation Interval) - target is 1"
    Write-Host "   • Memory bandwidth utilization"
    Write-Host "   • Burst length efficiency" 
    Write-Host "   • Resource conflicts"
    Write-Host "   • Pipeline depth and latency"
    Write-Host ""
    Write-Host "==============================================="
    Write-Host ""
    Read-Host "Press Enter to return to main menu"
}

# Function to sync HLS log from remote to local
function Sync-HLSLog {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing HLS log from remote to local..."
    Log-Message "Syncing HLS log from remote to local..."
    
    # Define remote and local HLS log paths
    $remoteHLSLogPath = "$remotePath/build/gemm_${gemmSize}x${gemmSize}x${gemmSize}/x1/hw/_x/dma_hls.hw/dma_hls/vitis_hls.log"
    $localHLSLogPath = ".\logs\vitis_hls.log"
    
    # Create local logs directory if it doesn't exist
    $localLogDir = ".\logs"
    if (-not (Test-Path $localLogDir)) {
        New-Item -ItemType Directory -Path $localLogDir -Force | Out-Null
    }
    
    # Check if HLS log file exists on remote
    $checkHLSLog = @"
if [ -f "$remoteHLSLogPath" ]; then
    echo "HLS log file exists"
    ls -l "$remoteHLSLogPath"
    exit 0
else
    echo "HLS log file not found at $remoteHLSLogPath"
    echo "Available files in directory:"
    ls -la "`$(dirname "$remoteHLSLogPath")" 2>/dev/null || echo "Directory not found"
    exit 1
fi
"@
    $checkHLSLog | Out-File -FilePath "check_hls_log.sh" -Encoding ASCII
    $result = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m check_hls_log.sh
    Remove-Item check_hls_log.sh
    
    if ($result -match "HLS log file exists") {
        # Remove old HLS log if it exists
        if (Test-Path $localHLSLogPath) {
            Remove-Item $localHLSLogPath -Force
            Write-Host "Removed old vitis_hls.log from local."
        }
        
        $remoteHLSSource = "$($sshUser)@$($sshServer):$($remoteHLSLogPath)"
        
        Write-Host "Copying from: $remoteHLSSource"
        Write-Host "To: $localHLSLogPath"
        
        try {
            pscp -pw $sshPassword "$remoteHLSSource" "$localHLSLogPath"
            if ($LASTEXITCODE -eq 0) {
                Write-Host "Successfully copied vitis_hls.log"
                Log-Message "Successfully copied vitis_hls.log"
                
                # Display key loop analysis information
                if (Test-Path $localHLSLogPath) {
                    Write-Host "`n=== HLS LOG ANALYSIS ==="
                    Write-Host "Searching for loop information..."
                    
                    # Search for loop pipelining results
                    $content = Get-Content $localHLSLogPath
                    $loopInfo = $content | Select-String -Pattern "Pipelining loop|Pipelining result|main_inp_A_block_loop|inp_B_optimized_loop|out_C_loop"
                    
                    if ($loopInfo) {
                        Write-Host "`nLoop Analysis Found:"
                        Write-Host "------------------------"
                        $loopInfo | ForEach-Object { Write-Host $_.Line }
                    } else {
                        Write-Host "No specific loop information found. Check the full log file."
                    }
                    
                    Write-Host "`n=== FIRST 20 LINES OF HLS LOG ==="
                    Get-Content $localHLSLogPath -TotalCount 20
                    
                    Write-Host "`n=== LAST 20 LINES OF HLS LOG ==="
                    Get-Content $localHLSLogPath -Tail 20
                    Write-Host "========================"
                }
            } else {
                Write-Host "Failed to copy vitis_hls.log"
                Log-Message "Failed to copy vitis_hls.log"
            }
        } catch {
            Write-Host "Error copying vitis_hls.log: $_"
            Log-Message "Error copying vitis_hls.log: $_"
        }
    } else {
        Write-Host "HLS log file not found on remote server."
        Write-Host "Make sure the HW build completed successfully."
        Write-Host "Expected path: $remoteHLSLogPath"
        Log-Message "HLS log file not found on remote server at $remoteHLSLogPath"
    }
}

# Function to sync sd_card.img from remote to local
function Sync-SDCardImage {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing sd_card.img from remote to local..."
    Log-Message "Syncing sd_card.img from remote to local..."
    
    # Define remote and local paths using parametric values
    $remoteSDCardPath = "$($sshUser)@$($sshServer):/media/josnu02/large_SDD/mgrailoo/build/gemm_${script:gemmSize}x${script:gemmSize}x${script:gemmSize}/x1/$($script:TARGET)/package/sd_card.img"
    $localSDCardPath = ".\sd_card\sd_card.img"
    
    # Create local sd_card directory if it doesn't exist
    $localSDCardDir = ".\sd_card"
    if (-not (Test-Path $localSDCardDir)) {
        New-Item -ItemType Directory -Path $localSDCardDir -Force | Out-Null
    }
    
    Write-Host "Copying from: $remoteSDCardPath"
    Write-Host "To: $localSDCardPath"
    
    try {
        pscp -pw $sshPassword "$remoteSDCardPath" "$localSDCardPath"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Successfully copied sd_card.img"
            Log-Message "Successfully copied sd_card.img"
        } else {
            Write-Host "Failed to copy sd_card.img"
            Log-Message "Failed to copy sd_card.img"
        }
    } catch {
        Write-Host "Error copying sd_card.img: $_"
        Log-Message "Error copying sd_card.img: $_"
    }
}

# Function to sync comprehensive logs and reports from remote to local
function Sync-ComprehensiveReports {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing comprehensive logs and reports from remote to local..."
    Log-Message "Syncing comprehensive logs and reports from remote to local..."
    
    # First, generate Vivado reports if they don't exist
    Write-Host "🔧 Checking and generating Vivado reports first..."
    Log-Message "Checking and generating Vivado reports first..."
    
    $gemmSize = $script:gemmSize
    $target = $script:TARGET
    $remoteCommands = @"
cd $remotePath
. ./Mahdieh_env_setup.sh

# Check if Vivado reports already exist
REPORTS_DIR="$remotePath/reports/gemm_${gemmSize}x${gemmSize}x${gemmSize}/x1"
if [ -f "$REPORTS_DIR/utilization_report.txt" ] && [ -f "$REPORTS_DIR/timing_summary.txt" ] && [ -f "$REPORTS_DIR/power_report.txt" ]; then
    echo "✅ Vivado reports already exist, skipping generation"
    exit 0
fi

# Check if project file exists
PROJECT_FILE="$remotePath/build/gemm_${gemmSize}x${gemmSize}x${gemmSize}/x1/${target}/_x/link/vivado/vpl/prj/prj.xpr"
if [ ! -f "$PROJECT_FILE" ]; then
    echo "❌ ERROR: Project file not found: $PROJECT_FILE"
    echo "Please run the build first (option 3)"
    exit 1
fi

# Create reports directory
mkdir -p "$REPORTS_DIR"

# Run Vivado report generation
cd "$REPORTS_DIR"
echo "🔧 Generating Vivado reports..."
echo "Project: gemm_${gemmSize}x${gemmSize}x${gemmSize}"
echo "Target: ${target}"
echo "=================================="

VIVADO_PROJECT_FILE="$PROJECT_FILE" VIVADO_REPORTS_DIR="$REPORTS_DIR" vivado -mode batch -source "$remotePath/design/vivado_metrics_scripts/report_metrics.tcl" 2>&1 | tee vivado_reports.log

echo "✅ Vivado report generation completed!"
echo "Reports generated in: $REPORTS_DIR"
ls -la "$REPORTS_DIR"
"@
    
    $remoteCommands | Out-File -FilePath "generate_vivado_reports.sh" -Encoding ASCII
    plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -t -m generate_vivado_reports.sh
    Remove-Item generate_vivado_reports.sh
    
    # Create local reports and logs directories if they don't exist
    $localReportsDir = ".\reports"
    $localLogDir = ".\logs"
    if (-not (Test-Path $localReportsDir)) { New-Item -ItemType Directory -Path $localReportsDir -Force | Out-Null }
    if (-not (Test-Path $localLogDir)) { New-Item -ItemType Directory -Path $localLogDir -Force | Out-Null }

    # Define remote build path
    $remoteBuildPath = "$remotePath/build/gemm_${script:gemmSize}x${script:gemmSize}x${script:gemmSize}/x1/$($script:TARGET)"
    # Clean up any double slashes in the path
    $remoteBuildPath = $remoteBuildPath -replace '//+', '/'
    # Define remote reports path (REPORTS_REPO)
    $remoteReportsRepo = "$remotePath/reports"
    # Define remote Work/reports path
    $remoteWorkReports = "$remoteBuildPath/Work/reports"
    # Define remote Vitis reports path
    $remoteVitisReports = "$remoteBuildPath/_x/reports"
    # Define remote HLS paths
    $remoteHlsRoot = "$remoteBuildPath/_x/dma_hls.$($script:TARGET)/dma_hls"
    $remoteHlsLog = "$remoteHlsRoot/vitis_hls.log"
    $remoteHlsReports = "$remoteHlsRoot/hls_reports"
    # Define remote build_output.log path
    $remoteBuildOutputLog = "$remotePath/build_output.log"

    # Helper to copy a remote directory recursively if it exists
    function Copy-RemoteDirIfExists {
        param(
            [string]$RemoteDir,
            [string]$LocalDir,
            [string]$Label
        )
        $check = @"
if [ -d "$RemoteDir" ]; then
  echo "EXISTS"
else
  echo "MISSING"
fi
"@
        $check | Out-File -FilePath "_check_dir_tmp.sh" -Encoding ASCII
        $existsOut = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m _check_dir_tmp.sh
        Remove-Item _check_dir_tmp.sh
        if ($existsOut -match "EXISTS") {
            Write-Host "Copying $Label from $RemoteDir to $LocalDir"
            pscp -r -pw $sshPassword "$($sshUser)@$($sshServer):$RemoteDir/*" "$LocalDir\" 2>$null
        } else {
            Write-Host "Skip: $Label not found at $RemoteDir"
        }
    }

    # Helper to copy a single file or pattern (non-recursive) if exists
    function Copy-RemoteFilesIfExist {
        param(
            [string]$RemotePattern,
            [string]$LocalDir,
            [string]$Label
        )
        $check = @"
for f in $RemotePattern; do
  if [ -f "$f" ]; then echo "$f"; fi
done
"@
        $check | Out-File -FilePath "_check_files_tmp.sh" -Encoding ASCII
        $foundOut = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m _check_files_tmp.sh
        Remove-Item _check_files_tmp.sh
        if ($foundOut) {
            $lines = $foundOut -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
            foreach ($path in $lines) {
                if ([string]::IsNullOrWhiteSpace($path)) { continue }
                $fname = Split-Path $path -Leaf
                if ([string]::IsNullOrWhiteSpace($fname)) { continue }
                Write-Host "Copying ${Label}: $path"
                pscp -pw $sshPassword "$($sshUser)@$($sshServer):$path" "$LocalDir\$fname" 2>$null
            }
        } else {
            Write-Host "Skip: No $Label files matching $RemotePattern"
        }
    }

    # Copy a bounded set of items to avoid long recursive scans
    # 1) Copy top-level useful files from the main build directory (non-recursive)
    $topPatterns = @("*.log", "*link_summary*", "*.xclbin.info", "*.compile_summary", "*system_estimate*.xtxt", "*guidance*.html")
    foreach ($pat in $topPatterns) {
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/$pat" -LocalDir $localReportsDir -Label "top-level report"
    }

    # 2) Copy Vitis reports folder recursively (_x/reports)
    Copy-RemoteDirIfExists -RemoteDir $remoteVitisReports -LocalDir $localReportsDir -Label "_x/reports"

    # 2a) Explicitly copy dma_hls.hw report directory if present
    Copy-RemoteDirIfExists -RemoteDir "$remoteBuildPath/_x/reports/dma_hls.hw" -LocalDir "$localReportsDir\dma_hls.hw" -Label "dma_hls.hw reports"

    # 3) Copy HLS files: vitis_hls.log to logs, and hls_reports/*.rpt to reports
    Copy-RemoteFilesIfExist -RemotePattern $remoteHlsLog -LocalDir $localLogDir -Label "HLS log"
    Copy-RemoteDirIfExists -RemoteDir $remoteHlsReports -LocalDir "$localReportsDir\hls" -Label "HLS hls_reports"

    # 4) Copy AIE Work/reports recursively
    Copy-RemoteDirIfExists -RemoteDir $remoteWorkReports -LocalDir $localReportsDir -Label "AIE Work/reports"
    
    # 4c) Copy AIE compilation logs specifically
    Write-Host "Looking for AI Engine compilation logs..."
    $aieCompileLogs = @("graph.aiecompile_summary", "*.aiecompile_summary", "aiecompiler.log", "aie_compile.log")
    foreach ($log in $aieCompileLogs) {
        # Look in Work directory
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/$log" -LocalDir $localLogDir -Label "AIE Compile $log"
        # Look in Work/aie directory
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/aie/$log" -LocalDir $localLogDir -Label "AIE Compile/aie $log"
        # Look in main build directory
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/$log" -LocalDir $localLogDir -Label "AIE Compile/main $log"
    }
    
    # 4d) Copy specific AIE files we know exist
    Write-Host "Looking for specific AI Engine files..."
    $specificAieFiles = @("graph.aiecompile_summary", "active_cores.json", "AddressSpace.txt", "AliasAnalysisReport.txt", "Makefile")
    foreach ($file in $specificAieFiles) {
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/$file" -LocalDir $localLogDir -Label "AIE Specific $file"
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/aie/$file" -LocalDir $localLogDir -Label "AIE Specific/aie $file"
    }
    
    # 4d1) Direct copy of known AI Engine files
    Write-Host "Copying known AI Engine files directly..."
    
    # Copy graph.aiecompile_summary from Work directory
    $graphFile = "$($sshUser)@$($sshServer):$remoteBuildPath/Work/graph.aiecompile_summary"
    Write-Host "Copying graph.aiecompile_summary from: $graphFile"
    pscp -pw $sshPassword "$graphFile" "$localLogDir\graph.aiecompile_summary" 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ Successfully copied graph.aiecompile_summary"
    } else {
        Write-Host "✗ Failed to copy graph.aiecompile_summary"
    }
    
    # Copy active_cores.json from Work/aie directory
    $activeCoresFile = "$($sshUser)@$($sshServer):$remoteBuildPath/Work/aie/active_cores.json"
    Write-Host "Copying active_cores.json from: $activeCoresFile"
    pscp -pw $sshPassword "$activeCoresFile" "$localLogDir\active_cores.json" 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ Successfully copied active_cores.json"
    } else {
        Write-Host "✗ Failed to copy active_cores.json"
    }
    
    # Copy AddressSpace.txt from Work/aie directory
    $addressSpaceFile = "$($sshUser)@$($sshServer):$remoteBuildPath/Work/aie/AddressSpace.txt"
    Write-Host "Copying AddressSpace.txt from: $addressSpaceFile"
    pscp -pw $sshPassword "$addressSpaceFile" "$localLogDir\AddressSpace.txt" 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ Successfully copied AddressSpace.txt"
    } else {
        Write-Host "✗ Failed to copy AddressSpace.txt"
    }
    
    # Copy AliasAnalysisReport.txt from Work/aie directory
    $aliasReportFile = "$($sshUser)@$($sshServer):$remoteBuildPath/Work/aie/AliasAnalysisReport.txt"
    Write-Host "Copying AliasAnalysisReport.txt from: $aliasReportFile"
    pscp -pw $sshPassword "$aliasReportFile" "$localLogDir\AliasAnalysisReport.txt" 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ Successfully copied AliasAnalysisReport.txt"
    } else {
        Write-Host "✗ Failed to copy AliasAnalysisReport.txt"
    }
    
    # Copy aiecompiler.log from main build directory
    $aieCompilerLogFile = "$($sshUser)@$($sshServer):$remoteBuildPath/aiecompiler.log"
    Write-Host "Copying aiecompiler.log from: $aieCompilerLogFile"
    pscp -pw $sshPassword "$aieCompilerLogFile" "$localLogDir\aiecompiler.log" 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ Successfully copied aiecompiler.log"
    } else {
        Write-Host "✗ Failed to copy aiecompiler.log"
    }
    
    # 4d2) Copy additional AI Engine files from Work/aie directory
    Write-Host "Copying additional AI Engine files from Work/aie directory..."
    
    # Copy Makefile from Work/aie directory
    $makefileFile = "$($sshUser)@$($sshServer):$remoteBuildPath/Work/aie/Makefile"
    Write-Host "Copying Makefile from: $makefileFile"
    pscp -pw $sshPassword "$makefileFile" "$localLogDir\aie_Makefile" 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ Successfully copied aie_Makefile"
    } else {
        Write-Host "✗ Failed to copy aie_Makefile"
    }
    
    # 4a) Copy AIE logs and reports from various locations
    $aieLogs = @("aiesimulator.log", "aiesim.log", "aiecompiler.log", "vcd.log", "*.log")
    foreach ($log in $aieLogs) {
        # Look in main build directory
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/$log" -LocalDir $localLogDir -Label "AIE $log"
        # Look in Work directory
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/$log" -LocalDir $localLogDir -Label "AIE Work $log"
        # Look in Work/logs directory
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/logs/$log" -LocalDir $localLogDir -Label "AIE Work/logs $log"
        # Look in Work/aie directory
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/aie/$log" -LocalDir $localLogDir -Label "AIE Work/aie $log"
        # Look in individual tile directories
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/aie/*/$log" -LocalDir $localLogDir -Label "AIE Work/aie/tiles $log"
    }
    
    # 4b) Copy AIE output files from various locations
    $aieOutputs = @("*.vcd", "*.xpe", "*.txt", "*.json", "*.csv")
    foreach ($pattern in $aieOutputs) {
        # Look in main build directory
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/$pattern" -LocalDir $localReportsDir -Label "AIE $pattern"
        # Look in Work directory
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/$pattern" -LocalDir $localReportsDir -Label "AIE Work $pattern"
        # Look in Work/aie directory
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/aie/$pattern" -LocalDir $localReportsDir -Label "AIE Work/aie $pattern"
        # Look in individual tile directories
        Copy-RemoteFilesIfExist -RemotePattern "$remoteBuildPath/Work/aie/*/$pattern" -LocalDir $localReportsDir -Label "AIE Work/aie/tiles $pattern"
    }

    # 5) Always copy build_output.log to logs folder
    Write-Host "Copying build_output.log to $localLogDir"
    pscp -pw $sshPassword "$($sshUser)@$($sshServer):$remoteBuildOutputLog" "$localLogDir\" 2>$null

    # 6) Copy global reports recursively if exists
    Copy-RemoteDirIfExists -RemoteDir $remoteReportsRepo -LocalDir $localReportsDir -Label "global reports"

    # 7) Copy Vivado reports from report_metrics.tcl if they exist
    Write-Host "Checking for Vivado reports from report_metrics.tcl..."
    
    # Search for Vivado reports in multiple possible locations
    $possibleVivadoPaths = @(
        "$remotePath/reports_dir/gemm_${script:gemmSize}x${script:gemmSize}x${script:gemmSize}/x1",
        "$remotePath/reports/gemm_${script:gemmSize}x${script:gemmSize}x${script:gemmSize}/x1",
        "$remotePath/build/gemm_${script:gemmSize}x${script:gemmSize}x${script:gemmSize}/x1/$($script:TARGET)/Work/reports"
    )
    
    $vivadoReportsFound = $false
    foreach ($vivadoPath in $possibleVivadoPaths) {
        Write-Host "Searching for Vivado reports in: $vivadoPath"
        
        # Check if this path contains Vivado reports
        $checkVivadoReports = @"
if [ -d "$vivadoPath" ]; then
    if [ -f "$vivadoPath/utilization_report.txt" ] || [ -f "$vivadoPath/timing_summary.txt" ] || [ -f "$vivadoPath/power_report.txt" ]; then
        echo "FOUND_VIVADO_REPORTS"
        ls -la "$vivadoPath"/*.txt 2>/dev/null || echo "No .txt files"
        ls -la "$vivadoPath"/*.html 2>/dev/null || echo "No .html files"
        ls -la "$vivadoPath"/*.xml 2>/dev/null || echo "No .xml files"
    else
        echo "NO_VIVADO_REPORTS"
    fi
else
    echo "PATH_NOT_FOUND"
fi
"@
        $checkVivadoReports | Out-File -FilePath "check_vivado_reports.sh" -Encoding ASCII
        $result = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m check_vivado_reports.sh
        Remove-Item check_vivado_reports.sh
        
        if ($result -match "FOUND_VIVADO_REPORTS") {
            Write-Host "✅ Found Vivado reports in: $vivadoPath"
            $vivadoReportsFound = $true
            
            # List all files in the directory to see what's actually there
            $listFiles = @"
ls -la "$vivadoPath"
"@
            $listFiles | Out-File -FilePath "list_vivado_files.sh" -Encoding ASCII
            $fileList = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m list_vivado_files.sh
            Remove-Item list_vivado_files.sh
            Write-Host "Files in Vivado reports directory:"
            Write-Host $fileList
            
            # Copy all Vivado reports from this location using direct pscp commands
            Write-Host "Copying Vivado reports directly..."
            
                   # Copy all .txt files (skip if already exist)
                   $txtFiles = @"
find "$vivadoPath" -name "*.txt" -type f
"@
                   $txtFiles | Out-File -FilePath "find_txt_files.sh" -Encoding ASCII
                   $txtFileList = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m find_txt_files.sh
                   Remove-Item find_txt_files.sh
                   
                   if ($txtFileList -and $txtFileList.Trim()) {
                       $txtFiles = $txtFileList -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
                       foreach ($txtFile in $txtFiles) {
                           if ([string]::IsNullOrWhiteSpace($txtFile)) { continue }
                           $fileName = Split-Path $txtFile -Leaf
                           $localFilePath = "$localReportsDir\$fileName"
                           if (-not (Test-Path $localFilePath)) {
                               Write-Host "Copying: $txtFile -> $localFilePath"
                               pscp -pw $sshPassword "$($sshUser)@$($sshServer):$txtFile" "$localFilePath" 2>$null
                           } else {
                               Write-Host "Skipping: $fileName (already exists)" -ForegroundColor Yellow
                           }
                       }
                   }
            
                   # Copy all .html files (skip if already exist)
                   $htmlFiles = @"
find "$vivadoPath" -name "*.html" -type f
"@
                   $htmlFiles | Out-File -FilePath "find_html_files.sh" -Encoding ASCII
                   $htmlFileList = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m find_html_files.sh
                   Remove-Item find_html_files.sh
                   
                   if ($htmlFileList -and $htmlFileList.Trim()) {
                       $htmlFiles = $htmlFileList -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
                       foreach ($htmlFile in $htmlFiles) {
                           if ([string]::IsNullOrWhiteSpace($htmlFile)) { continue }
                           $fileName = Split-Path $htmlFile -Leaf
                           $localFilePath = "$localReportsDir\$fileName"
                           if (-not (Test-Path $localFilePath)) {
                               Write-Host "Copying: $htmlFile -> $localFilePath"
                               pscp -pw $sshPassword "$($sshUser)@$($sshServer):$htmlFile" "$localFilePath" 2>$null
                           } else {
                               Write-Host "Skipping: $fileName (already exists)" -ForegroundColor Yellow
                           }
                       }
                   }
            
                   # Copy all .xml files (skip if already exist)
                   $xmlFiles = @"
find "$vivadoPath" -name "*.xml" -type f
"@
                   $xmlFiles | Out-File -FilePath "find_xml_files.sh" -Encoding ASCII
                   $xmlFileList = plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -m find_xml_files.sh
                   Remove-Item find_xml_files.sh
                   
                   if ($xmlFileList -and $xmlFileList.Trim()) {
                       $xmlFiles = $xmlFileList -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
                       foreach ($xmlFile in $xmlFiles) {
                           if ([string]::IsNullOrWhiteSpace($xmlFile)) { continue }
                           $fileName = Split-Path $xmlFile -Leaf
                           $localFilePath = "$localReportsDir\$fileName"
                           if (-not (Test-Path $localFilePath)) {
                               Write-Host "Copying: $xmlFile -> $localFilePath"
                               pscp -pw $sshPassword "$($sshUser)@$($sshServer):$xmlFile" "$localFilePath" 2>$null
                           } else {
                               Write-Host "Skipping: $fileName (already exists)" -ForegroundColor Yellow
                           }
                       }
                   }
            
            # Also create the expected directory structure for extract_metrics.ps1
            $expectedLocalDir = ".\reports\gemm_${script:gemmSize}x${script:gemmSize}x${script:gemmSize}\x1"
            if (-not (Test-Path $expectedLocalDir)) {
                New-Item -ItemType Directory -Path $expectedLocalDir -Force | Out-Null
                Write-Host "Created expected directory structure: $expectedLocalDir"
            }
            
            # Copy Vivado reports to the expected location
            Copy-Item "$localReportsDir\*.txt" "$expectedLocalDir\" -Force -ErrorAction SilentlyContinue
            Copy-Item "$localReportsDir\*.html" "$expectedLocalDir\" -Force -ErrorAction SilentlyContinue
            Copy-Item "$localReportsDir\*.xml" "$expectedLocalDir\" -Force -ErrorAction SilentlyContinue
            
            Write-Host "✅ Vivado reports copied to expected location: $expectedLocalDir"
            break
        } elseif ($result -match "PATH_NOT_FOUND") {
            Write-Host "❌ Path not found: $vivadoPath"
        } else {
            Write-Host "❌ No Vivado reports found in: $vivadoPath"
        }
    }
    
    if (-not $vivadoReportsFound) {
        Write-Host "⚠️  No Vivado reports found in any of the searched locations"
        Write-Host "   Searched locations:"
        foreach ($path in $possibleVivadoPaths) {
            Write-Host "   - $path"
        }
    }

    # Move build_log.txt to logs folder if it exists in root directory
    if (Test-Path ".\build_log.txt") {
        Write-Host "Moving build_log.txt to logs folder..."
        Move-Item ".\build_log.txt" "$localLogDir\build_log.txt" -Force
        Write-Host "✓ build_log.txt moved to logs folder"
        Log-Message "build_log.txt moved to logs folder"
    }
    
    Write-Host "Sync complete. Files have been copied to local directories."
    Write-Host "📊 Reports available in: $localReportsDir"
    Write-Host "   - Vitis build reports"
    Write-Host "   - HLS reports" 
    Write-Host "   - AI Engine reports and analysis files"
    Write-Host "   - Vivado utilization/timing/power reports (if generated)"
    Write-Host "📋 Logs available in: $localLogDir"
    Write-Host "   - Build output logs (build_log.txt)"
    Write-Host "   - AI Engine compilation logs (graph.aiecompile_summary, aiecompiler.log, etc.)"
    Write-Host "   - AI Engine analysis files (active_cores.json, AddressSpace.txt, etc.)"
    Write-Host "   - AI Engine simulation logs (if simulation was run)"
    Write-Host "   - HLS compilation logs"
    Log-Message "Sync complete. Files have been copied to local directories."
}

# Function to sync aie_control_xrt.cpp from remote to local
function Sync-AieControlFromRemote {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing aie_control_xrt.cpp from remote to local..."
    Log-Message "Syncing aie_control_xrt.cpp from remote to local..."
    
    # Define remote and local paths
    $remoteAieControlPath = "$($sshUser)@$($sshServer):$remotePath/build/gemm_${script:gemmSize}x${script:gemmSize}x${script:gemmSize}/x1/$($script:TARGET)/Work/ps/c_rts/aie_control_xrt.cpp"
    $localAieControlPath = ".\design\aie_src\aie_control_xrt.cpp"
    
    # Create local directory if it doesn't exist
    $localDir = Split-Path -Parent $localAieControlPath
    if (-not (Test-Path $localDir)) {
        New-Item -ItemType Directory -Path $localDir -Force | Out-Null
    }
    
    Write-Host "Copying from: $remoteAieControlPath"
    Write-Host "To: $localAieControlPath"
    
    try {
        pscp -pw $sshPassword "$remoteAieControlPath" "$localAieControlPath"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Successfully copied aie_control_xrt.cpp"
            Log-Message "Successfully copied aie_control_xrt.cpp"
        } else {
            Write-Host "Failed to copy aie_control_xrt.cpp"
            Log-Message "Failed to copy aie_control_xrt.cpp"
        }
    } catch {
        Write-Host "Error copying aie_control_xrt.cpp: $_"
        Log-Message "Error copying aie_control_xrt.cpp: $_"
    }
}

# Function to sync platform folder from local to remote
function Sync-PlatformToRemote {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing platform folder from local to remote..."
    Log-Message "Syncing platform folder from local to remote..."
    
    # Define local and remote paths
    $localPlatformPath = ".\platform_edge_hwemu"
    $remotePlatformPath = "$($sshUser)@$($sshServer):$remotePath/platform_edge_hwemu"
    
    # Check if local platform folder exists
    if (-not (Test-Path $localPlatformPath)) {
        Write-Host "Error: Local platform folder not found at $localPlatformPath"
        Log-Message "Error: Local platform folder not found at $localPlatformPath"
        return
    }
    
    Write-Host "Copying from: $localPlatformPath"
    Write-Host "To: $remotePlatformPath"
    
    try {
        # Create remote directory first
        $createDirCmd = "mkdir -p $remotePath/platform_edge_hwemu"
        plink -ssh $sshUser@$sshServer -pw $sshPassword -batch $createDirCmd
        
        # Sync the entire platform folder recursively
        pscp -pw $sshPassword -r "$localPlatformPath\*" "$remotePlatformPath/"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✓ Platform folder synced successfully"
            Log-Message "Platform folder synced successfully"
        } else {
            Write-Host "✗ Failed to sync platform folder"
            Log-Message "Failed to sync platform folder"
        }
    } catch {
        Write-Host "Error copying platform folder: $_"
        Log-Message "Error copying platform folder: $_"
    }
}

# Function to sync platform folder from remote to local
function Sync-PlatformFromRemote {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing platform folder from remote to local..."
    Log-Message "Syncing platform folder from remote to local..."
    
    # Define local and remote paths
    $localPlatformPath = ".\platform_edge_hwemu"
    $remotePlatformPath = "$($sshUser)@$($sshServer):$remotePath/platform_edge_hwemu"
    
    # Create local directory if it doesn't exist
    if (-not (Test-Path $localPlatformPath)) {
        New-Item -ItemType Directory -Path $localPlatformPath -Force | Out-Null
    }
    
    Write-Host "Copying from: $remotePlatformPath"
    Write-Host "To: $localPlatformPath"
    
    try {
        # Sync the entire platform folder recursively from remote to local
        pscp -pw $sshPassword -r "$remotePlatformPath/*" "$localPlatformPath\"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✓ Platform folder synced from remote successfully"
            Log-Message "Platform folder synced from remote successfully"
        } else {
            Write-Host "✗ Failed to sync platform folder from remote"
            Log-Message "Failed to sync platform folder from remote"
        }
    } catch {
        Write-Host "Error copying platform folder from remote: $_"
        Log-Message "Error copying platform folder from remote: $_"
    }
}

# Function to modify rootfs with ML packages and sync platform
function Modify-RootfsAndSyncPlatform {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Modifying rootfs with ML packages and syncing platform..."
    Log-Message "Modifying rootfs with ML packages and syncing platform..."
    
    # First sync the modification script to remote
    $localScriptPath = ".\modify_rootfs_simple.sh"
    $remoteScriptPath = "$($sshUser)@$($sshServer):$($remotePath)/modify_rootfs_simple.sh"
    
    if (Test-Path $localScriptPath) {
        Write-Host "Syncing rootfs modification script to remote..."
        pscp -pw $sshPassword "$localScriptPath" "$remoteScriptPath"
        
        # Make script executable and run it
        $runModification = @"
cd $remotePath
chmod +x modify_rootfs_simple.sh
echo "Running rootfs modification with ML packages..."
sudo ./modify_rootfs_simple.sh
echo "Rootfs modification completed"
"@
        
        $runModification | Out-File -FilePath "run_rootfs_modification.sh" -Encoding ASCII
        plink -ssh $sshUser@$sshServer -pw $sshPassword -batch -t -m run_rootfs_modification.sh
        Remove-Item run_rootfs_modification.sh
        
        Write-Host "✓ Rootfs modification completed on remote server"
        Log-Message "Rootfs modification completed on remote server"
        
        # Now sync the updated platform back to local
        Write-Host "Syncing updated platform back to local..."
        Sync-PlatformFromRemote
        
    } else {
        Write-Host "Error: modify_rootfs_simple.sh not found locally"
        Log-Message "Error: modify_rootfs_simple.sh not found locally"
    }
}

# Function to sync aie_control_xrt.cpp from local to remote
function Sync-AieControlToRemote {
    if (-not (Test-SSHConnection)) { return }
    
    Write-Host "Syncing aie_control_xrt.cpp from local to remote..."
    Log-Message "Syncing aie_control_xrt.cpp from local to remote..."
    
    # Define local and remote paths
    $localAieControlPath = ".\design\aie_src\aie_control_xrt.cpp"
    $remoteAieControlPath = "$($sshUser)@$($sshServer):$remotePath/build/gemm_${script:gemmSize}x${script:gemmSize}x${script:gemmSize}/x1/$($script:TARGET)/Work/ps/c_rts/aie_control_xrt.cpp"
    
    # Check if local file exists
    if (-not (Test-Path $localAieControlPath)) {
        Write-Host "Error: Local file not found at $localAieControlPath"
        Log-Message "Error: Local file not found at $localAieControlPath"
        return
    }
    
    Write-Host "Copying from: $localAieControlPath"
    Write-Host "To: $remoteAieControlPath"
    
    try {
        pscp -pw $sshPassword "$localAieControlPath" "$remoteAieControlPath"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Successfully copied aie_control_xrt.cpp to remote"
            Log-Message "Successfully copied aie_control_xrt.cpp to remote"
        } else {
            Write-Host "Failed to copy aie_control_xrt.cpp to remote"
            Log-Message "Failed to copy aie_control_xrt.cpp to remote"
        }
    } catch {
        Write-Host "Error copying aie_control_xrt.cpp to remote: $_"
        Log-Message "Error copying aie_control_xrt.cpp to remote: $_"
    }
}

# Function to show current configuration status
function Show-ConfigStatus {
    Write-Host "🔍 CURRENT CONFIGURATION STATUS"
    Write-Host "=" * 50
    Write-Host ""
    
    if (Test-Path $script:configPath) {
        try {
            $config = Get-Content $script:configPath -Raw | ConvertFrom-Json
            
            Write-Host "📋 Configuration File: $script:configPath"
            Write-Host "  GEMM_SIZE: $($config.GEMM_SIZE)"
            Write-Host "  DIM: $($config.DIM)"
            Write-Host "  DATA_TYPE: $($config.DATA_TYPE)"
            Write-Host "  TARGET: $($config.TARGET)"
            Write-Host "  SPLIT: $($config.SPLIT)"
            Write-Host "  CASC_LN: $($config.CASC_LN)"
            Write-Host "  ENABLE_ML_BENCHMARKS: $($config.ENABLE_ML_BENCHMARKS)"
            if ($null -ne $config.WRD_LN) { Write-Host "  WRD_LN: $($config.WRD_LN)" }
            if ($null -ne $config.SUB_TILE_M -and $null -ne $config.SUB_TILE_K -and $null -ne $config.SUB_TILE_N) {
                Write-Host ("  Sub-tile (M,K,N): {0},{1},{2}" -f $config.SUB_TILE_M, $config.SUB_TILE_K, $config.SUB_TILE_N)
            }
            # GRAPH_ITER_CNT per C++/PL definitions:
            # If ITER_CNT == -1, then GRAPH_ITER_CNT = ITER_CNT; else derived as (GEMM_SIZE*GEMM_SIZE)/(DIM*DIM)/SPLIT
            $graphIterCnt = $null
            if ($null -ne $config.ITER_CNT -and [int]$config.ITER_CNT -eq -1) {
                $graphIterCnt = -1
            } else {
                if ($null -ne $config.GRAPH_ITER_CNT) {
                    $graphIterCnt = [int]$config.GRAPH_ITER_CNT
                } elseif ($null -ne $config.GEMM_SIZE -and $null -ne $config.DIM -and $null -ne $config.SPLIT) {
                    $graphIterCnt = [int](([int]$config.GEMM_SIZE * [int]$config.GEMM_SIZE) / ([int]$config.DIM * [int]$config.DIM) / [Math]::Max(1,[int]$config.SPLIT))
                }
            }
            if ($null -ne $graphIterCnt) { Write-Host "  GRAPH_ITER_CNT: $graphIterCnt" }
            Write-Host ""
            
            # Data Type Analysis
            Write-Host "🔢 DATA TYPE ANALYSIS:"
            $DATA_TYPE = $config.DATA_TYPE
            $actual_aie_type = $DATA_TYPE  # AI Engine supports native data types
            $bytes_per_element = switch ($DATA_TYPE) {
                "int4" { 0.5 }
                "int8" { 1 }
                "int16" { 2 }
                "int32" { 4 }
                "float" { 4 }
                "bfloat16" { 2 }
                default { 2 }
            }
            $elements_per_128bit = switch ($DATA_TYPE) {
                "int4" { 32 }
                "int8" { 16 }
                "int16" { 8 }
                "int32" { 4 }
                "float" { 4 }
                "bfloat16" { 8 }
                default { 8 }
            }
            
            Write-Host "  Config Data Type: $DATA_TYPE"
            Write-Host "  Actual AIE Type: $actual_aie_type"
            Write-Host "  Bits per element: $($bytes_per_element * 8)"
            Write-Host "  Elements per 128-bit word: $elements_per_128bit"
            if ($null -ne $config.WRD_LN) { Write-Host "  WRD_LN (from config): $($config.WRD_LN)" }
            Write-Host "  Memory per element: $bytes_per_element bytes"
            Write-Host "  Note: AI Engine ML Architecture supports native data types"
            Write-Host ""
            
            # Memory Analysis
            Write-Host "💾 MEMORY ANALYSIS:"
            $GEMM_SIZE = $config.GEMM_SIZE
            $DIM = $config.DIM
            $memory_per_core = $DIM * $DIM * $bytes_per_element
            
            Write-Host "  Memory per core: $($memory_per_core.ToString('N0')) bytes"
            Write-Host "  AIE memory limit: 32,768 bytes"
            if ($memory_per_core -le 32768) {
                Write-Host "  Status: ✅ FITS (Memory usage: $([math]::Round(($memory_per_core/32768)*100, 1))%)"
            } else {
                Write-Host "  Status: ❌ EXCEEDS by $($memory_per_core - 32768) bytes"
            }
            Write-Host ""
            
            # Makefile Integration Check
            Write-Host "🔧 MAKEFILE INTEGRATION:"
            Write-Host "  String DATA_TYPE: '$DATA_TYPE'"
            $numeric_data_type = switch ($DATA_TYPE) {
                "int4" { 4 }
                "int8" { 8 }
                "int16" { 16 }
                "int32" { 32 }
                "float" { 33 }
                "bfloat16" { 17 }
                default { 16 }
            }
            Write-Host "  Numeric DATA_TYPE: $numeric_data_type"
            Write-Host "  Makefile will pass: -DDATA_TYPE=$numeric_data_type"
            Write-Host "  C++ preprocessor will use: DATA_TYPE=$numeric_data_type"
            Write-Host ""
            
            # Build Information
            Write-Host "🏗️ BUILD INFORMATION:"
            Write-Host "  Build path: $script:hwEmuPath"
            Write-Host "  Kernel: dma_hls.cpp (standard architecture)"
            Write-Host "  AIE Graph: matrix_mult_graph with dynamic data types"
            if ($null -ne $config.SUB_TILE_M -and $null -ne $config.SUB_TILE_K -and $null -ne $config.SUB_TILE_N) {
                Write-Host ("  Sub-tile size (M×K×N): {0}×{1}×{2}" -f $config.SUB_TILE_M, $config.SUB_TILE_K, $config.SUB_TILE_N)
            }
            
        } catch {
            Write-Host "Error reading config file: $_"
        }
    } else {
        Write-Host "Config file not found at: $script:configPath"
    }
    
    Write-Host ""
    Write-Host "=" * 50
    Read-Host "Press Enter to return to main menu"
}

# Function to show streaming architecture status
function Show-StreamingStatus {
    Write-Host "🔍 STREAMING ARCHITECTURE STATUS"
    Write-Host "=" * 40
    Write-Host ""
    
    if (Test-Path $script:configPath) {
        try {
            $config = Get-Content $script:configPath -Raw | ConvertFrom-Json
            
            Write-Host "Current Configuration:"
            Write-Host "  GEMM_SIZE: $($config.GEMM_SIZE)"
            Write-Host "  DIM: $($config.DIM)"
            Write-Host "  DATA_TYPE: $($config.DATA_TYPE)"
            Write-Host "  TARGET: $($config.TARGET)"
            Write-Host ""
            
            Write-Host "Streaming Architecture:"
            $streamingEnabled = $config.STREAMING_ENABLED
            if ($streamingEnabled) {
                Write-Host "  ✅ STREAMING_ENABLED: $streamingEnabled"
                Write-Host "  📦 STREAMING_TILE_SIZE: $($config.STREAMING_TILE_SIZE)"
                Write-Host "  🔢 TILES_PER_DIM: $($config.TILES_PER_DIM)"
                Write-Host "  📊 TOTAL_TILES: $($config.TOTAL_TILES)"
                Write-Host "  🎯 MEMORY_OPTIMIZATION: $($config.MEMORY_OPTIMIZATION)"
                Write-Host ""
                Write-Host "🚀 Build will use: dma_hls_streaming.cpp"
                Write-Host "💾 Memory usage: 50% reduction (16KB per tile)"
                Write-Host "📈 Scalability: Up to 512×512 matrices"
            } else {
                Write-Host "  ❌ STREAMING_ENABLED: $streamingEnabled"
                Write-Host ""
                Write-Host "📦 Build will use: dma_hls.cpp"
                Write-Host "💾 Memory usage: Standard (32KB per core)"
            }
            
            Write-Host ""
            Write-Host "Memory Analysis:"
            $GEMM_SIZE = $config.GEMM_SIZE
            $DIM = $config.DIM
            $DATA_TYPE = $config.DATA_TYPE
            $bytes_per_element = if ($DATA_TYPE -eq "int16") { 2 } else { 1 }
            $memory_per_core = $DIM * $DIM * $bytes_per_element
            
            Write-Host "  Memory per core: $($memory_per_core.ToString('N0')) bytes"
            Write-Host "  AIE limit: 32,768 bytes"
            if ($memory_per_core -le 32768) {
                Write-Host "  Status: ✅ FITS"
            } else {
                Write-Host "  Status: ❌ EXCEEDS"
            }
            
        } catch {
            Write-Host "Error reading config file: $_"
        }
    } else {
        Write-Host "Config file not found at: $script:configPath"
    }
    
    Write-Host ""
    Write-Host "=" * 40
    Read-Host "Press Enter to return to main menu"
}

# Function to compare generated outputs with golden reference files
function Compare-Outputs {
    Write-Host "Comparing generated outputs with golden reference files..."
    Log-Message "Comparing generated outputs with golden reference files..."
    
    # Read GEMM_SIZE from config file to get the correct size
    $configPath = ".\design\design_configs\config.json"
    $configGemmSize = $script:gemmSize  # Default fallback
    
    if (Test-Path $configPath) {
        try {
            $config = Get-Content $configPath -Raw | ConvertFrom-Json
            if ($null -ne $config.GEMM_SIZE) {
                $configGemmSize = [int]$config.GEMM_SIZE
                Write-Host "Using GEMM_SIZE from config: $configGemmSize"
            }
        } catch {
            Write-Host "Warning: Could not read config file, using default GEMM_SIZE: $configGemmSize"
        }
    }
    
    # Define paths based on config GEMM size
    $generatedOutputPath = ".\design\aie_src\aiesim_data\c.txt"
    $goldenOutputPath = ".\design\aie_src\aiesim_data\gemm_${configGemmSize}x${configGemmSize}x${configGemmSize}_ioFiles\c_golden.txt"
    $compareScriptPath = ".\design\aie_src\aiesim_data\compare_outputs.py"
    
    Write-Host "Generated output file: $generatedOutputPath"
    Write-Host "Golden reference file: $goldenOutputPath"
    Write-Host "Compare script: $compareScriptPath"
    
    # Check if files exist
    if (-not (Test-Path $generatedOutputPath)) {
        Write-Host "Error: Generated output file not found at $generatedOutputPath"
        Write-Host "Please run the simulation first to generate the output file."
        Log-Message "Error: Generated output file not found at $generatedOutputPath"
        return
    }
    
    if (-not (Test-Path $goldenOutputPath)) {
        Write-Host "Error: Golden reference file not found at $goldenOutputPath"
        Write-Host "Please generate the golden reference files first using plioGen.py."
        Log-Message "Error: Golden reference file not found at $goldenOutputPath"
        return
    }
    
    if (-not (Test-Path $compareScriptPath)) {
        Write-Host "Error: Compare script not found at $compareScriptPath"
        Log-Message "Error: Compare script not found at $compareScriptPath"
        return
    }
    
    # Run the comparison script
    Write-Host "Running comparison script..."
    Write-Host "----------------------------------------"
    
    try {
        # Change to the script directory and run the comparison
        $originalLocation = Get-Location
        Set-Location ".\design\aie_src\aiesim_data"
        
        # Run the Python comparison script
        python compare_outputs.py > compare_outputs.log 2>&1
        
        $exitCode = $LASTEXITCODE
        Set-Location $originalLocation
        
        if ($exitCode -eq 0) {
            Write-Host "----------------------------------------"
            Write-Host "✅ Comparison completed successfully - All outputs match!"
            Log-Message "Comparison completed successfully - All outputs match!"
        } else {
            Write-Host "----------------------------------------"
            Write-Host "❌ Comparison failed - Check the differences above"
            Log-Message "Comparison failed - Check the differences above"
        }
        
    } catch {
        Write-Host "Error running comparison script: $_"
        Log-Message "Error running comparison script: $_"
        Set-Location $originalLocation
    }
}

# Generate golden IO files (plioGen.py) and then compare outputs
function Generate-GoldenAndCompare {
    Write-Host "Generating golden IO files with plioGen.py..."
    Log-Message "Generating golden IO files with plioGen.py..."
    $orig = Get-Location
    try {
        Set-Location ".\design\aie_src\aiesim_data"
        python plioGen.py > plioGen.log 2>&1
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            Write-Host "plioGen.py failed with exit code $exitCode"
            Log-Message "plioGen.py failed with exit code $exitCode"
            return
        }
        Write-Host "Golden IO generation completed. Now comparing outputs..."
        Log-Message "Golden IO generation completed. Now comparing outputs..."
    } catch {
        Write-Host "Error running plioGen.py: $_"
        Log-Message "Error running plioGen.py: $_"
        return
    } finally {
        Set-Location $orig
    }
    Compare-Outputs
}

function Extract-Metrics {
    Write-Host "🔍 Extracting metrics from reports..." -ForegroundColor Green
    Log-Message "Extracting metrics from reports..."
    
    # Check if reports directory exists
    $reportsDir = "reports\link\imp"
    if (-not (Test-Path $reportsDir)) {
        Write-Host "❌ Reports directory not found: $reportsDir" -ForegroundColor Red
        Write-Host "Please run option 5.5 (Sync comprehensive logs and reports) first to download reports from remote server." -ForegroundColor Yellow
        Log-Message "Reports directory not found: $reportsDir"
        return
    }
    
    Write-Host "📊 Extracting essential metrics..." -ForegroundColor Cyan
    
    if (Test-Path "scripts\extract_metrics.ps1") {
        & "scripts\extract_metrics.ps1"
        Write-Host "✅ Metrics extraction completed!" -ForegroundColor Green
        Write-Host "📄 Check metrics_summary.txt for results" -ForegroundColor Cyan
    } else {
        Write-Host "❌ extract_metrics.ps1 not found in scripts folder" -ForegroundColor Red
    }
    
    Log-Message "Metrics extraction completed"
}

# Main menu
while ($true) {
    Write-Host "Remote Workflow Menu:"
    Write-Host "1. Sync from local to remote:"
    Write-Host "   1.1 Design folder (Most frequent)"
    Write-Host "   1.2 Makefile"
    Write-Host "   1.3 Mahdieh_env_setup.sh"
    Write-Host "   1.4 aie_control_xrt.cpp"
    Write-Host "   1.5 Platform folder (platform_edge_hwemu)"
    Write-Host "   1.6 Modify rootfs with ML packages and sync platform (Recommended)"
    Write-Host "2. Clean build (local + remote)"
    Write-Host "3. Run build on remote (GEMM_SIZE=$($script:gemmSize), DIM=$($script:DIM), DATA_TYPE=$($script:dataType), TARGET=$($script:TARGET), ENABLE_ML_BENCHMARKS=$($script:ENABLE_ML_BENCHMARKS))"
    Write-Host "4. Run hardware emulator (with real-time output)"
    Write-Host "5. Sync from remote to local:"
    Write-Host "   5.1 Design folder (Most frequent)"
    Write-Host "   5.2 Makefile"
    Write-Host "   5.3 Mahdieh_env_setup.sh"
    Write-Host "   5.4 Sync c.txt and log.txt from server"
    Write-Host "   5.5 Sync comprehensive logs and reports (includes Vivado report generation)"
    Write-Host "   5.6 Sync sd_card.img from remote to local"
    Write-Host "   5.7 Sync aie_control_xrt.cpp from server"
    Write-Host "   5.8 Platform folder (platform_edge_hwemu) from remote"
    Write-Host "   5.9 Sync Vivado reports from remote to local"
    Write-Host "6. Analysis and Tools:"
    Write-Host "   6.1 Guided: select DIM from valid list and build"
    Write-Host "   6.2 Show current configuration status (including DATA_TYPE analysis)"
    Write-Host "   6.3 List XCLBIN contents"
    Write-Host "7. Generate golden IO files and compare outputs"
    Write-Host "8. Extract metrics from reports"
    Write-Host "9. Exit"
    
    $choice = Read-Host "Enter your choice (1-9)"
    
    switch ($choice) {
        "1.1" { Sync-ToRemote }
        "1.2" { Sync-MakefileToRemote }
        "1.3" { Sync-EnvSetupToRemote }
        "1.4" { Sync-AieControlToRemote }
        "1.5" { Sync-PlatformToRemote }
        "1.6" { Modify-RootfsAndSyncPlatform }
        "2" { 
            Clean-LocalFiles
            Clean-RemoteBuild 
        }
        "3" { Run-RemoteBuild }
        # moved into 6 submenu
        "4" { Run-HardwareEmulator }
        "5.1" { Sync-FromRemote }
        "5.2" { Sync-MakefileFromRemote }
        "5.3" { Sync-EnvSetupFromRemote }
        "5.4" {
            $remotePathC = "mgrailoo@10.240.33.35:/media/josnu02/large_SDD/mgrailoo/c.txt"
            $remotePathLog = "mgrailoo@10.240.33.35:/media/josnu02/large_SDD/mgrailoo/log.txt"
            $localPath = "C:\aie_src_2025-3-28\ssh_workflow\design\aie_src\aiesim_data\"

            # Create local directory if it doesn't exist
            if (-not (Test-Path -Path $localPath)) {
                New-Item -ItemType Directory -Path $localPath
            }

            # Copy files from server to local without prompting for password
            pscp -pw $sshPassword "$remotePathC" "$localPath"
            pscp -pw $sshPassword "$remotePathLog" "$localPath"
            # pscp -pw $sshPassword "$remotePathC1" "$localPath"
            
            Write-Host "Files c.txt have been synced to $localPath"
            Log-Message "Files c.txt have been synced to $localPath"
        }
        "5.5" {
            Sync-ComprehensiveReports
        }
        "5.6" { Sync-SDCardImage }
        "5.7" { Sync-AieControlFromRemote }
        "5.8" { Sync-PlatformFromRemote }
        "5.9" { Sync-VivadoReports }
        "6.1" { Guided-SelectDim-And-Build }
        "6.2" { Show-ConfigStatus }
        "6.3" { List-XCLBINContents }
        "7" { Generate-GoldenAndCompare }
        "8" { Extract-Metrics }
        "9" { exit }
        default { Write-Host "Invalid choice. Please try again." }
    }
} 
