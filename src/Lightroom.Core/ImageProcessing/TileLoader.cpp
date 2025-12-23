#include "TileLoader.h"
#include <algorithm>
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <filesystem>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace Microsoft::WRL;

namespace LightroomCore {

TileLoader::TileLoader()
    : m_ImageWidth(0)
    , m_ImageHeight(0)
    , m_WICInitialized(false) {
}

TileLoader::~TileLoader() {
}

bool TileLoader::Initialize(std::shared_ptr<ImagePyramid> pyramid, 
                            std::shared_ptr<GPUTileCache> cache) {
    if (!pyramid || !cache) {
        return false;
    }
    
    m_Pyramid = pyramid;
    m_Cache = cache;
    
    return true;
}

void TileLoader::SetCachedImageData(const uint8_t* imageData, uint32_t width, uint32_t height) {
    // 这个方法现在不再使用，因为我们使用WIC区域解码
    // 保留接口以保持兼容性
    (void)imageData;
    (void)width;
    (void)height;
}

bool TileLoader::LoadTile(const TileCoordinate& coord) {
    if (!m_Pyramid || !m_Cache) {
        return false;
    }
    
    // 检查Tile是否已在GPU中
    int32_t slotIndex = m_Cache->GetTileSlotIndex(coord);
    if (slotIndex >= 0) {
        return true;  // 已在GPU中
    }
    
    // 尝试从缓存中获取
    TileData tileData;
    if (m_Pyramid->GetTileData(coord, tileData) && tileData.bLoaded) {
        // 已在内存中，直接上传到GPU
        int32_t newSlotIndex = m_Cache->RequestTile(coord, tileData.Data.data());
        return newSlotIndex >= 0;
    }
    
    // 需要从磁盘或内存中加载Tile
    std::vector<uint8_t> tileDataBuffer;
    if (LoadTileData(coord, tileDataBuffer)) {
        // 标记为已加载
        m_Pyramid->MarkTileLoaded(coord, tileDataBuffer.data(), tileDataBuffer.size());
        
        // 上传到GPU
        int32_t newSlotIndex = m_Cache->RequestTile(coord, tileDataBuffer.data());
        if (newSlotIndex >= 0) {
            return true;
        } else {
            std::ostringstream oss;
            oss << "[TileLoader] Failed to upload tile to GPU: Level=" 
                << coord.Level << ", X=" << coord.X << ", Y=" << coord.Y << "\n";
            OutputDebugStringA(oss.str().c_str());
        }
    } else {
        std::ostringstream oss;
        oss << "[TileLoader] Failed to load tile: Level=" 
            << coord.Level << ", X=" << coord.X << ", Y=" << coord.Y << "\n";
        OutputDebugStringA(oss.str().c_str());
    }
    
    return false;
}

uint32_t TileLoader::LoadTiles(const std::vector<TileCoordinate>& coords) {
    uint32_t successCount = 0;
    for (const auto& coord : coords) {
        if (LoadTile(coord)) {
            successCount++;
        }
    }
    return successCount;
}

bool TileLoader::LoadTileData(const TileCoordinate& coord, std::vector<uint8_t>& outData) {
    if (!m_Pyramid) {
        return false;
    }
    
    // 初始化WIC资源（延迟初始化，只初始化一次）
    if (!InitializeWICResources()) {
        return false;
    }
    
    // 计算Tile在LOD 0坐标系中的位置和尺寸
    uint32_t scaleFactor = 1U << coord.Level;  // 2^level
    uint32_t tileStartX = coord.X * TILE_SIZE * scaleFactor;
    uint32_t tileStartY = coord.Y * TILE_SIZE * scaleFactor;
    uint32_t srcWidth = TILE_SIZE * scaleFactor;
    uint32_t srcHeight = TILE_SIZE * scaleFactor;
    
    // 确保不越界
    if (tileStartX >= m_ImageWidth || tileStartY >= m_ImageHeight) {
        return false;
    }
    
    srcWidth = std::min(srcWidth, m_ImageWidth - tileStartX);
    srcHeight = std::min(srcHeight, m_ImageHeight - tileStartY);
    
    bool success = false;
    if (coord.Level == 0) {
        // LOD 0：直接从WIC读取区域
        success = ReadRegionFromWIC(tileStartX, tileStartY, srcWidth, srcHeight, outData);
    } else {
        // LOD > 0：使用WIC Scaler进行一次性下采样
        success = DownsampleWithWICScaler(tileStartX, tileStartY, srcWidth, srcHeight,
                                         TILE_SIZE, TILE_SIZE, outData);
    }
    
    // 调试：dump tile到文件（相同tile只dump一次）
    //if (success && !outData.empty()) {
    //    DumpTileToFile(coord, outData.data());
    //}
    
    return success;
}


bool TileLoader::InitializeWICResources() {
    if (m_WICInitialized) {
        return true;
    }
    
    if (!m_Pyramid) {
        return false;
    }
    
    const std::string& imagePath = m_Pyramid->GetImagePath();
    if (imagePath.empty()) {
        return false;
    }
    
    // 创建WIC Factory
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_WICFactory)
    );
    
    if (FAILED(hr)) {
        return false;
    }
    
    // 创建Decoder
    std::wstring wpath(imagePath.begin(), imagePath.end());
    hr = m_WICFactory->CreateDecoderFromFilename(
        wpath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &m_WICDecoder
    );
    
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取Frame
    hr = m_WICDecoder->GetFrame(0, &m_WICFrame);
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取图像尺寸
    UINT width, height;
    hr = m_WICFrame->GetSize(&width, &height);
    if (FAILED(hr)) {
        return false;
    }
    
    m_ImageWidth = width;
    m_ImageHeight = height;
    
    // 创建FormatConverter
    hr = m_WICFactory->CreateFormatConverter(&m_WICConverter);
    if (FAILED(hr)) {
        return false;
    }
    
    hr = m_WICConverter->Initialize(
        m_WICFrame.Get(),
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    
    if (FAILED(hr)) {
        return false;
    }
    
    m_WICInitialized = true;
    
    std::ostringstream oss;
    oss << "[TileLoader] WIC resources initialized: " << m_ImageWidth << "x" << m_ImageHeight << "\n";
    OutputDebugStringA(oss.str().c_str());
    
    return true;
}

bool TileLoader::ReadRegionFromWIC(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                   std::vector<uint8_t>& outData) {
    if (!m_WICInitialized || !m_WICConverter) {
        return false;
    }
    
    // 确保不越界
    if (x >= m_ImageWidth || y >= m_ImageHeight) {
        return false;
    }
    
    width = std::min(width, m_ImageWidth - x);
    height = std::min(height, m_ImageHeight - y);
    
    // 分配输出缓冲区（总是256x256，未使用的部分填充0）
    outData.resize(TILE_SIZE * TILE_SIZE * 4, 0);
    
    // 使用WIC区域解码：只读取需要的矩形区域
    WICRect rect;
    rect.X = static_cast<INT>(x);
    rect.Y = static_cast<INT>(y);
    rect.Width = static_cast<INT>(width);
    rect.Height = static_cast<INT>(height);
    
    uint32_t stride = TILE_SIZE * 4;  // 输出stride（256x4）
    HRESULT hr = m_WICConverter->CopyPixels(
        &rect,
        stride,
        static_cast<UINT>(outData.size()),
        outData.data()
    );
    
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[TileLoader] Failed to read region from WIC: HRESULT=0x" 
            << std::hex << hr << std::dec << "\n";
        OutputDebugStringA(oss.str().c_str());
        return false;
    }
    
    return true;
}

bool TileLoader::DownsampleWithWICScaler(uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight,
                                         uint32_t dstWidth, uint32_t dstHeight,
                                         std::vector<uint8_t>& outData) {
    if (!m_WICInitialized || !m_WICFrame) {
        return false;
    }
    
    // 创建Scaler
    ComPtr<IWICBitmapScaler> scaler;
    HRESULT hr = m_WICFactory->CreateBitmapScaler(&scaler);
    if (FAILED(hr)) {
        return false;
    }
    
    // 创建临时FormatConverter（用于源区域）
    ComPtr<IWICFormatConverter> srcConverter;
    hr = m_WICFactory->CreateFormatConverter(&srcConverter);
    if (FAILED(hr)) {
        return false;
    }
    
    hr = srcConverter->Initialize(
        m_WICFrame.Get(),
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    
    if (FAILED(hr)) {
        return false;
    }
    
    // 如果源区域就是整个图像，直接缩放
    if (srcX == 0 && srcY == 0 && srcWidth == m_ImageWidth && srcHeight == m_ImageHeight) {
        hr = scaler->Initialize(
            srcConverter.Get(),
            dstWidth,
            dstHeight,
            WICBitmapInterpolationModeLinear  // 使用线性插值
        );
    } else {
        // 需要先裁剪源区域，然后缩放
        // 注意：WIC Scaler不支持直接指定源区域，我们需要先创建一个裁剪的bitmap
        // 简化实现：使用CopyPixels读取源区域，然后创建临时bitmap
        std::vector<uint8_t> srcRegionData;
        srcRegionData.resize(srcWidth * srcHeight * 4);
        
        WICRect srcRect;
        srcRect.X = static_cast<INT>(srcX);
        srcRect.Y = static_cast<INT>(srcY);
        srcRect.Width = static_cast<INT>(srcWidth);
        srcRect.Height = static_cast<INT>(srcHeight);
        
        uint32_t srcStride = srcWidth * 4;
        hr = srcConverter->CopyPixels(&srcRect, srcStride, 
                                     static_cast<UINT>(srcRegionData.size()),
                                     srcRegionData.data());
        
        if (FAILED(hr)) {
            return false;
        }
        
        // 从内存数据创建bitmap
        ComPtr<IWICBitmap> tempBitmap;
        hr = m_WICFactory->CreateBitmapFromMemory(
            srcWidth,
            srcHeight,
            GUID_WICPixelFormat32bppBGRA,
            srcStride,
            static_cast<UINT>(srcRegionData.size()),
            srcRegionData.data(),
            &tempBitmap
        );
        
        if (FAILED(hr)) {
            return false;
        }
        
        // 对临时bitmap进行缩放
        hr = scaler->Initialize(
            tempBitmap.Get(),
            dstWidth,
            dstHeight,
            WICBitmapInterpolationModeLinear
        );
    }
    
    if (FAILED(hr)) {
        return false;
    }
    
    // 分配输出缓冲区
    outData.resize(dstWidth * dstHeight * 4);
    uint32_t dstStride = dstWidth * 4;
    
    // 复制缩放后的数据
    hr = scaler->CopyPixels(
        nullptr,
        dstStride,
        static_cast<UINT>(outData.size()),
        outData.data()
    );
    
    if (FAILED(hr)) {
        return false;
    }
    
    return true;
}

void TileLoader::DumpTileToFile(const TileCoordinate& coord, const uint8_t* tileData) {
    if (!tileData) {
        return;
    }
    
    // 检查是否已经dump过这个tile
    if (m_DumpedTiles.find(coord) != m_DumpedTiles.end()) {
        return;  // 已经dump过，跳过
    }
    
    // 标记为已dump
    m_DumpedTiles.insert(coord);
    
    // 创建dump目录
    std::filesystem::path dumpDir = "D:\\skiadump\\tile";
    try {
        std::filesystem::create_directories(dumpDir);
    } catch (...) {
        std::ostringstream oss;
        oss << "[TileLoader] Failed to create dump directory: " << dumpDir.string() << "\n";
        OutputDebugStringA(oss.str().c_str());
        return;
    }
    
    // 生成文件名：LOD_X_Y.png
    std::ostringstream filename;
    filename << "LOD" << coord.Level << "_" << coord.X << "_" << coord.Y << ".png";
    std::filesystem::path filePath = dumpDir / filename.str();
    
    // 使用WIC保存图像
    if (!m_WICFactory) {
        // 如果WIC Factory未初始化，创建一个临时的
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_WICFactory)
        );
        if (FAILED(hr)) {
            std::ostringstream oss;
            oss << "[TileLoader] Failed to create WIC Factory for dumping: HRESULT=0x" 
                << std::hex << hr << std::dec << "\n";
            OutputDebugStringA(oss.str().c_str());
            return;
        }
    }
    
    // 创建编码器
    ComPtr<IWICBitmapEncoder> encoder;
    // 转换文件路径为宽字符串（UTF-8 to UTF-16）
    std::string pathStr = filePath.string();
    std::wstring wpath;
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, pathStr.c_str(), -1, nullptr, 0);
    if (pathLen > 0) {
        wpath.resize(pathLen);
        MultiByteToWideChar(CP_UTF8, 0, pathStr.c_str(), -1, &wpath[0], pathLen);
    } else {
        // 如果UTF-8转换失败，尝试使用ANSI
        pathLen = MultiByteToWideChar(CP_ACP, 0, pathStr.c_str(), -1, nullptr, 0);
        if (pathLen > 0) {
            wpath.resize(pathLen);
            MultiByteToWideChar(CP_ACP, 0, pathStr.c_str(), -1, &wpath[0], pathLen);
        } else {
            // 最后尝试直接转换
            wpath = std::wstring(pathStr.begin(), pathStr.end());
        }
    }
    
    HRESULT hr = m_WICFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[TileLoader] Failed to create encoder: HRESULT=0x" 
            << std::hex << hr << std::dec << "\n";
        OutputDebugStringA(oss.str().c_str());
        return;
    }
    
    // 创建文件流
    ComPtr<IWICStream> stream;
    hr = m_WICFactory->CreateStream(&stream);
    if (FAILED(hr)) {
        return;
    }
    
    hr = stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[TileLoader] Failed to create file stream: " << filePath.string() 
            << ", HRESULT=0x" << std::hex << hr << std::dec << "\n";
        OutputDebugStringA(oss.str().c_str());
        return;
    }
    
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        return;
    }
    
    // 创建帧编码器
    ComPtr<IWICBitmapFrameEncode> frameEncoder;
    ComPtr<IPropertyBag2> propertyBag;
    hr = encoder->CreateNewFrame(&frameEncoder, &propertyBag);
    if (FAILED(hr)) {
        return;
    }
    
    hr = frameEncoder->Initialize(propertyBag.Get());
    if (FAILED(hr)) {
        return;
    }
    
    // 设置像素格式和尺寸
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    WICPixelFormatGUID* pPixelFormat = &pixelFormat;
    hr = frameEncoder->SetPixelFormat(pPixelFormat);
    if (FAILED(hr)) {
        return;
    }
    
    hr = frameEncoder->SetSize(TILE_SIZE, TILE_SIZE);
    if (FAILED(hr)) {
        return;
    }
    
    // 写入像素数据
    uint32_t stride = TILE_SIZE * 4;  // BGRA = 4 bytes per pixel
    uint32_t bufferSize = TILE_SIZE * TILE_SIZE * 4;
    hr = frameEncoder->WritePixels(TILE_SIZE, stride, bufferSize, const_cast<uint8_t*>(tileData));
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[TileLoader] Failed to write pixels: HRESULT=0x" 
            << std::hex << hr << std::dec << "\n";
        OutputDebugStringA(oss.str().c_str());
        return;
    }
    
    hr = frameEncoder->Commit();
    if (FAILED(hr)) {
        return;
    }
    
    hr = encoder->Commit();
    if (FAILED(hr)) {
        return;
    }
    
    std::ostringstream oss;
    oss << "[TileLoader] Dumped tile: LOD=" << coord.Level 
        << ", X=" << coord.X << ", Y=" << coord.Y 
        << " to " << filePath.string() << "\n";
    OutputDebugStringA(oss.str().c_str());
}

} // namespace LightroomCore
