#include "RAWDemosaicComputeNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11CommandContext.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include "../d3d11rhi/D3D11UnorderedAccessView.h"
#include <cstring>
#include <d3dcompiler.h>
#include <Windows.h>
#include <cstdio>

namespace LightroomCore {

    RAWDemosaicComputeNode::RAWDemosaicComputeNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
        : ComputeNode(rhi)
        , m_Constants{}
    {
        m_Constants.bayerPattern = 0;  // RGGB
        m_Constants.whiteBalanceR = 1.0f;
        m_Constants.whiteBalanceG = 1.0f;
        m_Constants.whiteBalanceB = 1.0f;
    }

    RAWDemosaicComputeNode::~RAWDemosaicComputeNode() {
        m_ConstantBuffer.reset();
    }

    void RAWDemosaicComputeNode::SetBayerPattern(uint32_t pattern) {
        m_Constants.bayerPattern = pattern;
    }

    void RAWDemosaicComputeNode::SetWhiteBalance(float r, float g, float b) {
        m_Constants.whiteBalanceR = r;
        m_Constants.whiteBalanceG = g;
        m_Constants.whiteBalanceB = b;
    }

    bool RAWDemosaicComputeNode::InitializeComputeShader(const char* csCode) {
        if (m_ComputeShaderInitialized) {
            return true;
        }

        if (!m_RHI) {
            return false;
        }

        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
        if (!d3d11RHI) {
            return false;
        }

        ID3D11Device* device = d3d11RHI->GetDevice();
        if (!device) {
            return false;
        }

        const char* shaderCode = GetComputeShaderCode();
        ComPtr<ID3DBlob> errorBlob;
        ComPtr<ID3DBlob> shaderBlob;

        HRESULT hr = D3DCompile(
            shaderCode,
            strlen(shaderCode),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "cs_5_0",
            0,
            0,
            shaderBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );

        if (FAILED(hr)) 
            return false;      

        hr = device->CreateComputeShader(
            shaderBlob->GetBufferPointer(),
            shaderBlob->GetBufferSize(),
            nullptr,
            m_D3D11ComputeShader.GetAddressOf()
        );

        if (FAILED(hr)) {
            return false;
        }
        if (!m_ConstantBuffer) {
            m_ConstantBuffer = m_RHI->RHICreateUniformBuffer(sizeof(DemosaicConstants));
            if (!m_ConstantBuffer) {
                return false;
            }
        }
        m_ComputeShaderInitialized = true;
        return true;
    }

    void RAWDemosaicComputeNode::UpdateConstantBuffers(uint32_t width, uint32_t height) {
        m_Constants.width = width;
        m_Constants.height = height;

        if (m_ConstantBuffer) {
            m_CommandContext->RHIUpdateUniformBuffer(m_ConstantBuffer, &m_Constants);
        }
    }


    const char* RAWDemosaicComputeNode::GetComputeShaderCode() {
        return R"(
cbuffer DemosaicConstants : register(b0) {
    uint width;
    uint height;
    uint bayerPattern;  // 0=RGGB, 1=GRBG, 2=GBRG, 3=BGGR
    uint padding;
    float whiteBalanceR;
    float whiteBalanceG;
    float whiteBalanceB;
    float padding2;
};

Texture2D<float> BayerTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) {
        return;
    }

    uint2 pos = id.xy;
    float center = BayerTexture[pos].r;
    
    // ���� Bayer pattern ȷ����ǰλ�õ���ɫ
    uint pattern = bayerPattern;
    uint xMod = pos.x % 2;
    uint yMod = pos.y % 2;
    uint colorIndex = (yMod << 1) | xMod;
    
    // ���� pattern ���� colorIndex
    // 0=RGGB: (0,0)=R, (1,0)=G, (0,1)=G, (1,1)=B
    // 1=GRBG: (0,0)=G, (1,0)=R, (0,1)=B, (1,1)=G
    // 2=GBRG: (0,0)=G, (1,0)=B, (0,1)=R, (1,1)=G
    // 3=BGGR: (0,0)=B, (1,0)=G, (0,1)=G, (1,1)=R
    uint actualColorIndex = (colorIndex + pattern) % 4;
    
    float r = 0.0, g = 0.0, b = 0.0;
    
    // ˫���Բ�ֵȥ������
    if (actualColorIndex == 0) {
        // Red pixel
        r = center;
        if (pos.x > 0 && pos.x < width - 1) {
            g = (BayerTexture[uint2(pos.x - 1, pos.y)].r + BayerTexture[uint2(pos.x + 1, pos.y)].r) * 0.5f;
        } else if (pos.x > 0) {
            g = BayerTexture[uint2(pos.x - 1, pos.y)].r;
        } else {
            g = BayerTexture[uint2(pos.x + 1, pos.y)].r;
        }
        if (pos.y > 0 && pos.y < height - 1) {
            b = (BayerTexture[uint2(pos.x, pos.y - 1)].r + BayerTexture[uint2(pos.x, pos.y + 1)].r) * 0.5f;
        } else if (pos.y > 0) {
            b = BayerTexture[uint2(pos.x, pos.y - 1)].r;
        } else {
            b = BayerTexture[uint2(pos.x, pos.y + 1)].r;
        }
    } else if (actualColorIndex == 1 || actualColorIndex == 2) {
        // Green pixel
        g = center;
        if (pos.x > 0 && pos.x < width - 1 && pos.y > 0 && pos.y < height - 1) {
            r = (BayerTexture[uint2(pos.x - 1, pos.y)].r + BayerTexture[uint2(pos.x + 1, pos.y)].r) * 0.5f;
            b = (BayerTexture[uint2(pos.x, pos.y - 1)].r + BayerTexture[uint2(pos.x, pos.y + 1)].r) * 0.5f;
        } else {
            // �߽紦��
            r = center;
            b = center;
        }
    } else {
        // Blue pixel
        b = center;
        if (pos.x > 0 && pos.x < width - 1) {
            g = (BayerTexture[uint2(pos.x - 1, pos.y)].r + BayerTexture[uint2(pos.x + 1, pos.y)].r) * 0.5f;
        } else if (pos.x > 0) {
            g = BayerTexture[uint2(pos.x - 1, pos.y)].r;
        } else {
            g = BayerTexture[uint2(pos.x + 1, pos.y)].r;
        }
        if (pos.y > 0 && pos.y < height - 1) {
            r = (BayerTexture[uint2(pos.x, pos.y - 1)].r + BayerTexture[uint2(pos.x, pos.y + 1)].r) * 0.5f;
        } else if (pos.y > 0) {
            r = BayerTexture[uint2(pos.x, pos.y - 1)].r;
        } else {
            r = BayerTexture[uint2(pos.x, pos.y + 1)].r;
        }
    }
    
    // Ӧ�ð�ƽ��
    r *= whiteBalanceR;
    g *= whiteBalanceG;
    b *= whiteBalanceB;
    
    // �����Ѿ��� 0-1 ��Χ�� float���� 16-bit ���ݹ�һ��������
    // ֱ��ʹ�ã������ٴι�һ��
    
    // ��� BGRA ��ʽ
    OutputTexture[pos] = float4(b, g, r, 1.0);
}
)";
	}

} // namespace LightroomCore