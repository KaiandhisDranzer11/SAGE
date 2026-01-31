#!/bin/bash
# Build script for SAGE

set -e

echo "Building SAGE..."

# Create build directory
mkdir -p build
cd build

# Configure with Release flags
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build all targets
make -j$(nproc)

echo "Build complete!"
echo "Binaries:"
echo "  - build/src/cal/sage_cal"
echo "  - build/src/ade/sage_ade"
echo "  - build/src/rme/sage_rme"
echo "  - build/src/poe/sage_poe"
echo "  - build/tests/test_core"
