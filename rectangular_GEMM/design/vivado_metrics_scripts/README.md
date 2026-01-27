# Vivado Metrics Scripts

This directory contains scripts for generating comprehensive utilization and power reports from Vivado projects for AI Engine GEMM designs on Versal boards.

## Files

- `report_metrics.tcl` - Main TCL script for generating utilization and power reports
- `README.md` - This documentation file

## Usage

### Prerequisites

1. **Target must be `hw`**: The `report_metrics` target only works with `TARGET=hw`, not `hw_emu`
2. **Vivado must be installed**: The script requires Vivado to be available in the PATH
3. **Project must be built**: The implementation must be completed before running reports

### Running Report Generation

#### Method 1: Using Makefile (Recommended)
```bash
# Set target to hw
export TARGET=hw

# Build the project first
make run

# Generate reports
make report_metrics
```

#### Method 2: Direct Vivado Command
```bash
# Navigate to the reports directory
cd build/gemm_<size>x<size>x<size>/x1/hw/reports

# Run Vivado with the script
vivado -mode batch -source ../../../design/vivado_metrics_scripts/report_metrics.tcl \
       ../_x/link/vivado/vpl/prj/prj.xpr
```

## Generated Reports

The script generates the following reports in the reports directory:

### Utilization Reports
- `utilization_report.txt` - Basic utilization report (text format)
- `utilization_report.xml` - Utilization report (XML format)
- `utilization_detailed.txt` - Detailed utilization breakdown
- `utilization_hierarchy.txt` - Hierarchical utilization
- `utilization_report.html` - Utilization report (HTML format)

### Timing Reports
- `timing_summary.txt` - Timing summary
- `timing_detailed.txt` - Detailed timing analysis
- `timing_clock_domains.txt` - Timing by clock domain
- `timing_summary.html` - Timing summary (HTML format)

### Power Reports (if power analysis is enabled)
- `power_report.txt` - Power analysis report
- `power_report.xml` - Power analysis (XML format)
- `power_hierarchy.txt` - Hierarchical power analysis
- `power_clock_domains.txt` - Power by clock domain
- `power_report.html` - Power analysis (HTML format)

### Resource Reports
- `resources_by_type.txt` - Resource utilization by type
- `resources_by_clock.txt` - Resource utilization by clock domain

### AI Engine Specific Reports
- `aie_utilization.txt` - AI Engine specific utilization
- `memory_utilization.txt` - Memory utilization
- `dsp_utilization.txt` - DSP utilization

### Summary Report
- `design_summary.txt` - Comprehensive design summary

## Enabling Power Analysis

To generate power reports, power analysis must be enabled in the Vivado project:

```tcl
# In Vivado TCL console or script
set_property POWER_ANALYSIS_ENABLED true [current_project]
```

## Troubleshooting

### Common Issues

1. **"No implementation runs found"**
   - Ensure the project has been built with `TARGET=hw`
   - Check that the implementation completed successfully

2. **"Power analysis not enabled"**
   - Enable power analysis in the project settings
   - Power reports will be skipped if not enabled

3. **"Project file does not exist"**
   - Verify the project path is correct
   - Ensure the build completed successfully

4. **Permission denied errors**
   - Check file permissions in the reports directory
   - Ensure write access to the reports directory

### Debug Mode

To run the script in debug mode for troubleshooting:

```bash
vivado -mode tcl -source design/vivado_metrics_scripts/report_metrics.tcl <project.xpr>
```

## Report Analysis

### Key Metrics to Look For

1. **Utilization**
   - Overall device utilization percentage
   - AI Engine utilization
   - Memory utilization (BRAM, URAM, etc.)
   - DSP utilization

2. **Timing**
   - Worst Negative Slack (WNS)
   - Total Negative Slack (TNS)
   - Clock frequency achieved
   - Critical path analysis

3. **Power**
   - Total power consumption
   - Power by clock domain
   - Static vs dynamic power
   - Power by resource type

### HTML Reports

The HTML reports can be opened in any web browser for interactive analysis:
- `utilization_report.html`
- `timing_summary.html`
- `power_report.html` (if power analysis enabled)

## Integration with Build System

The script is integrated with the Makefile build system:

```makefile
# Makefile target
report_metrics: xsa $(BLD_REPORTS_DIR)

$(BLD_REPORTS_DIR): $(VIVADO_METRICS_SCRIPTS_REPO)/report_metrics.tcl
ifeq ($(TARGET),hw_emu)
	@echo "This build target (report-metrics) not valid when design target is hw_emu"
else
	rm -rf $(BLD_REPORTS_DIR)
	mkdir -p $(BLD_REPORTS_DIR)
	cd $(BLD_REPORTS_DIR); \
	vivado -mode batch -source $(VIVADO_METRICS_SCRIPTS_REPO)/report_metrics.tcl \
	$(BUILD_TARGET_DIR)/_x/link/vivado/vpl/prj/prj.xpr
	chmod 755 -R $(REPORTS_REPO)
endif
```

## Customization

The script can be customized by modifying the TCL file:

1. **Add custom reports**: Add new `report_*` commands
2. **Modify output format**: Change file formats or locations
3. **Add custom analysis**: Include project-specific analysis
4. **Change report structure**: Modify the summary report format

## Support

For issues or questions:
1. Check the Vivado log files for error messages
2. Verify all prerequisites are met
3. Ensure the project builds successfully with `TARGET=hw`
4. Check file permissions and paths
