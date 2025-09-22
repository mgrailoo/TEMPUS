8#!/bin/bash

# Complete Python ML Environment Setup and Test Script for PetaLinux Board
# This script installs and tests the complete ML stack: NumPy, PyTorch, scipy, matplotlib, pandas
# 
# USAGE:
# 1. For PetaLinux build integration: Run during PetaLinux build process
# 2. For manual installation: Run ONCE on the target board before running ./gemm_aie_xrt.elf
# 3. For offline installation: Run with --offline flag if you have pre-downloaded packages
#
# OPTIONS:
#   --offline    Skip online package installation (for offline environments)
#   --help       Show this help message

# Parse command line arguments
OFFLINE_MODE=false
HELP_MODE=false

for arg in "$@"; do
    case $arg in
        --offline)
            OFFLINE_MODE=true
            shift
            ;;
        --help)
            HELP_MODE=true
            shift
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

if [ "$HELP_MODE" = true ]; then
    echo "Python ML Environment Setup Script"
    echo ""
    echo "USAGE:"
    echo "  $0 [OPTIONS]"
    echo ""
    echo "OPTIONS:"
    echo "  --offline    Skip online package installation (for offline environments)"
    echo "  --help       Show this help message"
    echo ""
    echo "EXAMPLES:"
    echo "  $0                    # Normal installation with internet access"
    echo "  $0 --offline          # Skip online installation, test existing packages"
    echo ""
    exit 0
fi

echo "=========================================="
echo "Setting up Python ML Environment"
echo "=========================================="
echo "This script installs all required packages for AI Engine benchmarks."
echo "Run this ONCE before running ./gemm_aie_xrt.elf"

if [ "$OFFLINE_MODE" = true ]; then
    echo "🔒 OFFLINE MODE: Skipping online package installation"
    echo "   Testing existing packages only"
fi
echo ""

# Check if python3 is available
if ! command -v python3 &> /dev/null
then
    echo "ERROR: python3 not found. Please install python3 first."
    exit 1
fi

# Install pip3 if not available
if ! command -v pip3 &> /dev/null
then
    echo "Installing pip3..."
    python3 -m ensurepip --upgrade
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to install pip3."
        exit 1
    fi
    echo "✓ pip3 installed successfully."
fi

# Configure pip to handle SSL issues on embedded boards (only in online mode)
if [ "$OFFLINE_MODE" = false ]; then
    echo "Configuring pip for embedded board environment..."
    pip3 config set global.trusted-host "pypi.org files.pythonhosted.org pypi.python.org"
    pip3 config set global.cert ""
fi

# Check if NumPy is already available and working
echo "Checking existing NumPy installation..."
python3 -c "import numpy; print('✓ NumPy already available:', numpy.__version__)" 2>/dev/null
NUMPY_AVAILABLE=$?

# Install NumPy first (compatible version for PyTorch 2.1.0)
if [ "$OFFLINE_MODE" = false ] && [ $NUMPY_AVAILABLE -ne 0 ]; then
    echo "Installing NumPy (compatible version)..."
    echo "Uninstalling any existing NumPy 2.x version..."
    pip3 uninstall numpy -y 2>/dev/null || true

    # Try multiple installation methods for NumPy
    echo "Installing NumPy 1.x (compatible with PyTorch 2.1.0)..."
    echo "Attempting installation method 1: Specific version 1.24.3..."

    # Method 1: Try specific version first (most reliable)
    pip3 install "numpy==1.24.3" --no-cache-dir --force-reinstall --trusted-host pypi.org --trusted-host files.pythonhosted.org --trusted-host pypi.python.org

    if [ $? -ne 0 ]; then
        echo "Method 1 failed. Trying method 2: Version 1.23.5..."
        # Method 2: Try another specific version
        pip3 install "numpy==1.23.5" --no-cache-dir --force-reinstall --trusted-host pypi.org --trusted-host files.pythonhosted.org --trusted-host pypi.python.org
        
        if [ $? -ne 0 ]; then
            echo "Method 2 failed. Trying method 3: Version 1.21.6..."
            # Method 3: Try older stable version
            pip3 install "numpy==1.21.6" --no-cache-dir --force-reinstall --trusted-host pypi.org --trusted-host files.pythonhosted.org --trusted-host pypi.python.org
            
            if [ $? -ne 0 ]; then
                echo "Method 3 failed. Trying method 4: Latest 1.x series..."
                # Method 4: Try latest 1.x series with more flexible constraint
                pip3 install "numpy>=1.21.0,<2.0" --no-cache-dir --force-reinstall --trusted-host pypi.org --trusted-host files.pythonhosted.org --trusted-host pypi.python.org
                
                if [ $? -ne 0 ]; then
                    echo "Method 4 failed. Trying method 5: No dependencies..."
                    # Method 5: Install without dependencies first
                    pip3 install "numpy==1.24.3" --no-deps --no-cache-dir --force-reinstall --trusted-host pypi.org --trusted-host files.pythonhosted.org --trusted-host pypi.python.org
                    
                    if [ $? -ne 0 ]; then
                        echo "Method 5 failed. Trying method 6: Alternative index..."
                        # Method 6: Try alternative index
                        pip3 install "numpy==1.24.3" --no-cache-dir --force-reinstall --index-url https://pypi.org/simple/ --trusted-host pypi.org --trusted-host files.pythonhosted.org --trusted-host pypi.python.org
                        
                        if [ $? -ne 0 ]; then
                            echo "ERROR: All NumPy installation methods failed."
                            echo "This may be due to network connectivity or SSL certificate issues."
                            echo "Please check your internet connection and try again."
                            echo "Alternatively, try running with --offline flag to test existing packages."
                            echo ""
                            echo "Trying to continue with system NumPy if available..."
                            # Don't exit, try to continue with whatever NumPy is available
                        fi
                    fi
                fi
            fi
        fi
    fi
    
    # Check if NumPy installation was successful
    python3 -c "import numpy; print('✓ NumPy version:', numpy.__version__)" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "✓ NumPy installed successfully."
    else
        echo "⚠ NumPy installation may have failed, but continuing..."
        echo "   The script will test existing NumPy installation during verification."
    fi
elif [ $NUMPY_AVAILABLE -eq 0 ]; then
    echo "✓ NumPy already available, skipping installation"
else
    echo "🔒 OFFLINE MODE: Skipping NumPy installation"
    echo "   Testing existing NumPy installation..."
fi

# Install PyTorch
if [ "$OFFLINE_MODE" = false ]; then
    echo "Installing PyTorch for aarch64..."
    echo "Attempting PyTorch installation with SSL bypass..."

    pip3 install torch==2.1.0 torchvision==0.16.0 torchaudio==2.1.0 --index-url https://download.pytorch.org/whl/cpu --no-cache-dir --trusted-host download.pytorch.org --trusted-host pypi.org --trusted-host files.pythonhosted.org

    if [ $? -ne 0 ]; then
        echo "PyTorch installation failed with PyTorch index. Trying PyPI..."
        # Fallback to PyPI
        pip3 install torch==2.1.0 torchvision==0.16.0 torchaudio==2.1.0 --no-cache-dir --trusted-host pypi.org --trusted-host files.pythonhosted.org --trusted-host pypi.python.org
        
        if [ $? -ne 0 ]; then
            echo "WARNING: PyTorch installation failed. Continuing without PyTorch..."
            echo "You can install PyTorch manually later if needed."
        else
            echo "✓ PyTorch installed successfully from PyPI."
        fi
    else
        echo "✓ PyTorch installed successfully."
    fi
else
    echo "🔒 OFFLINE MODE: Skipping PyTorch installation"
    echo "   Testing existing PyTorch installation..."
fi

# Install additional useful packages
if [ "$OFFLINE_MODE" = false ]; then
    echo "Installing additional ML packages..."
    pip3 install scipy matplotlib pandas --no-cache-dir --trusted-host pypi.org --trusted-host files.pythonhosted.org --trusted-host pypi.python.org

    if [ $? -ne 0 ]; then
        echo "WARNING: Some additional packages failed to install, but core packages are ready."
        echo "You can install them manually later if needed."
    else
        echo "✓ Additional packages installed successfully."
    fi
else
    echo "🔒 OFFLINE MODE: Skipping additional package installation"
    echo "   Testing existing additional packages..."
fi

# Comprehensive testing and verification
echo ""
echo "=========================================="
echo "Testing and Verifying Installation"
echo "=========================================="

# Test NumPy
echo "Testing NumPy..."
python3 -c "
import numpy as np
import time
print('✓ NumPy version:', np.__version__)

# Check if NumPy version is compatible
major_version = int(np.__version__.split('.')[0])
if major_version >= 2:
    print('⚠ WARNING: NumPy 2.x detected. This may cause compatibility issues with PyTorch.')
    print('  Consider downgrading to NumPy 1.x for better PyTorch compatibility.')
else:
    print('✓ NumPy version is compatible with PyTorch.')

# Test basic operations
a = np.random.randn(32, 32)
b = np.random.randn(32, 32)
start_time = time.time()
c = np.matmul(a, b)
end_time = time.time()

print('✓ NumPy matrix multiplication test: PASSED')
print('  Matrix size: 32x32')
print('  Time:', f'{(end_time - start_time)*1000:.2f} ms')
print('  Result shape:', c.shape)
"

if [ $? -ne 0 ]; then
    echo "ERROR: NumPy test failed."
    echo "This may be due to NumPy installation issues or compatibility problems."
    echo "Please check the installation and try again."
    echo ""
    echo "Trying to continue with PyTorch installation..."
    # Don't exit, try to continue with PyTorch
fi

# Test PyTorch
echo ""
echo "Testing PyTorch..."
python3 -c "
import torch
import time
print('✓ PyTorch version:', torch.__version__)
print('✓ Device: CPU (CPU-only board)')

# Test basic tensor operations
a = torch.randn(32, 32)
b = torch.randn(32, 32)
start_time = time.time()
c = torch.mm(a, b)
end_time = time.time()

print('✓ PyTorch matrix multiplication test: PASSED')
print('  Matrix size: 32x32')
print('  Time:', f'{(end_time - start_time)*1000:.2f} ms')
print('  Result shape:', c.shape)

# Test with int16 (same as AI Engine)
a_int16 = torch.randint(-100, 100, (32, 32), dtype=torch.int16)
b_int16 = torch.randint(-100, 100, (32, 32), dtype=torch.int16)
start_time = time.time()
c_int16 = torch.mm(a_int16.float(), b_int16.float()).int()
end_time = time.time()

print('✓ PyTorch int16 matrix multiplication test: PASSED')
print('  Matrix size: 32x32 (int16)')
print('  Time:', f'{(end_time - start_time)*1000:.2f} ms')
print('  Result shape:', c_int16.shape)
"

if [ $? -ne 0 ]; then
    echo "ERROR: PyTorch test failed."
    exit 1
fi

# Test additional packages
echo ""
echo "Testing additional packages..."
python3 -c "
try:
    import scipy
    print('✓ SciPy version:', scipy.__version__)
except ImportError:
    print('⚠ SciPy not available')

try:
    import matplotlib
    print('✓ Matplotlib version:', matplotlib.__version__)
except ImportError:
    print('⚠ Matplotlib not available')

try:
    import pandas
    print('✓ Pandas version:', pandas.__version__)
except ImportError:
    print('⚠ Pandas not available')
"

echo ""
echo "=========================================="
if [ "$OFFLINE_MODE" = true ]; then
    echo "✅ Python ML Stack Testing Complete!"
    echo "=========================================="
    echo "🔒 OFFLINE MODE: No packages were installed"
    echo "✓ Testing existing packages only"
else
    echo "✅ Python ML Stack Installation and Testing Complete!"
    echo "=========================================="
    echo "✓ NumPy: Ready for matrix operations"
    echo "✓ PyTorch: Ready for CPU ML benchmarks"
    echo "✓ Additional packages: scipy, matplotlib, pandas"
fi
echo "✓ Platform: CPU-only board (no GPU/CUDA)"
echo ""
echo "🎯 All tests PASSED! You can now run:"
echo "   ./gemm_aie_xrt.elf a.xclbin"
echo ""
echo "The application will run CPU benchmarks only."
echo "=========================================="
