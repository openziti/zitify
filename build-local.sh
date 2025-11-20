#!/usr/bin/env bash
set -euo pipefail

# Build zitify using the openziti/ziti-builder:v3 Docker image
# Based on the GitHub Actions workflow in .github/workflows/build-release.yml
#
# Usage:
#   ./build-local.sh         # Incremental build (faster)
#   ./build-local.sh --clean # Clean build (slower, use when needed)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
CLEAN_BUILD=false

# Parse arguments
if [[ "${1:-}" == "--clean" ]]; then
    CLEAN_BUILD=true
    echo "🧹 Clean build requested"
fi

echo "Building zitify using openziti/ziti-builder:v3..."
echo "Source directory: ${SCRIPT_DIR}"
echo "Build directory: ${BUILD_DIR}"
echo ""

# Clean build directory only if requested or if it has permission issues
if [ -d "${BUILD_DIR}" ]; then
    if [ "$CLEAN_BUILD" = true ]; then
        echo "Cleaning existing build directory..."
        if ! rm -rf "${BUILD_DIR}" 2>/dev/null; then
            echo "❌ ERROR: Cannot remove build directory (permission denied)"
            echo "   The build directory contains files owned by a different user."
            echo "   Please remove it manually: rm -rf ${BUILD_DIR}"
            exit 1
        fi
    else
        echo "ℹ️  Using existing build directory for incremental build"
        echo "   (Use --clean flag to force a clean build)"
    fi
else
    echo "Creating new build directory..."
fi
echo ""

# Get current user's UID and GID
USER_ID=$(id -u)
GROUP_ID=$(id -g)

# Run the build inside the ziti-builder container with matching UID:GID
docker run --rm \
    --user "${USER_ID}:${GROUP_ID}" \
    -v "${SCRIPT_DIR}:/workspace" \
    -w /workspace \
    openziti/ziti-builder:v3 \
    bash -c "
        set -euo pipefail
        
        echo '=== Creating build directory ==='
        cmake -E make_directory /workspace/build
        
        echo ''
        echo '=== Configuring CMake with vcpkg ==='
        cmake -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_TOOLCHAIN_FILE=/usr/local/vcpkg/scripts/buildsystems/vcpkg.cmake \
              -S /workspace \
              -B /workspace/build
        
        echo ''
        echo '=== Building zitify ==='
        cmake --build /workspace/build --config Release
        
        echo ''
        echo '=== Creating package ==='
        cmake --build /workspace/build --config Release --target zitify-pack
        
        echo ''
        echo '=== Build complete ==='
        echo 'Artifacts location: /workspace/build/package/'
        ls -lh /workspace/build/package/ || echo 'No package directory found'
        
    "

echo ""
echo "✓ Build complete!"
echo ""
echo "Build artifacts are in: ${BUILD_DIR}"
echo ""

# List the built artifacts
if [ -d "${BUILD_DIR}/package" ]; then
    echo "Package contents:"
    ls -lh "${BUILD_DIR}/package/"
else
    echo "Built binaries:"
    find "${BUILD_DIR}" -type f -executable -o -name "*.so" -o -name "*.a" | head -20
fi
