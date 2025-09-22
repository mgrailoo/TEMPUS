#!/bin/bash
# ============================================================================
# Simple Root Filesystem Modification Script for ML Packages
# ============================================================================
# This script modifies the Versal Common Image rootfs to include PyTorch and NumPy
# pre-installed, based on the setup_ml_environment.sh approach.
#
# Usage: ./modify_rootfs_simple.sh
# ============================================================================

set -e

# Configuration
COMMON_IMAGE_VERSAL="/home/mgrailoo/xilinx-versal-common-v2024.1"
CUSTOM_ROOTFS_DIR="custom_rootfs_ml"
BACKUP_DIR="rootfs_backup"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging function
log() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Check if running as root for mount operations
check_root() {
    if [ "$EUID" -ne 0 ]; then
        error "This script requires root privileges for filesystem operations"
        error "Please run with: sudo ./modify_rootfs_simple.sh"
        exit 1
    fi
}

# Verify prerequisites
check_prerequisites() {
    log "Checking prerequisites..."
    
    # Check if common image exists
    if [ ! -d "$COMMON_IMAGE_VERSAL" ]; then
        error "Common image directory not found: $COMMON_IMAGE_VERSAL"
        error "Please verify the path in Mahdieh_env_setup.sh"
        exit 1
    fi
    
    # Check for rootfs files
    if [ -f "$COMMON_IMAGE_VERSAL/rootfs.ext4" ]; then
        ROOTFS_TYPE="ext4"
        ROOTFS_PATH="$COMMON_IMAGE_VERSAL/rootfs.ext4"
        log "Found rootfs.ext4 image"
    elif [ -d "$COMMON_IMAGE_VERSAL/rootfs" ]; then
        ROOTFS_TYPE="directory"
        ROOTFS_PATH="$COMMON_IMAGE_VERSAL/rootfs"
        log "Found rootfs directory"
    else
        error "No rootfs found in $COMMON_IMAGE_VERSAL"
        error "Expected either rootfs.ext4 or rootfs/ directory"
        exit 1
    fi
    
    success "Prerequisites check passed"
}

# Create backup of original rootfs
create_backup() {
    log "Creating backup of original rootfs..."
    
    mkdir -p "$BACKUP_DIR"
    
    if [ "$ROOTFS_TYPE" = "ext4" ]; then
        if [ ! -f "$BACKUP_DIR/rootfs.ext4.backup" ]; then
            cp "$ROOTFS_PATH" "$BACKUP_DIR/rootfs.ext4.backup"
            success "Backup created: $BACKUP_DIR/rootfs.ext4.backup"
        else
            warning "Backup already exists, skipping"
        fi
    else
        if [ ! -d "$BACKUP_DIR/rootfs.backup" ]; then
            cp -r "$ROOTFS_PATH" "$BACKUP_DIR/rootfs.backup"
            success "Backup created: $BACKUP_DIR/rootfs.backup"
        else
            warning "Backup already exists, skipping"
        fi
    fi
}

# Extract rootfs
extract_rootfs() {
    log "Extracting rootfs for modification..."
    
    # Clean up any existing custom rootfs
    if [ -d "$CUSTOM_ROOTFS_DIR" ]; then
        log "Removing existing custom rootfs directory..."
        rm -rf "$CUSTOM_ROOTFS_DIR"
    fi
    
    mkdir -p "$CUSTOM_ROOTFS_DIR"
    cd "$CUSTOM_ROOTFS_DIR"
    
    if [ "$ROOTFS_TYPE" = "ext4" ]; then
        log "Mounting ext4 image..."
        mkdir -p mnt
        mount -o loop "$ROOTFS_PATH" mnt
        log "Copying files from mounted image..."
        cp -a mnt/* ./
        umount mnt
        rmdir mnt
    else
        log "Copying rootfs directory..."
        cp -a "$ROOTFS_PATH"/* ./
    fi
    
    success "Rootfs extracted successfully"
}

# Install ML packages in rootfs using the same approach as setup_ml_environment.sh
install_ml_packages() {
    log "Installing ML packages in rootfs using setup_ml_environment.sh approach..."
    
    # Copy the setup_ml_environment.sh script into rootfs
    if [ -f "$SCRIPT_DIR/design/setup_ml_environment.sh" ]; then
        cp "$SCRIPT_DIR/design/setup_ml_environment.sh" ./
    else
        # Create the script inline if not found
        cat > setup_ml_environment.sh << 'EOF'
#!/bin/bash

# Complete Python ML Environment Setup and Test Script for PetaLinux Board
echo "=========================================="
echo "Setting up Python ML Environment"
echo "=========================================="

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

# Install NumPy first (compatible version for PyTorch 2.1.0)
echo "Installing NumPy (compatible version)..."
echo "Uninstalling any existing NumPy 2.x version..."
pip3 uninstall numpy -y 2>/dev/null || true
echo "Installing NumPy 1.x (compatible with PyTorch 2.1.0)..."
pip3 install "numpy<2.0" --no-cache-dir --force-reinstall

if [ $? -ne 0 ]; then
    echo "ERROR: NumPy installation failed."
    exit 1
fi
echo "✓ NumPy installed successfully."

# Install PyTorch
echo "Installing PyTorch for aarch64..."
pip3 install torch==2.1.0 torchvision==0.16.0 torchaudio==2.1.0 --index-url https://download.pytorch.org/whl/cpu --no-cache-dir

if [ $? -ne 0 ]; then
    echo "ERROR: PyTorch installation failed."
    exit 1
fi
echo "✓ PyTorch installed successfully."

# Install additional useful packages
echo "Installing additional ML packages..."
pip3 install scipy matplotlib pandas --no-cache-dir

if [ $? -ne 0 ]; then
    echo "WARNING: Some additional packages failed to install, but core packages are ready."
else
    echo "✓ Additional packages installed successfully."
fi

# Test installations
echo "Testing installations..."
python3 -c "import numpy; print('NumPy version:', numpy.__version__)"
python3 -c "import torch; print('PyTorch version:', torch.__version__)"

echo "✅ ML packages installed successfully!"
EOF
    fi
    
    chmod +x setup_ml_environment.sh
    
    # Set up chroot environment
    log "Setting up chroot environment..."
    
    # Mount necessary filesystems for chroot
    mount --bind /proc proc
    mount --bind /sys sys
    mount --bind /dev dev
    mount --bind /dev/pts dev/pts
    
    # Copy DNS configuration
    cp /etc/resolv.conf etc/resolv.conf
    
    # Install system dependencies first
    log "Installing system dependencies..."
    chroot . /bin/bash -c "
        apt-get update && \
        apt-get install -y python3 python3-pip python3-dev python3-venv && \
        apt-get install -y libopenblas-dev liblapack-dev libjpeg-dev zlib1g-dev && \
        apt-get install -y build-essential cmake pkg-config
    "
    
    # Run ML package installation in chroot
    log "Running ML package installation in chroot..."
    chroot . /bin/bash -c "cd / && ./setup_ml_environment.sh"
    
    # Cleanup chroot environment
    umount dev/pts || true
    umount dev || true
    umount sys || true
    umount proc || true
    
    # Remove installation script
    rm -f setup_ml_environment.sh
    
    success "ML packages installed successfully"
}

# Repackage rootfs
repackage_rootfs() {
    log "Repackaging rootfs..."
    
    if [ "$ROOTFS_TYPE" = "ext4" ]; then
        # Create new ext4 image
        log "Creating new ext4 image..."
        cd ..
        
        # Calculate size needed (add 50% overhead for ML packages)
        ORIGINAL_SIZE=$(du -s "$CUSTOM_ROOTFS_DIR" | cut -f1)
        NEW_SIZE=$((ORIGINAL_SIZE * 150 / 100))
        
        # Create new ext4 image
        dd if=/dev/zero of="rootfs_ml.ext4" bs=1M count=$((NEW_SIZE / 1024))
        mkfs.ext4 -F "rootfs_ml.ext4"
        
        # Mount and copy files
        mkdir -p mnt_new
        mount -o loop "rootfs_ml.ext4" mnt_new
        cp -a "$CUSTOM_ROOTFS_DIR"/* mnt_new/
        umount mnt_new
        rmdir mnt_new
        
        # Replace original rootfs
        mv "rootfs_ml.ext4" "$COMMON_IMAGE_VERSAL/rootfs.ext4"
        success "New rootfs.ext4 created with ML packages"
    else
        # For directory-based rootfs, just replace the directory
        log "Replacing rootfs directory..."
        rm -rf "$ROOTFS_PATH"
        mv "$CUSTOM_ROOTFS_DIR" "$ROOTFS_PATH"
        success "Rootfs directory updated with ML packages"
    fi
}

# Create verification script
create_verification_script() {
    log "Creating verification script..."
    
    cat > verify_ml_installation.sh << 'EOF'
#!/bin/bash
# ML Package Verification Script
# Run this inside the emulated system to verify ML packages

echo "🔍 Verifying ML package installation..."
echo "======================================"

# Check Python version
echo "Python version:"
python3 --version

# Check NumPy
echo -n "NumPy: "
python3 -c "import numpy; print(numpy.__version__)" 2>/dev/null || echo "NOT FOUND"

# Check PyTorch
echo -n "PyTorch: "
python3 -c "import torch; print(torch.__version__)" 2>/dev/null || echo "NOT FOUND"

# Test basic functionality
echo ""
echo "Testing basic functionality..."

# NumPy test
python3 -c "
import numpy as np
a = np.array([1, 2, 3, 4, 5])
print('NumPy test: sum =', np.sum(a))
"

# PyTorch test
python3 -c "
import torch
x = torch.tensor([1.0, 2.0, 3.0])
print('PyTorch test: tensor =', x)
print('PyTorch test: sum =', torch.sum(x))
"

echo ""
echo "✅ ML package verification complete!"
EOF

    chmod +x verify_ml_installation.sh
    success "Verification script created: verify_ml_installation.sh"
}

# Cleanup function
cleanup() {
    log "Cleaning up temporary files..."
    
    # Unmount any remaining mounts
    umount "$CUSTOM_ROOTFS_DIR"/proc 2>/dev/null || true
    umount "$CUSTOM_ROOTFS_DIR"/sys 2>/dev/null || true
    umount "$CUSTOM_ROOTFS_DIR"/dev 2>/dev/null || true
    umount "$CUSTOM_ROOTFS_DIR"/dev/pts 2>/dev/null || true
    
    # Remove temporary directory
    if [ -d "$CUSTOM_ROOTFS_DIR" ]; then
        rm -rf "$CUSTOM_ROOTFS_DIR"
    fi
    
    success "Cleanup completed"
}

# Main execution
main() {
    echo "============================================================================"
    echo "🔧 Simple Root Filesystem Modification for ML Packages"
    echo "============================================================================"
    echo ""
    
    # Check if we're in the right directory
    if [ ! -f "Mahdieh_env_setup.sh" ]; then
        error "Please run this script from the project root directory"
        exit 1
    fi
    
    # Set up trap for cleanup on exit
    trap cleanup EXIT
    
    # Execute steps
    check_root
    check_prerequisites
    create_backup
    extract_rootfs
    install_ml_packages
    repackage_rootfs
    create_verification_script
    
    echo ""
    echo "============================================================================"
    success "Root filesystem modification completed successfully!"
    echo "============================================================================"
    echo ""
    echo "Next steps:"
    echo "1. Run your build: make run TARGET=hw_emu"
    echo "2. In the emulator, run: ./verify_ml_installation.sh"
    echo "3. Test ML functionality with your PyTorch/NumPy benchmarks"
    echo ""
    echo "Backup location: $BACKUP_DIR/"
    echo "To restore original rootfs:"
    if [ "$ROOTFS_TYPE" = "ext4" ]; then
        echo "  sudo cp $BACKUP_DIR/rootfs.ext4.backup $COMMON_IMAGE_VERSAL/rootfs.ext4"
    else
        echo "  sudo rm -rf $COMMON_IMAGE_VERSAL/rootfs"
        echo "  sudo cp -r $BACKUP_DIR/rootfs.backup $COMMON_IMAGE_VERSAL/rootfs"
    fi
    echo ""
}

# Run main function
main "$@"
