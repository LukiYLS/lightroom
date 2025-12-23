#pragma once

#include "RenderNode.h"
#include "../d3d11rhi/RHITexture2D.h"
#include "../d3d11rhi/RHIShdader.h"
#include "../d3d11rhi/RHIUniformBuffer.h"
#include "../d3d11rhi/RHIVertexBuffer.h"
#include "../ImageProcessing/ImagePyramid.h"
#include "../ImageProcessing/GPUTileCache.h"
#include "../ImageProcessing/TileLoader.h"
#include "../RenderGraph.h"
#include <memory>
#include <vector>

namespace LightroomCore {

// Tile渲染节点：使用Texture2DArray中的Tile进行渲染
// 优化版本：使用实例化渲染 + 全屏Quad采样
class TileRenderNode : public RenderNode {
public:
    TileRenderNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~TileRenderNode();

    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) override;

    virtual const char* GetName() const override { return "TileRender"; }

    // 设置Tile资源
    void SetTileResources(std::shared_ptr<ImagePyramid> pyramid, 
                         std::shared_ptr<GPUTileCache> cache,
                         std::shared_ptr<TileLoader> loader);

    // 设置缩放参数
    void SetZoomParams(double zoomLevel, double panX, double panY);

    // 获取缩放参数
    void GetZoomParams(double& zoomLevel, double& panX, double& panY) const;

    // 设置输入图片尺寸
    void SetInputImageSize(uint32_t width, uint32_t height);
    
    // 设置Tile处理RenderGraph（对每个Tile应用的处理节点，如ImageAdjustNode）
    void SetTileProcessGraph(std::shared_ptr<RenderGraph> graph);

protected:
    virtual void UpdateConstantBuffers(uint32_t width, uint32_t height) override;
    virtual void SetConstantBuffers() override;
    virtual void SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) override;

private:
    bool InitializeShaderResources();
    void CleanupShaderResources();

    // 计算当前应该使用的LOD级别
    uint32_t CalculateLODLevel() const;
    
    // 优化1: 简化CPU端投影计算 - 只计算AABB然后除以256得到Tile索引范围
    void CalculateVisibleTileRange(uint32_t width, uint32_t height,
                                   uint32_t& startTileX, uint32_t& endTileX,
                                   uint32_t& startTileY, uint32_t& endTileY,
                                   uint32_t& lodLevel) const;
    
    // 优化3: 渐进式显示 - 如果LOD 0的Tile没加载，查找对应的LOD 1的Tile
    bool FindFallbackTile(const TileCoordinate& desiredCoord, TileCoordinate& outCoord, int32_t& outPhysicalIndex) const;
    
    // 优化4: 分层处理 - 先更新所有可见Tile到显存池
    void UpdateVisibleTilesToGPU(const std::vector<TileCoordinate>& tiles);
    
    // 创建全屏Quad顶点缓冲区
    bool CreateFullscreenQuad();

    // Tile实例数据（用于StructuredBuffer）
    struct TileInstanceData {
        float TileX;           // Tile在图像中的X坐标（像素）
        float TileY;           // Tile在图像中的Y坐标（像素）
        float TileWidth;       // Tile宽度（像素）
        float TileHeight;      // Tile高度（像素）
        float ArrayIndex;      // Tile在Texture2DArray中的索引（-1表示未加载）
        float LODLevel;        // Tile的LOD级别
        float Padding[2];
    };

    // Shader resources
    CompiledShader m_Shader;
    std::shared_ptr<RenderCore::RHIUniformBuffer> m_ConstantBuffer;
    
    // 全屏Quad顶点缓冲区
    std::shared_ptr<RenderCore::RHIVertexBuffer> m_FullscreenQuadVB;
    
    // StructuredBuffer用于实例化渲染（存储Tile实例数据）
    ComPtr<ID3D11Buffer> m_TileInstanceBuffer;
    ComPtr<ID3D11ShaderResourceView> m_TileInstanceSRV;
    uint32_t m_MaxTileInstances = 1024;  // 最大Tile实例数

    // Tile资源
    std::shared_ptr<ImagePyramid> m_Pyramid;
    std::shared_ptr<GPUTileCache> m_Cache;
    std::shared_ptr<TileLoader> m_Loader;
    
    // Tile处理RenderGraph（对每个Tile应用的处理流程）
    std::shared_ptr<RenderGraph> m_TileProcessGraph;

    // 渲染参数
    double m_ZoomLevel = 1.0;
    double m_PanX = 0.0;
    double m_PanY = 0.0;
    uint32_t m_InputImageWidth = 0;
    uint32_t m_InputImageHeight = 0;
    
    // Tile临时纹理（用于处理单个Tile）
    std::shared_ptr<RenderCore::RHITexture2D> m_TileTempTexture;

    bool m_ShaderResourcesInitialized = false;
};

} // namespace LightroomCore
