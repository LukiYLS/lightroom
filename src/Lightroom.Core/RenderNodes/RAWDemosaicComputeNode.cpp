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

		// 在 InitializeComputeShader 函数中

		HRESULT hr = D3DCompile(
			shaderCode,
			strlen(shaderCode),
			nullptr,
			nullptr,
			nullptr,
			"main",
			"cs_5_0",
			0, // Flags1: 建议加上 D3DCOMPILE_DEBUG 用于调试，发布时去掉
			0,
			shaderBlob.GetAddressOf(),
			errorBlob.GetAddressOf() // 确保这里传入了 errorBlob
		);

		if (FAILED(hr)) {
			if (errorBlob) {
				// =======================================================
				// 【关键步骤】请把这段 Log 打印出来，或者在 VS 的 Output 窗口看
				// =======================================================
				const char* errorMsg = (const char*)errorBlob->GetBufferPointer();

				// 方式 A: 输出到 VS 调试窗口 (Debug Output)
				OutputDebugStringA("============= SHADER COMPILE ERROR =============\n");
				OutputDebugStringA(errorMsg);
				OutputDebugStringA("\n================================================\n");
			}
			return false;
		}
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
    uint bayerPattern; // 0=RGGB, 1=GRBG, 2=GBRG, 3=BGGR
    uint padding;
    float whiteBalanceR;
    float whiteBalanceG;
    float whiteBalanceB;
    float padding2;
};

Texture2D<float> BayerTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

float SafeLoad(int x, int y) {
    int cx = clamp(x, 0, (int)width - 1);
    int cy = clamp(y, 0, (int)height - 1);
    return BayerTexture.Load(int3(cx, cy, 0));
}

[numthreads(16, 16, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    int x = (int)dtid.x;
    int y = (int)dtid.y;
    
    if ((uint)x >= width || (uint)y >= height) {
        return;
    }

    int xOff = 0;
    int yOff = 0;
    
    if (bayerPattern == 1 || bayerPattern == 3) xOff = 1;
    if (bayerPattern == 2 || bayerPattern == 3) yOff = 1;

    int lx = x + xOff;
    int ly = y + yOff;

    bool isEvenX = (lx % 2) == 0;
    bool isEvenY = (ly % 2) == 0;

    bool isRed  = isEvenX && isEvenY;
    bool isBlue = !isEvenX && !isEvenY;
    bool isGreen = !isRed && !isBlue;

    float center = SafeLoad(x, y);
    
    if (center <= 0.0f) {
        OutputTexture[int2(x, y)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    if (isRed) {
        r = center;
        g = (SafeLoad(x-1, y) + SafeLoad(x+1, y) + SafeLoad(x, y-1) + SafeLoad(x, y+1)) * 0.25f;
        b = (SafeLoad(x-1, y-1) + SafeLoad(x+1, y-1) + SafeLoad(x-1, y+1) + SafeLoad(x+1, y+1)) * 0.25f;
    }
    else if (isBlue) {
        b = center;
        g = (SafeLoad(x-1, y) + SafeLoad(x+1, y) + SafeLoad(x, y-1) + SafeLoad(x, y+1)) * 0.25f;
        r = (SafeLoad(x-1, y-1) + SafeLoad(x+1, y-1) + SafeLoad(x-1, y+1) + SafeLoad(x+1, y+1)) * 0.25f;
    }
    else {
        g = center;
        if (isEvenX) {
            r = (SafeLoad(x-1, y) + SafeLoad(x+1, y)) * 0.5f;
            b = (SafeLoad(x, y-1) + SafeLoad(x, y+1)) * 0.5f;
        }
        else {
            b = (SafeLoad(x-1, y) + SafeLoad(x+1, y)) * 0.5f;
            r = (SafeLoad(x, y-1) + SafeLoad(x, y+1)) * 0.5f;
        }
    }

    r = saturate(r * whiteBalanceR);
    g = saturate(g * whiteBalanceG);
    b = saturate(b * whiteBalanceB);

    OutputTexture[int2(x, y)] = float4(b, g, r, 1.0f);
}
)";
	}
} // namespace LightroomCore