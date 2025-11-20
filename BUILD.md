# Building Zitify

You can build zitify locally using the provided build script, which uses the `openziti/ziti-builder:v3` Docker image with all required dependencies.

## Prerequisites

- Docker
- Bash

## Build Steps

1. Run the build script:

   ```bash
   ./build-local.sh
   ```

   The script will:
   - Clean any existing build directory if `--clean` flag is used
   - Run the build inside the `openziti/ziti-builder:v3` container
   - Use vcpkg to fetch and compile all dependencies
   - Build zitify and create distribution tarball
   - Set correct file ownership for all artifacts

2. Find the build artifacts:

   ```bash
   ls -lh build/package/
   ```

   You'll find:
   - `zitify-VERSION-Linux-x86_64.tar.gz` - Distribution tarball
   - The tarball contains `libzitify.so*` (shared library and symlinks) and `zitify` (wrapper shell script)

## Testing Your Build

Extract and test the locally-built binaries:

```bash
# Extract the package
cd build/package
tar -xzf zitify-*.tar.gz

# Test with your identity
./zitify -i /path/to/identity.json your-command

# Or copy to a test location
mkdir -p ~/test-zitify
cp libzitify.so* zitify ~/test-zitify/
cd ~/test-zitify
./zitify -i /path/to/identity.json your-command
```

**Note**: The first build will take 5-10 minutes as vcpkg compiles all dependencies from source. Subsequent builds reusing the same `build/` directory will be much faster.
