# Lightroom Clone

A professional-grade image and video editing application built with WPF frontend and C++ backend, featuring GPU-accelerated rendering and real-time adjustments.

## Features

### Image Editing
- RAW image support via LibRaw
- Standard formats (JPEG, PNG, TIFF) via WIC
- Real-time adjustments: exposure, contrast, highlights, shadows, white balance, vibrance, saturation, sharpness, noise reduction, vignette, grain, HSL
- 3D LUT filters (.cube files) for cinematic color grading
- GPU-accelerated processing

### Video Editing
- Video format support (MP4, MOV, AVI, MKV) via FFmpeg
- Real-time video playback with frame-by-frame rendering
- Video adjustments: all image adjustment parameters apply to video frames
- Video export to HEVC format
- Automatic thumbnail generation

### User Interface
- Modern WPF UI with dark theme
- Filmstrip view for navigation
- Interactive zoom and pan controls
- Real-time histogram display

## Prerequisites

- Visual Studio 2022 with:
  - .NET desktop development workload
  - Desktop development with C++ workload
  - Windows 10/11 SDK

## Building

1. Clone the repository
2. Open `LightroomClone.sln` in Visual Studio 2022
3. Select **x64** platform
4. Build the solution (F6)
5. Run the application (F5)

## Usage

1. Click **"Select Folder"** to browse for images/videos
2. Click a thumbnail in the filmstrip to load it
3. Use the right panel to adjust parameters in real-time
4. Apply LUT filters from the filter selector
5. Use zoom controls (+/- buttons) to zoom in/out

## Project Structure

```
lightroom/
├── src/
│   ├── Lightroom.App/          # WPF UI Application (C#)
│   └── Lightroom.Core/          # Image/Video Processing SDK (C++)
│       ├── d3d11rhi/            # DirectX 11 RHI layer
│       ├── RenderNodes/         # Rendering nodes
│       ├── ImageProcessing/     # Image loading
│       └── VideoProcessing/     # Video decoding and export
├── third_party/
│   ├── ffmpeg/                  # FFmpeg libraries
│   └── libraw/                  # LibRaw for RAW support
└── resources/
    └── luts/                    # 3D LUT files
```

## Key Technologies

- WPF (.NET 8.0) for UI
- DirectX 11 for GPU rendering
- FFmpeg for video processing
- LibRaw for RAW image support
- HLSL shaders for image processing

## License

This is a prototype project for educational and research purposes.
