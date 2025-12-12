#pragma once

#include <cstdint>

// C API 类型定义
// 这些结构体用于 C API 接口，需要保持 C 兼容性
// 注意：结构体布局必须与 C# P/Invoke 定义匹配

extern "C" {
    // RAW 图片元数据结构
    struct RAWImageMetadata {
        uint32_t width;
        uint32_t height;
        uint32_t bitsPerPixel;
        uint32_t bayerPattern;
        float whiteBalance[4];
        char cameraModel[64];
        char lensModel[64];
        float iso;
        float aperture;
        float shutterSpeed;
        float focalLength;
    };

    // RAW 开发参数结构（C API 版本）
    // 注意：这是 C API 的简化版本，用于跨语言互操作
    // C++ 内部使用 RAWProcessor.h 中的完整版本（包含 enableDemosaicing 等字段）
    struct RAWDevelopParams {
        float whiteBalance[4];  // R, G1, B, G2 multipliers
        float exposure;          // EV adjustment (-5.0 to +5.0)
        float contrast;          // Contrast adjustment (-100 to +100)
        float saturation;       // Saturation multiplier (0.0 to 2.0)
        float highlights;       // Highlights recovery (-100 to +100)
        float shadows;         // Shadows lift (-100 to +100)
        float temperature;      // Color temperature in Kelvin (2000 to 50000)
        float tint;            // Tint adjustment (-150 to +150)
        // 注意：C++ 版本还有 enableDemosaicing 和 demosaicAlgorithm 字段
        // 这些字段在 C API 中暂不暴露，使用默认值
    };
}

