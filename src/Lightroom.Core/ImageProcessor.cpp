#include "ImageProcessor.h"
#include <wincodec.h>
#include <comdef.h>
#include <windows.h>
#include <iostream>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace LightroomCore {

ImageProcessor::ImageProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : m_RHI(rhi)
    , m_LastImageWidth(0)
    , m_LastImageHeight(0)
{
}

ImageProcessor::~ImageProcessor() {
}

std::shared_ptr<RenderCore::RHITexture2D> ImageProcessor::LoadImageFromFile(const char* imagePath) {
    if (!imagePath) {
        return nullptr;
    }

    // 转换路径为宽字符
    std::wstring wpath;
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, nullptr, 0);
    if (pathLen > 0) {
        wpath.resize(pathLen);
        MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, &wpath[0], pathLen);
        if (!wpath.empty() && wpath.back() == L'\0') {
            wpath.pop_back();
        }
    } else {
        pathLen = MultiByteToWideChar(CP_ACP, 0, imagePath, -1, nullptr, 0);
        if (pathLen > 0) {
            wpath.resize(pathLen);
            MultiByteToWideChar(CP_ACP, 0, imagePath, -1, &wpath[0], pathLen);
            if (!wpath.empty() && wpath.back() == L'\0') {
                wpath.pop_back();
            }
        } else {
            std::cerr << "[ImageProcessor] Failed to convert path to wide string: " << imagePath << std::endl;
            return nullptr;
        }
    }

    return LoadImageFromFile(wpath);
}

std::shared_ptr<RenderCore::RHITexture2D> ImageProcessor::LoadImageFromFile(const std::wstring& imagePath) {
    // 验证文件是否存在
    DWORD fileAttributes = GetFileAttributesW(imagePath.c_str());
    if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        std::cerr << "[ImageProcessor] File not found or cannot access: " << std::endl;
        return nullptr;
    }

    // 使用 WIC 加载图片数据
    std::vector<uint8_t> imageData;
    uint32_t width, height, stride;
    if (!LoadImageDataWithWIC(imagePath, imageData, width, height, stride)) {
        return nullptr;
    }

    m_LastImageWidth = width;
    m_LastImageHeight = height;

    // 使用 RHI 接口创建纹理
    // 注意：RHI 接口期望的格式可能需要调整，这里使用 BGRA8
    auto texture = m_RHI->RHICreateTexture2D(
        RenderCore::EPixelFormat::PF_B8G8R8A8,
        RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
        width,
        height,
        1,  // NumMips
        imageData.data(),
        stride
    );

    if (!texture) {
        std::cerr << "[ImageProcessor] Failed to create texture via RHI" << std::endl;
        return nullptr;
    }

    std::wcout << L"[ImageProcessor] Successfully loaded image: " << imagePath 
               << L" (" << width << L"x" << height << L")" << std::endl;
    return texture;
}

bool ImageProcessor::LoadImageDataWithWIC(const std::wstring& imagePath, std::vector<uint8_t>& outData,
                                           uint32_t& outWidth, uint32_t& outHeight, uint32_t& outStride) {
    // 初始化 WIC
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        std::cerr << "[ImageProcessor] Failed to create WIC factory: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 创建解码器
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(imagePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        std::cerr << "[ImageProcessor] Failed to create decoder (HR: 0x" << std::hex << hr << ")" << std::endl;
        _com_error err(hr);
        std::cerr << "[ImageProcessor] Error message: " << err.ErrorMessage() << std::endl;
        return false;
    }

    // 获取第一帧
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        std::cerr << "[ImageProcessor] Failed to get frame: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 获取图片尺寸
    UINT width, height;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr)) {
        std::cerr << "[ImageProcessor] Failed to get image size: 0x" << std::hex << hr << std::endl;
        return false;
    }

    outWidth = width;
    outHeight = height;

    // 转换格式为 BGRA32
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        std::cerr << "[ImageProcessor] Failed to create format converter: 0x" << std::hex << hr << std::endl;
        return false;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        std::cerr << "[ImageProcessor] Failed to initialize format converter: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 复制像素数据
    outStride = width * 4;  // BGRA32 = 4 bytes per pixel
    uint32_t bufferSize = outStride * height;
    outData.resize(bufferSize);

    hr = converter->CopyPixels(nullptr, outStride, bufferSize, outData.data());
    if (FAILED(hr)) {
        std::cerr << "[ImageProcessor] Failed to copy pixels: 0x" << std::hex << hr << std::endl;
        return false;
    }

    return true;
}

} // namespace LightroomCore

