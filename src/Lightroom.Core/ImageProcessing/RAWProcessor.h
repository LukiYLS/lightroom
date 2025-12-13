#pragma once

#include "RAWImageInfo.h"
#include "../d3d11rhi/DynamicRHI.h"
#include <memory>
#include <vector>

namespace LightroomCore {

// RAW 开发参数（C++ 内部完整版本）
// 注意：C API 使用 LightroomSDKTypes.h 中的 RAWDevelopParams（简化版本）
struct RAWDevelopParamsInternal {
    float whiteBalance[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // R, G1, B, G2 multipliers
    float exposure = 0.0f;  // EV adjustment (-5.0 to +5.0)
    float contrast = 0.0f;  // Contrast adjustment (-100 to +100)
    float saturation = 1.0f;  // Saturation multiplier (0.0 to 2.0)
    float highlights = 0.0f;  // Highlights recovery (-100 to +100)
    float shadows = 0.0f;     // Shadows lift (-100 to +100)
    float temperature = 5500.0f;  // Color temperature in Kelvin (2000 to 50000)
    float tint = 0.0f;  // Tint adjustment (-150 to +150)
    bool enableDemosaicing = true;
    uint32_t demosaicAlgorithm = 0;  // 0=bilinear, 1=AMaZE, 2=VNG, 3=DCB, 4=AHD
};

// RAW 处理器：GPU 加速的 RAW 数据处理
class RAWProcessor {
public:
    RAWProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~RAWProcessor();

    // 处理 RAW 数据到 RGB 纹理
    // rawData: 16-bit Bayer pattern 数据
    // rawInfo: RAW 图片信息
    // params: RAW 开发参数
    // 返回处理后的 RGB 纹理
    std::shared_ptr<RenderCore::RHITexture2D> ProcessRAW(
        const std::vector<uint16_t>& rawData,
        const RAWImageInfo& rawInfo,
        const RAWDevelopParamsInternal& params);

    // 检查是否已初始化
    bool IsInitialized() const { return m_Initialized; }

private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    bool m_Initialized;

    // 初始化 GPU 资源（shaders, buffers, etc.）
    bool InitializeGPUResources();

    // 清理 GPU 资源
    void CleanupGPUResources();

    // GPU shaders（将在后续 GPU 加速阶段实现）
    // 当前使用 CPU 处理，这些成员暂时不需要
    // Microsoft::WRL::ComPtr<ID3D11VertexShader> m_D3D11VS;
    // Microsoft::WRL::ComPtr<ID3D11PixelShader> m_DemosaicPS;
    // Microsoft::WRL::ComPtr<ID3D11PixelShader> m_WhiteBalancePS;
    // Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ExposurePS;

    // 临时实现：使用 CPU 处理（后续将改为 GPU）
    std::shared_ptr<RenderCore::RHITexture2D> ProcessRAW_CPU(
        const std::vector<uint16_t>& rawData,
        const RAWImageInfo& rawInfo,
        const RAWDevelopParamsInternal& params);

    // CPU 去马赛克（bilinear，临时实现）
    void DemosaicBilinear(const uint16_t* rawData, uint8_t* rgbData,
                          uint32_t width, uint32_t height, uint32_t bayerPattern);

    // CPU 白平衡调整
    void ApplyWhiteBalance(uint8_t* rgbData, uint32_t pixelCount,
                          const float* whiteBalance);

    // CPU 曝光补偿
    void ApplyExposure(uint8_t* rgbData, uint32_t pixelCount, float exposure);
};

} // namespace LightroomCore

