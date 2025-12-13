# Lightroom Clone

A professional-grade image and video editing application built with WPF frontend and C++ backend, featuring GPU-accelerated rendering and real-time adjustments.

## Overview

Lightroom Clone is a desktop application that provides a Lightroom-like editing experience for both images and videos. It features a modern WPF user interface with a powerful C++ SDK backend that leverages DirectX 11 for GPU-accelerated image processing and rendering.

## Features

### Image Editing
- **RAW Image Support**: Full support for RAW formats via LibRaw
- **Standard Image Formats**: JPEG, PNG, TIFF, and more via WIC (Windows Imaging Component)
- **Real-time Adjustments**: 
  - Exposure, Contrast, Highlights, Shadows, Whites, Blacks
  - White Balance (Temperature, Tint)
  - Vibrance, Saturation
  - Sharpness, Noise Reduction
  - Vignette, Grain
  - HSL adjustments for individual color ranges
- **LUT Filters**: Support for 3D LUT (.cube) files for cinematic color grading
- **GPU-Accelerated Processing**: All adjustments processed in real-time on GPU

### Video Editing
- **Video Format Support**: MP4, MOV, AVI, MKV via FFmpeg
- **Real-time Video Playback**: Smooth video playback with frame-by-frame rendering
- **Video Adjustments**: All image adjustment parameters apply to video frames in real-time
- **Video Thumbnails**: Automatic thumbnail generation for video files in filmstrip view

### User Interface
- **Modern WPF UI**: Clean, professional interface with dark theme
- **Filmstrip View**: Thumbnail navigation for images and videos
- **Zoom & Pan**: Interactive zoom controls with aspect ratio preservation
- **Adjustment Panels**: Intuitive controls for all image/video parameters
- **Histogram Display**: Real-time histogram for exposure analysis

### Technical Features
- **GPU Rendering Pipeline**: DirectX 11 based rendering with D3D11 to D3D9 interop for WPF
- **Modular Architecture**: Render graph system with pluggable processing nodes
- **RHI Abstraction**: High-level rendering interface for DirectX 11 operations
- **Zero-Copy Texture Sharing**: Efficient texture sharing between D3D11 and D3D9

## Project Structure

```
lightroom/
├── src/
│   ├── Lightroom.App/              # WPF UI Application (C# .NET 8.0)
│   │   ├── Controls/                # UI Controls
│   │   │   ├── FilmstripView.*      # Thumbnail filmstrip
│   │   │   ├── ImageEditorView.*    # Main image/video editor
│   │   │   ├── LeftPanel.*          # Folder browser
│   │   │   ├── RightPanel.*         # Adjustment controls
│   │   │   ├── TopMenuBar.*         # Menu bar
│   │   │   └── ThumbnailItem.*      # Thumbnail item
│   │   └── Core/
│   │       └── NativeMethods.cs     # P/Invoke declarations
│   │
│   └── Lightroom.Core/              # Image/Video Processing SDK (C++)
│       ├── d3d11rhi/                # DirectX 11 RHI abstraction layer
│       ├── RenderNodes/              # Rendering nodes
│       │   ├── ImageAdjustNode.*     # Image adjustments (exposure, contrast, etc.)
│       │   ├── FilterNode.*          # LUT filter application
│       │   ├── ScaleNode.*           # Scaling, zoom, pan
│       │   └── RenderNode.*         # Base render node class
│       ├── ImageProcessing/          # Image loading and processing
│       │   └── ImageProcessor.*     # WIC-based image loading
│       ├── VideoProcessing/          # Video decoding and processing
│       │   ├── FFmpegVideoLoader.*   # FFmpeg video decoder
│       │   ├── VideoProcessor.*      # Video processing wrapper
│       │   └── LightroomSDK_Video.* # Video SDK APIs
│       ├── D3D9Interop.*             # D3D11 to D3D9 interop for WPF
│       ├── RenderGraph.*             # Render graph management
│       ├── RenderTargetManager.*     # Render target lifecycle
│       └── LightroomSDK.*            # Main SDK API
│
├── third_party/
│   ├── ffmpeg/                      # FFmpeg libraries (headers, libs, DLLs)
│   └── libraw/                      # LibRaw for RAW image support
│
├── resources/
│   └── luts/                        # 3D LUT files (.cube)
│
├── docs/                            # Documentation
│   ├── VIDEO_SUPPORT_ARCHITECTURE.md
│   ├── VIDEO_DECODER_IMPLEMENTATION.md
│   └── TROUBLESHOOTING_DLL.md
│
└── LightroomClone.sln              # Visual Studio solution
```

## Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    WPF Application (C#)                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │   UI Controls │  │  Event       │  │  P/Invoke     │    │
│  │   (XAML)      │  │  Handling    │  │  Wrappers     │    │
│  └──────────────┘  └──────────────┘  └──────┬────────┘    │
└───────────────────────────────────────────────┼────────────┘
                                                 │
                                    ┌────────────▼────────────┐
                                    │  Lightroom Core SDK    │
                                    │       (C++)            │
                                    └────────────┬───────────┘
                                                 │
        ┌────────────────────────────────────────┼────────────────────────┐
        │                                        │                        │
┌───────▼────────┐                    ┌─────────▼──────────┐   ┌─────────▼──────────┐
│ Image          │                    │ Video              │   │ Render            │
│ Processor      │                    │ Processor          │   │ Graph             │
│ (WIC/LibRaw)   │                    │ (FFmpeg)           │   │ (Nodes)           │
└───────┬────────┘                    └─────────┬──────────┘   └─────────┬──────────┘
        │                                        │                        │
        └────────────────────────────────────────┴────────────────────────┘
                                         │
                            ┌────────────▼────────────┐
                            │   D3D11 RHI Layer      │
                            │  (Abstraction Layer)    │
                            └────────────┬────────────┘
                                         │
                            ┌────────────▼────────────┐
                            │   DirectX 11 API       │
                            │   (GPU Rendering)       │
                            └─────────────────────────┘
```

### Rendering Pipeline

1. **Media Loading**:
   - Images: WIC (Windows Imaging Component) or LibRaw for RAW files
   - Videos: FFmpeg for decoding video frames
   - Both are loaded into D3D11 textures

2. **Render Graph Execution**:
   - `ImageAdjustNode`: Applies exposure, contrast, highlights, shadows, white balance, etc.
   - `FilterNode`: Applies 3D LUT filters for color grading
   - `ScaleNode`: Handles scaling, zoom, pan, and aspect ratio preservation
   - Nodes are chained together in a render graph

3. **D3D11 to D3D9 Interop**:
   - Shared textures between D3D11 and D3D9
   - GPU-accelerated copy to D3D9 surface for WPF display

4. **WPF Display**:
   - D3DImage displays the final rendered result
   - Real-time updates as adjustments are made

## Prerequisites

### Required Software
- **Visual Studio 2022** with:
  - .NET desktop development workload
  - Desktop development with C++ workload
  - Windows 10/11 SDK

### Required Libraries
- **FFmpeg**: Pre-built libraries in `third_party/ffmpeg/`
  - Headers: `third_party/ffmpeg/include/`
  - Libraries: `third_party/ffmpeg/lib/`
  - DLLs: `third_party/ffmpeg/bin/` (copied to output directory)
- **LibRaw**: Pre-built libraries in `third_party/libraw/`
  - DLL: `third_party/libraw/buildfiles/release-x86_64/libraw.dll`

## Building the Project

### 1. Clone the Repository
```bash
git clone <repository-url>
cd lightroom
```

### 2. Verify Dependencies
Ensure the following directories exist:
- `third_party/ffmpeg/include/` - FFmpeg headers
- `third_party/ffmpeg/lib/` - FFmpeg libraries (.lib files)
- `third_party/ffmpeg/bin/` - FFmpeg DLLs
- `third_party/libraw/buildfiles/release-x86_64/libraw.dll` - LibRaw DLL

If FFmpeg is missing, see `third_party/ffmpeg/README_SETUP.md` for setup instructions.

### 3. Open in Visual Studio
```bash
# Open LightroomClone.sln in Visual Studio 2022
```

### 4. Configure Build
- Select **x64** platform (required for C++ SDK)
- Select **Debug** or **Release** configuration
- Ensure `Lightroom.App` is set as the startup project

### 5. Build
- Press **F6** or **Build → Build Solution**
- The build process will:
  - Compile the C++ SDK (`Lightroom.Core`)
  - Compile the WPF application (`Lightroom.App`)
  - Copy native DLLs to the output directory:
    - `LightroomCore.dll`
    - `libraw.dll`
    - FFmpeg DLLs (from `third_party/ffmpeg/bin/`)

### 6. Run
- Press **F5** to run the application
- Or set `Lightroom.App` as startup project and run

## Usage

### Loading Images
1. Click **"Select Folder"** in the left panel
2. Browse and select a folder containing images
3. Thumbnails appear in the bottom filmstrip view
4. Click a thumbnail to load it into the center editor

### Loading Videos
1. Select a folder containing video files (MP4, MOV, AVI, MKV)
2. Video thumbnails appear in the filmstrip with a play icon overlay
3. Click a video thumbnail to load and play it
4. Videos play automatically and can be paused/resumed

### Image/Video Adjustments
Use the right panel to adjust parameters:
- **Basic**: Exposure, Contrast, Highlights, Shadows, Whites, Blacks
- **Color**: Temperature, Tint, Vibrance, Saturation
- **Detail**: Sharpness, Noise Reduction
- **Effects**: Vignette, Grain

All adjustments apply in real-time to both images and videos.

### Applying Filters
1. Use the filter selector in the right panel
2. Choose from available 3D LUT files in `resources/luts/`
3. Adjust filter intensity with the slider

### Zoom Controls
- **+/- Buttons**: Zoom in/out
- **1:1 Button**: Fit image to window
- **Pan**: Click and drag to pan when zoomed in

## Key Technologies

- **WPF (Windows Presentation Foundation)**: Modern UI framework
- **.NET 8.0**: Latest .NET framework
- **DirectX 11**: GPU-accelerated rendering
- **DirectX 9 Interop**: Texture sharing with WPF D3DImage
- **FFmpeg**: Video decoding and processing
- **LibRaw**: RAW image format support
- **WIC (Windows Imaging Component)**: Standard image format support
- **HLSL Shaders**: GPU shaders for image processing
- **P/Invoke**: C# to C++ interop

## Technical Highlights

### GPU-Accelerated Rendering
- All image processing happens on the GPU
- Real-time adjustments with no CPU overhead
- Efficient texture operations using DirectX 11

### Modular Render Graph
- Pluggable render nodes for flexible processing pipelines
- Easy to add new processing nodes
- Chain multiple nodes together for complex effects

### Aspect Ratio Preservation
- Automatic letterbox/pillarbox for different aspect ratios
- No image distortion
- Smart scaling algorithm

### Video Processing
- Frame-by-frame decoding via FFmpeg
- Real-time adjustment application to video frames
- Efficient texture management for video playback

## Troubleshooting

### DLL Loading Issues
If you encounter DLL loading errors, see `docs/TROUBLESHOOTING_DLL.md` for detailed troubleshooting steps.

Common issues:
- Missing `LightroomCore.dll`: Ensure C++ project built successfully
- Missing FFmpeg DLLs: Check `third_party/ffmpeg/bin/` exists and DLLs are copied
- Missing `libraw.dll`: Verify LibRaw DLL path in project settings
- Architecture mismatch: Ensure all DLLs are x64

### Build Errors
- **C++ compilation errors**: Check file encoding (should be UTF-8 with BOM)
- **Linker errors**: Verify all library paths are correct
- **P/Invoke errors**: Ensure function signatures match between C# and C++

## Development

### Adding New Adjustment Parameters
1. Add parameter to `ImageAdjustParams` struct in `LightroomSDKTypes.h`
2. Update `ImageAdjustNode` shader to apply the adjustment
3. Add UI control in `RightPanel.xaml`
4. Wire up event handler in `MainWindow.xaml.cs`

### Adding New Render Nodes
1. Create new class inheriting from `RenderNode`
2. Implement `Execute()` method
3. Add HLSL shader code for processing
4. Register node in render graph

### Video Format Support
Video formats are handled by FFmpeg. To add support for new formats:
1. Ensure FFmpeg is compiled with the required codec
2. Update `IsVideoFormat()` in `LightroomSDK_Video.cpp` if needed

## Future Enhancements

- [ ] Undo/Redo functionality
- [ ] Image export with quality settings
- [ ] Batch processing support
- [ ] Video export functionality
- [ ] Additional adjustment parameters
- [ ] Custom LUT creation tools
- [ ] Multi-threaded processing
- [ ] Performance optimizations

## License

This is a prototype project for educational and research purposes.

## Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

## Acknowledgments

- **FFmpeg**: Video decoding library
- **LibRaw**: RAW image processing library
- **DirectX**: GPU rendering APIs
