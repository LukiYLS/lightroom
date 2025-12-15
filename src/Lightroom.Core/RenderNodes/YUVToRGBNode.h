#pragma once

#include "RenderNode.h"
#include "../d3d11rhi/RHITexture2D.h"
#include <memory>

// Forward declaration
struct ID3D11ShaderResourceView;

namespace LightroomCore {

// YUV to RGB conversion node: performs color space conversion on GPU
// Supports NV12, YUV420P and other formats
class YUVToRGBNode : public RenderNode {
public:
    YUVToRGBNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~YUVToRGBNode();

    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) override;

    virtual const char* GetName() const override { return "YUVToRGB"; }
    
    // Set YUV format type
    enum class YUVFormat {
        NV12,      // Common format for hardware decoding
        YUV420P,   // Common format for software decoding
        YUV422P,
        YUV444P
    };
    void SetYUVFormat(YUVFormat format) { m_YUVFormat = format; }
    YUVFormat GetYUVFormat() const { return m_YUVFormat; }
    
    // Multi-texture input (for separated Y/U/V planes)
    // If input is a single texture (like NV12), use Execute method
    // If input is separated Y/U/V textures, use this method
    bool ExecuteMultiTexture(
        std::shared_ptr<RenderCore::RHITexture2D> yTexture,
        std::shared_ptr<RenderCore::RHITexture2D> uTexture,
        std::shared_ptr<RenderCore::RHITexture2D> vTexture,
        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
        uint32_t width, uint32_t height);

public:
    bool InitializeShaderResources();
    
    // Set custom SRV for NV12 format (Y and UV planes)
    // If set, these will be used instead of the input texture's SRV
    void SetCustomShaderResourceViews(ID3D11ShaderResourceView* ySRV, ID3D11ShaderResourceView* uvSRV);
    
    // Set custom SRV for YUV420P format (separate Y, U, V planes)
    // If set, these will be used instead of the input texture's SRV
    void SetCustomShaderResourceViewsYUV420P(ID3D11ShaderResourceView* ySRV, ID3D11ShaderResourceView* uSRV, ID3D11ShaderResourceView* vSRV);

protected:
    virtual void UpdateConstantBuffers(uint32_t width, uint32_t height) override;
    virtual void SetConstantBuffers() override;
    virtual void SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) override;

private:
    void CleanupShaderResources();
    
    // Compile shader for different formats
    bool CompileShaderForFormat(YUVFormat format);
    
    struct __declspec(align(16)) YUVToRGBCBuffer {
        float Width;
        float Height;
        float Padding[2];
    };
    
    CompiledShader m_Shader;
    std::shared_ptr<RenderCore::RHIUniformBuffer> m_ParamsBuffer;
    YUVFormat m_YUVFormat = YUVFormat::NV12;
    bool m_ShaderResourcesInitialized = false;
    
    // Custom SRV for NV12 format (Y and UV planes)
    ID3D11ShaderResourceView* m_CustomYSRV = nullptr;
    ID3D11ShaderResourceView* m_CustomUVSRV = nullptr;
    
    // Custom SRV for YUV420P format (separate Y, U, V planes)
    ID3D11ShaderResourceView* m_CustomUSRV = nullptr;
    ID3D11ShaderResourceView* m_CustomVSRV = nullptr;
};

} // namespace LightroomCore






