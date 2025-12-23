#pragma once

#include "ImagePyramid.h"
#include "../d3d11rhi/DynamicRHI.h"
#include "../d3d11rhi/D3D11RHI.h"
#include <d3d11.h>
#include <memory>
#include <unordered_map>
#include <list>
#include <atomic>

namespace LightroomCore {

// GPU Tile Cache中的插槽信息
struct TileSlot {
    TileCoordinate LogicalCoord;  // 逻辑坐标（Level, X, Y）
    uint32_t PhysicalIndex;      // 物理插槽索引
    bool bInUse;                 // 是否正在使用
    uint64_t LastAccessTime;     // 最后访问时间（用于LRU）
    
    TileSlot() : PhysicalIndex(0), bInUse(false), LastAccessTime(0) {}
};

// GPU Tile Cache管理器
// 使用ID3D11Texture2DArray维护固定大小的物理插槽池
// 实现LRU置换策略和异步加载
class GPUTileCache {
public:
    GPUTileCache();
    ~GPUTileCache();
    
    // 初始化GPU Tile Cache
    // poolSizeMB: 物理池大小（MB），范围512MB-2GB
    bool Initialize(std::shared_ptr<RenderCore::DynamicRHI> rhi, uint32_t poolSizeMB = 1024);
    
    // 清理资源
    void Shutdown();
    
    // 请求Tile（如果不在GPU中，会触发异步加载）
    // 返回物理插槽索引，如果失败返回-1
    int32_t RequestTile(const TileCoordinate& coord, const uint8_t* tileData = nullptr);
    
    // 释放Tile（标记为可置换）
    void ReleaseTile(const TileCoordinate& coord);
    
    // 获取Tile的物理插槽索引（如果已在GPU中）
    int32_t GetTileSlotIndex(const TileCoordinate& coord) const;
    
    // 获取Texture2DArray（用于渲染）
    ID3D11Texture2D* GetTextureArray() const { return m_TextureArray.Get(); }
    ID3D11ShaderResourceView* GetSRV() const { return m_SRV.Get(); }
    
    // 获取Tile的物理索引（用于Shader查找）
    // 返回-1表示Tile不在GPU中
    int32_t GetTilePhysicalIndex(const TileCoordinate& coord) const;
    
    // 更新Tile数据到GPU（同步）
    bool UpdateTileData(uint32_t slotIndex, const uint8_t* tileData);
    
    // 获取缓存统计信息
    void GetCacheStats(uint32_t& usedSlots, uint32_t& totalSlots, uint32_t& hitCount, uint32_t& missCount) const {
        usedSlots = m_UsedSlotCount;
        totalSlots = m_TotalSlots;
        hitCount = m_HitCount;
        missCount = m_MissCount;
    }
    
    // 设置池大小（需要重新初始化）
    void SetPoolSize(uint32_t poolSizeMB);
    
    // 批量请求Tiles（避免重复映射，返回需要加载的tiles）
    // visibleTiles: 输入的可见tiles列表
    // outTilesToLoad: 输出的需要加载的tiles列表（不在GPU中的）
    // outTilesInGPU: 输出的已在GPU中的tiles及其物理索引
    void BatchRequestTiles(const std::vector<TileCoordinate>& visibleTiles,
                           std::vector<TileCoordinate>& outTilesToLoad,
                           std::vector<std::pair<TileCoordinate, int32_t>>& outTilesInGPU);
    
    // 批量更新Tile数据到GPU（一次性更新多个tiles，避免频繁调用UpdateSubresource）
    // tilesToUpdate: 需要更新的tiles列表，每个元素是 (TileCoordinate, tileData指针)
    // 返回成功更新的数量
    uint32_t BatchUpdateTileData(const std::vector<std::pair<TileCoordinate, const uint8_t*>>& tilesToUpdate);
    
private:
    // 查找空闲插槽
    int32_t FindFreeSlot();
    
    // 使用LRU策略查找可置换的插槽
    int32_t FindLRUSlot();
    
    // 更新访问时间
    void UpdateAccessTime(uint32_t slotIndex);
    
    // 创建Texture2DArray
    // slotCount: 输入输出参数，如果超过限制会被限制到最大值
    bool CreateTextureArray(uint32_t& slotCount);
    
    // 计算插槽数量（基于池大小）
    uint32_t CalculateSlotCount(uint32_t poolSizeMB) const;
    
private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    RenderCore::D3D11DynamicRHI* m_D3D11RHI;
    
    // GPU资源
    ComPtr<ID3D11Texture2D> m_TextureArray;
    ComPtr<ID3D11ShaderResourceView> m_SRV;
    
    // 插槽管理
    uint32_t m_TotalSlots;
    uint32_t m_UsedSlotCount;
    std::vector<TileSlot> m_Slots;
    
    // 映射表：逻辑坐标 -> 物理插槽索引
    std::unordered_map<TileCoordinate, uint32_t, TileCoordinate::Hash> m_LogicalToPhysical;
    
    // LRU链表（按访问时间排序）
    std::list<uint32_t> m_LRUList;  // 存储插槽索引，最久未访问的在前面
    
    // 统计信息
    mutable std::atomic<uint32_t> m_HitCount;
    mutable std::atomic<uint32_t> m_MissCount;
    
    // 时间戳计数器（用于LRU）
    std::atomic<uint64_t> m_TimeCounter;
    
    static constexpr uint32_t TILE_SIZE = 256;  // 每个Tile为256x256像素
    static constexpr uint32_t BYTES_PER_PIXEL = 4;  // BGRA格式
    static constexpr uint32_t TILE_SIZE_BYTES = TILE_SIZE * TILE_SIZE * BYTES_PER_PIXEL;  // 256KB per tile
};

} // namespace LightroomCore
