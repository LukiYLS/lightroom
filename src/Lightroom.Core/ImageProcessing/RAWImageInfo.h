#pragma once

#include <string>
#include <cstdint>

namespace LightroomCore {

// RAW 图片信息结构
struct RAWImageInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bayerPattern = 0;  // 0=RGGB, 1=BGGR, 2=GRBG, 3=GBRG
    uint32_t bitsPerPixel = 0;   // 12, 14, 16
    float whiteBalance[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // R, G1, B, G2 multipliers
    float colorMatrix[9] = {0.0f};  // 3x3 color matrix
    std::string cameraModel;
    std::string lensModel;
    float iso = 0.0f;
    float aperture = 0.0f;
    float shutterSpeed = 0.0f;
    float focalLength = 0.0f;
    std::string make;  // Camera manufacturer
    std::string model; // Camera model
    std::string software; // Software used
    // ... other EXIF/metadata can be added here
};

} // namespace LightroomCore


