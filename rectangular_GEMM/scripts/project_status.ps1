# ============================================================================
# AI Engine GEMM Project Status Script
# ============================================================================
# This script provides a comprehensive overview of the project status,
# including build status, file organization, and key metrics.
#
# Usage: .\scripts\project_status.ps1
# ============================================================================

Write-Host "📊 AI Engine GEMM Project Status Report" -ForegroundColor Green
Write-Host "=======================================" -ForegroundColor Green
Write-Host "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Gray

# Read project configuration
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
        Write-Host "Project: GEMM ${gemmSize}x${gemmSize}x${gemmSize} (${dataType}, DIM=${dimValue})" -ForegroundColor Cyan
    } catch {
        Write-Host "Project: GEMM ${gemmSize}x${gemmSize}x${gemmSize} (${dataType}, DIM=${dimValue}) [Config Error]" -ForegroundColor Yellow
    }
} else {
    Write-Host "Project: GEMM ${gemmSize}x${gemmSize}x${gemmSize} (${dataType}, DIM=${dimValue}) [No Config]" -ForegroundColor Yellow
}

Write-Host ""

# Project Structure Check
Write-Host "📁 PROJECT STRUCTURE" -ForegroundColor Yellow
Write-Host "===================" -ForegroundColor Yellow

$keyDirs = @(
    "design",
    "design/aie_src",
    "design/host_app_src", 
    "design/pl_src",
    "docs",
    "logs",
    "reports",
    "reports/gemm_32x32x32/x1",
    "scripts",
    "platform_edge_hwemu"
)

foreach ($dir in $keyDirs) {
    if (Test-Path $dir) {
        $fileCount = (Get-ChildItem $dir -File -Recurse -ErrorAction SilentlyContinue | Measure-Object).Count
        Write-Host "  ✅ $dir ($fileCount files)" -ForegroundColor Green
    } else {
        Write-Host "  ❌ $dir (MISSING)" -ForegroundColor Red
    }
}

# Key Files Check
Write-Host "`n📄 KEY FILES" -ForegroundColor Yellow
Write-Host "============" -ForegroundColor Yellow

$keyFiles = @(
    "Makefile",
    "sync_and_run.ps1",
    "scripts/extract_real_metrics.ps1",
    "real_metrics_summary.txt",
    "README.md"
)

foreach ($file in $keyFiles) {
    if (Test-Path $file) {
        $size = (Get-Item $file).Length
        $sizeKB = [math]::Round($size / 1KB, 2)
        Write-Host "  ✅ $file ($sizeKB KB)" -ForegroundColor Green
    } else {
        Write-Host "  ❌ $file (MISSING)" -ForegroundColor Red
    }
}

# Build Status
Write-Host "`n🔨 BUILD STATUS" -ForegroundColor Yellow
Write-Host "===============" -ForegroundColor Yellow

if (Test-Path "logs/build_output.log") {
    $buildLog = Get-Content "logs/build_output.log" -Tail 10
    $lastLine = $buildLog[-1]
    if ($lastLine -match "Build completed successfully" -or $lastLine -match "Build finished") {
        Write-Host "  ✅ Last build: SUCCESS" -ForegroundColor Green
    } elseif ($lastLine -match "ERROR" -or $lastLine -match "FAILED") {
        Write-Host "  ❌ Last build: FAILED" -ForegroundColor Red
    } else {
        Write-Host "  ⚠️  Last build: UNKNOWN STATUS" -ForegroundColor Yellow
    }
    Write-Host "  📝 Last log line: $lastLine" -ForegroundColor Gray
} else {
    Write-Host "  ❌ No build log found" -ForegroundColor Red
}

# Reports Status
Write-Host "`n📊 REPORTS STATUS" -ForegroundColor Yellow
Write-Host "=================" -ForegroundColor Yellow

$reportsDir = "reports/gemm_32x32x32/x1"
if (Test-Path $reportsDir) {
    $reportFiles = Get-ChildItem $reportsDir -File
    $reportCount = $reportFiles.Count
    Write-Host "  ✅ Reports directory exists ($reportCount files)" -ForegroundColor Green
    
    $keyReports = @(
        "utilization_report.txt",
        "power_report.txt", 
        "timing_summary.txt"
    )
    
    foreach ($report in $keyReports) {
        if (Test-Path "$reportsDir/$report") {
            Write-Host "    ✅ $report" -ForegroundColor Green
        } else {
            Write-Host "    ❌ $report (MISSING)" -ForegroundColor Red
        }
    }
} else {
    Write-Host "  ❌ Reports directory not found" -ForegroundColor Red
}

# Metrics Status
Write-Host "`n📈 METRICS STATUS" -ForegroundColor Yellow
Write-Host "=================" -ForegroundColor Yellow

if (Test-Path "real_metrics_summary.txt") {
    $metricsContent = Get-Content "real_metrics_summary.txt" -Raw
    if ($metricsContent -match "WNS.*:.*ns" -and $metricsContent -match "Total On-Chip Power.*W") {
        Write-Host "  ✅ Metrics summary: COMPLETE" -ForegroundColor Green
        
        # Extract key metrics
        $wnsMatch = [regex]::Match($metricsContent, "WNS.*: ([\d.-]+) ns")
        $powerMatch = [regex]::Match($metricsContent, "Total On-Chip Power: ([\d.]+) W")
        $lutMatch = [regex]::Match($metricsContent, "LUTs: (\d+) / (\d+) \(([\d.]+)%\)")
        
        if ($wnsMatch.Success) {
            Write-Host "    📊 WNS: $($wnsMatch.Groups[1].Value) ns" -ForegroundColor Cyan
        }
        if ($powerMatch.Success) {
            Write-Host "    ⚡ Power: $($powerMatch.Groups[1].Value) W" -ForegroundColor Cyan
        }
        if ($lutMatch.Success) {
            Write-Host "    🔧 LUTs: $($lutMatch.Groups[1].Value)/$($lutMatch.Groups[2].Value) ($($lutMatch.Groups[3].Value)%)" -ForegroundColor Cyan
        }
    } else {
        Write-Host "  ⚠️  Metrics summary: INCOMPLETE" -ForegroundColor Yellow
    }
} else {
    Write-Host "  ❌ No metrics summary found" -ForegroundColor Red
}

# Scripts Status
Write-Host "`n🔧 SCRIPTS STATUS" -ForegroundColor Yellow
Write-Host "=================" -ForegroundColor Yellow

$scripts = Get-ChildItem "scripts" -File -Name "*.ps1"
foreach ($script in $scripts) {
    Write-Host "  ✅ $script" -ForegroundColor Green
}

# Recommendations
Write-Host "`n💡 RECOMMENDATIONS" -ForegroundColor Yellow
Write-Host "==================" -ForegroundColor Yellow

# Check for large files
$largeFiles = Get-ChildItem . -Recurse -File | Where-Object { $_.Length -gt 10MB } | Sort-Object Length -Descending
if ($largeFiles.Count -gt 0) {
    Write-Host "  📁 Large files found:" -ForegroundColor Yellow
    foreach ($file in $largeFiles | Select-Object -First 5) {
        $sizeMB = [math]::Round($file.Length / 1MB, 2)
        Write-Host "    - $($file.Name) ($sizeMB MB)" -ForegroundColor Gray
    }
}

# Check for old files
$oldFiles = Get-ChildItem . -Recurse -File | Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-30) } | Measure-Object
if ($oldFiles.Count -gt 10) {
    Write-Host "  📅 $($oldFiles.Count) files older than 30 days - consider cleanup" -ForegroundColor Yellow
}

Write-Host "`n✅ Project status check completed!" -ForegroundColor Green
Write-Host "Run '.\scripts\cleanup_project.ps1 -DryRun' to see cleanup suggestions" -ForegroundColor Cyan
