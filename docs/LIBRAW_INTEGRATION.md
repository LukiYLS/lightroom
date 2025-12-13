# LibRaw Integration Guide

## Overview

This document explains how to integrate LibRaw library into the Lightroom Clone project to enable RAW format support.

## Downloading LibRaw

1. Visit the official LibRaw website: https://www.libraw.org/
2. Download the latest stable release (source code)
3. Extract the archive to a location accessible by the project

## Integration Steps

### Option 1: Static Library (Recommended)

1. **Build LibRaw as a static library:**
   ```bash
   # In LibRaw source directory
   ./configure
   make
   # This will create libraw.a (or libraw.lib on Windows)
   ```

2. **Add LibRaw to the project:**
   - Copy LibRaw headers to `third_party/libraw/include/`
   - Copy the static library to `third_party/libraw/lib/`

3. **Update project settings:**
   - Add `third_party/libraw/include` to Include Directories
   - Add `third_party/libraw/lib` to Library Directories
   - Add `libraw.lib` (or `libraw.a`) to Additional Dependencies

4. **Enable LibRaw in code:**
   - In `LibRawWrapper.h`, uncomment: `#define LIBRAW_AVAILABLE`
   - Or add to project preprocessor definitions: `LIBRAW_AVAILABLE=1`

### Option 2: Dynamic Library

1. **Build LibRaw as a DLL:**
   ```bash
   # Build as shared library
   ./configure --enable-shared
   make
   ```

2. **Copy DLL to output directory:**
   - Copy `libraw.dll` to `bin/Debug/` or `bin/Release/`

3. **Update project settings:**
   - Same as Option 1, but link against `libraw.dll` import library

### Option 3: Include Source Directly

1. **Copy LibRaw source to project:**
   - Copy LibRaw source files to `third_party/libraw/src/`
   - Add all `.cpp` files to the project

2. **Update project settings:**
   - Add `third_party/libraw/src` to Include Directories
   - No library linking needed

## Project Configuration

### Visual Studio Project Settings

1. **C/C++ → General → Additional Include Directories:**
   ```
   $(ProjectDir)third_party\libraw\include
   ```

2. **Linker → General → Additional Library Directories:**
   ```
   $(ProjectDir)third_party\libraw\lib
   ```

3. **Linker → Input → Additional Dependencies:**
   ```
   libraw.lib
   ```

4. **C/C++ → Preprocessor → Preprocessor Definitions:**
   ```
   LIBRAW_AVAILABLE
   ```

## Testing the Integration

After integration, test with a RAW file:

```cpp
// Test code
RAWImageLoader loader;
if (loader.CanLoad(L"test.CR2")) {
    auto texture = loader.Load(L"test.CR2", rhi);
    if (texture) {
        // Success!
    }
}
```

## Troubleshooting

### Common Issues

1. **"LibRaw not integrated" error:**
   - Ensure `LIBRAW_AVAILABLE` is defined
   - Check that LibRaw headers are in the include path

2. **Link errors:**
   - Verify library path is correct
   - Check library architecture (x64 vs x86)
   - Ensure all LibRaw dependencies are linked

3. **Runtime errors:**
   - Check that DLL is in the output directory (if using dynamic linking)
   - Verify file path encoding (UTF-8)

## License Notes

LibRaw is available under two licenses:
- **LGPL**: For open-source projects
- **CDDL**: Alternative license option

Ensure compliance with the chosen license.

## Current Status

- ✅ Code structure ready for LibRaw integration
- ✅ Wrapper class implemented
- ⏳ Waiting for LibRaw library download and integration
- ⏳ Project configuration pending

Once LibRaw is integrated, uncomment `#define LIBRAW_AVAILABLE` in `LibRawWrapper.h` or add it to project preprocessor definitions.



