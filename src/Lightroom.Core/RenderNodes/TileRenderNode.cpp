#include "TileRenderNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include "../d3d11rhi/D3D11State.h"
#include "../d3d11rhi/D3D11VertexBuffer.h"
#include "../d3d11rhi/RHI.h"
#include "../d3d11rhi/Common.h"
#include <d3dcompiler.h>
#include <Windows.h>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cfloat>

#pragma comment(lib, "d3dcompiler.lib")

namespace LightroomCore {

// Constant buffer结构体
struct __declspec(align(16)) TileRenderConstantBuffer {
    float ZoomLevel;
    float PanX;
    float PanY;
    float FitScale;
    float InputImageWidth;
    float InputImageHeight;
    float OutputWidth;
    float OutputHeight;
    float TileSize;           // Tile大小（256）
    float InvTileSize;        // 1.0 / TileSize
    float LODLevel;           // 当前LOD级别
    float Padding[3];
    
    // 图像调整参数（在Tile级别应用）
    float Exposure;
    float Contrast;
    float Highlights;
    float Shadows;
    float Whites;
    float Blacks;
    float Temperature;
    float Saturation;
    float Padding2[1];
};

TileRenderNode::TileRenderNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : RenderNode(rhi)
    , m_ShaderResourcesInitialized(false)
    , m_ZoomLevel(1.0)
    , m_PanX(0.0)
    , m_PanY(0.0)
    , m_InputImageWidth(0)
    , m_InputImageHeight(0) {
    InitializeShaderResources();
}

TileRenderNode::~TileRenderNode() {
    CleanupShaderResources();
}

bool TileRenderNode::InitializeShaderResources() {
    if (m_ShaderResourcesInitialized || !m_RHI) {
        return m_ShaderResourcesInitialized;
    }

    // 优化4: 全屏Quad + Texture2DArray采样
    // Vertex Shader: 简单的全屏Quad，在Pixel Shader中采样Texture2DArray
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

    // Pixel Shader: 将Tile拼装成完整的图像纹理
    // 注意：这里输出的是完整的原始图像尺寸，后续节点会处理缩放和调整
    const char* psCode = R"(
        Texture2DArray TileArray : register(t0);
        SamplerState TileSampler : register(s0);
        
        // StructuredBuffer存储Tile实例数据
        struct TileInstanceData {
            float TileX;
            float TileY;
            float TileWidth;
            float TileHeight;
            float ArrayIndex;
            float LODLevel;
            float2 Padding;
        };
        StructuredBuffer<TileInstanceData> TileInstances : register(t1);
        
        cbuffer TileRenderParams : register(b0) {
            float ZoomLevel;
            float PanX;
            float PanY;
            float FitScale;
            float InputImageWidth;
            float InputImageHeight;
            float OutputWidth;
            float OutputHeight;
            float TileSize;
            float InvTileSize;
            float LODLevel;
        };
        
        struct PSInput {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        
        float4 main(PSInput input) : SV_Target {
            // input.TexCoord是输出纹理的UV坐标 [0,1]
            // 转换为原始图像的像素坐标
            float pixelX = input.TexCoord.x * InputImageWidth;
            float pixelY = input.TexCoord.y * InputImageHeight;
            
            // 查找对应的Tile实例（遍历所有实例）
            float4 color = float4(0, 0, 0, 0);
            uint instanceCount = 0;
            TileInstances.GetDimensions(instanceCount, instanceCount);
            
            for (uint i = 0; i < instanceCount; ++i) {
                TileInstanceData tile = TileInstances[i];
                
                // 检查当前像素是否在这个Tile范围内
                if (pixelX >= tile.TileX && pixelX < tile.TileX + tile.TileWidth &&
                    pixelY >= tile.TileY && pixelY < tile.TileY + tile.TileHeight &&
                    tile.ArrayIndex >= 0) {
                    
                    // 计算Tile内的UV坐标
                    // 注意：如果Tile来自更高LOD，需要调整UV
                    float2 tileUV;
                    tileUV.x = (pixelX - tile.TileX) / tile.TileWidth;
                    tileUV.y = (pixelY - tile.TileY) / tile.TileHeight;
                    
                    // 采样Texture2DArray
                    color = TileArray.Sample(TileSampler, float3(tileUV, tile.ArrayIndex));
                    break;
                }
            }
            
            return color;
        }
    )";

    // 使用基类的CompileShaders方法
    if (!CompileShaders(vsCode, psCode, m_Shader)) {
        std::ostringstream oss;
        oss << "[TileRenderNode] Failed to compile shaders\n";
        OutputDebugStringA(oss.str().c_str());
        return false;
    }

    // 创建常量缓冲区
    m_ConstantBuffer = m_RHI->RHICreateUniformBuffer(sizeof(TileRenderConstantBuffer));

    // 创建StructuredBuffer用于Tile实例数据
    RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
    if (!d3d11RHI) {
        std::ostringstream oss;
        oss << "[TileRenderNode] Failed to get D3D11RHI\n";
        OutputDebugStringA(oss.str().c_str());
        return false;
    }

    ID3D11Device* device = d3d11RHI->GetDevice();
    if (!device) {
        std::ostringstream oss;
        oss << "[TileRenderNode] Failed to get D3D11Device\n";
        OutputDebugStringA(oss.str().c_str());
        return false;
    }

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(TileInstanceData) * m_MaxTileInstances;
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(TileInstanceData);

    HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, m_TileInstanceBuffer.GetAddressOf());
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[TileRenderNode] Failed to create TileInstanceBuffer\n";
        OutputDebugStringA(oss.str().c_str());
        return false;
    }

    // 创建SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = m_MaxTileInstances;

    hr = device->CreateShaderResourceView(m_TileInstanceBuffer.Get(), &srvDesc, m_TileInstanceSRV.GetAddressOf());
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[TileRenderNode] Failed to create TileInstanceSRV\n";
        OutputDebugStringA(oss.str().c_str());
        return false;
    }

    // 创建全屏Quad
    if (!CreateFullscreenQuad()) {
        return false;
    }

    m_ShaderResourcesInitialized = true;
    return true;
}

void TileRenderNode::CleanupShaderResources() {
    m_Shader.VS.Reset();
    m_Shader.PS.Reset();
    m_ConstantBuffer.reset();
    m_FullscreenQuadVB.reset();
    m_TileInstanceBuffer.Reset();
    m_TileInstanceSRV.Reset();
    m_ShaderResourcesInitialized = false;
}

bool TileRenderNode::CreateFullscreenQuad() {
    // 全屏Quad顶点数据（位置和UV）
    struct FullscreenQuadVertex {
        float Position[2];
        float TexCoord[2];
    };

    FullscreenQuadVertex quadVertices[4] = {
        { {-1.0f,  1.0f}, {0.0f, 0.0f} },  // 左上
        { { 1.0f,  1.0f}, {1.0f, 0.0f} },  // 右上
        { {-1.0f, -1.0f}, {0.0f, 1.0f} },  // 左下
        { { 1.0f, -1.0f}, {1.0f, 1.0f} }   // 右下
    };

    m_FullscreenQuadVB = m_RHI->RHICreateVertexBuffer(
        quadVertices,
        RenderCore::EBufferUsageFlags::BUF_Static,
        sizeof(FullscreenQuadVertex),
        4
    );

    return m_FullscreenQuadVB != nullptr;
}

void TileRenderNode::SetTileResources(std::shared_ptr<ImagePyramid> pyramid, 
                                     std::shared_ptr<GPUTileCache> cache,
                                     std::shared_ptr<TileLoader> loader) {
    m_Pyramid = pyramid;
    m_Cache = cache;
    m_Loader = loader;
}

void TileRenderNode::SetTileProcessGraph(std::shared_ptr<RenderGraph> graph) {
    m_TileProcessGraph = graph;
}

void TileRenderNode::SetZoomParams(double zoomLevel, double panX, double panY) {
    m_ZoomLevel = zoomLevel;
    m_PanX = panX;
    m_PanY = panY;
}

void TileRenderNode::GetZoomParams(double& zoomLevel, double& panX, double& panY) const {
    zoomLevel = m_ZoomLevel;
    panX = m_PanX;
    panY = m_PanY;
}

void TileRenderNode::SetInputImageSize(uint32_t width, uint32_t height) {
    m_InputImageWidth = width;
    m_InputImageHeight = height;
}

// 优化1: 简化CPU端投影计算 - 只计算AABB然后除以256得到Tile索引范围
// 注意：应该使用ImagePyramid::CalculateVisibleTiles，它会自动选择正确的LOD
void TileRenderNode::CalculateVisibleTileRange(uint32_t width, uint32_t height,
                                               uint32_t& startTileX, uint32_t& endTileX,
                                               uint32_t& startTileY, uint32_t& endTileY,
                                               uint32_t& lodLevel) const {
    startTileX = endTileX = startTileY = endTileY = 0;
    lodLevel = 0;

    if (!m_Pyramid || m_InputImageWidth == 0 || m_InputImageHeight == 0) {
        return;
    }

    // 使用ImagePyramid的CalculateVisibleTiles方法，它会自动选择正确的LOD
    std::vector<TileCoordinate> visibleTiles;
    m_Pyramid->CalculateVisibleTiles(
        m_InputImageWidth, m_InputImageHeight,
        0, 0,  // viewport offset (暂时设为0，后续可以从参数传入)
        width, height,
        m_ZoomLevel, m_PanX, m_PanY,
        visibleTiles
    );
    
    if (visibleTiles.empty()) {
        return;
    }
    
    // 从可见Tile列表中提取LOD和范围
    lodLevel = visibleTiles[0].Level;
    uint32_t minX = UINT32_MAX, maxX = 0;
    uint32_t minY = UINT32_MAX, maxY = 0;
    
    for (const auto& coord : visibleTiles) {
        if (coord.Level == lodLevel) {
            minX = (std::min)(minX, coord.X);
            maxX = (std::max)(maxX, coord.X);
            minY = (std::min)(minY, coord.Y);
            maxY = (std::max)(maxY, coord.Y);
        }
    }
    
    startTileX = minX;
    endTileX = maxX;
    startTileY = minY;
    endTileY = maxY;
    
    return;

    // 计算自适应缩放
    float fitScale = 1.0f;
    if (width > 0 && height > 0) {
        float scaleX = static_cast<float>(width) / static_cast<float>(m_InputImageWidth);
        float scaleY = static_cast<float>(height) / static_cast<float>(m_InputImageHeight);
        fitScale = (scaleX < scaleY) ? scaleX : scaleY;
    }

    // 计算图像显示尺寸（归一化）
    float imageDisplayWidthNorm = (m_InputImageWidth * fitScale) / static_cast<float>(width);
    float imageDisplayHeightNorm = (m_InputImageHeight * fitScale) / static_cast<float>(height);

    // 计算屏幕四个角在图像空间中的像素坐标
    struct ScreenCorner {
        float x, y;
    };
    ScreenCorner screenCorners[4] = {
        {-1.0f, -1.0f},  // 左上
        { 1.0f, -1.0f},  // 右上
        {-1.0f,  1.0f},  // 左下
        { 1.0f,  1.0f}   // 右下
    };

    float minPixelX = FLT_MAX, maxPixelX = -FLT_MAX;
    float minPixelY = FLT_MAX, maxPixelY = -FLT_MAX;

    for (int i = 0; i < 4; ++i) {
        float screenX = screenCorners[i].x;
        float screenY = screenCorners[i].y;

        float imagePosX = screenX / imageDisplayWidthNorm;
        float imagePosY = screenY / imageDisplayHeightNorm;

        imagePosX = imagePosX / static_cast<float>(m_ZoomLevel);
        imagePosY = imagePosY / static_cast<float>(m_ZoomLevel);
        imagePosX = imagePosX + static_cast<float>(m_PanX);
        imagePosY = imagePosY + static_cast<float>(m_PanY);

        float imageCoordX = imagePosX * 0.5f + 0.5f;
        float imageCoordY = imagePosY * 0.5f + 0.5f;

        float pixelX = imageCoordX * static_cast<float>(m_InputImageWidth);
        float pixelY = imageCoordY * static_cast<float>(m_InputImageHeight);

        minPixelX = std::min(minPixelX, pixelX);
        maxPixelX = std::max(maxPixelX, pixelX);
        minPixelY = std::min(minPixelY, pixelY);
        maxPixelY = std::max(maxPixelY, pixelY);
    }

    // 计算Tile索引范围（直接除以256）
    constexpr float TILE_SIZE = 256.0f;
    startTileX = static_cast<uint32_t>(std::max(0.0f, std::floor(minPixelX / TILE_SIZE)));
    endTileX = static_cast<uint32_t>(std::min(static_cast<float>(m_InputImageWidth / 256), std::ceil(maxPixelX / TILE_SIZE)));
    startTileY = static_cast<uint32_t>(std::max(0.0f, std::floor(minPixelY / TILE_SIZE)));
    endTileY = static_cast<uint32_t>(std::min(static_cast<float>(m_InputImageHeight / 256), std::ceil(maxPixelY / TILE_SIZE)));
}

// 优化3: 渐进式显示 - 如果LOD 0的Tile没加载，查找对应的LOD 1的Tile
bool TileRenderNode::FindFallbackTile(const TileCoordinate& desiredCoord, TileCoordinate& outCoord, int32_t& outPhysicalIndex) const {
    // 先尝试请求的Tile
    outCoord = desiredCoord;
    outPhysicalIndex = m_Cache->GetTilePhysicalIndex(desiredCoord);
    if (outPhysicalIndex >= 0) {
        return true;
    }

    // 如果LOD 0的Tile没加载，尝试查找LOD 1的对应Tile
    if (desiredCoord.Level == 0) {
        TileCoordinate lod1Coord;
        lod1Coord.Level = 1;
        lod1Coord.X = desiredCoord.X / 2;  // LOD 1的Tile覆盖LOD 0的4个Tile
        lod1Coord.Y = desiredCoord.Y / 2;

        outPhysicalIndex = m_Cache->GetTilePhysicalIndex(lod1Coord);
        if (outPhysicalIndex >= 0) {
            outCoord = lod1Coord;
            return true;
        }
    }

    return false;
}

// 计算当前应该使用的LOD级别
// 核心逻辑：根据图像在屏幕上的实际显示尺寸选择LOD（1:1像素密度原则）
uint32_t TileRenderNode::CalculateLODLevel() const {
    if (!m_Pyramid || m_InputImageWidth == 0 || m_InputImageHeight == 0) {
        return 0;
    }
    
    // 注意：这里需要viewport尺寸，但Execute方法中才有，所以暂时使用简化的逻辑
    // 实际应该调用ImagePyramid::CalculateVisibleTiles，它会自动选择正确的LOD
    
    // 简化版本：根据zoomLevel估算显示尺寸
    // 假设viewport尺寸等于输入图像尺寸（实际应该从参数传入）
    // 这里使用一个保守的估算：zoomLevel越大，显示尺寸越小，应该使用更高的LOD
    
    // 计算自适应缩放（假设viewport等于图像尺寸）
    float fitScale = 1.0f;  // 简化假设
    
    // 计算图像在屏幕上的实际显示尺寸
    float imageDisplayWidth = m_InputImageWidth * fitScale / static_cast<float>(m_ZoomLevel);
    float imageDisplayHeight = m_InputImageHeight * fitScale / static_cast<float>(m_ZoomLevel);
    
    // 选择最接近显示尺寸的LOD层级
    uint32_t lodLevel = 0;
    float minDiff = FLT_MAX;
    
    for (uint32_t i = 0; i < m_Pyramid->GetLevelCount(); ++i) {
        PyramidLevel levelInfo;
        if (m_Pyramid->GetLevelInfo(i, levelInfo)) {
            // 计算该LOD层级与显示尺寸的差异
            float diff = std::abs(static_cast<float>(levelInfo.Width) - imageDisplayWidth) +
                         std::abs(static_cast<float>(levelInfo.Height) - imageDisplayHeight);
            
            if (diff < minDiff) {
                minDiff = diff;
                lodLevel = i;
            }
        }
    }
    
    return lodLevel;
}

// 优化4: 分层处理 - 先更新所有可见Tile到显存池
void TileRenderNode::UpdateVisibleTilesToGPU(const std::vector<TileCoordinate>& tiles) {
    if (!m_Loader) {
        return;
    }

    // 加载所有需要的Tile
    m_Loader->LoadTiles(tiles);
}

void TileRenderNode::UpdateConstantBuffers(uint32_t width, uint32_t height) {
    if (!m_ConstantBuffer) {
        return;
    }

    // 计算自适应缩放
    float fitScale = 1.0f;
    if (width > 0 && height > 0 && m_InputImageWidth > 0 && m_InputImageHeight > 0) {
        float scaleX = static_cast<float>(width) / static_cast<float>(m_InputImageWidth);
        float scaleY = static_cast<float>(height) / static_cast<float>(m_InputImageHeight);
        fitScale = (scaleX < scaleY) ? scaleX : scaleY;
    }

    TileRenderConstantBuffer cbData;
    cbData.ZoomLevel = static_cast<float>(m_ZoomLevel);
    cbData.PanX = static_cast<float>(m_PanX);
    cbData.PanY = static_cast<float>(m_PanY);
    cbData.FitScale = fitScale;
    cbData.InputImageWidth = static_cast<float>(m_InputImageWidth);
    cbData.InputImageHeight = static_cast<float>(m_InputImageHeight);
    cbData.OutputWidth = static_cast<float>(width);
    cbData.OutputHeight = static_cast<float>(height);
    cbData.TileSize = 256.0f;
    cbData.InvTileSize = 1.0f / 256.0f;
    cbData.LODLevel = static_cast<float>(CalculateLODLevel());

    m_CommandContext->RHIUpdateUniformBuffer(m_ConstantBuffer, &cbData);
}

void TileRenderNode::SetConstantBuffers() {
    if (!m_CommandContext || !m_ConstantBuffer) {
        return;
    }

    m_CommandContext->RHISetShaderUniformBuffer(RenderCore::EShaderFrequency::SF_Pixel, 0, m_ConstantBuffer);
    m_CommandContext->RHISetShaderUniformBuffer(RenderCore::EShaderFrequency::SF_Vertex, 0, m_ConstantBuffer);
}

void TileRenderNode::SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) {
    if (!m_CommandContext || !m_Cache) {
        return;
    }

    // 设置Texture2DArray
    ID3D11ShaderResourceView* srv = m_Cache->GetSRV();
    if (srv) {
        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
        if (d3d11RHI) {
            ID3D11DeviceContext* context = d3d11RHI->GetDeviceContext();
            if (context) {
                context->PSSetShaderResources(0, 1, &srv);
                context->PSSetShaderResources(1, 1, m_TileInstanceSRV.GetAddressOf());
            }
        }
    }

    // 设置采样器（使用基类的公共采样器状态）
    if (m_CommonSamplerState) {
        m_CommandContext->RHISetShaderSampler(RenderCore::EShaderFrequency::SF_Pixel, 0, m_CommonSamplerState);
    }
}

bool TileRenderNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                            std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                            uint32_t width, uint32_t height) {
    if (!m_Pyramid) {
        return false;
    }

    // 暂时只调试CalculateVisibleTiles，不做实际渲染
    // 调用CalculateVisibleTiles并输出调试信息
    std::vector<TileCoordinate> visibleTiles;
    m_Pyramid->CalculateVisibleTiles(
        m_InputImageWidth, m_InputImageHeight,
        0, 0,  // viewport offset (暂时设为0)
        width, height,
        m_ZoomLevel, m_PanX, m_PanY,
        visibleTiles
    );
    
    // 调试输出：显示找到的Tile
    std::ostringstream oss;
    oss << "[TileRenderNode] Execute: Found " << visibleTiles.size() << " visible tiles\n";
    OutputDebugStringA(oss.str().c_str());
    
    // 暂时返回true，实际渲染逻辑后续实现
    return true;
}

} // namespace LightroomCore
