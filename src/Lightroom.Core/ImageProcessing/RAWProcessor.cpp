// 禁用 Windows.h 的 min/max 宏定义，避免与 std::min/std::max 冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RAWProcessor.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace LightroomCore {

RAWProcessor::RAWProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : m_RHI(rhi)
    , m_Initialized(false)
{
    InitializeGPUResources();
}

RAWProcessor::~RAWProcessor() {
    CleanupGPUResources();
}

bool RAWProcessor::InitializeGPUResources() {
    if (!m_RHI) {
        std::cerr << "[RAWProcessor] RHI is null" << std::endl;
        return false;
    }

    // TODO: 初始化 GPU shaders 和资源
    // 当前使用 CPU 处理，所以暂时不需要 GPU 资源
    // 后续实现 GPU 加速时会添加：
    // - 编译 demosaicing shader
    // - 编译 white balance shader
    // - 编译 exposure shader
    // - 创建 constant buffers
    // - 创建临时纹理

    m_Initialized = true;
    return true;
}

void RAWProcessor::CleanupGPUResources() {
    // TODO: 清理 GPU 资源
    m_Initialized = false;
}

std::shared_ptr<RenderCore::RHITexture2D> RAWProcessor::ProcessRAW(
    const std::vector<uint16_t>& rawData,
    const RAWImageInfo& rawInfo,
    const RAWDevelopParamsInternal& params) {
    
    if (!m_Initialized || !m_RHI) {
        std::cerr << "[RAWProcessor] Not initialized" << std::endl;
        return nullptr;
    }

    if (rawData.empty() || rawInfo.width == 0 || rawInfo.height == 0) {
        std::cerr << "[RAWProcessor] Invalid input data" << std::endl;
        return nullptr;
    }

    // 当前使用 CPU 处理（临时实现）
    // 后续将改为 GPU 处理以获得更好性能
    return ProcessRAW_CPU(rawData, rawInfo, params);
}

std::shared_ptr<RenderCore::RHITexture2D> RAWProcessor::ProcessRAW_CPU(
    const std::vector<uint16_t>& rawData,
    const RAWImageInfo& rawInfo,
    const RAWDevelopParamsInternal& params) {
    
    uint32_t width = rawInfo.width;
    uint32_t height = rawInfo.height;
    uint32_t pixelCount = width * height;

    // 分配 RGB 缓冲区（每像素 3 字节）
    std::vector<uint8_t> rgbData(pixelCount * 3);

    // 步骤 1: 去马赛克（Bayer pattern -> RGB）
    if (params.enableDemosaicing) {
        DemosaicBilinear(rawData.data(), rgbData.data(), width, height, rawInfo.bayerPattern);
    } else {
        // 如果禁用去马赛克，直接复制（这通常不会产生正确结果，仅用于测试）
        for (uint32_t i = 0; i < pixelCount; ++i) {
            uint16_t value = rawData[i];
            uint8_t normalized = static_cast<uint8_t>((value * 255) / 65535);
            rgbData[i * 3 + 0] = normalized;  // R
            rgbData[i * 3 + 1] = normalized;  // G
            rgbData[i * 3 + 2] = normalized;  // B
        }
    }

    // 步骤 2: 白平衡调整
    ApplyWhiteBalance(rgbData.data(), pixelCount, params.whiteBalance);

    // 步骤 3: 曝光补偿
    ApplyExposure(rgbData.data(), pixelCount, params.exposure);

    // 步骤 4: 对比度和饱和度（简化实现）
    // TODO: 实现更完整的对比度和饱和度调整

    // 转换 RGB 到 BGRA（RHI 期望的格式）
    std::vector<uint8_t> bgraData(pixelCount * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        bgraData[i * 4 + 0] = rgbData[i * 3 + 2];  // B
        bgraData[i * 4 + 1] = rgbData[i * 3 + 1];  // G
        bgraData[i * 4 + 2] = rgbData[i * 3 + 0];  // R
        bgraData[i * 4 + 3] = 255;                  // A
    }

    // 使用 RHI 接口创建纹理
    uint32_t stride = width * 4;  // BGRA = 4 bytes per pixel
    auto texture = m_RHI->RHICreateTexture2D(
        RenderCore::EPixelFormat::PF_B8G8R8A8,
        RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
        width,
        height,
        1,  // NumMips
        bgraData.data(),
        stride
    );

    if (!texture) {
        std::cerr << "[RAWProcessor] Failed to create texture via RHI" << std::endl;
        return nullptr;
    }

    return texture;
}

void RAWProcessor::DemosaicBilinear(const uint16_t* rawData, uint8_t* rgbData,
                                    uint32_t width, uint32_t height, uint32_t bayerPattern) {
    // 简化的 bilinear 去马赛克算法
    // Bayer pattern: 0=RGGB, 1=BGGR, 2=GRBG, 3=GBRG
    
    for (uint32_t y = 1; y < height - 1; ++y) {
        for (uint32_t x = 1; x < width - 1; ++x) {
            uint32_t idx = y * width + x;
            uint32_t rgbIdx = idx * 3;

            // 根据 Bayer pattern 和位置确定当前像素的颜色
            uint32_t pattern = (y % 2) * 2 + (x % 2);
            uint32_t color = (pattern + bayerPattern) % 4;

            float r = 0.0f, g = 0.0f, b = 0.0f;

            if (color == 0) {  // Red
                r = rawData[idx];
                g = (rawData[idx - 1] + rawData[idx + 1] + rawData[idx - width] + rawData[idx + width]) / 4.0f;
                b = (rawData[idx - width - 1] + rawData[idx - width + 1] + rawData[idx + width - 1] + rawData[idx + width + 1]) / 4.0f;
            } else if (color == 1) {  // Green1 (在 RGGB 中)
                r = (rawData[idx - 1] + rawData[idx + 1]) / 2.0f;
                g = rawData[idx];
                b = (rawData[idx - width] + rawData[idx + width]) / 2.0f;
            } else if (color == 2) {  // Blue
                r = (rawData[idx - width - 1] + rawData[idx - width + 1] + rawData[idx + width - 1] + rawData[idx + width + 1]) / 4.0f;
                g = (rawData[idx - 1] + rawData[idx + 1] + rawData[idx - width] + rawData[idx + width]) / 4.0f;
                b = rawData[idx];
            } else {  // Green2
                r = (rawData[idx - width] + rawData[idx + width]) / 2.0f;
                g = rawData[idx];
                b = (rawData[idx - 1] + rawData[idx + 1]) / 2.0f;
            }

            // 归一化到 0-255（假设输入是 16-bit，0-65535）
            rgbData[rgbIdx + 0] = static_cast<uint8_t>((std::min)(255.0f, (r * 255.0f) / 65535.0f));
            rgbData[rgbIdx + 1] = static_cast<uint8_t>((std::min)(255.0f, (g * 255.0f) / 65535.0f));
            rgbData[rgbIdx + 2] = static_cast<uint8_t>((std::min)(255.0f, (b * 255.0f) / 65535.0f));
        }
    }

    // 处理边界像素（简化处理，使用最近邻）
    // TODO: 实现更完整的边界处理
}

void RAWProcessor::ApplyWhiteBalance(uint8_t* rgbData, uint32_t pixelCount,
                                     const float* whiteBalance) {
    // 应用白平衡乘数
    // whiteBalance[0] = R, [1] = G1, [2] = B, [3] = G2
    // 简化：使用 G1 作为 G 的乘数
    float rMul = whiteBalance[0];
    float gMul = whiteBalance[1];
    float bMul = whiteBalance[2];

    for (uint32_t i = 0; i < pixelCount; ++i) {
        uint32_t idx = i * 3;
        float r = rgbData[idx + 0] * rMul;
        float g = rgbData[idx + 1] * gMul;
        float b = rgbData[idx + 2] * bMul;

        // 归一化（防止溢出）
        float maxVal = (std::max)(r, (std::max)(g, b));
        if (maxVal > 255.0f) {
            float scale = 255.0f / maxVal;
            r *= scale;
            g *= scale;
            b *= scale;
        }

        rgbData[idx + 0] = static_cast<uint8_t>((std::min)(255.0f, r));
        rgbData[idx + 1] = static_cast<uint8_t>((std::min)(255.0f, g));
        rgbData[idx + 2] = static_cast<uint8_t>((std::min)(255.0f, b));
    }
}

void RAWProcessor::ApplyExposure(uint8_t* rgbData, uint32_t pixelCount, float exposure) {
    // 曝光补偿：exposure 是 EV 值（-5.0 到 +5.0）
    // 转换为乘数：multiplier = 2^exposure
    float multiplier = std::pow(2.0f, exposure);

    for (uint32_t i = 0; i < pixelCount; ++i) {
        uint32_t idx = i * 3;
        float r = rgbData[idx + 0] * multiplier;
        float g = rgbData[idx + 1] * multiplier;
        float b = rgbData[idx + 2] * multiplier;

        // 裁剪到 0-255
        rgbData[idx + 0] = static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, r)));
        rgbData[idx + 1] = static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, g)));
        rgbData[idx + 2] = static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, b)));
    }
}

} // namespace LightroomCore

