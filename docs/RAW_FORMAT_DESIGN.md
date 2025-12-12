# RAW Format Support Design Document

## Overview

This document outlines the design for adding RAW image format support to the Lightroom Clone Framework. RAW support will enable professional photographers to edit unprocessed camera sensor data with full control over image processing parameters.

## Current Architecture

```
┌─────────────────────────────────────────┐
│         WPF Application (C#)            │
└──────────────────┬──────────────────────┘
                    │ P/Invoke
┌───────────────────▼──────────────────────┐
│      Lightroom Core SDK (C++)            │
│  ┌──────────────────────────────────┐   │
│  │ ImageProcessor                    │   │
│  │  - WIC-based loading              │   │
│  │  - BGRA8 conversion               │   │
│  └──────────────┬─────────────────────┘   │
│                 │                          │
│  ┌──────────────▼─────────────────────┐   │
│  │ RenderGraph                         │   │
│  │  - ScaleNode                        │   │
│  │  - BrightnessContrastNode          │   │
│  │  - PassthroughNode                 │   │
│  └────────────────────────────────────┘   │
│                 │                          │
│  ┌──────────────▼─────────────────────┐   │
│  │ D3D11 RHI Layer                    │   │
│  └────────────────────────────────────┘   │
└────────────────────────────────────────────┘
```

## Design Goals

1. **Seamless Integration**: RAW support should integrate with existing architecture
2. **Performance**: GPU-accelerated RAW processing where possible
3. **Flexibility**: Support multiple RAW formats (CR2, NEF, ARW, DNG, etc.)
4. **Quality**: Professional-grade demosaicing and color processing
5. **Extensibility**: Easy to add new RAW formats and processing algorithms

## Architecture Design

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    WPF Application                       │
│  (RAW-specific UI controls: WB, Exposure, etc.)        │
└────────────────────┬────────────────────────────────────┘
                     │ P/Invoke
┌────────────────────▼────────────────────────────────────┐
│              Lightroom Core SDK (C++)                   │
│  ┌──────────────────────────────────────────────────┐  │
│  │ ImageProcessor (Factory Pattern)                 │  │
│  │  ├── StandardImageLoader (WIC)                   │  │
│  │  └── RAWImageLoader (libraw/dcraw)               │  │
│  └──────────────┬───────────────────────────────────┘  │
│                 │                                       │
│  ┌──────────────▼───────────────────────────────────┐  │
│  │ RAWProcessor                                     │  │
│  │  - Demosaicing (GPU shader)                     │  │
│  │  - White Balance                                │  │
│  │  - Exposure Compensation                        │  │
│  │  - Color Space Conversion                       │  │
│  └──────────────┬───────────────────────────────────┘  │
│                 │                                       │
│  ┌──────────────▼───────────────────────────────────┐  │
│  │ RenderGraph                                       │  │
│  │  - RAWDevelopNode (new)                          │  │
│  │  - ScaleNode                                      │  │
│  │  - BrightnessContrastNode                        │  │
│  └───────────────────────────────────────────────────┘  │
│                 │                                       │
│  ┌──────────────▼───────────────────────────────────┐  │
│  │ D3D11 RHI Layer                                  │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## Component Design

### 1. ImageLoader Interface (Strategy Pattern)

```cpp
// ImageLoader.h
namespace LightroomCore {

enum class ImageFormat {
    Unknown,
    Standard,  // JPEG, PNG, etc. (WIC)
    RAW        // CR2, NEF, ARW, DNG, etc.
};

class IImageLoader {
public:
    virtual ~IImageLoader() = default;
    virtual bool CanLoad(const std::wstring& filePath) = 0;
    virtual std::shared_ptr<RenderCore::RHITexture2D> Load(
        const std::wstring& filePath,
        std::shared_ptr<RenderCore::DynamicRHI> rhi) = 0;
    virtual ImageFormat GetFormat() const = 0;
    virtual void GetImageInfo(uint32_t& width, uint32_t& height) const = 0;
};

} // namespace LightroomCore
```

### 2. RAWImageLoader Implementation

```cpp
// RAWImageLoader.h
namespace LightroomCore {

struct RAWImageInfo {
    uint32_t width;
    uint32_t height;
    uint32_t bayerPattern;  // RGGB, BGGR, GRBG, GBRG
    uint32_t bitsPerPixel;   // 12, 14, 16
    float whiteBalance[4];   // R, G1, B, G2 multipliers
    float colorMatrix[9];    // 3x3 color matrix
    std::string cameraModel;
    std::string lensModel;
    // ... other EXIF/metadata
};

class RAWImageLoader : public IImageLoader {
public:
    RAWImageLoader();
    ~RAWImageLoader();
    
    bool CanLoad(const std::wstring& filePath) override;
    std::shared_ptr<RenderCore::RHITexture2D> Load(
        const std::wstring& filePath,
        std::shared_ptr<RenderCore::DynamicRHI> rhi) override;
    ImageFormat GetFormat() const override { return ImageFormat::RAW; }
    void GetImageInfo(uint32_t& width, uint32_t& height) const override;
    
    // RAW-specific methods
    const RAWImageInfo& GetRAWInfo() const { return m_RAWInfo; }
    bool LoadRAWData(const std::wstring& filePath, 
                     std::vector<uint16_t>& rawData);  // 16-bit raw data
    
private:
    RAWImageInfo m_RAWInfo;
    // libraw or dcraw instance
    void* m_RAWProcessor;  // LibRaw* or similar
};

} // namespace LightroomCore
```

### 3. RAWProcessor (GPU-Accelerated Processing)

```cpp
// RAWProcessor.h
namespace LightroomCore {

struct RAWDevelopParams {
    float whiteBalance[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // R, G1, B, G2
    float exposure = 0.0f;  // EV adjustment
    float contrast = 0.0f;
    float saturation = 1.0f;
    float highlights = 0.0f;
    float shadows = 0.0f;
    float temperature = 5500.0f;  // Kelvin
    float tint = 0.0f;  // -150 to +150
    bool enableDemosaicing = true;
    uint32_t demosaicAlgorithm = 0;  // 0=bilinear, 1=AMaZE, 2=VNG, etc.
};

class RAWProcessor {
public:
    RAWProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~RAWProcessor();
    
    // Process RAW data to RGB texture
    std::shared_ptr<RenderCore::RHITexture2D> ProcessRAW(
        const std::vector<uint16_t>& rawData,
        const RAWImageInfo& rawInfo,
        const RAWDevelopParams& params);
    
private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    
    // GPU shaders for RAW processing
    std::shared_ptr<RenderCore::RHIPixelShader> m_DemosaicShader;
    std::shared_ptr<RenderCore::RHIPixelShader> m_WhiteBalanceShader;
    std::shared_ptr<RenderCore::RHIPixelShader> m_ExposureShader;
    // ... other processing shaders
};

} // namespace LightroomCore
```

### 4. RAWDevelopNode (Render Node)

```cpp
// RenderNodes/RAWDevelopNode.h
namespace LightroomCore {

class RAWDevelopNode : public RenderNode {
public:
    RAWDevelopNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~RAWDevelopNode();
    
    bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                 std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                 uint32_t width, uint32_t height) override;
    
    const char* GetName() const override { return "RAWDevelop"; }
    
    // Set RAW development parameters
    void SetDevelopParams(const RAWDevelopParams& params);
    void SetRAWInfo(const RAWImageInfo& rawInfo);
    
private:
    RAWDevelopParams m_Params;
    RAWImageInfo m_RAWInfo;
    std::unique_ptr<RAWProcessor> m_RAWProcessor;
    
    // Shader resources
    std::shared_ptr<RenderCore::RHIUniformBuffer> m_ParamsBuffer;
    // ... shader initialization
};

} // namespace LightroomCore
```

### 5. ImageProcessor Factory

```cpp
// ImageProcessor.h (updated)
namespace LightroomCore {

class ImageProcessor {
public:
    ImageProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~ImageProcessor();
    
    // Factory method - automatically selects loader
    std::shared_ptr<RenderCore::RHITexture2D> LoadImageFromFile(
        const std::wstring& imagePath);
    
    // Check if file is RAW format
    bool IsRAWFormat(const std::wstring& filePath);
    
    // Get image format
    ImageFormat GetImageFormat(const std::wstring& filePath);
    
    // RAW-specific: Get RAW info
    const RAWImageInfo* GetRAWInfo() const;
    
private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    
    // Loaders
    std::unique_ptr<IImageLoader> m_StandardLoader;
    std::unique_ptr<RAWImageLoader> m_RAWLoader;
    
    ImageFormat m_LastFormat;
    std::unique_ptr<RAWImageInfo> m_LastRAWInfo;
};

} // namespace LightroomCore
```

## RAW Processing Pipeline

### Stage 1: RAW Decoding
1. **File Detection**: Identify RAW format by extension or magic bytes
2. **Metadata Extraction**: Read EXIF, camera settings, color matrix
3. **RAW Data Loading**: Load 12/14/16-bit Bayer pattern data
4. **Preprocessing**: Linearization, black level subtraction, white balance multipliers

### Stage 2: GPU Processing (RAWDevelopNode)
1. **Demosaicing**: Convert Bayer pattern to RGB
   - Options: Bilinear, AMaZE, VNG, DCB, AHD
   - Implemented as GPU compute shader or pixel shader
2. **White Balance**: Apply WB multipliers
3. **Exposure Compensation**: Apply exposure adjustment
4. **Color Space Conversion**: Convert to sRGB/Adobe RGB
5. **Tone Mapping**: Apply tone curve
6. **Final Adjustments**: Contrast, saturation, highlights, shadows

### Stage 3: Standard Processing
- Continue through existing render graph (ScaleNode, BrightnessContrastNode, etc.)

## Library Selection

### Option 1: LibRaw (Recommended)
**Pros:**
- Actively maintained, modern C++ API
- Supports 50+ RAW formats
- Good performance
- Open source (LGPL/CDDL)

**Cons:**
- Requires linking external library
- LGPL license considerations

**Integration:**
```cpp
#include <libraw/libraw.h>

class RAWImageLoader {
    LibRaw m_Processor;
    // ...
};
```

### Option 2: dcraw
**Pros:**
- Very mature, supports many formats
- Public domain license
- Well-tested

**Cons:**
- C code, needs wrapper
- Less modern API
- Slower than LibRaw

### Option 3: Adobe DNG SDK
**Pros:**
- Professional quality
- Good documentation
- Supports DNG and some RAW formats

**Cons:**
- Primarily for DNG format
- Limited RAW format support
- More complex integration

### Recommendation: **LibRaw**
- Best balance of features, performance, and maintainability

## GPU Acceleration Strategy

### Demosaicing Shader
```hlsl
// DemosaicShader.hlsl
Texture2D<uint> RawBayerData : register(t0);
cbuffer DemosaicParams : register(b0) {
    uint2 ImageSize;
    uint BayerPattern;  // 0=RGGB, 1=BGGR, 2=GRBG, 3=GBRG
    float WhiteBalance[4];
};

float4 main(float2 texCoord : SV_Position) : SV_TARGET {
    uint2 pixelCoord = uint2(texCoord);
    uint2 bayerCoord = pixelCoord;
    
    // Sample Bayer pattern
    float r, g1, g2, b;
    // ... demosaicing algorithm (AMaZE, VNG, etc.)
    
    // Apply white balance
    r *= WhiteBalance[0];
    g1 *= WhiteBalance[1];
    b *= WhiteBalance[2];
    g2 *= WhiteBalance[3];
    
    // Average greens
    float g = (g1 + g2) * 0.5f;
    
    return float4(r, g, b, 1.0f);
}
```

### Processing Pipeline
1. **Upload RAW Data**: 16-bit texture for Bayer data
2. **Demosaicing Pass**: Compute shader or full-screen quad
3. **White Balance Pass**: Pixel shader
4. **Exposure/Tone Mapping**: Pixel shader
5. **Final Output**: 8-bit or 16-bit RGB texture

## SDK API Extensions

```cpp
// LightroomSDK.h (additions)

// RAW-specific structures
struct RAWDevelopParams {
    float whiteBalance[4];
    float exposure;
    float contrast;
    float saturation;
    float highlights;
    float shadows;
    float temperature;
    float tint;
};

struct RAWImageMetadata {
    uint32_t width;
    uint32_t height;
    char cameraModel[64];
    char lensModel[64];
    float iso;
    float aperture;
    float shutterSpeed;
    // ... more EXIF data
};

// API functions
LIGHTROOM_API bool IsRAWFormat(const char* imagePath);
LIGHTROOM_API bool GetRAWMetadata(void* renderTargetHandle, RAWImageMetadata* outMetadata);
LIGHTROOM_API void SetRAWDevelopParams(void* renderTargetHandle, const RAWDevelopParams* params);
LIGHTROOM_API void ResetRAWDevelopParams(void* renderTargetHandle);  // Reset to camera defaults
```

## UI Integration

### C# Side (WPF)
```csharp
// RAWDevelopPanel.xaml.cs
public class RAWDevelopPanel : UserControl {
    // White Balance controls
    private Slider TemperatureSlider;
    private Slider TintSlider;
    private Button AutoWBButton;
    
    // Exposure controls
    private Slider ExposureSlider;
    private Slider HighlightsSlider;
    private Slider ShadowsSlider;
    
    // Color controls
    private Slider ContrastSlider;
    private Slider SaturationSlider;
    
    private void OnParameterChanged() {
        var params = new RAWDevelopParams {
            Temperature = TemperatureSlider.Value,
            Tint = TintSlider.Value,
            Exposure = ExposureSlider.Value,
            // ...
        };
        NativeMethods.SetRAWDevelopParams(_renderTargetHandle, ref params);
        NativeMethods.RenderToTarget(_renderTargetHandle);
    }
}
```

## Performance Considerations

### 1. Lazy Loading
- Load RAW metadata immediately
- Defer full RAW decoding until needed
- Cache decoded results

### 2. Multi-Resolution Processing
- Generate preview from downsampled RAW
- Full resolution processing on demand
- Smart preview generation (1:1, 1:2, 1:4)

### 3. GPU Memory Management
- Use 16-bit textures for RAW data (save memory)
- Stream processing for very large RAWs
- Texture pooling for intermediate results

### 4. Async Processing
- Load RAW data asynchronously
- Background demosaicing
- Progressive rendering

## Implementation Phases

### Phase 1: Foundation (Week 1-2)
- [ ] Integrate LibRaw library
- [ ] Implement RAWImageLoader
- [ ] Basic RAW file detection and loading
- [ ] Extract RAW metadata

### Phase 2: Basic Processing (Week 3-4)
- [ ] Implement basic demosaicing (bilinear)
- [ ] White balance adjustment
- [ ] Exposure compensation
- [ ] Create RAWDevelopNode

### Phase 3: GPU Acceleration (Week 5-6)
- [ ] GPU demosaicing shader
- [ ] GPU white balance shader
- [ ] GPU exposure/tone mapping shader
- [ ] Optimize GPU pipeline

### Phase 4: Advanced Features (Week 7-8)
- [ ] Advanced demosaicing algorithms (AMaZE, VNG)
- [ ] Color space conversion
- [ ] Highlight/shadow recovery
- [ ] UI controls for RAW parameters

### Phase 5: Polish (Week 9-10)
- [ ] Performance optimization
- [ ] Memory management
- [ ] Error handling
- [ ] Documentation

## File Structure

```
src/Lightroom.Core/
├── ImageProcessor.h/cpp          (updated with factory)
├── ImageLoader.h                  (new - interface)
├── StandardImageLoader.h/cpp     (new - WIC-based)
├── RAWImageLoader.h/cpp          (new - LibRaw-based)
├── RAWProcessor.h/cpp             (new - GPU processing)
├── RAWImageInfo.h                 (new - metadata structures)
├── RenderNodes/
│   └── RAWDevelopNode.h/cpp       (new - render node)
└── Shaders/
    ├── DemosaicShader.hlsl        (new)
    ├── WhiteBalanceShader.hlsl     (new)
    └── ExposureShader.hlsl         (new)
```

## Dependencies

### Required Libraries
- **LibRaw**: RAW file decoding
  - Download from: https://www.libraw.org/
  - License: LGPL or CDDL
  - Integration: Static or dynamic linking

### Optional Libraries
- **OpenEXR**: For HDR RAW support (future)
- **Little CMS**: For color management (future)

## Testing Strategy

1. **Unit Tests**: RAW loader, processor components
2. **Integration Tests**: End-to-end RAW processing
3. **Performance Tests**: Large RAW files, GPU performance
4. **Format Tests**: Multiple RAW formats (CR2, NEF, ARW, etc.)
5. **Regression Tests**: Ensure standard image formats still work

## Future Enhancements

1. **RAW Histogram**: Show RAW data histogram
2. **RAW Clipping Indicators**: Highlight over/under-exposed areas
3. **Lens Correction**: Apply lens profile corrections
4. **Noise Reduction**: RAW-level noise reduction
5. **HDR Merge**: Merge multiple RAW exposures
6. **Focus Stacking**: Combine multiple focus points
7. **RAW Presets**: Save/load development presets

## Conclusion

This design provides a comprehensive foundation for RAW format support while maintaining compatibility with the existing architecture. The modular approach allows for incremental implementation and easy extension with new features and formats.


