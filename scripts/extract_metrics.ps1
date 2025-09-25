# ============================================================================
# AI Engine GEMM Essential Metrics Extraction
# ============================================================================
# This script extracts ONLY the most important metrics:
# - Resource utilization (LUTs, BRAMs, DSPs)
# - Timing performance (WNS, violations)
# - Power consumption (total, AI Engine)
#
# Usage: .\scripts\extract_essential_metrics.ps1
# ============================================================================

$ReportsDir = "reports"
$OutputFile = "metrics_summary.txt"

Write-Host "=== AI Engine GEMM Essential Metrics ===" -ForegroundColor Green

# 1. RESOURCE UTILIZATION (Most Important)
Write-Host "`n📊 RESOURCE UTILIZATION" -ForegroundColor Yellow

$utilContent = Get-Content "$ReportsDir\utilization_report.txt" -Raw -ErrorAction SilentlyContinue
$resourcesContent = Get-Content "$ReportsDir\resources_by_type.txt" -Raw -ErrorAction SilentlyContinue

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

$timingContent = Get-Content "$ReportsDir\timing_summary.txt" -Raw -ErrorAction SilentlyContinue
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
        $period = $match.Groups[2].Value
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

# 3. POWER CONSUMPTION (Most Important)
Write-Host "`n⚡ POWER CONSUMPTION" -ForegroundColor Yellow

# Extract power data from power report
$powerContent = Get-Content "$ReportsDir\power_report.txt" -Raw -ErrorAction SilentlyContinue
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
    $aiePower = "N/A"
    Write-Host "  AI Engine Power: $aiePower W (Not found)" -ForegroundColor Yellow
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
    $aiePower = "N/A"
    $aieCoresUsed = "N/A"
    $aieCoresTotal = "34"
    $aieUtilPercent = "N/A"
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
AI Engine Cores: $aieCoresUsed / $aieCoresTotal ($aieUtilPercent%)

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
Write-Host "  Power: $totalPower W total, $aiePower W AI Engine" -ForegroundColor White
