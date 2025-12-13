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

    // 图像调整参数结构（通用，适用于 RAW 和标准图片）
    // 参考 Adobe Lightroom 的调整面板
    struct ImageAdjustParams {
        // 基本调整
        float exposure;          // 曝光度 (-5.0 to +5.0 EV)
        float contrast;          // 对比度 (-100 to +100)
        float highlights;        // 高光 (-100 to +100)
        float shadows;          // 阴影 (-100 to +100)
        float whites;            // 白色色阶 (-100 to +100)
        float blacks;            // 黑色色阶 (-100 to +100)
        
        // 白平衡（仅对 RAW 有效，标准图片会被忽略）
        float temperature;       // 色温 (2000 to 50000 Kelvin)
        float tint;             // 色调 (-150 to +150)
        
        // 颜色调整
        float vibrance;         // 自然饱和度 (-100 to +100)
        float saturation;       // 饱和度 (-100 to +100)
        
        // HSL 调整（按颜色通道）
        // 色相 (-100 to +100)
        float hueRed;
        float hueOrange;
        float hueYellow;
        float hueGreen;
        float hueAqua;
        float hueBlue;
        float huePurple;
        float hueMagenta;
        
        // 饱和度 (-100 to +100)
        float satRed;
        float satOrange;
        float satYellow;
        float satGreen;
        float satAqua;
        float satBlue;
        float satPurple;
        float satMagenta;
        
        // 明亮度 (-100 to +100)
        float lumRed;
        float lumOrange;
        float lumYellow;
        float lumGreen;
        float lumAqua;
        float lumBlue;
        float lumPurple;
        float lumMagenta;
        
        // 细节调整
        float sharpness;        // 锐化 (0 to 150)
        float noiseReduction;   // 降噪 (0 to 100)
        
        // 镜头校正
        float lensDistortion;   // 镜头畸变 (-100 to +100)
        float chromaticAberration; // 色差校正 (0 to 100)
        
        // 效果
        float vignette;         // 晕影 (-100 to +100)
        float grain;            // 颗粒 (0 to 100)
        
        // 校准（相机校准）
        float shadowTint;       // 阴影色调 (-100 to +100)
        float redHue;           // 红色色相 (-100 to +100)
        float redSaturation;    // 红色饱和度 (-100 to +100)
        float greenHue;         // 绿色色相 (-100 to +100)
        float greenSaturation;  // 绿色饱和度 (-100 to +100)
        float blueHue;           // 蓝色色相 (-100 to +100)
        float blueSaturation;    // 蓝色饱和度 (-100 to +100)
    };

    // 视频格式枚举（C 兼容）
    enum VideoFormat {
        VideoFormat_Unknown = 0,
        VideoFormat_MP4 = 1,
        VideoFormat_MOV = 2,
        VideoFormat_AVI = 3,
        VideoFormat_MKV = 4
    };

    // 视频元数据结构
    struct VideoMetadata {
        uint32_t width;
        uint32_t height;
        double frameRate;        // fps
        int64_t totalFrames;      // 总帧数
        int64_t duration;         // 时长（微秒）
        VideoFormat format;
        bool hasAudio;
    };
}
