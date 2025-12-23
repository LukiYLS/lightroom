#pragma once

#include "ImagePyramid.h"
#include "GPUTileCache.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_set>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace LightroomCore {

// Tile加载器（同步加载）
// 从内存或磁盘加载Tile数据并上传到GPU
class TileLoader {
public:
    TileLoader();
    ~TileLoader();
    
    // 初始化加载器
    bool Initialize(std::shared_ptr<ImagePyramid> pyramid, 
                   std::shared_ptr<GPUTileCache> cache);
    
    // 设置已加载的图像数据（避免重复从磁盘加载）
    void SetCachedImageData(const uint8_t* imageData, uint32_t width, uint32_t height);
    
    // 加载Tile（同步）
    // 返回是否成功加载并上传到GPU
    bool LoadTile(const TileCoordinate& coord);
    
    // 加载多个Tile（同步）
    // 返回成功加载的Tile数量
    uint32_t LoadTiles(const std::vector<TileCoordinate>& coords);
    
    // 从磁盘或内存加载Tile数据（公开接口，用于批量加载）
    bool LoadTileData(const TileCoordinate& coord, std::vector<uint8_t>& outData);
    
private:
    
    
private:
    // 初始化WIC资源（延迟初始化，只初始化一次）
    bool InitializeWICResources();
    
    // 从WIC直接读取指定区域（不加载全图）
    bool ReadRegionFromWIC(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                          std::vector<uint8_t>& outData);
    
    // 使用WIC Scaler进行一次性下采样
    bool DownsampleWithWICScaler(uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight,
                                uint32_t dstWidth, uint32_t dstHeight,
                                std::vector<uint8_t>& outData);
    
    // 调试：将Tile数据dump到文件（相同tile只dump一次）
    void DumpTileToFile(const TileCoordinate& coord, const uint8_t* tileData);
    
private:
    std::shared_ptr<ImagePyramid> m_Pyramid;
    std::shared_ptr<GPUTileCache> m_Cache;
    
    // WIC资源（延迟初始化，避免全量解压）
    Microsoft::WRL::ComPtr<IWICImagingFactory> m_WICFactory;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> m_WICDecoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> m_WICFrame;
    Microsoft::WRL::ComPtr<IWICFormatConverter> m_WICConverter;
    uint32_t m_ImageWidth;
    uint32_t m_ImageHeight;
    bool m_WICInitialized;
    
    // 已dump的tiles集合（用于避免重复dump）
    std::unordered_set<TileCoordinate, TileCoordinate::Hash> m_DumpedTiles;
    
    static constexpr uint32_t TILE_SIZE = 256;
};

} // namespace LightroomCore
