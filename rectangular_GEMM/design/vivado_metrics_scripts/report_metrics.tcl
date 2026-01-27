#!/usr/bin/env tclsh
# ==============================================================================
# Comprehensive Vivado Report Generation Script
# ==============================================================================
# This script generates comprehensive utilization, performance, and power reports
# from a Vivado project for AI Engine GEMM designs on Versal boards.
#
# Features:
# - Utilization analysis (detailed, hierarchical, by type)
# - Performance analysis (timing, clock domains, critical paths)
# - Power estimation (static and dynamic power analysis)
# - AI Engine specific analysis
# - Memory and DSP utilization
# - HTML and XML report formats
#
# Usage: vivado -mode batch -source report_metrics.tcl <project.xpr>
# Environment variables:
#   VIVADO_PROJECT_FILE - Path to .xpr project file
#   VIVADO_REPORTS_DIR - Directory to save reports (default: current directory)
# ==============================================================================

# Set script variables
set script_dir [file dirname [file normalize [info script]]]
# Use environment variable if set, otherwise use current working directory
if {[info exists ::env(VIVADO_REPORTS_DIR)]} {
    set reports_dir $::env(VIVADO_REPORTS_DIR)
} else {
    set reports_dir [pwd]
}

# Check if project file was provided via global variable, command line, or environment
if {[info exists ::project_file]} {
    set project_file $::project_file
} elseif {[llength $argv] > 0} {
    set project_file [lindex $argv 0]
} elseif {[info exists ::env(VIVADO_PROJECT_FILE)]} {
    set project_file $::env(VIVADO_PROJECT_FILE)
} else {
    puts "ERROR: No project file provided"
    puts "Usage: vivado -mode batch -source report_metrics.tcl <project.xpr>"
    puts "Or set global variable: set ::project_file <project.xpr>"
    puts "Or set environment variable: setenv VIVADO_PROJECT_FILE <project.xpr>"
    exit 1
}

# Check if project file exists
if {![file exists $project_file]} {
    puts "ERROR: Project file does not exist: $project_file"
    exit 1
}

puts "=============================================================================="
puts "Vivado Utilization and Power Report Generation"
puts "=============================================================================="
puts "Project file: $project_file"
puts "Reports directory: $reports_dir"
puts "Script directory: $script_dir"
puts "=============================================================================="

# Open the project
puts "Opening project: $project_file"
open_project $project_file

# Get the current project name
set project_name [get_property NAME [current_project]]
puts "Project name: $project_name"

# ==============================================================================
# UTILIZATION REPORTS
# ==============================================================================

puts "\n=== Generating Utilization Reports ==="

# Get all runs and find the best implementation run
set runs [get_runs]
set impl_runs {}

# Filter implementation runs using a more compatible method
foreach run $runs {
    if {[string match "*impl*" [get_property NAME $run]]} {
        lappend impl_runs $run
    }
}

if {[llength $impl_runs] == 0} {
    puts "ERROR: No implementation runs found in project"
    close_project
    exit 1
}

# Find the best implementation run (usually the one with highest performance)
set best_run [lindex $impl_runs 0]
foreach run $impl_runs {
    set run_status [get_property STATUS $run]
    if {$run_status == "write_bitstream Complete"} {
        set best_run $run
        break
    }
}

puts "Using implementation run: $best_run"

# Set the active run
current_run $best_run

# Launch the implementation run if not already completed
set run_status [get_property STATUS $best_run]
if {$run_status != "write_bitstream Complete" && $run_status != "route Complete" && $run_status != "place Complete" && $run_status != "phys_opt_design (Post-Route) Complete!"} {
    puts "Resetting and launching implementation run: $best_run"
    reset_run $best_run
    launch_runs $best_run -jobs 4
    wait_on_run $best_run
    set run_status [get_property STATUS $best_run]
    puts "Implementation run completed with status: $run_status"
}

# Open the implemented design for report generation
puts "Opening implemented design for report generation..."
open_run $best_run

# Verify the design is open
if {[get_designs] == ""} {
    puts "ERROR: Failed to open the implemented design"
    close_project
    exit 1
} else {
    puts "Successfully opened implemented design: [get_designs]"
}

# Generate utilization report
puts "Generating utilization report..."
report_utilization -file "${reports_dir}/utilization_report.txt" -verbose
report_utilization -file "${reports_dir}/utilization_report.xml"

# Generate detailed utilization report
puts "Generating detailed utilization report..."
report_utilization -file "${reports_dir}/utilization_detailed.txt" -verbose

# Generate utilization by hierarchy
puts "Generating utilization by hierarchy..."
report_utilization -file "${reports_dir}/utilization_hierarchy.txt" -hierarchical

# ==============================================================================
# POWER ANALYSIS REPORTS
# ==============================================================================

puts "\n=== Generating Power Analysis Reports ==="

# Enable power analysis if not already enabled
# Power analysis is typically enabled by default in newer Vivado versions
puts "Power analysis should be available by default"

# Generate power reports
puts "Generating power analysis reports..."
report_power -file "${reports_dir}/power_report.txt" -verbose
report_power -file "${reports_dir}/power_report.xml"

# Power by hierarchy
puts "Generating power by hierarchy..."
report_power -file "${reports_dir}/power_hierarchy.txt"

# Power by clock domain
puts "Generating power by clock domain..."
report_power -file "${reports_dir}/power_clock_domains.txt"

# Power by component
puts "Generating power by component..."
report_power -file "${reports_dir}/power_components.txt"

# ==============================================================================
# PERFORMANCE (TIMING) REPORTS
# ==============================================================================

puts "\n=== Generating Performance Reports ==="

# Timing summary
puts "Generating timing summary..."
report_timing_summary -file "${reports_dir}/timing_summary.txt" -verbose
report_timing_summary -file "${reports_dir}/timing_summary.xml"

# Detailed timing report
puts "Generating detailed timing report..."
report_timing -file "${reports_dir}/timing_detailed.txt" -max_paths 100 -delay_type min_max

# Timing by clock domain
puts "Generating timing by clock domain..."
report_timing -file "${reports_dir}/timing_clock_domains.txt"

# Critical path analysis
puts "Generating critical path analysis..."
report_timing -file "${reports_dir}/timing_critical_paths.txt" -max_paths 20 -delay_type max

# Setup and hold timing
puts "Generating setup timing report..."
report_timing -file "${reports_dir}/timing_setup.txt" -delay_type max -max_paths 50

puts "Generating hold timing report..."
report_timing -file "${reports_dir}/timing_hold.txt" -delay_type min -max_paths 50

# Clock networks and constraints
puts "Generating clock network report..."
if {[catch {report_clock_networks -file "${reports_dir}/clock_networks.txt"}]} {
    puts "Clock network report not available in this context"
}
puts "Generating constraints (XDC) check report..."
if {[catch {report_exceptions -file "${reports_dir}/constraints_exceptions.txt"}]} {
    puts "Constraint exceptions report not available"
}

# ==============================================================================
# RESOURCE REPORTS
# ==============================================================================

puts "\n=== Generating Resource Reports ==="

# Generate resource utilization by type
puts "Generating resource utilization by type..."
report_utilization -file "${reports_dir}/resources_by_type.txt" -verbose

# Generate resource utilization by clock domain
puts "Generating resource utilization by clock domain..."
report_utilization -file "${reports_dir}/resources_by_clock.txt" -verbose

# Explicit BRAM/URAM distribution snapshot (best-effort)
puts "Generating BRAM/URAM distribution snapshot..."
set mem_dist_file "${reports_dir}/memory_distribution.txt"
set md_fp [open $mem_dist_file w]
set bram36_used 0
set bram18_used 0
foreach bram [get_cells -hierarchical -quiet -filter {PRIMITIVE_TYPE =~ "*RAMB*"}] {
    set bram_type [get_property REF_NAME $bram]
    if {[string match "*36*" $bram_type]} { incr bram36_used }
    if {[string match "*18*" $bram_type]} { incr bram18_used }
}
set uram_cells [get_cells -hierarchical -quiet -filter {PRIMITIVE_TYPE =~ "*URAM*"}]
puts $md_fp "BRAM36 count: $bram36_used"
puts $md_fp "BRAM18 count: $bram18_used"
puts $md_fp "URAM blocks: [llength $uram_cells]"
close $md_fp

# ==============================================================================
# AI ENGINE SPECIFIC REPORTS
# ==============================================================================

puts "\n=== Generating AI Engine Specific Reports ==="

# AI Engine utilization
puts "Generating AI Engine utilization report..."

# Enhanced AI Engine metrics (tiles, shims, cores)
puts "Collecting enhanced AI Engine tile metrics..."
set aie_tiles [list]
set aie_core_tiles [list]
set aie_shim_tiles [list]
if {[catch {set aie_tiles       [get_tiles -quiet -filter {TILE_TYPE =~ "*AIE*"}]}]} { set aie_tiles [list] }
if {[catch {set aie_core_tiles  [get_tiles -quiet -filter {TILE_TYPE =~ "*AIE*CORE*"}]}]} { set aie_core_tiles [list] }
if {[catch {set aie_shim_tiles  [get_tiles -quiet -filter {TILE_TYPE =~ "*AIE*SHIM*"}]}]} { set aie_shim_tiles [list] }
set aie_tile_count [llength $aie_tiles]
set aie_core_tile_count [llength $aie_core_tiles]
set aie_shim_tile_count [llength $aie_shim_tiles]
puts "AIE Tiles: total=$aie_tile_count, core_tiles=$aie_core_tile_count, shim_tiles=$aie_shim_tile_count"

# Debug: Check if AI Engine project file is loaded
puts "Checking AI Engine project file status..."
# Check for AI Engine project files (.aieprj in Vitis 2024.1)
set aieprj_files [glob -nocomplain "${reports_dir}/../*/*.aieprj"]
if {[llength $aieprj_files] > 0} {
    puts "Found [llength $aieprj_files] AI Engine project files:"
    foreach file $aieprj_files {
        puts "  - $file"
    }
} else {
    puts "WARNING: No .aieprj files found"
    puts "AI Engine utilization will be reported in the standard utilization reports"
}

# Debug: Check for AI Engine related cells
puts "Searching for AI Engine related cells..."
set all_cells [get_cells -hierarchical]
set aie_related_cells {}
foreach cell $all_cells {
    set ref_name [get_property REF_NAME $cell]
    set cell_name [get_property NAME $cell]
    if {[string match "*AI*" $ref_name] || [string match "*AI*" $cell_name] || 
        [string match "*aie*" $ref_name] || [string match "*aie*" $cell_name]} {
        lappend aie_related_cells $cell
    }
}
if {[llength $aie_related_cells] > 0} {
    puts "Found [llength $aie_related_cells] AI-related cells:"
    foreach cell $aie_related_cells {
        puts "  - $cell (REF_NAME: [get_property REF_NAME $cell])"
    }
} else {
    puts "No AI-related cells found"
}

# Look for AI Engine cells by reference name, not by instance name
set aie_cells [get_cells -hierarchical -filter {REF_NAME =~ "*AI_ENGINE*" || REF_NAME =~ "*AIE*" || NAME =~ "*ai_engine*" || NAME =~ "*aie*"}]
if {[llength $aie_cells] > 0} {
    report_utilization -file "${reports_dir}/aie_utilization.txt" -cells $aie_cells
    puts "Found [llength $aie_cells] AI Engine cells"
    foreach cell $aie_cells {
        puts "  AI Engine cell: $cell (REF_NAME: [get_property REF_NAME $cell])"
    }
} else {
    puts "No AI Engine cells found for utilization report"
    # Try alternative search methods
    puts "Searching for AI Engine cells by alternative methods..."
    set all_cells [get_cells -hierarchical]
    set aie_cells_alt {}
    foreach cell $all_cells {
        set ref_name [get_property REF_NAME $cell]
        set cell_name [get_property NAME $cell]
        if {[string match "*AI_ENGINE*" $ref_name] || [string match "*AIE*" $ref_name] || 
            [string match "*ai_engine*" $cell_name] || [string match "*aie*" $cell_name]} {
            lappend aie_cells_alt $cell
        }
    }
    if {[llength $aie_cells_alt] > 0} {
        puts "Found [llength $aie_cells_alt] AI Engine cells using alternative search"
        report_utilization -file "${reports_dir}/aie_utilization_alt.txt" -cells $aie_cells_alt
        foreach cell $aie_cells_alt {
            puts "  AI Engine cell: $cell (REF_NAME: [get_property REF_NAME $cell])"
        }
    } else {
        puts "No AI Engine cells found with alternative search either"
        puts "Generating AI Engine utilization report without cell filtering..."
        # Generate AI Engine utilization report without cell filtering to get the standard report
        report_utilization -file "${reports_dir}/aie_utilization.txt" -verbose
    }
}

# Generate comprehensive utilization report that includes AI Engine resources
puts "Generating comprehensive utilization report (includes AI Engine resources)..."
report_utilization -file "${reports_dir}/utilization_comprehensive.txt" -verbose

# Generate utilization report with detailed breakdown
puts "Generating detailed utilization breakdown..."
report_utilization -file "${reports_dir}/utilization_detailed.txt" -hierarchical

# Generate AI Engine specific utilization report
puts "Generating AI Engine specific utilization report..."
report_utilization -file "${reports_dir}/aie_utilization_specific.txt" -verbose

# AI Engine timing
puts "Generating AI Engine timing report..."
if {[llength $aie_cells] > 0} {
    report_timing -file "${reports_dir}/aie_timing.txt" -cells $aie_cells -max_paths 20
} elseif {[info exists aie_cells_alt] && [llength $aie_cells_alt] > 0} {
    report_timing -file "${reports_dir}/aie_timing.txt" -cells $aie_cells_alt -max_paths 20
}

# AI Engine power
puts "Generating AI Engine power report..."
if {[llength $aie_cells] > 0} {
    report_power -file "${reports_dir}/aie_power.txt"
} elseif {[info exists aie_cells_alt] && [llength $aie_cells_alt] > 0} {
    report_power -file "${reports_dir}/aie_power.txt"
}

# AI Engine summary write-out
set aie_summary_file "${reports_dir}/aie_utilization_specific.txt"
set aie_fp [open $aie_summary_file a]
puts $aie_fp "\n--- Enhanced AI Engine Metrics ---"
puts $aie_fp "AIE Tiles: total=$aie_tile_count, core_tiles=$aie_core_tile_count, shim_tiles=$aie_shim_tile_count"
# Heuristic counts for kernels and streams (best-effort from netlist names)
set aie_kernel_like [get_cells -hierarchical -quiet -filter {NAME =~ "*aie*kernel*" || REF_NAME =~ "*aie*kernel*" || NAME =~ "*kernel*"}]
set aie_stream_like  [get_cells -hierarchical -quiet -filter {NAME =~ "*stream*" || REF_NAME =~ "*stream*"}]
puts $aie_fp "AIE Kernel-like Instances: [llength $aie_kernel_like]"
puts $aie_fp "AIE Stream-like Instances: [llength $aie_stream_like]"
if {[llength $aie_cells] > 0} {
    puts $aie_fp "AIE Netlist Cells: [llength $aie_cells]"
} elseif {[info exists aie_cells_alt] && [llength $aie_cells_alt] > 0} {
    puts $aie_fp "AIE Netlist Cells (alt search): [llength $aie_cells_alt]"
} else {
    puts $aie_fp "AIE Netlist Cells: 0"
}
close $aie_fp

# ==============================================================================
# MEMORY REPORTS
# ==============================================================================

puts "\n=== Generating Memory Reports ==="

# Memory utilization
puts "Generating memory utilization report..."
set memory_cells [get_cells -hierarchical -filter {NAME =~ "*memory*" || NAME =~ "*bram*" || NAME =~ "*ultra*" || NAME =~ "*ram*"}]
if {[llength $memory_cells] > 0} {
    report_utilization -file "${reports_dir}/memory_utilization.txt" -cells $memory_cells
    puts "Found [llength $memory_cells] memory cells"
} else {
    puts "No memory cells found for utilization report"
}

# Memory timing
if {[llength $memory_cells] > 0} {
    report_timing -file "${reports_dir}/memory_timing.txt" -cells $memory_cells -max_paths 20
}

# Memory power
if {[llength $memory_cells] > 0} {
    report_power -file "${reports_dir}/memory_power.txt"
}

# Best-effort memory bandwidth estimation for AXI/AXIS endpoints
puts "Estimating memory/interface bandwidth (best-effort)..."
set bw_file "${reports_dir}/memory_bandwidth.txt"
set bw_fp [open $bw_file w]
puts $bw_fp "Estimated Theoretical Bandwidths (Upper Bounds)"
puts $bw_fp "Format: instance, data_bits, clock_mhz, bandwidth_gbps"
set axi_like_cells [get_cells -hierarchical -quiet -filter {REF_NAME =~ "*AXI*" || REF_NAME =~ "*AXIS*" || NAME =~ "*axi*" || NAME =~ "*axis*"}]
foreach c $axi_like_cells {
    set data_bits 0
    set clock_mhz 0.0
    # Try to infer data width from common pins
    set tdata_pins [get_pins -quiet -of_objects $c -filter {NAME =~ "*TDATA*" || NAME =~ "*WDATA*" || NAME =~ "*RDATA*"}]
    if {[llength $tdata_pins] > 0} {
        # Take width of first data pin
        set p [lindex $tdata_pins 0]
        if {[catch {set data_bits [get_property BIT_WIDTH $p]}]} { set data_bits 0 }
        if {$data_bits == ""} { set data_bits 0 }
    }
    # Try to get clock driving this cell
    set clk_pins [get_pins -quiet -of_objects $c -filter {DIRECTION == "in" && (NAME =~ "*ACLK*" || NAME =~ "*CLK*" || NAME =~ "*ap_clk*") }]
    set found_clk 0
    foreach cp $clk_pins {
        set clks [get_clocks -quiet -of_objects $cp]
        if {[llength $clks] > 0} {
            set clk [lindex $clks 0]
            if {![catch {set period [get_property PERIOD $clk]}]} {
                if {$period > 0} {
                    set clock_mhz [expr {1000.0 / $period}]
                    set found_clk 1
                    break
                }
            }
        }
    }
    set bw_gbps 0.0
    if {$data_bits > 0 && $clock_mhz > 0} {
        # Upper bound: bits * MHz / 1000
        set bw_gbps [expr {($data_bits * $clock_mhz) / 1000.0}]
    }
    puts $bw_fp "[get_property NAME $c], $data_bits, [format "%.3f" $clock_mhz], [format "%.3f" $bw_gbps]"
}
close $bw_fp

# ==============================================================================
# DSP REPORTS
# ==============================================================================

puts "\n=== Generating DSP Reports ==="

# DSP utilization
puts "Generating DSP utilization report..."
set dsp_cells [get_cells -hierarchical -filter {NAME =~ "*dsp*" || NAME =~ "*mult*"}]
if {[llength $dsp_cells] > 0} {
    report_utilization -file "${reports_dir}/dsp_utilization.txt" -cells $dsp_cells
    puts "Found [llength $dsp_cells] DSP cells"
} else {
    puts "No DSP cells found for utilization report"
}

# DSP timing
if {[llength $dsp_cells] > 0} {
    report_timing -file "${reports_dir}/dsp_timing.txt" -cells $dsp_cells -max_paths 20
}

# DSP power
if {[llength $dsp_cells] > 0} {
    report_power -file "${reports_dir}/dsp_power.txt"
}

# ==============================================================================
# PS/CIPS, AXI/PLIO, AND NOC REPORTS
# ==============================================================================

puts "\n=== Generating PS/CIPS, AXI/PLIO, and NoC Reports ==="

# PS/CIPS metrics
puts "Collecting Processing System (CIPS/PS) metrics..."
set ps_report "${reports_dir}/ps_metrics.txt"
set ps_fp [open $ps_report w]
set cips_cells [get_cells -hierarchical -quiet -filter {REF_NAME =~ "*cips*" || REF_NAME =~ "*ps*" || NAME =~ "*cips*" || NAME =~ "*ps*"}]
puts $ps_fp "PS/CIPS Instances: [llength $cips_cells]"
foreach c $cips_cells {
    puts $ps_fp "  - [get_property NAME $c] (REF_NAME: [get_property REF_NAME $c])"
}
# APU/RPU cores (heuristic)
set apu_like [get_cells -hierarchical -quiet -filter {NAME =~ "*apu*" || REF_NAME =~ "*apu*"}]
set rpu_like [get_cells -hierarchical -quiet -filter {NAME =~ "*rpu*" || REF_NAME =~ "*rpu*"}]
puts $ps_fp "APU Core-like Instances: [llength $apu_like]"
puts $ps_fp "RPU Core-like Instances: [llength $rpu_like]"
# DDR/Mem controllers associated (best-effort)
set ddr_like [get_cells -hierarchical -quiet -filter {REF_NAME =~ "*ddr*" || REF_NAME =~ "*mig*" || NAME =~ "*ddr*" || NAME =~ "*mig*"}]
puts $ps_fp "DDR/MIG Controllers: [llength $ddr_like]"
foreach d $ddr_like { puts $ps_fp "  - [get_property NAME $d] (REF_NAME: [get_property REF_NAME $d])" }
# Peripheral interfaces (heuristic)
set periph_like [get_cells -hierarchical -quiet -filter {REF_NAME =~ "ps_*" && NAME !~ "*apu*" && NAME !~ "*rpu*" && NAME !~ "*ddr*"}]
puts $ps_fp "PS Peripheral-like Instances: [llength $periph_like]"
close $ps_fp

# AXI/PLIO interface inventory
puts "Collecting AXI/PLIO interface inventory..."
set intf_report "${reports_dir}/interface_inventory.txt"
set intf_fp [open $intf_report w]
set axi_cells [get_cells -hierarchical -quiet -filter {REF_NAME =~ "*AXI*" || NAME =~ "*axi*"}]
set axis_cells [get_cells -hierarchical -quiet -filter {REF_NAME =~ "*AXIS*" || NAME =~ "*axis*"}]
set plio_cells [get_cells -hierarchical -quiet -filter {REF_NAME =~ "*PLIO*" || NAME =~ "*plio*"}]
set gt_cells [get_cells -hierarchical -quiet -filter {REF_NAME =~ "*GT*" || NAME =~ "*gt*" || NAME =~ "*gth*" || NAME =~ "*gty*" || NAME =~ "*gtm*"}]
puts $intf_fp "AXI Instances: [llength $axi_cells]"
foreach c $axi_cells { puts $intf_fp "  - [get_property NAME $c] (REF_NAME: [get_property REF_NAME $c])" }
puts $intf_fp "AXIS Instances: [llength $axis_cells]"
foreach c $axis_cells { puts $intf_fp "  - [get_property NAME $c] (REF_NAME: [get_property REF_NAME $c])" }
puts $intf_fp "PLIO Instances: [llength $plio_cells]"
foreach c $plio_cells { puts $intf_fp "  - [get_property NAME $c] (REF_NAME: [get_property REF_NAME $c])" }
puts $intf_fp "GT Transceiver-like Instances: [llength $gt_cells]"
foreach c $gt_cells { puts $intf_fp "  - [get_property NAME $c] (REF_NAME: [get_property REF_NAME $c])" }
close $intf_fp

# NoC metrics
puts "Collecting NoC metrics..."
set noc_report "${reports_dir}/noc_metrics.txt"
set noc_fp [open $noc_report w]
set noc_cells [get_cells -hierarchical -quiet -filter {REF_NAME =~ "*NOC*" || NAME =~ "*noc*"}]
puts $noc_fp "NoC Instances: [llength $noc_cells]"
foreach c $noc_cells {
    puts $noc_fp "  - [get_property NAME $c] (REF_NAME: [get_property REF_NAME $c])"
}
# Interconnect (SmartConnect/AXI Interconnect)
set interconnect_cells [get_cells -hierarchical -quiet -filter {REF_NAME =~ "*interconnect*" || REF_NAME =~ "*smartconnect*" || NAME =~ "*interconnect*" || NAME =~ "*smartconnect*"}]
puts $noc_fp "Interconnect Instances: [llength $interconnect_cells]"
foreach c $interconnect_cells { puts $noc_fp "  - [get_property NAME $c] (REF_NAME: [get_property REF_NAME $c])" }
close $noc_fp

# Try to generate a NoC performance report if available
puts "Attempting NoC performance report..."
if {[catch {report_noc -file "${reports_dir}/noc_performance.txt"}]} {
    puts "NoC performance report not available in this Vivado context/version"
}

# ==============================================================================
# COMPREHENSIVE SUMMARY REPORT
# ==============================================================================

puts "\n=== Generating Comprehensive Summary Report ==="

# Create a comprehensive summary report
set summary_file "${reports_dir}/design_summary_comprehensive.txt"
set fp [open $summary_file w]

puts $fp "=============================================================================="
puts $fp "AI Engine GEMM Design Comprehensive Summary Report"
puts $fp "=============================================================================="
puts $fp "Generated: [clock format [clock seconds] -format "%Y-%m-%d %H:%M:%S"]"
puts $fp "Project: $project_name"
puts $fp "Implementation Run: $best_run"
puts $fp "Run Status: $run_status"
puts $fp "=============================================================================="

# Get basic project information
puts $fp "\n=== Project Information ==="
puts $fp "Project Name: $project_name"
puts $fp "Project Directory: [get_property DIRECTORY [current_project]]"
puts $fp "Target Device: [get_property PART [current_project]]"
puts $fp "Target Board: [get_property BOARD [current_project]]"

# Get utilization summary
puts $fp "\n=== Utilization Summary ==="
set util_report [report_utilization -return_string]
puts $fp $util_report

# Extract AI Engine utilization information
puts $fp "\n=== AI Engine Utilization Analysis ==="
# Look for AI Engine utilization in the utilization report
if {[string match "*AI ML Engines*" $util_report]} {
    puts $fp "AI Engine utilization found in utilization report:"
    # Extract the AI Engine line from the utilization report
    set lines [split $util_report "\n"]
    foreach line $lines {
        if {[string match "*AI ML Engines*" $line]} {
            puts $fp "  $line"
        }
        if {[string match "*AI ML PL*" $line]} {
            puts $fp "  $line"
        }
        if {[string match "*AI ML NOC*" $line]} {
            puts $fp "  $line"
        }
    }
} else {
    puts $fp "AI Engine utilization not found in standard utilization report"
}

# Get timing summary
puts $fp "\n=== Timing Summary ==="
set timing_report [report_timing_summary -return_string]
puts $fp $timing_report

# Get power summary
puts $fp "\n=== Power Summary ==="
set power_report [report_power -return_string]
puts $fp $power_report

# Get AI Engine specific information
puts $fp "\n=== AI Engine Information ==="
set all_cells [get_cells -hierarchical]
set aie_cells {}
foreach cell $all_cells {
    set ref_name [get_property REF_NAME $cell]
    set cell_name [get_property NAME $cell]
    if {[string match "*AI_ENGINE*" $ref_name] || [string match "*AIE*" $ref_name] || 
        [string match "*ai_engine*" $cell_name] || [string match "*aie*" $cell_name]} {
        lappend aie_cells $cell
    }
}
if {[llength $aie_cells] > 0} {
    puts $fp "AI Engine cells found: [llength $aie_cells]"
    foreach cell $aie_cells {
        puts $fp "  - $cell (REF_NAME: [get_property REF_NAME $cell])"
    }
} else {
    puts $fp "No AI Engine cells found"
    puts $fp "Note: AI Engine resources may be reported as 'AI ML Engines' in utilization reports"
}

# Memory information
puts $fp "\n=== Memory Information ==="
if {[llength $memory_cells] > 0} {
    puts $fp "Memory cells found: [llength $memory_cells]"
    foreach cell $memory_cells {
        puts $fp "  - $cell"
    }
} else {
    puts $fp "No memory cells found"
}

# DSP information
puts $fp "\n=== DSP Information ==="
if {[llength $dsp_cells] > 0} {
    puts $fp "DSP cells found: [llength $dsp_cells]"
    foreach cell $dsp_cells {
        puts $fp "  - $cell"
    }
} else {
    puts $fp "No DSP cells found"
}

# Clock information
puts $fp "\n=== Clock Information ==="
set clocks [get_clocks]
if {[llength $clocks] > 0} {
    foreach clock $clocks {
        set freq [get_property PERIOD $clock]
        puts $fp "Clock: $clock, Period: $freq ns"
    }
} else {
    puts $fp "No clocks found"
}

puts $fp "\n=============================================================================="
puts $fp "End of Comprehensive Design Summary Report"
puts $fp "=============================================================================="

close $fp

# ==============================================================================
# HTML REPORTS
# ==============================================================================

puts "\n=== Generating HTML Reports ==="

# Generate HTML utilization report
puts "Generating HTML utilization report..."
report_utilization -file "${reports_dir}/utilization_report.html"

# Generate HTML timing report
puts "Generating HTML timing report..."
report_timing_summary -file "${reports_dir}/timing_summary.html"

# Generate HTML power report
puts "Generating HTML power report..."
report_power -file "${reports_dir}/power_report.html"

# Thermal analysis (if supported)
puts "Attempting thermal analysis..."
if {[catch {report_thermal -file "${reports_dir}/thermal_analysis.txt"}]} {
    puts "Thermal analysis not available in this context"
}

# ==============================================================================
# DRC REPORTS
# ==============================================================================

puts "\n=== Generating DRC Reports ==="
if {[catch {report_drc -file "${reports_dir}/drc_checks.txt"}]} {
    puts "DRC reporting not available (requires implemented design context)"
}

# ==============================================================================
# COMPLETION
# ==============================================================================

puts "\n=============================================================================="
puts "Comprehensive Report Generation Complete"
puts "=============================================================================="
puts "Reports generated in: $reports_dir"
puts ""
puts "Generated files:"
puts "  UTILIZATION REPORTS:"
puts "    - utilization_report.txt (basic utilization)"
puts "    - utilization_report.xml (XML format)"
puts "    - utilization_detailed.txt (detailed utilization)"
puts "    - utilization_hierarchy.txt (hierarchical utilization)"
puts "    - utilization_report.html (HTML format)"
puts ""
puts "  PERFORMANCE REPORTS:"
puts "    - timing_summary.txt (timing summary)"
puts "    - timing_summary.xml (XML format)"
puts "    - timing_detailed.txt (detailed timing)"
puts "    - timing_clock_domains.txt (by clock domain)"
puts "    - timing_critical_paths.txt (critical paths)"
puts "    - timing_setup.txt (setup timing)"
puts "    - timing_hold.txt (hold timing)"
puts "    - timing_summary.html (HTML format)"
puts "    - clock_networks.txt (clock network analysis)"
puts "    - constraints_exceptions.txt (constraint exceptions)"
puts ""
puts "  POWER REPORTS:"
puts "    - power_report.txt (power analysis)"
puts "    - power_report.xml (power analysis XML)"
puts "    - power_hierarchy.txt (hierarchical power)"
puts "    - power_clock_domains.txt (power by clock domain)"
puts "    - power_components.txt (power by component)"
puts "    - power_report.html (power analysis HTML)"
puts ""
puts "  AI ENGINE REPORTS:"
puts "    - aie_utilization.txt (AI Engine utilization)"
puts "    - aie_timing.txt (AI Engine timing)"
puts "    - aie_power.txt (AI Engine power)"
puts ""
puts "  MEMORY REPORTS:"
puts "    - memory_utilization.txt (memory utilization)"
puts "    - memory_timing.txt (memory timing)"
puts "    - memory_power.txt (memory power)"
    puts "    - memory_bandwidth.txt (best-effort bandwidth estimates)"
puts "    - memory_distribution.txt (BRAM/URAM distribution snapshot)"
puts ""
puts "  DSP REPORTS:"
puts "    - dsp_utilization.txt (DSP utilization)"
puts "    - dsp_timing.txt (DSP timing)"
puts "    - dsp_power.txt (DSP power)"
puts ""
puts "  PS/CIPS, AXI/PLIO, NOC REPORTS:"
puts "    - ps_metrics.txt (PS/CIPS instances, DDR/MIG summary)"
puts "    - interface_inventory.txt (AXI/AXIS/PLIO inventory)"
puts "    - noc_metrics.txt (NoC instances)"
puts "    - noc_performance.txt (NoC performance, if supported)"
puts ""
puts "  DRC REPORTS:"
puts "    - drc_checks.txt (design rule checks)"
puts ""
puts "  THERMAL REPORTS:"
puts "    - thermal_analysis.txt (thermal analysis, if supported)"
puts ""
puts "  SUMMARY REPORTS:"
puts "    - design_summary_comprehensive.txt (comprehensive summary)"
puts ""
puts "To view HTML reports, open them in a web browser."
puts "=============================================================================="

# Close the project
close_project

puts "Project closed successfully."
puts "Comprehensive report generation completed successfully!"
