#include "FilterNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11UniformBuffer.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include "../d3d11rhi/D3D11State.h"
#include "../d3d11rhi/RHI.h"
#include "../d3d11rhi/Common.h"
#include <d3dcompiler.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace LightroomCore {

FilterNode::FilterNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : RenderNode(rhi)
    , m_ShaderResourcesInitialized(false)
    , m_LUTSize(0)
    , m_Intensity(1.0f)
{
    InitializeShaderResources();
}

FilterNode::~FilterNode() {
    CleanupShaderResources();
}

bool FilterNode::InitializeShaderResources() {
    if (m_ShaderResourcesInitialized || !m_RHI) {
        return m_ShaderResourcesInitialized;
    }

    // ---------------------------------------------------------
    // Vertex Shader (保持不变)
    // ---------------------------------------------------------
    const char* vsCode = R"(
        struct VSInput {
            float2 Position : POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        struct VSOutput {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.Position = float4(input.Position, 0.0, 1.0);
            output.TexCoord = input.TexCoord;
            return output;
        }
    )";

    // ---------------------------------------------------------
    // Pixel Shader (完全重写)
    // ---------------------------------------------------------
    // 修正点：
    // 1. 适配水平长条纹理 (Width = Size*Size, Height = Size)
    // 2. 实现双切片混合 (模拟 3D 纹理的三线性插值)，消除颜色断层
    const char* psCode = R"(
        Texture2D inputTexture : register(t0);
        Texture2D lutTexture : register(t1);
        SamplerState samLinear : register(s0);
        
        cbuffer FilterParams : register(b0) {
            float LUTSize;
            float Intensity;
            float2 Padding;
        };
        
        struct PSInput {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        
        // 计算单个切片的 UV
        float2 ComputeSliceUV(float3 color, float sliceIndex, float size) {
            // 每一个切片(Blue块)的宽度是 size，总宽度是 size * size
            // U 坐标 = (SliceIndex * size + Red * (size-1)) / (size * size)
            // V 坐标 = (Green * (size-1)) / size
            
            // 半像素偏移，确保采样中心
            float pixelSize = 1.0 / size;
            float halfPixel = 0.5 / size;
            
            // 基础坐标 (0 到 1)
            float r = color.r;
            float g = color.g;
            
            // 映射到像素空间 (0 到 size-1) + 0.5
            // 注意：标准的 LUT 采样通常缩放范围是 (size - 1) / size
            float scale = (size - 1.0) / size;
            float offset = 0.5 / size;
            
            float u_local = r * scale + offset; // 在当前块内的 U (0..1)
            float v_local = g * scale + offset; // V (0..1)
            
            // 计算全局 U
            // SliceIndex / size 将我们移动到正确的 Blue 块
            // u_local / size 将 Red 映射到该块内
            float u_global = (sliceIndex + u_local) / size;
            
            return float2(u_global, v_local);
        }

        float3 SampleLUT(float3 color) {
            color = saturate(color);
            
            float size = LUTSize;
            
            // 计算 Blue 通道在 LUT 中的位置
            // 范围从 0 到 size-1
            float blueScaled = color.b * (size - 1.0);
            
            // 找到相邻的两个 Blue 切片 (Slice)
            float slice0 = floor(blueScaled);
            float slice1 = min(size - 1.0, slice0 + 1.0);
            
            // 计算两个切片的 UV 坐标
            float2 uv0 = ComputeSliceUV(color, slice0, size);
            float2 uv1 = ComputeSliceUV(color, slice1, size);
            
            // 采样两个切片
            float3 color0 = lutTexture.Sample(samLinear, uv0).rgb;
            float3 color1 = lutTexture.Sample(samLinear, uv1).rgb;
            
            // 在两个切片之间进行线性插值 (Lerp)
            float fraction = blueScaled - slice0;
            return lerp(color0, color1, fraction);
        }
        
        float4 main(PSInput input) : SV_Target {
            float4 originalColor = inputTexture.Sample(samLinear, input.TexCoord);
            float3 rgb = originalColor.rgb;
            
            if (LUTSize < 1.0) {
                return originalColor;
            }
            
            float3 lutColor = SampleLUT(rgb);
            
            // 根据强度混合
            float3 finalColor = lerp(rgb, lutColor, Intensity);
            
            return float4(finalColor, originalColor.a);
        }
    )";

    std::cout << "[FilterNode] Compiling shaders..." << std::endl;
    if (!CompileShaders(vsCode, psCode, m_Shader)) {
        std::cerr << "[FilterNode] Failed to compile shaders" << std::endl;
        return false;
    }
    std::cout << "[FilterNode] Shaders compiled successfully" << std::endl;

    FilterConstantBuffer params = {};
    params.LUTSize = 0.0f;
    params.Intensity = m_Intensity;
    
    m_ParamsBuffer = m_RHI->RHICreateUniformBuffer(
        &params,
        sizeof(FilterConstantBuffer)
    );

    if (!m_ParamsBuffer) {
        std::cerr << "[FilterNode] Failed to create constant buffer" << std::endl;
        return false;
    }

    m_ShaderResourcesInitialized = true;
    m_CurrentShader = &m_Shader;
    return true;
}

void FilterNode::CleanupShaderResources() {
    m_LUTTexture.reset();
    m_ParamsBuffer.reset();
    m_ShaderResourcesInitialized = false;
    m_CurrentShader = nullptr;
}

// ---------------------------------------------------------
// LUT 转换逻辑 (核心修复)
// ---------------------------------------------------------
std::vector<uint8_t> FilterNode::ConvertLUTTo2DTexture(uint32_t lutSize, const float* lutData) {
    if (!lutData || lutSize == 0) {
        return {};
    }

    // 修正：使用水平长条布局
    // 宽度 = Size * Size (Blue 沿 X 轴展开)
    // 高度 = Size (Green 沿 Y 轴)
    uint32_t width = lutSize * lutSize;
    uint32_t height = lutSize;
    uint32_t totalPixels = width * height;
    
    std::vector<uint8_t> textureData(totalPixels * 4);

    // 标准 .CUBE 文件数据顺序通常是:
    // Red 变化最快, 然后是 Green, 最后是 Blue.
    // Index = b * (Size*Size) + g * Size + r
    
    for (uint32_t b = 0; b < lutSize; ++b) {
        for (uint32_t g = 0; g < lutSize; ++g) {
            for (uint32_t r = 0; r < lutSize; ++r) {
                
                // 1. 获取源数据索引 (假设输入 lutData 是按照标准 3D 顺序排列的 RGBRGB...)
                uint32_t srcIndex = (b * lutSize * lutSize) + (g * lutSize) + r;
                
                // 2. 计算目标 2D 纹理坐标 (水平长条)
                // Y = Green
                // X = Blue * Size + Red
                uint32_t texY = g; 
                uint32_t texX = b * lutSize + r;
                
                // 目标像素索引 (行主序: Y * Width + X)
                uint32_t dstIndex = texY * width + texX;
                
                // 3. 读取数据并处理格式
                // 输入数据是 RGB float [0, 1]
                float rVal = lutData[srcIndex * 3 + 0];
                float gVal = lutData[srcIndex * 3 + 1];
                float bVal = lutData[srcIndex * 3 + 2];
                
                // 4. 写入 PF_B8G8R8A8 (DX11 常用格式)
                // 内存布局: Byte0=B, Byte1=G, Byte2=R, Byte3=A
                textureData[dstIndex * 4 + 0] = static_cast<uint8_t>(std::clamp(bVal * 255.0f, 0.0f, 255.0f)); // B
                textureData[dstIndex * 4 + 1] = static_cast<uint8_t>(std::clamp(gVal * 255.0f, 0.0f, 255.0f)); // G
                textureData[dstIndex * 4 + 2] = static_cast<uint8_t>(std::clamp(rVal * 255.0f, 0.0f, 255.0f)); // R
                textureData[dstIndex * 4 + 3] = 255; // Alpha
            }
        }
    }

    return textureData;
}

bool FilterNode::LoadLUT(uint32_t lutSize, const float* lutData) {
    if (!lutData || lutSize == 0 || lutSize > 256) {
        std::cerr << "[FilterNode] Invalid LUT parameters: lutSize=" << lutSize << std::endl;
        return false;
    }

    std::cout << "[FilterNode] Loading LUT: " << lutSize << "^3" << std::endl;

    // 转换数据
    std::vector<uint8_t> textureData = ConvertLUTTo2DTexture(lutSize, lutData);
    if (textureData.empty()) {
        std::cerr << "[FilterNode] Failed to convert LUT data" << std::endl;
        return false;
    }

    // 修正：创建纹理时使用正确的尺寸
    uint32_t width = lutSize * lutSize; // 宽度变大
    uint32_t height = lutSize;          // 高度保持为 Size
    uint32_t rowBytes = width * 4;

    m_LUTTexture = m_RHI->RHICreateTexture2D(
        RenderCore::EPixelFormat::PF_B8G8R8A8,
        RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
        width,
        height,
        1, 
        textureData.data(),
        rowBytes
    );

    if (!m_LUTTexture) {
        std::cerr << "[FilterNode] Failed to create LUT texture" << std::endl;
        return false;
    }

    m_LUTSize = lutSize;
    
    // 更新 Constant Buffer
    if (m_CommandContext && m_ParamsBuffer) {
        FilterConstantBuffer params = {};
        params.LUTSize = static_cast<float>(m_LUTSize);
        params.Intensity = m_Intensity;
        m_CommandContext->RHIUpdateUniformBuffer(m_ParamsBuffer, &params);
    }

    std::cout << "[FilterNode] LUT Loaded. Texture Size: " << width << "x" << height << std::endl;
    return true;
}

bool FilterNode::LoadLUTFromTexture(std::shared_ptr<RenderCore::RHITexture2D> lutTexture, uint32_t lutSize) {
    if (!lutTexture || lutSize == 0 || lutSize > 256) {
        std::cerr << "[FilterNode] Invalid LUT texture parameters" << std::endl;
        return false;
    }

    m_LUTTexture = lutTexture;
    m_LUTSize = lutSize;
    
    // 更新 constant buffer（将在下次渲染时通过 UpdateConstantBuffers 更新）

    std::cout << "[FilterNode] LUT texture loaded successfully: " << lutSize << "x" << lutSize << "x" << lutSize << std::endl;
    return true;
}

bool FilterNode::LoadLUTFromFile(const char* filePath) {
    if (!filePath) {
        std::cerr << "[FilterNode] Invalid file path" << std::endl;
        return false;
    }
    
    // 转换 UTF-8 路径为宽字符路径（Windows 需要）
    std::wstring wFilePath;
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, filePath, -1, nullptr, 0);
    if (pathLen > 0) {
        wFilePath.resize(pathLen);
        MultiByteToWideChar(CP_UTF8, 0, filePath, -1, &wFilePath[0], pathLen);
        if (!wFilePath.empty() && wFilePath.back() == L'\0') {
            wFilePath.pop_back();
        }
    } else {
        // 如果 UTF-8 转换失败，尝试使用当前代码页
        pathLen = MultiByteToWideChar(CP_ACP, 0, filePath, -1, nullptr, 0);
        if (pathLen > 0) {
            wFilePath.resize(pathLen);
            MultiByteToWideChar(CP_ACP, 0, filePath, -1, &wFilePath[0], pathLen);
            if (!wFilePath.empty() && wFilePath.back() == L'\0') {
                wFilePath.pop_back();
            }
        } else {
            std::cerr << "[FilterNode] Failed to convert file path to wide string" << std::endl;
            return false;
        }
    }

    // 打开文件（使用宽字符路径）
    std::filebuf fileBuf;
    if (!fileBuf.open(wFilePath, std::ios::in)) {
        std::cerr << "[FilterNode] Failed to open file: " << filePath << std::endl;
        return false;
    }
    std::istream file(&fileBuf);

    uint32_t lutSize = 0;
    float domainMin[3] = { 0.0f, 0.0f, 0.0f };
    float domainMax[3] = { 1.0f, 1.0f, 1.0f };
    std::vector<float> lutData; // 存储 RGB 浮点数
    
    std::string line;
    bool foundLUTSize = false;
    bool foundDomainMin = false;
    bool foundDomainMax = false;
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty()) continue;
        
        if (line.find("LUT_3D_SIZE") == 0) {
            std::istringstream iss(line);
            std::string token;
            iss >> token >> lutSize;
            foundLUTSize = true;
            if (lutSize > 0) lutData.reserve(lutSize * lutSize * lutSize * 3);
            continue;
        }
        if (line.find("DOMAIN_MIN") == 0) {
            std::istringstream iss(line);
            std::string token;
            iss >> token >> domainMin[0] >> domainMin[1] >> domainMin[2];
            foundDomainMin = true;
            continue;
        }
        if (line.find("DOMAIN_MAX") == 0) {
            std::istringstream iss(line);
            std::string token;
            iss >> token >> domainMax[0] >> domainMax[1] >> domainMax[2];
            foundDomainMax = true;
            continue;
        }
        
        if (foundLUTSize) {
            std::istringstream iss(line);
            float r, g, b;
            if (iss >> r >> g >> b) {
                lutData.push_back(r);
                lutData.push_back(g);
                lutData.push_back(b);
            }
        }
    }
    fileBuf.close();

    if (!foundLUTSize || lutData.size() != lutSize * lutSize * lutSize * 3) {
        std::cerr << "[FilterNode] Invalid LUT file format" << std::endl;
        return false;
    }

    // 处理 Domain 映射
    if (foundDomainMin && foundDomainMax) {
         for (size_t i = 0; i < lutData.size(); i += 3) {
             // 简单的归一化
             for(int c=0; c<3; ++c) {
                 float range = domainMax[c] - domainMin[c];
                 if(abs(range) > 1e-6) 
                    lutData[i+c] = (lutData[i+c] - domainMin[c]) / range;
                 lutData[i+c] = std::clamp(lutData[i+c], 0.0f, 1.0f);
             }
         }
    }

    // 关键调用：加载数据
    return LoadLUT(lutSize, lutData.data());
}

void FilterNode::UpdateConstantBuffers(uint32_t width, uint32_t height) {
    if (!m_ParamsBuffer || !m_CommandContext) return;
    FilterConstantBuffer params = {};
    params.LUTSize = static_cast<float>(m_LUTSize);
    params.Intensity = m_Intensity;
    m_CommandContext->RHIUpdateUniformBuffer(m_ParamsBuffer, &params);
}

void FilterNode::SetConstantBuffers() {
    if (m_CommandContext && m_ParamsBuffer) {
        m_CommandContext->RHISetShaderUniformBuffer(RenderCore::EShaderFrequency::SF_Pixel, 0, m_ParamsBuffer);
    }
}

void FilterNode::SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) {
    if (!m_CommandContext || !inputTexture) return;
    m_CommandContext->RHISetShaderTexture(RenderCore::EShaderFrequency::SF_Pixel, 0, inputTexture);
    if (m_LUTTexture) {
        m_CommandContext->RHISetShaderTexture(RenderCore::EShaderFrequency::SF_Pixel, 1, m_LUTTexture);
    }
    if (m_CommonSamplerState) {
        m_CommandContext->RHISetShaderSampler(RenderCore::EShaderFrequency::SF_Pixel, 0, m_CommonSamplerState);
    }
}

bool FilterNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) {
    if (!m_ShaderResourcesInitialized) return false;
    if (!m_LUTTexture || m_LUTSize == 0) return false; // 允许静默失败或透传

    m_CurrentShader = &m_Shader;
    return RenderNode::Execute(inputTexture, outputTarget, width, height);
}

} // namespace LightroomCore
