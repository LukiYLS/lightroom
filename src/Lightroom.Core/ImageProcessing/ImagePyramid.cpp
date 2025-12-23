#define NOMINMAX
#include "ImagePyramid.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cfloat>
#include <Windows.h>
#include <sstream>

namespace LightroomCore {

ImagePyramid::ImagePyramid()
    : m_OriginalWidth(0), m_OriginalHeight(0) {
}

ImagePyramid::~ImagePyramid() {
}

bool ImagePyramid::Initialize(uint32_t originalWidth, uint32_t originalHeight, const std::string& imagePath) {
    if (originalWidth == 0 || originalHeight == 0) {
        return false;
    }
    
    m_OriginalWidth = originalWidth;
    m_OriginalHeight = originalHeight;
    m_ImagePath = imagePath;
    m_Levels.clear();
    
    uint32_t currentWidth = originalWidth;
    uint32_t currentHeight = originalHeight;
    uint32_t level = 0;
    
    while (currentWidth >= TILE_SIZE && currentHeight >= TILE_SIZE) {
        PyramidLevel levelInfo;
        levelInfo.Width = currentWidth;
        levelInfo.Height = currentHeight;
        
        levelInfo.TileCountX = (currentWidth + TILE_SIZE - 1) / TILE_SIZE;  // ???????
        levelInfo.TileCountY = (currentHeight + TILE_SIZE - 1) / TILE_SIZE;
        
        m_Levels.push_back(levelInfo);
        
        currentWidth = (std::max)(1u, currentWidth / 2);
        currentHeight = (std::max)(1u, currentHeight / 2);
        level++;
        
        if (level >= 16) {
            break;
        }
    }
    
    if (m_Levels.empty()) {
        PyramidLevel levelInfo;
        levelInfo.Width = originalWidth;
        levelInfo.Height = originalHeight;
        levelInfo.TileCountX = (originalWidth + TILE_SIZE - 1) / TILE_SIZE;
        levelInfo.TileCountY = (originalHeight + TILE_SIZE - 1) / TILE_SIZE;
        m_Levels.push_back(levelInfo);
    }
    
    for (size_t i = 0; i < m_Levels.size(); ++i) {
        std::ostringstream oss;
        oss << "[ImagePyramid] Level " << i << ": " << m_Levels[i].Width << "x" << m_Levels[i].Height
            << ", Tiles: " << m_Levels[i].TileCountX << "x" << m_Levels[i].TileCountY << "\n";
        OutputDebugStringA(oss.str().c_str());
    }
    
    return true;
}

bool ImagePyramid::GetLevelInfo(uint32_t level, PyramidLevel& outInfo) const {
    if (level >= m_Levels.size()) {
        return false;
    }
    
    outInfo = m_Levels[level];
    return true;
}

void ImagePyramid::CalculateLevelTiles(uint32_t level, uint32_t& tileCountX, uint32_t& tileCountY) const {
    if (level >= m_Levels.size()) {
        tileCountX = 0;
        tileCountY = 0;
        return;
    }
    
    const PyramidLevel& levelInfo = m_Levels[level];
    tileCountX = levelInfo.TileCountX;
    tileCountY = levelInfo.TileCountY;
}

bool ImagePyramid::HasTile(const TileCoordinate& coord) const {
    if (coord.Level >= m_Levels.size()) {
        return false;
    }
    
    const PyramidLevel& levelInfo = m_Levels[coord.Level];
    return coord.X < levelInfo.TileCountX && coord.Y < levelInfo.TileCountY;
}

bool ImagePyramid::GetTileData(const TileCoordinate& coord, TileData& outData) {
    std::lock_guard<std::mutex> lock(m_TileDataMutex);
    
    auto it = m_TileData.find(coord);
    if (it != m_TileData.end() && it->second.bLoaded) {
        outData = it->second;
        return true;
    }
    
    return false;
}

void ImagePyramid::MarkTileLoaded(const TileCoordinate& coord, const uint8_t* data, size_t dataSize) {
    if (!HasTile(coord)) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_TileDataMutex);
    
    TileData& tileData = m_TileData[coord];
    size_t expectedSize = TILE_SIZE * TILE_SIZE * 4;
    
    if (dataSize >= expectedSize) {
        tileData.Data.resize(expectedSize);
        std::memcpy(tileData.Data.data(), data, expectedSize);
        tileData.bLoaded = true;
    }
}

void ImagePyramid::GetTilesForViewport(uint32_t level,
                                      float viewportX, float viewportY,
                                      float viewportWidth, float viewportHeight,
                                      float zoomLevel,
                                      std::vector<TileCoordinate>& outTiles) const {
    outTiles.clear();
    
    if (level >= m_Levels.size()) {
        return;
    }
    
    const PyramidLevel& levelInfo = m_Levels[level];
    

    float scale = 1.0f / zoomLevel;
    float levelViewportX = viewportX * scale;
    float levelViewportY = viewportY * scale;
    float levelViewportWidth = viewportWidth * scale;
    float levelViewportHeight = viewportHeight * scale;
    
    int32_t startTileX = (std::max)(0, static_cast<int32_t>(std::floor(levelViewportX / TILE_SIZE)) - 1);
    int32_t startTileY = (std::max)(0, static_cast<int32_t>(std::floor(levelViewportY / TILE_SIZE)) - 1);
    int32_t endTileX = (std::min)(static_cast<int32_t>(levelInfo.TileCountX) - 1,
                                static_cast<int32_t>(std::ceil((levelViewportX + levelViewportWidth) / TILE_SIZE)) + 1);
    int32_t endTileY = (std::min)(static_cast<int32_t>(levelInfo.TileCountY) - 1,
                                static_cast<int32_t>(std::ceil((levelViewportY + levelViewportHeight) / TILE_SIZE)) + 1);
    
    for (int32_t y = startTileY; y <= endTileY; ++y) {
        for (int32_t x = startTileX; x <= endTileX; ++x) {
            TileCoordinate coord;
            coord.Level = level;
            coord.X = static_cast<uint32_t>(x);
            coord.Y = static_cast<uint32_t>(y);
            
            if (HasTile(coord)) {
                outTiles.push_back(coord);
            }
        }
    }
}

void ImagePyramid::CalculateVisibleTiles(uint32_t imageWidth, uint32_t imageHeight,
                                         uint32_t viewportX, uint32_t viewportY,
                                         uint32_t viewportWidth, uint32_t viewportHeight,
                                         double zoomLevel, double panX, double panY,
                                         std::vector<TileCoordinate>& outTiles) const {
    outTiles.clear();
    
    if (imageWidth == 0 || imageHeight == 0 || viewportWidth == 0 || viewportHeight == 0) {
        return;
    }
    
    // 1. 计算当前应该使用的LOD级别
    // 核心逻辑：根据图像在屏幕上的实际显示尺寸选择LOD
    // 理想状态：1个纹理像素对应1个屏幕像素（1:1像素密度）
    
    // 计算自适应缩放（保持宽高比）
    float fitScale = 1.0f;
    float scaleX = static_cast<float>(viewportWidth) / static_cast<float>(imageWidth);
    float scaleY = static_cast<float>(viewportHeight) / static_cast<float>(imageHeight);
    fitScale = (scaleX < scaleY) ? scaleX : scaleY;  // 取较小值，确保图片完全显示
    
    // 计算图像在屏幕上的实际显示尺寸（考虑自适应缩放和用户缩放）
    float imageDisplayWidth = imageWidth * fitScale / static_cast<float>(zoomLevel);
    float imageDisplayHeight = imageHeight * fitScale / static_cast<float>(zoomLevel);
    
    // 选择最接近显示尺寸的LOD层级
    uint32_t lodLevel = 0;
    float minDiff = FLT_MAX;
    
    for (uint32_t i = 0; i < m_Levels.size(); ++i) {
        const PyramidLevel& levelInfo = m_Levels[i];
        // 计算该LOD层级与显示尺寸的差异
        float diff = std::abs(static_cast<float>(levelInfo.Width) - imageDisplayWidth) +
                     std::abs(static_cast<float>(levelInfo.Height) - imageDisplayHeight);
        
        if (diff < minDiff) {
            minDiff = diff;
            lodLevel = i;
        }
    }
    
    // 确保LOD级别有效
    if (lodLevel >= m_Levels.size()) {
        lodLevel = m_Levels.size() > 0 ? static_cast<uint32_t>(m_Levels.size() - 1) : 0;
    }
    
    // 2. 计算图像显示尺寸（归一化，相对于视口尺寸）
    float imageDisplayWidthNorm = (imageWidth * fitScale) / static_cast<float>(viewportWidth);
    float imageDisplayHeightNorm = (imageHeight * fitScale) / static_cast<float>(viewportHeight);
    
    // 3. 计算视口四个角在图像空间中的像素坐标
    // 注意：考虑viewport offset (viewportX, viewportY)
    // 视口在屏幕上的位置：从(viewportX, viewportY)到(viewportX+viewportWidth, viewportY+viewportHeight)
    // 转换为归一化屏幕坐标[-1, 1]：需要知道整个屏幕的尺寸
    // 简化：假设viewport就是整个屏幕，viewport offset是相对于某个父窗口的
    
    // 视口的四个角（相对于视口本身，范围[0, viewportWidth/Height]）
    // 转换为归一化坐标[-1, 1]（相对于视口）
    struct ViewportCorner {
        float x, y;  // 相对于视口的归一化坐标 [-1, 1]
    };
    ViewportCorner viewportCorners[4] = {
        {-1.0f, -1.0f},  // 视口左上
        { 1.0f, -1.0f},  // 视口右上
        {-1.0f,  1.0f},  // 视口左下
        { 1.0f,  1.0f}   // 视口右下
    };
    
    float minPixelX = FLT_MAX, maxPixelX = -FLT_MAX;
    float minPixelY = FLT_MAX, maxPixelY = -FLT_MAX;
    
    for (int i = 0; i < 4; ++i) {
        // 视口归一化坐标 [-1, 1]
        float viewportNormX = viewportCorners[i].x;
        float viewportNormY = viewportCorners[i].y;
        
        // 根据ScaleNode的shader逻辑：
        // screenPos = viewportNormPos  // 已经是[-1,1]
        // imagePos = screenPos / imageDisplaySize
        // imagePos = imagePos / ZoomLevel + Pan
        // texCoord = imagePos * 0.5 + 0.5  // 转换回[0,1]
        // pixelCoord = texCoord * ImageSize
        
        // imagePos = screenPos / imageDisplaySize
        float imagePosX = viewportNormX / imageDisplayWidthNorm;
        float imagePosY = viewportNormY / imageDisplayHeightNorm;
        
        // 应用用户缩放和平移
        imagePosX = imagePosX / static_cast<float>(zoomLevel);
        imagePosY = imagePosY / static_cast<float>(zoomLevel);
        imagePosX = imagePosX + static_cast<float>(panX);
        imagePosY = imagePosY + static_cast<float>(panY);
        
        // 转换到图像空间坐标 [0, 1]
        float imageCoordX = imagePosX * 0.5f + 0.5f;
        float imageCoordY = imagePosY * 0.5f + 0.5f;
        
        // 转换为像素坐标
        float pixelX = imageCoordX * static_cast<float>(imageWidth);
        float pixelY = imageCoordY * static_cast<float>(imageHeight);
        
        minPixelX = (std::min)(minPixelX, pixelX);
        maxPixelX = (std::max)(maxPixelX, pixelX);
        minPixelY = (std::min)(minPixelY, pixelY);
        maxPixelY = (std::max)(maxPixelY, pixelY);
    }
    
    // 5. 计算Tile索引范围（在LOD 0坐标系中）
    const float TILE_SIZE_F = 256.0f;
    float startTileX = (std::max)(0.0f, std::floor(minPixelX / TILE_SIZE_F));
    uint32_t maxTileX = (imageWidth + static_cast<uint32_t>(TILE_SIZE_F) - 1) / static_cast<uint32_t>(TILE_SIZE_F);
    float endTileX = (std::min)(static_cast<float>(maxTileX), std::ceil(maxPixelX / TILE_SIZE_F));
    float startTileY = (std::max)(0.0f, std::floor(minPixelY / TILE_SIZE_F));
    uint32_t maxTileY = (imageHeight + static_cast<uint32_t>(TILE_SIZE_F) - 1) / static_cast<uint32_t>(TILE_SIZE_F);
    float endTileY = (std::min)(static_cast<float>(maxTileY), std::ceil(maxPixelY / TILE_SIZE_F));
    
    // 6. 如果当前LOD不是0，需要将LOD 0的Tile坐标转换到当前LOD
    if (lodLevel > 0) {
        // LOD N的Tile覆盖LOD 0的2^N个Tile
        uint32_t scaleFactor = 1U << lodLevel;
        startTileX = startTileX / static_cast<float>(scaleFactor);
        endTileX = endTileX / static_cast<float>(scaleFactor);
        startTileY = startTileY / static_cast<float>(scaleFactor);
        endTileY = endTileY / static_cast<float>(scaleFactor);
    }
    
    // 7. 获取当前LOD层级的Tile信息
    const PyramidLevel& levelInfo = m_Levels[lodLevel];
    
    // 8. 生成Tile坐标列表
    int32_t startX = static_cast<int32_t>((std::max)(0.0f, std::floor(startTileX)));
    int32_t endX = static_cast<int32_t>((std::min)(static_cast<float>(levelInfo.TileCountX - 1), std::ceil(endTileX)));
    int32_t startY = static_cast<int32_t>((std::max)(0.0f, std::floor(startTileY)));
    int32_t endY = static_cast<int32_t>((std::min)(static_cast<float>(levelInfo.TileCountY - 1), std::ceil(endTileY)));
    
    for (int32_t y = startY; y <= endY; ++y) {
        for (int32_t x = startX; x <= endX; ++x) {
            TileCoordinate coord;
            coord.Level = lodLevel;
            coord.X = static_cast<uint32_t>(x);
            coord.Y = static_cast<uint32_t>(y);
            
            if (HasTile(coord)) {
                outTiles.push_back(coord);
            }
        }
    }
    
    // 调试输出
    std::ostringstream oss;
    oss << "[ImagePyramid] CalculateVisibleTiles:\n";
    oss << "  Image: " << imageWidth << "x" << imageHeight << "\n";
    oss << "  Viewport: (" << viewportX << "," << viewportY << ") " << viewportWidth << "x" << viewportHeight << "\n";
    oss << "  FitScale: " << fitScale << ", Zoom: " << zoomLevel << ", Pan: (" << panX << "," << panY << ")\n";
    oss << "  DisplaySize: " << imageDisplayWidth << "x" << imageDisplayHeight << "\n";
    oss << "  imageDisplayWidthNorm: " << imageDisplayWidthNorm << ", imageDisplayHeightNorm: " << imageDisplayHeightNorm << "\n";
    oss << "  Selected LOD: " << lodLevel << " (LevelSize=" << levelInfo.Width << "x" << levelInfo.Height << ")\n";
    oss << "  PixelRange: [" << minPixelX << "," << maxPixelX << "] x [" << minPixelY << "," << maxPixelY << "]\n";
    oss << "  ImageCenter: (" << (imageWidth * 0.5f) << "," << (imageHeight * 0.5f) << ")\n";
    oss << "  VisibleRegionCenter: (" << ((minPixelX + maxPixelX) * 0.5f) << "," << ((minPixelY + maxPixelY) * 0.5f) << ")\n";
    oss << "  TileRange: [" << startX << "," << endX << "] x [" << startY << "," << endY << "]\n";
    oss << "  Found " << outTiles.size() << " tiles:\n";
    for (size_t i = 0; i < outTiles.size() && i < 10; ++i) {
        oss << "    Tile[" << i << "]: LOD=" << outTiles[i].Level 
            << ", X=" << outTiles[i].X << ", Y=" << outTiles[i].Y << "\n";
    }
    if (outTiles.size() > 10) {
        oss << "    ... and " << (outTiles.size() - 10) << " more tiles\n";
    }
    OutputDebugStringA(oss.str().c_str());
}

void ImagePyramid::ConvertToLevelCoordinates(uint32_t sourceLevel, uint32_t sourceX, uint32_t sourceY,
                                            uint32_t targetLevel, uint32_t& targetX, uint32_t& targetY) const {
    if (sourceLevel >= m_Levels.size() || targetLevel >= m_Levels.size()) {
        targetX = 0;
        targetY = 0;
        return;
    }
    
    int32_t levelDiff = static_cast<int32_t>(targetLevel) - static_cast<int32_t>(sourceLevel);
    
    if (levelDiff > 0) {
        targetX = sourceX >> levelDiff;
        targetY = sourceY >> levelDiff;
    } else if (levelDiff < 0) {
        targetX = sourceX << (-levelDiff);
        targetY = sourceY << (-levelDiff);
    } else {
        targetX = sourceX;
        targetY = sourceY;
    }
}

} // namespace LightroomCore
