# AI Engine GEMM Metrics Extraction Scripts

This collection of scripts extracts important power, performance, and resource utilization information from generated Vivado reports for your AI Engine GEMM design.

## Available Scripts

### 1. PowerShell Script (Windows) - `extract_metrics.ps1`
**Recommended for Windows users**

```powershell
# Basic usage
.\extract_metrics.ps1

# With custom parameters
.\extract_metrics.ps1 -ReportsDir "reports\gemm_32x32x32\x1" -OutputFile "my_metrics.txt" -Verbose
```

**Features:**
- Comprehensive extraction from all report types
- Advanced regex pattern matching
- Detailed analysis and recommendations
- Verbose logging option
- Cross-platform compatible

### 2. Python Script (Cross-platform) - `extract_metrics.py`
**Recommended for Linux/Mac users**

```bash
# Basic usage
python extract_metrics.py

# With custom parameters
python extract_metrics.py --reports-dir "reports/gemm_32x32x32/x1" --output "my_metrics.txt"
```

**Features:**
- Full cross-platform compatibility
- Object-oriented design
- Comprehensive error handling
- JSON output support (future enhancement)

### 3. Batch Script (Windows) - `extract_metrics.bat`
**Simple Windows batch file**

```cmd
# Basic usage
extract_metrics.bat

# With custom parameters
extract_metrics.bat "reports\gemm_32x32x32\x1" "my_metrics.txt"
```

**Features:**
- Simple Windows batch implementation
- Basic extraction capabilities
- No external dependencies

## What Information is Extracted

### Resource Utilization
- **LUTs**: Look-Up Tables used vs. available
- **FFs**: Flip-Flops used vs. available  
- **BRAMs**: Block RAMs used vs. available
- **DSPs**: Digital Signal Processors used vs. available
- **URAMs**: UltraRAMs used vs. available
- **AI ML Engines**: AI Engine cores used vs. available
- **AI ML PL Master/Slave**: AI Engine interface components
- **AI ML NOC Slave**: AI Engine network-on-chip components

### Power Analysis
- **Total On-Chip Power**: Overall power consumption
- **Static Power**: Power consumed when idle
- **Dynamic Power**: Power consumed during operation
- **I/O Power**: Input/Output power consumption
- **AI Engine Power**: Specific AI Engine power consumption
- **AI Engine Cores**: Number of cores used vs. total
- **Power Efficiency**: Power per performance metrics

### Timing Performance
- **WNS (Worst Negative Slack)**: Timing margin
- **TNS (Total Negative Slack)**: Total timing violation
- **WHS (Worst Hold Slack)**: Hold timing margin
- **THS (Total Hold Slack)**: Total hold violation
- **Clock Frequency**: Operating frequency
- **Timing Closure Status**: Pass/Fail indication

### AI Engine Specific Analysis
- **AI Engine Utilization Details**: Detailed resource usage
- **AI Engine Power Details**: Power breakdown by component
- **AI Engine Timing Details**: Timing analysis for AI Engine
- **Core Mapping**: Physical core allocation

### Kernel Analysis
- **Kernel Utilization**: HLS kernel resource usage
- **Memory Utilization**: Memory resource usage
- **DSP Utilization**: DSP resource usage
- **Performance Metrics**: Kernel-specific performance

## Generated Output

### Console Output
The scripts display key metrics to the console:
```
=== KEY METRICS SUMMARY ===
Resource Utilization:
  LUTs: 12345|45678|27.1%
  BRAMs: 234|512|45.7%
  DSPs: 89|123|72.4%
  AI ML Engines: 0|16|0.00%

Power Analysis:
  Total Power: 2.345 W
  AI Engine Power: 0.123 W

Timing Performance:
  WNS: 0.123 ns
  Clock Frequency: 312.5 MHz
```

### Detailed Report File
A comprehensive text report is generated with:
- Complete resource utilization breakdown
- Detailed power analysis
- Timing performance metrics
- AI Engine specific analysis
- Kernel analysis
- Performance recommendations
- Design optimization suggestions

## Usage Examples

### Extract metrics from default location
```powershell
.\extract_metrics.ps1
```

### Extract metrics with verbose output
```powershell
.\extract_metrics.ps1 -Verbose
```

### Extract metrics from custom reports directory
```powershell
.\extract_metrics.ps1 -ReportsDir "reports\gemm_64x64x64\x1" -OutputFile "metrics_64x64.txt"
```

### Python usage with custom parameters
```bash
python extract_metrics.py --reports-dir "reports/gemm_32x32x32/x1" --output "detailed_metrics.txt"
```

## Required Report Files

The scripts expect the following report files in the reports directory:

### Essential Reports
- `utilization_detailed.txt` - Resource utilization data
- `power_report.txt` - Power consumption analysis
- `timing_summary.txt` - Timing performance data

### AI Engine Specific Reports
- `aie_utilization.txt` - AI Engine utilization details
- `aie_power.txt` - AI Engine power analysis
- `aie_timing.txt` - AI Engine timing analysis

### Optional Reports
- `memory_utilization.txt` - Memory resource usage
- `dsp_utilization.txt` - DSP resource usage

## Understanding the Output

### Resource Utilization Format
```
LUTs: 12345|45678|27.1%
```
- **12345**: Used resources
- **45678**: Total available resources  
- **27.1%**: Utilization percentage

### AI Engine Utilization Note
```
AI ML Engines: 0|16|0.00%
```
**This is normal behavior!** AI Engine IP blocks show 0% utilization in Vivado reports because:
- AI Engine is treated as a single IP block, not individual cores
- Vivado's utilization reporting doesn't break down individual cores
- The AI Engine is functional and consuming power as expected
- Check power reports to confirm AI Engine is active

### Power Analysis
```
Total On-Chip Power: 2.345 W
AI Engine Power: 0.123 W
```
- **Total Power**: Overall system power consumption
- **AI Engine Power**: Power consumed by AI Engine cores
- **Power Share**: Percentage of total power used by AI Engine

### Timing Performance
```
WNS: 0.123 ns
Clock Frequency: 312.5 MHz
```
- **WNS ≥ 0**: Timing closure PASSED
- **WNS < 0**: Timing closure FAILED (needs optimization)
- **Clock Frequency**: Operating frequency in MHz

## Troubleshooting

### Missing Reports
If reports are missing, the scripts will:
- Display warnings for missing files
- Continue with available reports
- Indicate "not available" for missing data

### No Data Extracted
If no data is extracted:
1. Check that report files exist and are readable
2. Verify report file format matches expected format
3. Ensure reports were generated successfully
4. Check file permissions

### AI Engine Shows 0% Utilization
This is **normal behavior** for AI Engine IP blocks:
- AI Engine is functional and working correctly
- Power consumption confirms AI Engine is active
- Utilization reporting limitation in Vivado
- No action required

## Integration with Build Process

### Add to Makefile
```makefile
# Add to your Makefile
extract_metrics: report_metrics
	@echo "Extracting design metrics..."
	python extract_metrics.py --reports-dir $(BLD_REPORTS_DIR) --output $(BLD_REPORTS_DIR)/design_metrics_summary.txt
	@echo "Metrics extraction complete!"
```

### Add to PowerShell Workflow
```powershell
# Add to sync_and_run.ps1
Write-Host "Extracting design metrics..." -ForegroundColor Green
.\extract_metrics.ps1 -ReportsDir "reports\gemm_32x32x32\x1" -OutputFile "design_metrics_summary.txt"
Write-Host "Metrics extraction complete!" -ForegroundColor Green
```

## Future Enhancements

- JSON output format support
- CSV export for spreadsheet analysis
- Graphical visualization of metrics
- Historical trend analysis
- Automated optimization recommendations
- Integration with CI/CD pipelines

## Support

For issues or questions:
1. Check that all required report files exist
2. Verify report file formats are correct
3. Ensure proper file permissions
4. Check console output for error messages

The scripts are designed to be robust and provide helpful error messages when issues occur.
