#include "RenderNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11VertexBuffer.h"
#include "../d3d11rhi/RHI.h"
#include "../d3d11rhi/Common.h"
#include <d3dcompiler.h>
#include <iostream>
#include <cfloat>

#pragma comment(lib, "d3dcompiler.lib")

namespace LightroomCore {

RenderNode::RenderNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : m_RHI(rhi)
{
    if (m_RHI) {
        m_CommandContext = m_RHI->GetDefaultCommandContext();
    }
    InitializeCommonResources();
}

RenderNode::~RenderNode() {
    CleanupCommonResources();
}

bool RenderNode::InitializeCommonResources() {
    if (m_CommonResourcesInitialized || !m_RHI) {
        return m_CommonResourcesInitialized;
    }

    try {
        // 创建全屏四边形顶点缓冲区（所有节点共享）
        SimpleVertex vertices[4] = {
            { -1.0f,  1.0f, 0.0f, 0.0f },  // 左上
            {  1.0f,  1.0f, 1.0f, 0.0f },  // 右上
            { -1.0f, -1.0f, 0.0f, 1.0f },  // 左下
            {  1.0f, -1.0f, 1.0f, 1.0f }   // 右下
        };

        m_CommonVertexBuffer = m_RHI->RHICreateVertexBuffer(
            vertices,
            RenderCore::EBufferUsageFlags::BUF_Static,
            sizeof(SimpleVertex),
            4
        );

        // 创建采样器状态（所有节点共享相同的配置）
        RenderCore::SamplerStateInitializerRHI samplerInit(
            RenderCore::SF_Bilinear,  // Filter
            RenderCore::AM_Clamp,     // AddressU
            RenderCore::AM_Clamp,     // AddressV
            RenderCore::AM_Clamp,     // AddressW
            0.0f,                      // MipBias
            0,                         // MaxAnisotropy
            0.0f,                      // MinMipLevel
            FLT_MAX,                   // MaxMipLevel
            0,                         // BorderColor
            RenderCore::SCF_Never      // SamplerComparisonFunction
        );
        m_CommonSamplerState = m_RHI->RHICreateSamplerState(samplerInit);

        m_CommonResourcesInitialized = (m_CommonVertexBuffer != nullptr && 
                                        m_CommonSamplerState != nullptr);
    }
    catch (const std::exception& e) {
        std::cerr << "[RenderNode] Failed to initialize common resources: " << e.what() << std::endl;
        m_CommonResourcesInitialized = false;
    }

    return m_CommonResourcesInitialized;
}

void RenderNode::CleanupCommonResources() {
    m_CommonVertexBuffer.reset();
    m_CommonSamplerState.reset();
    m_CommonResourcesInitialized = false;
}

bool RenderNode::CompileShaders(const char* vsCode, const char* psCode, CompiledShader& outShader) {
    if (!m_RHI) {
        return false;
    }

    RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
    if (!d3d11RHI) {
        std::cerr << "[RenderNode] Failed to cast to D3D11DynamicRHI" << std::endl;
        return false;
    }
    
    ID3D11Device* device = d3d11RHI->GetDevice();
    if (!device) {
        std::cerr << "[RenderNode] Failed to get D3D11 device" << std::endl;
        return false;
    }

    // 编译 Vertex Shader
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, 
                           &outShader.Blob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "[RenderNode] VS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    // 创建 Vertex Shader
    hr = device->CreateVertexShader(outShader.Blob->GetBufferPointer(), outShader.Blob->GetBufferSize(), 
                                     nullptr, &outShader.VS);
    if (FAILED(hr)) {
        std::cerr << "[RenderNode] Failed to create VS: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 编译 Pixel Shader
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, 
                    &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "[RenderNode] PS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    // 创建 Pixel Shader
    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), 
                                    nullptr, &outShader.PS);
    if (FAILED(hr)) {
        std::cerr << "[RenderNode] Failed to create PS: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 创建输入布局
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    
    hr = device->CreateInputLayout(layout, 2, outShader.Blob->GetBufferPointer(), 
                                    outShader.Blob->GetBufferSize(), &outShader.InputLayout);
    if (FAILED(hr)) {
        std::cerr << "[RenderNode] Failed to create input layout: 0x" << std::hex << hr << std::endl;
        return false;
    }

    return true;
}

void RenderNode::SetupRenderState(ID3D11DeviceContext* d3d11Context,
                                  std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                                  uint32_t width, uint32_t height) {
    if (!d3d11Context || !m_CurrentShader || !m_CommonResourcesInitialized) {
        return;
    }

    // 设置 Vertex Shader
    d3d11Context->VSSetShader(m_CurrentShader->VS.Get(), nullptr, 0);

    // 设置 Pixel Shader
    d3d11Context->PSSetShader(m_CurrentShader->PS.Get(), nullptr, 0);

    // 设置输入布局
    d3d11Context->IASetInputLayout(m_CurrentShader->InputLayout.Get());

    // 设置顶点缓冲区
    RenderCore::D3D11VertexBuffer* d3d11VB = dynamic_cast<RenderCore::D3D11VertexBuffer*>(m_CommonVertexBuffer.get());
    if (d3d11VB) {
        ID3D11Buffer* vb = d3d11VB->GetNativeBuffer();
        if (vb) {
            UINT stride = sizeof(SimpleVertex);
            UINT offset = 0;
            d3d11Context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        }
    }

    // 设置图元拓扑
    d3d11Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // 设置输入纹理和采样器（由子类实现具体逻辑）
    SetShaderResources(inputTexture);

    // 禁用深度测试和混合（全屏四边形不需要）
    d3d11Context->OMSetDepthStencilState(nullptr, 0);
    float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    d3d11Context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
}

void RenderNode::Draw(ID3D11DeviceContext* d3d11Context) {
    if (!d3d11Context) {
        return;
    }
    // 绘制全屏四边形（4 个顶点，三角形条带）
    d3d11Context->Draw(4, 0);
}

bool RenderNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) {
    if (!m_CommandContext || !inputTexture || !outputTarget || !m_CommonResourcesInitialized) {
        return false;
    }

    if (!m_CurrentShader) {
        std::cerr << "[RenderNode] Current shader not set" << std::endl;
        return false;
    }

    // 获取 D3D11 命令上下文
    RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
    if (!d3d11RHI) {
        return false;
    }
    
    ID3D11DeviceContext* d3d11Context = d3d11RHI->GetDeviceContext();
    if (!d3d11Context) {
        return false;
    }

    // 1. 更新 constant buffers（由子类实现）
    UpdateConstantBuffers(width, height);

    // 2. 设置渲染目标
    m_CommandContext->SetRenderTarget(outputTarget, nullptr);

    // 3. 设置视口
    m_CommandContext->SetViewPort(0, 0, width, height);

    // 4. 清除渲染目标
    m_CommandContext->Clear(outputTarget, nullptr, core::FLinearColor(0, 0, 0, 1));

    // 5. 设置 constant buffers（由子类实现）
    SetConstantBuffers();

    // 6. 设置渲染状态（shader、顶点缓冲区、纹理、采样器等）
    SetupRenderState(d3d11Context, inputTexture, width, height);

    // 7. 绘制（由子类可以重写以自定义）
    Draw(d3d11Context);

    // 8. 清理状态（可选，避免影响后续渲染）
    ID3D11ShaderResourceView* nullSRV = nullptr;
    d3d11Context->PSSetShaderResources(0, 1, &nullSRV);

    // 9. 刷新命令
    m_CommandContext->FlushCommands();
    
    return true;
}

} // namespace LightroomCore





