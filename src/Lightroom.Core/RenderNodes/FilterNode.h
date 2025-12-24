#pragma once

#include "RenderNode.h"
#include "../d3d11rhi/RHITexture2D.h"
#include <memory>
#include <vector>

namespace LightroomCore {

// 滤镜节点：使用 LUT (Look-Up Table) 实现滤镜效果
class FilterNode : public RenderNode {
public:
    FilterNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~FilterNode();

    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) override;

    virtual const char* GetName() const override { return "Filter"; }

    // 从数据创建 LUT 纹理
    // lutSize: LUT 尺寸（例如 32 表示 32x32x32 的 3D LUT）
    // lutData: LUT 数据，格式为 RGB float 数组，大小为 lutSize^3 * 3
    bool LoadLUT(uint32_t lutSize, const float* lutData);

    // 从 2D 纹理创建 LUT（用于预生成的 LUT 纹理）
    bool LoadLUTFromTexture(std::shared_ptr<RenderCore::RHITexture2D> lutTexture, uint32_t lutSize);

    // 从 .cube 文件加载 LUT
    // filePath: .cube 文件路径（UTF-8 编码）
    bool LoadLUTFromFile(const char* filePath);

    // 设置滤镜强度（0.0 = 无效果，1.0 = 完全应用）
    void SetIntensity(float intensity) { m_Intensity = intensity; }
    float GetIntensity() const { return m_Intensity; }

    // 获取 LUT 信息（用于克隆）
    uint32_t GetLUTSize() const { return m_LUTSize; }
    std::shared_ptr<RenderCore::RHITexture2D> GetLUTTexture() const { return m_LUTTexture; }

protected:
    // 重写基类的钩子方法
    virtual void UpdateConstantBuffers(uint32_t width, uint32_t height) override;
    virtual void SetConstantBuffers() override;
    virtual void SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) override;

private:
    bool InitializeShaderResources();
    void CleanupShaderResources();

    // 将 3D LUT 数据转换为 2D 纹理格式
    // 3D LUT (size x size x size) 展开为 2D 纹理 (size x size*size)
    std::vector<uint8_t> ConvertLUTTo2DTexture(uint32_t lutSize, const float* lutData);

    // Constant buffer 结构
    struct __declspec(align(16)) FilterConstantBuffer {
        float LUTSize;          // LUT 尺寸（例如 32.0）
        float Intensity;        // 滤镜强度 (0.0 - 1.0)
        float Padding[2];
    };

    CompiledShader m_Shader;
    std::shared_ptr<RenderCore::RHIUniformBuffer> m_ParamsBuffer;
    std::shared_ptr<RenderCore::RHITexture2D> m_LUTTexture;
    
    uint32_t m_LUTSize = 0;
    float m_Intensity = 1.0f;
    bool m_ShaderResourcesInitialized = false;
};

} // namespace LightroomCore

