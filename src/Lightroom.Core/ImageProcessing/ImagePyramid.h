#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace LightroomCore {

// Tile坐标：三元组(Level, X, Y)
struct TileCoordinate {
    uint32_t Level;  // LOD层级
    uint32_t X;      // Tile X坐标
    uint32_t Y;      // Tile Y坐标
    
    bool operator==(const TileCoordinate& other) const {
        return Level == other.Level && X == other.X && Y == other.Y;
    }
    
    // 用于unordered_map的哈希函数
    struct Hash {
        std::size_t operator()(const TileCoordinate& coord) const {
            return std::hash<uint32_t>()(coord.Level) ^
                   (std::hash<uint32_t>()(coord.X) << 1) ^
                   (std::hash<uint32_t>()(coord.Y) << 2);
        }
    };
};

// Tile数据（256x256像素，BGRA格式）
struct TileData {
    std::vector<uint8_t> Data;  // 256 * 256 * 4 bytes
    bool bLoaded;               // 是否已加载
    bool bInGPU;                 // 是否在GPU中
    
    TileData() : bLoaded(false), bInGPU(false) {
        Data.resize(256 * 256 * 4, 0);
    }
};

// 图像金字塔层级信息
struct PyramidLevel {
    uint32_t Width;      // 该层级图像宽度
    uint32_t Height;     // 该层级图像高度
    uint32_t TileCountX; // X方向Tile数量
    uint32_t TileCountY; // Y方向Tile数量
    
    PyramidLevel() : Width(0), Height(0), TileCountX(0), TileCountY(0) {}
};

// 图像金字塔管理器
// 负责管理多层级图像切片，支持按需加载
class ImagePyramid {
public:
    ImagePyramid();
    ~ImagePyramid();
    
    // 初始化金字塔（从原始图像尺寸计算所有层级）
    bool Initialize(uint32_t originalWidth, uint32_t originalHeight, const std::string& imagePath);
    
    // 获取指定层级的尺寸信息
    bool GetLevelInfo(uint32_t level, PyramidLevel& outInfo) const;
    
    // 获取总层级数
    uint32_t GetLevelCount() const { return m_Levels.size(); }
    
    // 获取原始图像尺寸
    void GetOriginalSize(uint32_t& width, uint32_t& height) const {
        width = m_OriginalWidth;
        height = m_OriginalHeight;
    }
    
    // 检查Tile是否存在
    bool HasTile(const TileCoordinate& coord) const;
    
    // 获取Tile数据（如果已加载）
    bool GetTileData(const TileCoordinate& coord, TileData& outData);
    
    // 标记Tile为已加载
    void MarkTileLoaded(const TileCoordinate& coord, const uint8_t* data, size_t dataSize);
    
    // 获取需要加载的Tile列表（基于视口和缩放级别）
    void GetTilesForViewport(uint32_t level, 
                            float viewportX, float viewportY, 
                            float viewportWidth, float viewportHeight,
                            float zoomLevel,
                            std::vector<TileCoordinate>& outTiles) const;
    
    // 根据原图大小、viewport和缩放信息计算可见Tile坐标
    // imageWidth, imageHeight: 原图尺寸
    // viewportX, viewportY: 视口偏移（屏幕坐标，像素）
    // viewportWidth, viewportHeight: 视口尺寸（屏幕尺寸，像素）
    // zoomLevel: 缩放级别（1.0 = 100%, 2.0 = 200%等）
    // panX, panY: 平移量（归一化坐标，范围-1到1）
    // outTiles: 输出的可见Tile坐标列表
    void CalculateVisibleTiles(uint32_t imageWidth, uint32_t imageHeight,
                               uint32_t viewportX, uint32_t viewportY,
                               uint32_t viewportWidth, uint32_t viewportHeight,
                               double zoomLevel, double panX, double panY,
                               std::vector<TileCoordinate>& outTiles) const;
    
    // 获取图像路径
    const std::string& GetImagePath() const { return m_ImagePath; }
    
private:
    // 计算指定层级需要的Tile数量
    void CalculateLevelTiles(uint32_t level, uint32_t& tileCountX, uint32_t& tileCountY) const;
    
    // 从原始图像坐标转换到指定层级的坐标
    void ConvertToLevelCoordinates(uint32_t sourceLevel, uint32_t sourceX, uint32_t sourceY,
                                  uint32_t targetLevel, uint32_t& targetX, uint32_t& targetY) const;
    
private:
    uint32_t m_OriginalWidth;
    uint32_t m_OriginalHeight;
    std::vector<PyramidLevel> m_Levels;
    std::string m_ImagePath;
    
    // Tile数据存储（按需加载）
    mutable std::mutex m_TileDataMutex;
    std::unordered_map<TileCoordinate, TileData, TileCoordinate::Hash> m_TileData;
    
    static constexpr uint32_t TILE_SIZE = 256;  // 每个Tile为256x256像素
};

} // namespace LightroomCore
