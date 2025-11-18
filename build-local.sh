#!/usr/bin/env bash
set -euo pipefail

# Build zitify using the openziti/ziti-builder:v3 Docker image
# Based on the GitHub Actions workflow in .github/workflows/build-release.yml
#
# Features:
#   - Automatically detects and fixes CMake cache path mismatches
#   - Preserves compiled artifacts for faster incremental builds
#   - Only reconfigures CMake when paths change
#
# Usage:
#   ./build-local.sh         # Incremental build (faster, auto-fixes cache issues)
#   ./build-local.sh --clean # Full clean build (slower, use when needed)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
CLEAN_BUILD=false

# Parse arguments
if [[ "${1:-}" == "--clean" ]]; then
    CLEAN_BUILD=true
    echo "🧹 Clean build requested"
fi

echo "Pulling latest openziti/ziti-builder:v3 image..."
if ! docker pull openziti/ziti-builder:v3; then
    echo "❌ ERROR: Failed to pull openziti/ziti-builder:v3"
    exit 1
fi

echo "Building zitify using openziti/ziti-builder:v3..."
echo "Source directory: ${SCRIPT_DIR}"
echo "Build directory: ${BUILD_DIR}"
echo ""

# Check for CMake cache path mismatch and clean if needed
if [ -d "${BUILD_DIR}" ]; then
    CACHE_MISMATCH=false
    
    # Check main cache if it exists
    if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
        CACHED_SOURCE=$(grep "CMAKE_HOME_DIRECTORY:INTERNAL=" "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null | cut -d'=' -f2)
        CACHED_BUILD=$(grep "CMAKE_CACHEFILE_DIR:INTERNAL=" "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null | cut -d'=' -f2)
        
        # We build in Docker where paths are /workspace, but cache might have host paths
        if [ -n "$CACHED_SOURCE" ] && [[ "$CACHED_SOURCE" != "/workspace"* ]]; then
            CACHE_MISMATCH=true
            echo "⚠️  Detected CMake cache path mismatch in main cache:"
            echo "   Cached source:  $CACHED_SOURCE"
            echo "   Docker path:    /workspace"
        elif [ -n "$CACHED_BUILD" ] && [[ "$CACHED_BUILD" != "/workspace"* ]]; then
            CACHE_MISMATCH=true
            echo "⚠️  Detected CMake cache path mismatch in main cache:"
            echo "   Cached build:   $CACHED_BUILD"
            echo "   Docker path:    /workspace/build"
        fi
    fi
    
    # Also check for any nested caches with host paths (e.g., in _deps/)
    if find "${BUILD_DIR}" -type f -name "CMakeCache.txt" -exec grep -l "CMAKE_HOME_DIRECTORY:INTERNAL=/home" {} \; 2>/dev/null | grep -q .; then
        CACHE_MISMATCH=true
        if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
            echo "   Also found nested CMake caches with host paths"
        else
            echo "⚠️  Detected CMake cache path mismatch in nested caches (e.g., _deps/)"
        fi
    fi
    
    # Clean all caches if mismatch detected
    if [ "$CACHE_MISMATCH" = true ]; then
        echo "   → Automatically cleaning all CMake caches to prevent build errors"
        # Clean all CMake caches recursively
        find "${BUILD_DIR}" -type f -name "CMakeCache.txt" -delete 2>/dev/null || true
        find "${BUILD_DIR}" -type d -name "CMakeFiles" -exec rm -rf {} + 2>/dev/null || true
        # Also clean _deps/ directory since FetchContent will recreate it with correct paths
        if [ -d "${BUILD_DIR}/_deps" ]; then
            echo "   → Cleaning _deps/ directory (FetchContent will recreate it)"
            rm -rf "${BUILD_DIR}/_deps" 2>/dev/null || true
        fi
        echo "   ✓ All CMake caches cleaned, will reconfigure"
    fi
fi

# Clean build directory only if requested
if [ -d "${BUILD_DIR}" ]; then
    if [ "$CLEAN_BUILD" = true ]; then
        echo "🧹 Cleaning existing build directory..."
        if ! rm -rf "${BUILD_DIR}" 2>/dev/null; then
            echo "❌ ERROR: Cannot remove build directory (permission denied)"
            echo "   The build directory contains files owned by a different user."
            echo "   Please remove it manually: rm -rf ${BUILD_DIR}"
            exit 1
        fi
    else
        echo "ℹ️  Using existing build directory for incremental build"
        echo "   (Use --clean flag to force a full clean build)"
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
