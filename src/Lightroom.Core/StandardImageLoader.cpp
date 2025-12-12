// 防止 Winsock 冲突 - 必须在包含任何 Windows 头文件之前
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincodec.h>
#include <comdef.h>

#include "StandardImageLoader.h"
#include <iostream>
#include <vector>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")

namespace LightroomCore {

StandardImageLoader::StandardImageLoader()
    : m_LastImageWidth(0)
    , m_LastImageHeight(0)
{
}

StandardImageLoader::~StandardImageLoader() {
}

bool StandardImageLoader::IsStandardImageFormat(const std::wstring& filePath) const {
    // 转换为小写进行比较
    std::wstring lowerPath = filePath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::towlower);

    // 检查标准图片格式扩展名
    const wchar_t* extensions[] = {
        L".jpg", L".jpeg", L".png", L".bmp",
        L".gif", L".tiff", L".tif", L".webp"
    };
    
    for (const wchar_t* ext : extensions) {
        size_t extLen = wcslen(ext);
        if (lowerPath.length() >= extLen) {
            if (lowerPath.compare(lowerPath.length() - extLen, extLen, ext) == 0) {
                return true;
            }
        }
    }
    
    return false;
}

bool StandardImageLoader::CanLoad(const std::wstring& filePath) {
    // 首先检查扩展名
    if (!IsStandardImageFormat(filePath)) {
        return false;
    }

    // 验证文件是否存在
    DWORD fileAttributes = GetFileAttributesW(filePath.c_str());
    if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    // 可以进一步尝试用 WIC 打开文件来确认
    // 这里先简单返回 true，实际加载时再验证
    return true;
}

std::shared_ptr<RenderCore::RHITexture2D> StandardImageLoader::Load(
    const std::wstring& filePath,
    std::shared_ptr<RenderCore::DynamicRHI> rhi) {
    
    if (!rhi) {
        std::cerr << "[StandardImageLoader] RHI is null" << std::endl;
        return nullptr;
    }

    // 验证文件是否存在
    DWORD fileAttributes = GetFileAttributesW(filePath.c_str());
    if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        std::cerr << "[StandardImageLoader] File not found or cannot access" << std::endl;
        return nullptr;
    }

    // 使用 WIC 加载图片数据
    std::vector<uint8_t> imageData;
    uint32_t width, height, stride;
    if (!LoadImageDataWithWIC(filePath, imageData, width, height, stride)) {
        return nullptr;
    }

    m_LastImageWidth = width;
    m_LastImageHeight = height;

    // 使用 RHI 接口创建纹理
    auto texture = rhi->RHICreateTexture2D(
        RenderCore::EPixelFormat::PF_B8G8R8A8,
        RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
        width,
        height,
        1,  // NumMips
        imageData.data(),
        stride
    );

    if (!texture) {
        std::cerr << "[StandardImageLoader] Failed to create texture via RHI" << std::endl;
        return nullptr;
    }

    std::wcout << L"[StandardImageLoader] Successfully loaded image: " << filePath 
               << L" (" << width << L"x" << height << L")" << std::endl;
    return texture;
}

void StandardImageLoader::GetImageInfo(uint32_t& width, uint32_t& height) const {
    width = m_LastImageWidth;
    height = m_LastImageHeight;
}

bool StandardImageLoader::LoadImageDataWithWIC(const std::wstring& imagePath, 
                                                std::vector<uint8_t>& outData,
                                                uint32_t& outWidth, 
                                                uint32_t& outHeight, 
                                                uint32_t& outStride) {
    // 初始化 WIC
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        std::cerr << "[StandardImageLoader] Failed to create WIC factory: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 创建解码器
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(imagePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        std::cerr << "[StandardImageLoader] Failed to create decoder (HR: 0x" << std::hex << hr << ")" << std::endl;
        _com_error err(hr);
        std::cerr << "[StandardImageLoader] Error message: " << err.ErrorMessage() << std::endl;
        return false;
    }

    // 获取第一帧
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        std::cerr << "[StandardImageLoader] Failed to get frame: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 获取图片尺寸
    UINT width, height;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr)) {
        std::cerr << "[StandardImageLoader] Failed to get image size: 0x" << std::hex << hr << std::endl;
        return false;
    }

    outWidth = width;
    outHeight = height;

    // 转换格式为 BGRA32
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        std::cerr << "[StandardImageLoader] Failed to create format converter: 0x" << std::hex << hr << std::endl;
        return false;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        std::cerr << "[StandardImageLoader] Failed to initialize format converter: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 复制像素数据
    outStride = width * 4;  // BGRA32 = 4 bytes per pixel
    uint32_t bufferSize = outStride * height;
    outData.resize(bufferSize);

    hr = converter->CopyPixels(nullptr, outStride, bufferSize, outData.data());
    if (FAILED(hr)) {
        std::cerr << "[StandardImageLoader] Failed to copy pixels: 0x" << std::hex << hr << std::endl;
        return false;
    }

    return true;
}

} // namespace LightroomCore

