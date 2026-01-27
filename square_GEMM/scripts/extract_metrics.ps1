    $ReportsDir = "reports"
    $OutputFile = "metrics_summary.txt"

function Find-ReportFile {
    param($name)
    $p = Join-Path $ReportsDir $name
    if (Test-Path $p) { return $p }
    $alt = Get-ChildItem -Path $ReportsDir -Filter $name -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($alt) { return $alt.FullName }
    return $null
}

# Safe Test-Path wrapper: returns $false for null/empty paths to avoid PowerShell warnings
function SafeTestPath {
    param($p)
    if (-not $p) { return $false }
    return (Test-Path $p)
}

    $aiePowerPath = Join-Path $ReportsDir "aie_power.txt"
    if (-not (Test-Path $aiePowerPath)) {
        $altAiePower = Get-ChildItem -Path $ReportsDir -Filter "aie_power.txt" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($altAiePower) { $aiePowerPath = $altAiePower.FullName }
    }
    $memUtilPath = Join-Path $ReportsDir "memory_utilization.txt"
    $aiePowerTxt = if (Test-Path $aiePowerPath) { Get-Content $aiePowerPath -Raw } else { "" }
    # Aggregate Memory Power (PL BRAM + NoC-DDRMC + XRAM)
    $memoryPowerSum = $null
try {
    # Extract from On-Chip Components section directly for more accuracy
    $bramMatch = [regex]::Match($aiePowerTxt, "Block RAM\s*\|\s*([\d\.]+)")
    $nocDdrmcMatch = [regex]::Match($aiePowerTxt, "NoC-DDRMC\s*\|\s*([\d\.]+)")
    $xramMatch = [regex]::Match($aiePowerTxt, "XRAM\s*\|\s*([\d\.]+)")
    $p1 = if ($bramMatch.Success) { [double]$bramMatch.Groups[1].Value } else { $null }
    $p2 = if ($nocDdrmcMatch.Success) { [double]$nocDdrmcMatch.Groups[1].Value } else { $null }
    $p3 = if ($xramMatch.Success) { [double]$xramMatch.Groups[1].Value } else { $null }
    if ($p1 -ne $null -and $p2 -ne $null -and $p3 -ne $null) {
        $sum = $p1 + $p2 + $p3
        if ($sum -gt 0) { $memoryPowerSum = [math]::Round($sum, 3) }
    }
    # Fallback to previous variables if direct extraction fails
    if ($memoryPowerSum -eq $null -or $memoryPowerSum -eq 0) {
        $p1 = if ($bramPowerW) { [double]$bramPowerW } else { $null }
    $p2 = if ($nocDdrmcPowerW) { [double]$nocDdrmcPowerW } else { $null }
    $p3 = if ($xramPowerW) { [double]$xramPowerW } else { $null }
    if ($p1 -ne $null -and $p2 -ne $null -and $p3 -ne $null) {
            $sum = $p1 + $p2 + $p3
            if ($sum -gt 0) { $memoryPowerSum = [math]::Round($sum, 3) }
        }
    }
    if ($memoryPowerSum -eq $null -or $memoryPowerSum -eq 0) {
        $memoryPowerSum = 'N/A'
    }
} catch {
    $memoryPowerSum = 'N/A'
}
if ($memoryPowerSum -eq $null -or $memoryPowerSum -eq 0) { $memoryPowerSum = 'N/A' }
$memoryPowerDisplay = if ($memoryPowerSum -ne 'N/A') { "$memoryPowerSum W" } else { 'N/A' }
Write-Host "  Power: $totalPower W total, $aiePower W AI Engine, Memory: $memoryPowerDisplay" -ForegroundColor White
$compBlock += "Memory Power (W) [BRAM + NoC-DDRMC + XRAM]: $memoryPowerDisplay"
# ============================================================================
# AI Engine GEMM Metrics Extraction (Essential + Comprehensive)
# ============================================================================
# This script extracts essential and comprehensive metrics:
# - Resource utilization (LUTs, FFs, BRAM, URAM, DSP)
# - Timing performance (WNS/TNS/WHS/THS, clocks, constraints)
# - Power consumption (total, AIE; thermal if available)
# - AI Engine (tiles, kernels, streams, utilization)
# - PS metrics (APU/RPU, DDR/MIG, peripherals)
# - Memory metrics (BRAM/URAM distribution, bandwidth)
# - Interfaces (AXI/AXIS/PLIO, GT)
# - NoC metrics (instances, performance if available)
# - DRC (violations summary)
# vitis_analyzer xrt.run_summary
# Usage: .\scripts\extract_metrics.ps1
# ============================================================================

$OutputFile = "metrics_summary.txt"

Write-Host "=== AI Engine GEMM Metrics (Essential + Comprehensive) ===" -ForegroundColor Green

# Detect runtime profiling artifacts copied into reports/
$xrtRunSummaryPath = Find-ReportFile "xrt.run_summary"
$runtimeCsvs = Get-ChildItem -Path $ReportsDir -Filter "*profile*.csv" -Recurse -File -ErrorAction SilentlyContinue
$timelineCsvs = Get-ChildItem -Path $ReportsDir -Filter "*timeline*.csv" -Recurse -File -ErrorAction SilentlyContinue
$traceCsvs = Get-ChildItem -Path $ReportsDir -Filter "*trace*.csv" -Recurse -File -ErrorAction SilentlyContinue

# Prepare a brief kernel duration summary from runtime CSVs (if any)
$kernelSummary = @()
try {
    $kernelsCsv = Get-ChildItem -Path $ReportsDir -Filter "*profile_kernels*.csv" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($kernelsCsv) {
        $rows = Import-Csv -Path $kernelsCsv.FullName
        $parsed = @()
        foreach ($r in $rows) {
            $name = $r.Kernel; if (-not $name) { $name = $r."Compute Unit" }
            $dur = $r.DurationMs; if (-not $dur) { $dur = $r.Duration_ms }
            if (-not $dur) { $dur = $r.Duration }
            $calls = $r.Calls
            if ($name -and $dur -and [double]::TryParse($dur, [ref]([double]$null))) {
                $parsed += [pscustomobject]@{ Name=$name; DurationMs=[double]$dur; Calls=$calls }
            }
        }
        if ($parsed.Count -gt 0) {
            $parsed | Sort-Object DurationMs -Descending | Select-Object -First 5 | ForEach-Object {
                $kernelSummary += ("  - {0}: {1} ms over {2} calls" -f $_.Name,$_.DurationMs,$_.Calls)
            }
        }
    }
} catch {}

# 1. RESOURCE UTILIZATION (Most Important)
Write-Host "`n📊 RESOURCE UTILIZATION" -ForegroundColor Yellow

$utilPath = Find-ReportFile "utilization_report.txt"
$utilContent = if ($utilPath) { Get-Content $utilPath -Raw -ErrorAction SilentlyContinue } else { "" }
$resourcesPath = Find-ReportFile "resources_by_type.txt"
$resourcesContent = if ($resourcesPath) { Get-Content $resourcesPath -Raw -ErrorAction SilentlyContinue } else { "" }
# Note: resources_by_clock.txt is generated but not parsed here to keep output concise

# Extract LUTs
$lutMatch = [regex]::Match($utilContent, "CLB LUTs\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+([\d.]+)")
$lutUsed = if ($lutMatch.Success) { $lutMatch.Groups[1].Value } else { "N/A" }
$lutTotal = if ($lutMatch.Success) { $lutMatch.Groups[4].Value } else { "N/A" }
$lutPercent = if ($lutMatch.Success) { $lutMatch.Groups[5].Value } else { "N/A" }

# Extract BRAMs
$bramMatch = [regex]::Match($utilContent, "Block RAM Tile\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+([\d.]+)")
$bramUsed = if ($bramMatch.Success) { $bramMatch.Groups[1].Value } else { "N/A" }
$bramTotal = if ($bramMatch.Success) { $bramMatch.Groups[4].Value } else { "N/A" }
$bramPercent = if ($bramMatch.Success) { $bramMatch.Groups[5].Value } else { "N/A" }

# Extract DSPs from detailed resources
$dspMatch = [regex]::Match($resourcesContent, "DSPs\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+([\d.]+)")
$dspUsed = if ($dspMatch.Success) { $dspMatch.Groups[1].Value } else { "0" }
$dspTotal = if ($dspMatch.Success) { $dspMatch.Groups[4].Value } else { "0" }
$dspPercent = if ($dspMatch.Success) { $dspMatch.Groups[5].Value } else { "0.00" }

# Extract URAM
$uramMatch = [regex]::Match($resourcesContent, "URAM\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+([\d.]+)")
$uramUsed = if ($uramMatch.Success) { $uramMatch.Groups[1].Value } else { "0" }
$uramTotal = if ($uramMatch.Success) { $uramMatch.Groups[4].Value } else { "0" }
$uramPercent = if ($uramMatch.Success) { $uramMatch.Groups[5].Value } else { "0.00" }

# Extract CLB Registers
$ffMatch = [regex]::Match($resourcesContent, "CLB Registers\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+([\d.]+)")
$ffUsed = if ($ffMatch.Success) { $ffMatch.Groups[1].Value } else { "0" }
$ffTotal = if ($ffMatch.Success) { $ffMatch.Groups[4].Value } else { "0" }
$ffPercent = if ($ffMatch.Success) { $ffMatch.Groups[5].Value } else { "0.00" }

Write-Host "  LUTs: $lutUsed / $lutTotal ($lutPercent%)" -ForegroundColor White
Write-Host "  BRAMs: $bramUsed / $bramTotal ($bramPercent%)" -ForegroundColor White
Write-Host "  DSPs: $dspUsed / $dspTotal ($dspPercent%)" -ForegroundColor White
Write-Host "  URAM: $uramUsed / $uramTotal ($uramPercent%)" -ForegroundColor White
Write-Host "  CLB Registers: $ffUsed / $ffTotal ($ffPercent%)" -ForegroundColor White

# 2. TIMING PERFORMANCE (Most Important)
Write-Host "`n⏱️ TIMING PERFORMANCE" -ForegroundColor Yellow

$timingPath = Find-ReportFile "timing_summary.txt"
$timingContent = if ($timingPath) { Get-Content $timingPath -Raw -ErrorAction SilentlyContinue } else { "" }
$clockNetworksPath = Find-ReportFile "clock_networks.txt"
$clockNetworks = if ($clockNetworksPath) { Get-Content $clockNetworksPath -Raw -ErrorAction SilentlyContinue } else { "" }
$constraintExceptionsPath = Find-ReportFile "constraints_exceptions.txt"
$constraintExceptions = if ($constraintExceptionsPath) { Get-Content $constraintExceptionsPath -Raw -ErrorAction SilentlyContinue } else { "" }
$timingMatch = [regex]::Match($timingContent, "(\d+\.\d+)\s+(\d+\.\d+)\s+(\d+)\s+(\d+)\s+(\d+\.\d+)\s+(\d+\.\d+)\s+(\d+)\s+(\d+)")

if ($timingMatch.Success) {
    $wns = $timingMatch.Groups[1].Value
    $tns = $timingMatch.Groups[2].Value
    $whs = $timingMatch.Groups[5].Value
    $ths = $timingMatch.Groups[6].Value
    
    Write-Host "  WNS (Worst Negative Slack): $wns ns" -ForegroundColor White
    Write-Host "  TNS (Total Negative Slack): $tns ns" -ForegroundColor White
    Write-Host "  WHS (Worst Hold Slack): $whs ns" -ForegroundColor White
    Write-Host "  THS (Total Hold Slack): $ths ns" -ForegroundColor White
    
    # Extract clock frequencies
    $clockFrequencies = @()
    $clockMatches = [regex]::Matches($timingContent, "(\w+)\s+\{[^}]+\}\s+(\d+\.\d+)\s+(\d+\.\d+)")
    foreach ($match in $clockMatches) {
        $clockName = $match.Groups[1].Value
        # $period available but unused for now
        $freq = $match.Groups[3].Value
        if ($freq -gt 0) {
            $clockFrequencies += "${clockName}: $freq MHz"
        }
    }
    
    if ($clockFrequencies.Count -gt 0) {
        Write-Host "`n  Clock Frequencies:" -ForegroundColor White
        foreach ($clock in $clockFrequencies) {
            Write-Host "    $clock" -ForegroundColor White
        }
    }
    
    # Timing status
    if ([double]$wns -gt 0) {
        Write-Host "`n  Status: ✅ TIMING CLOSED" -ForegroundColor Green
    } else {
        Write-Host "`n  Status: ❌ TIMING FAILED" -ForegroundColor Red
    }
} else {
    Write-Host "  ❌ Could not extract timing data" -ForegroundColor Red
}

# Constraint checks summary
if ($constraintExceptions) {
    $excLines = ($constraintExceptions -split "\r?\n").Where({ $_.Trim().Length -gt 0 }).Count
    Write-Host "  Constraint Exceptions: $excLines lines (see constraints_exceptions.txt)" -ForegroundColor White
}
if ($clockNetworks) {
    $clkNetsLines = ($clockNetworks -split "\r?\n").Where({ $_.Trim().Length -gt 0 }).Count
    Write-Host "  Clock Network Report: $clkNetsLines lines (see clock_networks.txt)" -ForegroundColor White
}

# 3. POWER CONSUMPTION (Most Important)
Write-Host "`n⚡ POWER CONSUMPTION" -ForegroundColor Yellow

# Extract power data from power report
$powerPath = Find-ReportFile "power_report.txt"
$powerContent = if ($powerPath) { Get-Content $powerPath -Raw -ErrorAction SilentlyContinue } else { "" }
$thermalPath = Find-ReportFile "thermal_analysis.txt"
$thermalContent = if ($thermalPath) { Get-Content $thermalPath -Raw -ErrorAction SilentlyContinue } else { "" }
$totalPowerMatch = [regex]::Match($powerContent, "Total On-Chip Power \(W\)\s+\|\s+([\d.]+)")
$aiePowerMatch = [regex]::Match($powerContent, "\|\s+vitis_design_i/ai_engine_0\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)\s+\|\s+(\d+)\s+\|")

if ($totalPowerMatch.Success) {
    $totalPower = $totalPowerMatch.Groups[1].Value
    Write-Host "  Total On-Chip Power: $totalPower W" -ForegroundColor White
} else {
    $totalPower = "N/A"
    Write-Host "  Total On-Chip Power: $totalPower W (Not found)" -ForegroundColor Yellow
}

if ($aiePowerMatch.Success) {
    $aiePower = $aiePowerMatch.Groups[1].Value
    $aieClock = $aiePowerMatch.Groups[2].Value
    $aieCores = $aiePowerMatch.Groups[3].Value
    Write-Host "  AI Engine Power: $aiePower W" -ForegroundColor White
    Write-Host "  AI Engine Clock: $aieClock MHz" -ForegroundColor White
    Write-Host "  AI Engine Cores: $aieCores" -ForegroundColor White
} else {
    # Fallback: Parse reports/aie_power.txt for AIE2 power and complexity.csv for cores
    $aiePower = "N/A"
    $aieCores = "N/A"
    $aiePowerPath2 = Find-ReportFile "aie_power.txt"
    $aiePowerTxtFallback = if ($aiePowerPath2) { Get-Content $aiePowerPath2 -Raw -ErrorAction SilentlyContinue } else { "" }
    if ($aiePowerTxtFallback) {
        $aie2PowerMatch2 = [regex]::Match($aiePowerTxtFallback, "\bAIE2\s*\|\s*([\d\.]+)")
        if ($aie2PowerMatch2.Success) {
            $aiePower = $aie2PowerMatch2.Groups[1].Value
            Write-Host "  AI Engine Power (fallback): $aiePower W" -ForegroundColor White
        } else {
            Write-Host "  AI Engine Power: N/A (Not found)" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  AI Engine Power: N/A (aie_power.txt missing)" -ForegroundColor Yellow
    }
    # Try cores from complexity.csv
    $complexityPathEssential = Find-ReportFile "complexity.csv"
    if ($complexityPathEssential) {
        $complexityEssential = Get-Content $complexityPathEssential -Raw -ErrorAction SilentlyContinue
        $coresMatch = [regex]::Match($complexityEssential, "^total_num_cores,([^\r\n]+)", 'Multiline')
        if ($coresMatch.Success) { $aieCores = $coresMatch.Groups[1].Value }
    }
}

# Set values for report generation
if ($aiePowerMatch.Success) {
    $aiePower = $aiePowerMatch.Groups[1].Value
    $aieCoresUsed = $aiePowerMatch.Groups[3].Value
    $aieCoresTotal = "34"  # Total AI Engine cores available on platform
    $aieUtilPercent = [math]::Round(($aieCoresUsed / $aieCoresTotal) * 100, 2)
    
    Write-Host "  AI Engine Power: $aiePower W" -ForegroundColor White
    Write-Host "  AI Engine Cores: $aieCoresUsed / $aieCoresTotal ($aieUtilPercent%)" -ForegroundColor White
    Write-Host "  Source: Vivado power report (accurate data)" -ForegroundColor Green
} else {
    # Fallback values from above parsing
    $aieCoresUsed = if ($aieCores -ne "N/A") { $aieCores } else { "N/A" }
    $aieCoresTotal = "34"
    $aieUtilPercent = if ($aieCoresUsed -ne "N/A") { [math]::Round(($aieCoresUsed / $aieCoresTotal) * 100, 2) } else { "N/A" }
}

# Aggregate Memory Power (PL BRAM + NoC-DDRMC + XRAM)
$memoryPowerSum = 'N/A'
$memoryPowerDisplay = 'N/A'
try {
    $memoryPowerPath = Join-Path $ReportsDir "aie_power.txt"
    if (-not (Test-Path $memoryPowerPath)) {
        $altMemoryPower = Get-ChildItem -Path $ReportsDir -Filter "aie_power.txt" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($altMemoryPower) { $memoryPowerPath = $altMemoryPower.FullName }
    }
    if (Test-Path $memoryPowerPath) {
        $memoryPowerText = Get-Content $memoryPowerPath -Raw
        $memoryComponents = @()
        $bramMatchMem = [regex]::Match($memoryPowerText, "Block RAM\s*\|\s*([\d\.]+)")
        if ($bramMatchMem.Success) { $memoryComponents += [double]$bramMatchMem.Groups[1].Value }
        $nocMatchMem = [regex]::Match($memoryPowerText, "NoC-DDRMC\s*\|\s*([\d\.]+)")
        if ($nocMatchMem.Success) { $memoryComponents += [double]$nocMatchMem.Groups[1].Value }
        $xramMatchMem = [regex]::Match($memoryPowerText, "XRAM\s*\|\s*([\d\.]+)")
        if ($xramMatchMem.Success) { $memoryComponents += [double]$xramMatchMem.Groups[1].Value }
        if ($memoryComponents.Count -gt 0) {
            $sumMem = ($memoryComponents | Measure-Object -Sum).Sum
            if ($sumMem -gt 0) {
                $memoryPowerSum = [math]::Round([double]$sumMem, 3)
                $memoryPowerDisplay = "$memoryPowerSum W"
            }
        }
    }
} catch {
    $memoryPowerSum = 'N/A'
    $memoryPowerDisplay = 'N/A'
}


# 4. READ PROJECT CONFIGURATION
Write-Host "`n📄 READING PROJECT CONFIGURATION" -ForegroundColor Green

# Read config.json to get DIM and DATA_TYPE
$configFile = "design\design_configs\config.json"
$dimValue = "32"
$dataType = "int16"
$gemmSize = "32"

if (Test-Path $configFile) {
    try {
        $configData = Get-Content $configFile | ConvertFrom-Json
        $dimValue = $configData.DIM
        $dataType = $configData.DATA_TYPE
        $gemmSize = $configData.GEMM_SIZE
        Write-Host "  ✅ Config loaded: DIM=$dimValue, DATA_TYPE=$dataType, GEMM_SIZE=$gemmSize" -ForegroundColor Green
    } catch {
        Write-Host "  ⚠️  Error reading config.json, using defaults" -ForegroundColor Yellow
    }
} else {
    Write-Host "  ⚠️  config.json not found, using defaults" -ForegroundColor Yellow
}

# 5. GENERATE ESSENTIAL REPORT
Write-Host "`n📄 GENERATING ESSENTIAL REPORT" -ForegroundColor Green

$essentialReport = @"
===============================================================================
AI Engine GEMM Essential Metrics
===============================================================================
Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
Project: GEMM ${gemmSize}x${gemmSize}x${gemmSize} (${dataType}, DIM=${dimValue}, 16 cores)
===============================================================================

=== RESOURCE UTILIZATION ===
LUTs: $lutUsed / $lutTotal ($lutPercent%)
BRAMs: $bramUsed / $bramTotal ($bramPercent%)
DSPs: $dspUsed / $dspTotal ($dspPercent%)
URAM: $uramUsed / $uramTotal ($uramPercent%)
CLB Registers: $ffUsed / $ffTotal ($ffPercent%)

=== TIMING PERFORMANCE ===
WNS (Worst Negative Slack): $wns ns
TNS (Total Negative Slack): $tns ns
WHS (Worst Hold Slack): $whs ns
THS (Total Hold Slack): $ths ns

Clock Frequencies:
"@

# Add clock frequencies to report with explanations
if ($clockFrequencies.Count -gt 0) {
    foreach ($clock in $clockFrequencies) {
        $clockName = $clock -replace ":.*", ""
        $clockFreq = $clock -replace ".*: ", ""
        
        # Add explanation for each clock
        $explanation = ""
        switch ($clockName) {
            "clk_pl_0" { $explanation = " (Base PL Clock)" }
            "clkfbout_primitive" { $explanation = " (MMCM Feedback)" }
            "clkout1_primitive" { $explanation = " (PL Logic Clock)" }
            "clkout1_primitive_1" { $explanation = " (AI Engine Clock)" }
            "bank1_clkout0" { $explanation = " (DDR4 Memory Clock)" }
            "bank1_xpll0_fifo_rd_clk" { $explanation = " (DDR4 FIFO Clock)" }
            "mc_clk_xpll" { $explanation = " (Memory Controller Clock)" }
            "pll_clk_xpll" { $explanation = " (High-Speed I/O Clock)" }
            "pll_clktoxphy[0]" { $explanation = " (DDR4 PHY Clock 0)" }
            "pll_clktoxphy[2]" { $explanation = " (DDR4 PHY Clock 2)" }
            "sys_clk0_0_clk_p[0]" { $explanation = " (System Clock 0)" }
            default { $explanation = " (System Clock)" }
        }
        
        $essentialReport += "`n  ${clockName}: $clockFreq$explanation"
    }
    
    # Add clock domain explanation
    $essentialReport += @"

Clock Domain Explanation:
  • Base PL Clock (100 MHz): Main programmable logic clock
  • AI Engine Clock (312.5 MHz): High-speed AI Engine processing
  • DDR4 Memory Clock (800 MHz): High-speed memory interface
  • DDR4 PHY Clocks (3.2 GHz): Physical layer memory interface
  • System Clock (200 MHz): System-level operations
  • High-Speed I/O Clock (3.2 GHz): Ultra-fast data transfer
"@
} else {
    $essentialReport += "`n  No clock frequency data available"
}

$essentialReport += @"

Status: $(if ([double]$wns -gt 0) { "✅ TIMING CLOSED" } else { "❌ TIMING FAILED" })

=== POWER CONSUMPTION ===
Total On-Chip Power: $totalPower W
AI Engine Power: $aiePower W
Memory Power (BRAM + NoC-DDRMC + XRAM): $memoryPowerDisplay
AI Engine Cores: $aieCoresUsed / $aieCoresTotal ($aieUtilPercent%)

=== RUNTIME PROFILING (if available) ===
XRT Run Summary: $(if (SafeTestPath $xrtRunSummaryPath) { "Present" } else { "Not found" })
Kernel Durations (top):
"@

# Append brief runtime kernel summary (if any)
if ($kernelSummary -and $kernelSummary.Count -gt 0) {
    foreach ($ln in $kernelSummary) { $essentialReport += ($ln + "`n") }
} else {
    $essentialReport += "  (none)\n"
}

$essentialReport += @"
 
=== KEY INSIGHTS ===
"@

# Add key insights
$insights = @()

if ($timingMatch.Success -and [double]$wns -gt 0) {
    $insights += "✅ Design meets timing requirements (WNS = $wns ns)"
} elseif ($timingMatch.Success) {
    $insights += "❌ Design has timing violations (WNS = $wns ns)"
}

if ($lutMatch.Success -and [double]$lutPercent -lt 20) {
    $insights += "📈 Low LUT utilization ($lutPercent%) - room for additional functionality"
}

if ($bramMatch.Success -and [double]$bramPercent -gt 50) {
    $insights += "⚠️ Moderate BRAM usage ($bramPercent%) - monitor memory requirements"
}

if ($dspMatch.Success -and [double]$dspPercent -gt 0) {
    $insights += "🔢 DSP utilization: $dspPercent% ($dspUsed/$dspTotal)"
} elseif ($dspMatch.Success) {
    $insights += "🔢 No DSP usage detected - design may be LUT-based"
}

if ($uramMatch.Success -and [double]$uramPercent -eq 0) {
    $insights += "💾 No URAM usage - using BRAM for memory"
}

if ($ffMatch.Success -and [double]$ffPercent -lt 10) {
    $insights += "📊 Low register usage ($ffPercent%) - efficient design"
}

if ($clockFrequencies.Count -gt 0) {
    $mainClock = $clockFrequencies[0] -replace ".*: (\d+\.\d+) MHz", '$1'
    $insights += "🕐 Main clock frequency: $mainClock MHz"
}

if ($aiePowerMatch.Success -and [double]$aiePower -gt 0) {
    $insights += "🤖 AI Engine is active with 16 cores consuming $aiePower W power"
}

if ($totalPowerMatch.Success) {
    $insights += "⚡ Total power consumption: $totalPower W"
}

foreach ($insight in $insights) {
    $essentialReport += "`n$insight"
}

$essentialReport += @"

===============================================================================
End of Essential Metrics
===============================================================================
"@

# Save essential report
$essentialReport | Out-File -FilePath $OutputFile -Encoding UTF8

Write-Host "`n✅ Essential metrics extracted!" -ForegroundColor Green
Write-Host "📄 Report saved to: $OutputFile" -ForegroundColor Cyan

# Show summary
Write-Host "`n📊 SUMMARY:" -ForegroundColor Yellow
Write-Host "  Resources: LUTs $lutPercent%, BRAMs $bramPercent%, DSPs $dspPercent%" -ForegroundColor White
Write-Host "  Timing: WNS $wns ns $(if ([double]$wns -gt 0) { '(PASS)' } else { '(FAIL)' })" -ForegroundColor White
Write-Host "  Power: $totalPower W total, $aiePower W AI Engine, Memory: $memoryPowerDisplay" -ForegroundColor White
if ($thermalContent) {
    Write-Host "  Thermal analysis available (thermal_analysis.txt)" -ForegroundColor White
}

# 3a. RUNTIME PROFILING (if available)
Write-Host "`n🧪 RUNTIME PROFILING (if available)" -ForegroundColor Yellow
if ((SafeTestPath $xrtRunSummaryPath) -or ($runtimeCsvs -and $runtimeCsvs.Count -gt 0) -or ($timelineCsvs -and $timelineCsvs.Count -gt 0) -or ($traceCsvs -and $traceCsvs.Count -gt 0)) {
    if (SafeTestPath $xrtRunSummaryPath) {
        Write-Host "  Found: xrt.run_summary" -ForegroundColor White
    }
    if ($kernelSummary -and $kernelSummary.Count -gt 0) {
        Write-Host "  Top runtime kernel durations:" -ForegroundColor White
        $kernelSummary | ForEach-Object { Write-Host $_ -ForegroundColor White }
    } else {
        Write-Host "  (No kernel CSV parsed)" -ForegroundColor DarkGray
    }
} else {
    Write-Host "  (No runtime profiling files found in reports/)" -ForegroundColor DarkGray
}


# ============================================================================
# ADDITIONAL REQUESTED METRICS (Counts, Placement, Power breakdown, etc.)
# ============================================================================
try {
    Write-Host "`n🔎 Collecting additional requested metrics" -ForegroundColor Yellow

    $complexityPath = Find-ReportFile "complexity.csv"
    $compilerMapPath = Find-ReportFile "graph_mapping_analysis_report.txt"
    # $dmaReportPath reserved for future parsing of DMA details
    # $dmaReportPath = Join-Path $ReportsDir "DMA_report.txt"
    $aieUtilSpecPath = Find-ReportFile "aie_utilization_specific.txt"
    $aiePowerPath = Find-ReportFile "aie_power.txt"
    $memUtilPath = Find-ReportFile "memory_utilization.txt"
    $memDistPath = Find-ReportFile "memory_distribution.txt"
    $memBwPath = Find-ReportFile "memory_bandwidth.txt"
    $psMetricsPath = Find-ReportFile "ps_metrics.txt"
    $intfInvPath = Find-ReportFile "interface_inventory.txt"
    $nocMetricsPath = Find-ReportFile "noc_metrics.txt"
    $nocPerfPath = Find-ReportFile "noc_performance.txt"
    $drcPath = Find-ReportFile "drc_checks.txt"

    # Load files (safe)
    $complexity = if ($complexityPath) { Get-Content $complexityPath -Raw -ErrorAction SilentlyContinue } else { "" }
    $compilerMap = if ($compilerMapPath) { Get-Content $compilerMapPath -Raw -ErrorAction SilentlyContinue } else { "" }
    # $dmaReport currently not used in the report
    # $dmaReport = if ($dmaReportPath) { Get-Content $dmaReportPath -Raw } else { "" }
    $aieUtilSpec = if ($aieUtilSpecPath) { Get-Content $aieUtilSpecPath -Raw -ErrorAction SilentlyContinue } else { "" }
    $aiePowerTxt = if ($aiePowerPath) { Get-Content $aiePowerPath -Raw -ErrorAction SilentlyContinue } else { "" }
    $memUtilTxt = if ($memUtilPath) { Get-Content $memUtilPath -Raw -ErrorAction SilentlyContinue } else { "" }
    $memDistTxt = if ($memDistPath) { Get-Content $memDistPath -Raw -ErrorAction SilentlyContinue } else { "" }
    $memBwTxt = if ($memBwPath) { Get-Content $memBwPath -Raw -ErrorAction SilentlyContinue } else { "" }
    $psMetricsTxt = if ($psMetricsPath) { Get-Content $psMetricsPath -Raw -ErrorAction SilentlyContinue } else { "" }
    $intfInvTxt = if ($intfInvPath) { Get-Content $intfInvPath -Raw -ErrorAction SilentlyContinue } else { "" }
    $nocMetricsTxt = if ($nocMetricsPath) { Get-Content $nocMetricsPath -Raw -ErrorAction SilentlyContinue } else { "" }
    $nocPerfTxt = if ($nocPerfPath) { Get-Content $nocPerfPath -Raw -ErrorAction SilentlyContinue } else { "" }
    $drcTxt = if ($drcPath) { Get-Content $drcPath -Raw -ErrorAction SilentlyContinue } else { "" }

    # Parse complexity.csv (key,value lines)
    $getComplex = {
        param($key)
        $m = [regex]::Match($complexity, "^$key,([^\r\n]+)", 'Multiline')
        if ($m.Success) { $m.Groups[1].Value } else { $null }
    }
    # $totalTiles is not used explicitly in the current summary
    # $totalTiles = & $getComplex "total_num_tiles"
    $totalKernels = & $getComplex "total_num_kernels"
    $totalCores = & $getComplex "total_num_cores"
    $totalCoreUtil = & $getComplex "total_core_util"
    $plioCount = & $getComplex "plio_node_count"

    # Compute AIE core utilization percent if data available
    $aieUtilPct = if ($totalCoreUtil -and $totalCores -and [double]::TryParse($totalCoreUtil, [ref]([double]$null)) -and [int]::TryParse($totalCores, [ref]([int]$null))) {
        try { [math]::Round(([double]$totalCoreUtil / [double]$totalCores) * 100, 2) } catch { $null }
    } else { $null }

    # Count MatMul kernels from mapping (fallback to complexity)
    $matmultMatches = [regex]::Matches($compilerMap, "\bi\d+:matMult\b")
    $matmultCount = if ($matmultMatches.Count -gt 0) { $matmultMatches.Count } elseif ($totalKernels) { [int]$totalKernels } else { $null }

    # AIE kernel placement heuristic: gather CR(x,y)
    $crMatches = [regex]::Matches($compilerMap, "CR\((\d+),(\d+)\)")
    $rows = New-Object System.Collections.Generic.HashSet[int]
    foreach ($m in $crMatches) { [void]$rows.Add([int]$m.Groups[2].Value) }
    $placement = if ($rows.Count -le 2) { "Horizontal" } elseif ($rows.Count -gt 2) { "Staggered horizontal" } else { "Auto" }

    # AIE buffer placement: if Memory Bank Report present, mark custom
    $hasMemBankReport = $compilerMap -match "Memory Bank Report:"
    $bufferPlacement = if ($hasMemBankReport) { "Custom (explicit MG mappings)" } else { "Auto (compiler default)" }

    # Memory Banks used: count unique MG(x,y) pairs
    $mgMatches = [regex]::Matches($compilerMap, "MG\((\d+),(\d+)\):\d+")
    $mgPairs = New-Object System.Collections.Generic.HashSet[string]
    foreach ($m in $mgMatches) { [void]$mgPairs.Add($m.Groups[1].Value + "," + $m.Groups[2].Value) }
    $memoryBanksUsed = if ($mgPairs.Count -gt 0) { $mgPairs.Count } else { $null }

    # DMA banks: DDRMC usage from aie_utilization_specific.txt
    $ddrmcUsedMatch = [regex]::Match($aieUtilSpec, "\bDDRMC\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|")
    $dmaBanksUsed = if ($ddrmcUsedMatch.Success) { $ddrmcUsedMatch.Groups[1].Value } else { $null }
    $dmaBanksAvail = if ($ddrmcUsedMatch.Success) { $ddrmcUsedMatch.Groups[2].Value } else { $null }

    # PLIOs from complexity (fallback to compiler_report.json if needed)
    $plios = if ($plioCount) { $plioCount } else { $null }

    # Power breakdown from aie_power.txt
    $totalPowerMatch2 = [regex]::Match($aiePowerTxt, "Total On-Chip Power \(W\)\s*\|\s*([\d\.]+)")
    $aie2PowerMatch = [regex]::Match($aiePowerTxt, "\bAIE2\s*\|\s*([\d\.]+)")
    $nocDdrmcPowerMatch = [regex]::Match($aiePowerTxt, "NoC-DDRMC\s*\|\s*([\d\.]+)")
    $xramPowerMatch = [regex]::Match($aiePowerTxt, "\bXRAM\s*\|\s*([\d\.]+)")
    $bramPowerMatch = [regex]::Match($aiePowerTxt, "Block RAM\s*\|\s*([\d\.]+)")

    $totalPowerW = if ($totalPowerMatch2.Success) { $totalPowerMatch2.Groups[1].Value } else { $null }
    $aieCorePowerW = if ($aie2PowerMatch.Success) { $aie2PowerMatch.Groups[1].Value } else { $null }
    $nocDdrmcPowerW = if ($nocDdrmcPowerMatch.Success) { $nocDdrmcPowerMatch.Groups[1].Value } else { $null }
    $xramPowerW = if ($xramPowerMatch.Success) { $xramPowerMatch.Groups[1].Value } else { $null }
    $bramPowerW = if ($bramPowerMatch.Success) { $bramPowerMatch.Groups[1].Value } else { $null }

    $bramUtilMatch = [regex]::Match($memUtilTxt, "Block RAM Tile\s*\|\s*(\d+)\s*\|\s*\d+\s*\|\s*\d+\s*\|\s*(\d+)\s*\|\s*([\d\.]+)")
    $bramUsed = if ($bramUtilMatch.Success) { $bramUtilMatch.Groups[1].Value } else { $null }
    $bramAvail = if ($bramUtilMatch.Success) { $bramUtilMatch.Groups[2].Value } else { $null }
    $bramPct = if ($bramUtilMatch.Success) { $bramUtilMatch.Groups[3].Value } else { $null }

    # Throughput / TOPS from runtime logs if available
    $runtimeLogCandidates = @(
        "logs/runtime.log",
        "logs/run_output.log",
        "logs/host_app.log",
        "logs/aie_runtime.log"
    )
    $tops = $null; $topsPerW = $null
    foreach ($cand in $runtimeLogCandidates) {
        if (Test-Path $cand) {
            $r = Get-Content $cand -Raw
            $topsMatch = [regex]::Match($r, "TOPS\s*[:=]\s*([\d\.]+)")
            if ($topsMatch.Success) { $tops = $topsMatch.Groups[1].Value }
            $effMatch = [regex]::Match($r, "TOPS/W\s*[:=]\s*([\d\.]+)")
            if ($effMatch.Success) { $topsPerW = $effMatch.Groups[1].Value }
            if ($tops -or $topsPerW) { break }
        }
    }

    # Optional: Compute Throughput (TOPS) from AIE emulation TLAST timestamps
    $tops = $null; $topsPerW = $null
    try {
        $emuDataDir = Join-Path (Get-Location) "Emulation-AIE/aiesimulator_output/data"
        if (Test-Path $emuDataDir) {
            $matCFiles = Get-ChildItem -Path $emuDataDir -Filter "matC*.txt" -File -ErrorAction SilentlyContinue
            if ($matCFiles -and $matCFiles.Count -gt 0) {
                $globalDiffNs = New-Object System.Collections.Generic.List[double]
                foreach ($f in $matCFiles) {
                    $lines = Get-Content $f.FullName -ErrorAction SilentlyContinue
                    if (-not $lines) { continue }
                    $fileTlastNs = New-Object System.Collections.Generic.List[double]
                    for ($i = 1; $i -lt $lines.Count; $i++) {
                        if ($lines[$i] -match "TLAST") {
                            $prev = $lines[$i-1]
                            $m = [regex]::Match($prev, "([\d\.]+)\s*(ns|ps|us)")
                            if ($m.Success) {
                                $val = [double]$m.Groups[1].Value
                                $unit = $m.Groups[2].Value
                                switch ($unit) {
                                    "ns" { $fileTlastNs.Add($val) }
                                    "ps" { $fileTlastNs.Add($val/1000.0) }
                                    "us" { $fileTlastNs.Add($val*1000.0) }
                                }
                            }
                        }
                    }
                    for ($j = 1; $j -lt $fileTlastNs.Count; $j++) {
                        $globalDiffNs.Add($fileTlastNs[$j] - $fileTlastNs[$j-1])
                    }
                }
                if ($globalDiffNs.Count -gt 0) {
                    $meanNs = ($globalDiffNs | Measure-Object -Average).Average
                    # Use GEMM cube with size from config as default (M=K=N=GEMM_SIZE); multipliers assumed 1
                    $M = [int]$gemmSize; $K = [int]$gemmSize; $N = [int]$gemmSize
                    $ops = 2.0 * $M * $K * $N
                    $timeSec = $meanNs * 1e-9
                    if ($timeSec -gt 0) {
                        $tops = [math]::Round(($ops / $timeSec) / 1e12, 6)
                        if ($totalPowerW) {
                            $p = 0.0; [void][double]::TryParse($totalPowerW, [ref]$p)
                            if ($p -gt 0) { $topsPerW = [math]::Round($tops / $p, 6) }
                        }
                    }
                }
            }
        }
    } catch {}

    # KCC, KCE, MACs/Cyc - not found in current reports; leave N/A unless runtime logs provide
    $kcc = $null; $kce = $null; $macsPerCyc = $null

    # AIE local memory utilization (assume 64KB per used AIE core)
    $aieMemPerCoreKB = 64
    $aieCoresUsedNum = $null
    if ($totalCores -and [int]::TryParse($totalCores, [ref]([int]$null))) { $aieCoresUsedNum = [int]$totalCores }
    $aieLocalCapKB = if ($aieCoresUsedNum) { $aieCoresUsedNum * $aieMemPerCoreKB } else { $null }
    $totalAieMemKB = $null
    $memSizeMatch = [regex]::Match($complexity, "^total_memory_size,([^\r\n]+)", 'Multiline')
    if ($memSizeMatch.Success) {
        # Treat as KB if value is reasonable; otherwise try to convert bytes->KB
        $val = $memSizeMatch.Groups[1].Value
        $num = $null; [void][double]::TryParse($val, [ref]$num)
        if ($num -gt 0) {
            if ($num -gt 1024*1024) { $totalAieMemKB = [math]::Round(($num/1024.0),0) } else { $totalAieMemKB = [math]::Round($num,0) }
        }
    }
    $aieLocalUtilPct = $null
    if ($aieLocalCapKB -and $totalAieMemKB) {
        $aieLocalUtilPct = [math]::Round(($totalAieMemKB / $aieLocalCapKB) * 100, 2)
    }

    # Build output block
    $metricsBlock = @()
    $metricsBlock += "==============================================================================="
    $metricsBlock += "Requested Metrics Summary (auto-extracted)"
    $metricsBlock += "==============================================================================="
    $metricsBlock += "- # MatMul kernels: " + ($(if ($matmultCount) { $matmultCount } else { 'N/A' }))
    $metricsBlock += "- # AIE cores: " + ($(if ($totalCores) { $totalCores } else { 'N/A' }))
    $metricsBlock += "- # Memory Banks (AIE tiles used): " + ($(if ($memoryBanksUsed) { $memoryBanksUsed } else { 'N/A' }))
    $metricsBlock += "- # DMA banks (DDRMC used/avail): " + ($(if ($dmaBanksUsed) { $dmaBanksUsed } else { 'N/A' })) + " / " + ($(if ($dmaBanksAvail) { $dmaBanksAvail } else { 'N/A' }))
    $metricsBlock += "- # PLIOs: " + ($(if ($plios) { $plios } else { 'N/A' }))
    $metricsBlock += "- Throughput (TOPS): " + ($(if ($tops) { $tops } else { 'N/A' }))
    $metricsBlock += "- Energy efficiency (TOPS/W): " + ($(if ($topsPerW) { $topsPerW } else { 'N/A' }))
    $metricsBlock += "- Power (W): " + ($(if ($totalPowerW) { $totalPowerW } else { 'N/A' }))
    $metricsBlock += "- AIE core Power (W): " + ($(if ($aieCorePowerW) { $aieCorePowerW } else { 'N/A' }))
    $metricsBlock += "- Memory Power (W) [BRAM + NoC-DDRMC + XRAM]: $memoryPowerDisplay"
    $metricsBlock += "- PL BRAM utilization (tiles): " + ($(if ($bramUsed -and $bramAvail -and $bramPct) { "$bramUsed / $bramAvail ($bramPct%)" } else { 'N/A' }))
    $metricsBlock += "- AIE local memory capacity: " + ($(if ($aieLocalCapKB) { "$aieLocalCapKB KB ($aieCoresUsedNum cores × ${aieMemPerCoreKB}KB)" } else { 'N/A' }))
    $metricsBlock += "- AIE local memory used (from complexity.csv): " + ($(if ($totalAieMemKB) { "$totalAieMemKB KB" } else { 'N/A' }))
    $metricsBlock += "- AIE local memory utilization: " + ($(if ($aieLocalUtilPct) { "$aieLocalUtilPct%" } else { 'N/A' }))
    $metricsBlock += "- AIE buffer placement: " + $bufferPlacement
    $metricsBlock += "- AIE kernel placement: " + $placement
    $metricsBlock += "- AIE KERNEL COMPUTE CYCLES (KCC): " + ($(if ($kcc) { $kcc } else { 'N/A' }))
    $metricsBlock += "- KERNEL COMPUTE EFFICIENCY (KCE): " + ($(if ($kce) { $kce } else { 'N/A' }))
    $metricsBlock += "- MACs/Cyc: " + ($(if ($macsPerCyc) { $macsPerCyc } else { 'N/A' }))
    $metricsBlock += "- AIE kernel utilization (avg %): " + ($(if ($aieUtilPct) { "$aieUtilPct%" } else { 'N/A' }))
    $metricsBlock += "- Shared Memory performance (TOPS): N/A"
    $metricsBlock += "- Cascade Stream Performance (TOPS): N/A"
    $metricsBlock += "==============================================================================="

    # Append to output file
    $metricsBlock -join "`n" | Add-Content -Path $OutputFile -Encoding UTF8

    Write-Host "  ✅ Additional metrics appended to $OutputFile" -ForegroundColor Green

    # User guidance for deterministic TOPS
    Write-Host "`n💡 Deterministic TOPS/TOPS/W:" -ForegroundColor Cyan
    Write-Host "  • Provide core computation time (ms) from the host run" -ForegroundColor White
    Write-Host "  • Confirm GEMM size N (for cubic GEMM)" -ForegroundColor White
    Write-Host "  • Use ops = 2 * N^3 (int16 MACs)" -ForegroundColor White
    Write-Host "  • Compute TOPS = (ops / (ms/1000)) / 1e12 ;  TOPS/W = TOPS / TotalPower" -ForegroundColor White

} catch {
    Write-Host "  ⚠️ Failed to collect additional requested metrics: $($_.Exception.Message)" -ForegroundColor Yellow
}

# ============================================================================
# COMPREHENSIVE VERSAL METRICS SECTIONS
# ============================================================================

Write-Host "`n📚 COMPREHENSIVE METRICS" -ForegroundColor Green

# Helper: safe regex group value
function Get-FirstMatchGroup {
    param($text, $pattern, $groupIdx)
    if (-not $text) { return $null }
    $m = [regex]::Match($text, $pattern)
    if ($m.Success) { return $m.Groups[$groupIdx].Value } else { return $null }
}

$compBlock = @()
$compBlock += "==============================================================================="
$compBlock += "Comprehensive Versal Metrics (from generated reports)"
$compBlock += "==============================================================================="

# AI Engine metrics (tiles, kernels, streams)
$aieTilesLine = Get-FirstMatchGroup $aieUtilSpec "AIE Tiles: total=(\d+), core_tiles=(\d+), shim_tiles=(\d+)" 0
if ($aieTilesLine) {
    $compBlock += "AI Engine Tiles: $aieTilesLine"
}
$aieKernels = Get-FirstMatchGroup $aieUtilSpec "AIE Kernel-like Instances: (\d+)" 1
$aieStreams = Get-FirstMatchGroup $aieUtilSpec "AIE Stream-like Instances: (\d+)" 1
if ($aieKernels) { $compBlock += "AIE Kernel-like Instances: $aieKernels" }
if ($aieStreams) { $compBlock += "AIE Stream-like Instances: $aieStreams" }

# PS metrics
$psCips = Get-FirstMatchGroup $psMetricsTxt "PS/CIPS Instances: (\d+)" 1
$psApu = Get-FirstMatchGroup $psMetricsTxt "APU Core-like Instances: (\d+)" 1
$psRpu = Get-FirstMatchGroup $psMetricsTxt "RPU Core-like Instances: (\d+)" 1
$psDdr = Get-FirstMatchGroup $psMetricsTxt "DDR/MIG Controllers: (\d+)" 1
$psPeriph = Get-FirstMatchGroup $psMetricsTxt "PS Peripheral-like Instances: (\d+)" 1
if ($psCips) { $compBlock += "PS/CIPS Instances: $psCips" }
if ($psApu) { $compBlock += "APU Core-like Instances: $psApu" }
if ($psRpu) { $compBlock += "RPU Core-like Instances: $psRpu" }
if ($psDdr) { $compBlock += "DDR/MIG Controllers: $psDdr" }
if ($psPeriph) { $compBlock += "PS Peripheral-like Instances: $psPeriph" }

# PL utilization details (already printed essentials). Optionally echo again.
if ($lutPercent -ne 'N/A') { $compBlock += "PL LUTs: $lutUsed / $lutTotal ($lutPercent%)" }
if ($ffPercent -ne '0.00') { $compBlock += "PL FFs: $ffUsed / $ffTotal ($ffPercent%)" }
if ($bramPercent -ne 'N/A') { $compBlock += "PL BRAM: $bramUsed / $bramTotal ($bramPercent%)" }
if ($uramPercent -ne '0.00') { $compBlock += "PL URAM: $uramUsed / $uramTotal ($uramPercent%)" }
if ($dspPercent -ne '0.00') { $compBlock += "PL DSPs: $dspUsed / $dspTotal ($dspPercent%)" }
$compBlock += "Memory Power (W) [BRAM + NoC-DDRMC + XRAM]: $memoryPowerDisplay"

# Memory metrics
$mdBram36 = Get-FirstMatchGroup $memDistTxt "BRAM36 count: (\d+)" 1
$mdBram18 = Get-FirstMatchGroup $memDistTxt "BRAM18 count: (\d+)" 1
$mdUram = Get-FirstMatchGroup $memDistTxt "URAM blocks: (\d+)" 1
if ($mdBram36 -or $mdBram18 -or $mdUram) {
    $compBlock += "Memory Distribution: BRAM36=$mdBram36, BRAM18=$mdBram18, URAM=$mdUram"
}
if ($memBwTxt) {
    # Show top 5 bandwidth entries by reported Gbps (last column)
    $bwLines = ($memBwTxt -split "\r?\n").Where({ $_ -and -not $_.StartsWith("Estimated") -and -not $_.StartsWith("Format") })
    $parsed = @()
    foreach ($ln in $bwLines) {
        $cols = $ln.Split(",").ForEach({ $_.Trim() })
        if ($cols.Count -ge 4) {
            $obj = [pscustomobject]@{ Name=$cols[0]; Bits=$cols[1]; MHz=$cols[2]; Gbps=$cols[3] }
            $parsed += $obj
        }
    }
    if ($parsed.Count -gt 0) {
        $top = $parsed | Sort-Object {[double]$_.Gbps} -Descending | Select-Object -First 5
        $compBlock += "Top Interfaces by Theoretical Bandwidth (Gbps):"
        foreach ($t in $top) { $compBlock += ("  {0} : {1} Gbps ({2}b @ {3} MHz)" -f $t.Name,$t.Gbps,$t.Bits,$t.MHz) }
    }
}

# Interfaces (AXI/AXIS/PLIO/GT)
$axiCount = Get-FirstMatchGroup $intfInvTxt "AXI Instances: (\d+)" 1
$axisCount = Get-FirstMatchGroup $intfInvTxt "AXIS Instances: (\d+)" 1
$plioCountInv = Get-FirstMatchGroup $intfInvTxt "PLIO Instances: (\d+)" 1
$gtCount = Get-FirstMatchGroup $intfInvTxt "GT Transceiver-like Instances: (\d+)" 1
if ($axiCount) { $compBlock += "AXI Instances: $axiCount" }
if ($axisCount) { $compBlock += "AXIS Instances: $axisCount" }
if ($plioCountInv) { $compBlock += "PLIO Instances: $plioCountInv" }
if ($gtCount) { $compBlock += "GT Transceiver-like Instances: $gtCount" }

# NoC metrics
$nocCount = Get-FirstMatchGroup $nocMetricsTxt "NoC Instances: (\d+)" 1
if ($nocCount) { $compBlock += "NoC Instances: $nocCount" }
if ($nocPerfTxt) { $compBlock += "NoC Performance report available (noc_performance.txt)" }

# Timing additions
if ($clockFrequencies.Count -gt 0) { $compBlock += ("Clock domains listed: {0}" -f $clockFrequencies.Count) }
if ($constraintExceptions) { $compBlock += "Constraint Exceptions present (see constraints_exceptions.txt)" }

# Power/Thermal
if ($totalPower -ne 'N/A') { $compBlock += "Total On-Chip Power: $totalPower W" }
if ($thermalContent) { $compBlock += "Thermal analysis available (thermal_analysis.txt)" }

# DRC summary (rough)
if ($drcTxt) {
    $violations = ($drcTxt -split "\r?\n").Where({ $_ -match "^\s*VIOLATION|^\s*Rule|^\s*Critical|^\s*CRITICAL" }).Count
    $compBlock += "DRC Report: $violations potential violation lines (see drc_checks.txt)"
}

$compBlock += "==============================================================================="
$compBlock -join "`n" | Add-Content -Path $OutputFile -Encoding UTF8

Write-Host "  ✅ Comprehensive metrics appended to $OutputFile" -ForegroundColor Green