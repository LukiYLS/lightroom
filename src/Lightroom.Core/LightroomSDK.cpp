#include "LightroomSDK.h"
#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>

#include <d3d9.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <comdef.h>
#include <windows.h>
#include <d3dcompiler.h>

#include "d3d11rhi/Common.h"
#include "d3d11rhi/DynamicRHI.h"
#include "d3d11rhi/D3D11RHI.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace RenderCore;

// 渲染目标结构
struct RenderTargetInfo
{
    ComPtr<ID3D11Texture2D> D3D11Texture;
    ComPtr<ID3D11RenderTargetView> D3D11RTV;
    ComPtr<ID3D11Texture2D> ImageTexture;  // 加载的图片纹理
    ComPtr<ID3D11ShaderResourceView> ImageSRV;  // 图片的 SRV
    HANDLE D3D11SharedHandle;
    IDirect3DTexture9* D3D9Texture;
    IDirect3DSurface9* D3D9Surface;
    IDirect3DTexture9* D3D9SharedTexture;  // 从 D3D11 共享纹理打开的 D3D9 纹理（用于 GPU 拷贝）
    IDirect3DSurface9* D3D9SharedSurface;  // D3D9SharedTexture 的表面
    uint32_t Width;
    uint32_t Height;
    bool bHasImage;  // 是否已加载图片
    
    RenderTargetInfo() 
        : D3D11SharedHandle(nullptr)
        , D3D9Texture(nullptr)
        , D3D9Surface(nullptr)
        , D3D9SharedTexture(nullptr)
        , D3D9SharedSurface(nullptr)
        , Width(0)
        , Height(0)
        , bHasImage(false)
    {
    }
    
    ~RenderTargetInfo() {
        if (D3D9SharedSurface) {
            D3D9SharedSurface->Release();
            D3D9SharedSurface = nullptr;
        }
        if (D3D9SharedTexture) {
            D3D9SharedTexture->Release();
            D3D9SharedTexture = nullptr;
        }
        if (D3D9Surface) {
            D3D9Surface->Release();
            D3D9Surface = nullptr;
        }
        if (D3D9Texture) {
            D3D9Texture->Release();
            D3D9Texture = nullptr;
        }
    }
};

// 全局状态
static std::shared_ptr<DynamicRHI> g_DynamicRHI = nullptr;
static std::unordered_map<void*, std::unique_ptr<RenderTargetInfo>> g_RenderTargets;

// D3D9Ex 设备（全局，所有渲染目标共享）
static IDirect3D9Ex* g_D3D9Ex = nullptr;
static IDirect3DDevice9Ex* g_D3D9ExDevice = nullptr;

// 简单的全屏四边形渲染资源（全局共享）
static ComPtr<ID3D11VertexShader> g_SimpleVS = nullptr;
static ComPtr<ID3D11PixelShader> g_SimplePS = nullptr;
static ComPtr<ID3D11InputLayout> g_InputLayout = nullptr;
static ComPtr<ID3D11Buffer> g_VertexBuffer = nullptr;
static ComPtr<ID3D11SamplerState> g_SamplerState = nullptr;

// 简单的全屏四边形顶点结构
struct SimpleVertex {
    float x, y;      // 位置
    float u, v;      // 纹理坐标
};

// 初始化简单的渲染资源（全屏四边形 + shader）
static bool InitializeSimpleRenderResources()
{
    D3D11DynamicRHI* d3d11RHI = dynamic_cast<D3D11DynamicRHI*>(g_DynamicRHI.get());
    if (!d3d11RHI) {
        return false;
    }
    
    ID3D11Device* device = d3d11RHI->GetDevice();
    if (!device) {
        return false;
    }
    
    // 简单的 Vertex Shader（全屏四边形）
    const char* vsCode = R"(
        struct VS_INPUT {
            float2 pos : POSITION;
            float2 tex : TEXCOORD0;
        };
        struct VS_OUTPUT {
            float4 pos : SV_POSITION;
            float2 tex : TEXCOORD0;
        };
        VS_OUTPUT main(VS_INPUT input) {
            VS_OUTPUT output;
            output.pos = float4(input.pos, 0.0, 1.0);
            output.tex = input.tex;
            return output;
        }
    )";
    
    // 简单的 Pixel Shader（采样纹理）
    const char* psCode = R"(
        Texture2D tex : register(t0);
        SamplerState samp : register(s0);
        struct PS_INPUT {
            float4 pos : SV_POSITION;
            float2 tex : TEXCOORD0;
        };
        float4 main(PS_INPUT input) : SV_Target {
            return tex.Sample(samp, input.tex);
        }
    )";
    
    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
    
    // 编译 Vertex Shader
    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "[SDK] VS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }
    
    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, g_SimpleVS.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to create VS: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 编译 Pixel Shader
    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "[SDK] PS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }
    
    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, g_SimplePS.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to create PS: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 创建输入布局
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    
    hr = device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), g_InputLayout.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to create input layout: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 创建全屏四边形顶点缓冲区
    SimpleVertex vertices[] = {
        { -1.0f,  1.0f, 0.0f, 0.0f },  // 左上
        {  1.0f,  1.0f, 1.0f, 0.0f },  // 右上
        { -1.0f, -1.0f, 0.0f, 1.0f },  // 左下
        {  1.0f, -1.0f, 1.0f, 1.0f }   // 右下
    };
    
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = vertices;
    
    hr = device->CreateBuffer(&vbDesc, &vbData, g_VertexBuffer.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to create vertex buffer: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 创建采样器状态
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    
    hr = device->CreateSamplerState(&sampDesc, g_SamplerState.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to create sampler state: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    std::cout << "[SDK] Simple render resources initialized successfully" << std::endl;
    return true;
}

// 使用 WIC 加载图片并创建 D3D11 纹理
static bool LoadImageWithWIC(const char* imagePath, ID3D11Device* device, ComPtr<ID3D11Texture2D>& outTexture, ComPtr<ID3D11ShaderResourceView>& outSRV, uint32_t& outWidth, uint32_t& outHeight)
{
    // 初始化 WIC
    ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to create WIC factory: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 验证文件是否存在
    DWORD fileAttributes = GetFileAttributesA(imagePath);
    if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        std::cerr << "[SDK] File not found or cannot access: " << imagePath << " (Error: " << error << ")" << std::endl;
        return false;
    }
    
    // 转换路径为宽字符（支持 UTF-8 和 ANSI）
    std::wstring wpath;
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, nullptr, 0);
    if (pathLen > 0) {
        wpath.resize(pathLen);
        MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, &wpath[0], pathLen);
        // 移除末尾的 null 字符
        if (!wpath.empty() && wpath.back() == L'\0') {
            wpath.pop_back();
        }
    } else {
        // 如果 UTF-8 转换失败，尝试 ANSI
        pathLen = MultiByteToWideChar(CP_ACP, 0, imagePath, -1, nullptr, 0);
        if (pathLen > 0) {
            wpath.resize(pathLen);
            MultiByteToWideChar(CP_ACP, 0, imagePath, -1, &wpath[0], pathLen);
            if (!wpath.empty() && wpath.back() == L'\0') {
                wpath.pop_back();
            }
        } else {
            std::cerr << "[SDK] Failed to convert path to wide string: " << imagePath << std::endl;
            return false;
        }
    }
    
    std::wcout << L"[SDK] Loading image from: " << wpath << std::endl;
    
    // 创建解码器
    ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to create decoder for: " << imagePath << " (HR: 0x" << std::hex << hr << ")" << std::endl;
        _com_error err(hr);
        std::cerr << "[SDK] Error message: " << err.ErrorMessage() << std::endl;
        return false;
    }
    
    // 获取第一帧
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to get frame: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 获取图片尺寸
    UINT width, height;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to get image size: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    outWidth = width;
    outHeight = height;
    
    // 转换格式为 RGBA32
    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to create format converter: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 转换为 BGRA32 格式（与 D3D11 渲染目标格式匹配）
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to initialize format converter: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 复制像素数据
    UINT stride = width * 4;  // BGRA32 = 4 bytes per pixel
    UINT bufferSize = stride * height;
    std::vector<uint8_t> imageData(bufferSize);
    
    hr = converter->CopyPixels(nullptr, stride, bufferSize, imageData.data());
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to copy pixels: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 创建 D3D11 纹理（使用 BGRA8 格式，与渲染目标匹配）
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;
    
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = imageData.data();
    initData.SysMemPitch = stride;
    initData.SysMemSlicePitch = 0;
    
    hr = device->CreateTexture2D(&texDesc, &initData, outTexture.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to create texture: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 创建 SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    
    hr = device->CreateShaderResourceView(outTexture.Get(), &srvDesc, outSRV.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "[SDK] Failed to create SRV: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    std::cout << "[SDK] Successfully loaded image: " << imagePath << " (" << width << "x" << height << ")" << std::endl;
    return true;
}

// DXGI 格式到 D3D9 格式的转换（内部函数，不需要导出）
static D3DFORMAT DXGIToD3D9Format(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return D3DFMT_A8R8G8B8;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return D3DFMT_A8R8G8B8;
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            return D3DFMT_X8R8G8B8;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return D3DFMT_A8B8G8R8;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return D3DFMT_A8B8G8R8;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return D3DFMT_A2B10G10R10;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return D3DFMT_A16B16G16R16F;
        default:
            return D3DFMT_UNKNOWN;
    }
}

extern "C" {

LIGHTROOM_API bool InitSDK() {
    try {
        std::cout << "[SDK] Initializing Lightroom Core SDK..." << std::endl;
        
        // 0. 初始化 COM（WIC 需要）
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            std::cerr << "[SDK] Failed to initialize COM: 0x" << std::hex << hr << std::endl;
            return false;
        }
        
        // 1. 创建 D3D11 RHI
        g_DynamicRHI = PlatformCreateDynamicRHI(RHIAPIType::E_D3D11);
        if (!g_DynamicRHI) {
            std::cerr << "[SDK] Failed to create D3D11 RHI" << std::endl;
            return false;
        }
        g_DynamicRHI->Init();
        
        // 初始化简单的全屏四边形渲染资源
        if (!InitializeSimpleRenderResources()) {
            std::cerr << "[SDK] Failed to initialize simple render resources" << std::endl;
            return false;
        }
        
        // 2. 初始化 D3D9Ex（用于 WPF D3DImage 互操作）
        hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &g_D3D9Ex);
        if (FAILED(hr) || !g_D3D9Ex) {
            std::cerr << "[SDK] Failed to create D3D9Ex: 0x" << std::hex << hr << std::endl;
            return false;
        }
        
        // 3. 创建 D3D9Ex 设备（用于 WPF D3DImage）
        // 注意：D3D9 设备必须与 WPF 在同一线程上创建
        D3DPRESENT_PARAMETERS d3dpp = {};
        d3dpp.Windowed = TRUE;
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow = GetDesktopWindow();
        d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
        d3dpp.BackBufferCount = 0;
        d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        d3dpp.Flags = D3DPRESENTFLAG_VIDEO;  // 重要：启用视频模式，支持共享表面
        
        hr = g_D3D9Ex->CreateDeviceEx(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            GetDesktopWindow(),
            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
            &d3dpp,
            nullptr,
            &g_D3D9ExDevice
        );
        
        if (FAILED(hr)) {
            std::cerr << "[SDK] Failed to create D3D9Ex device: 0x" << std::hex << hr << std::endl;
            g_D3D9Ex->Release();
            g_D3D9Ex = nullptr;
            return false;
        }
        
        std::cout << "[SDK] D3D9Ex device created successfully" << std::endl;
        
        std::cout << "[SDK] D3D11 RHI and D3D9Ex Interop Initialized Successfully" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[SDK] Exception during initialization: " << e.what() << std::endl;
        return false;
    }
}

LIGHTROOM_API void ShutdownSDK() {
    std::cout << "[SDK] Shutting down..." << std::endl;
    
    // 清理所有渲染目标
    g_RenderTargets.clear();
    
    // 清理 D3D9Ex 设备
    if (g_D3D9ExDevice) {
        g_D3D9ExDevice->Release();
        g_D3D9ExDevice = nullptr;
    }
    if (g_D3D9Ex) {
        g_D3D9Ex->Release();
        g_D3D9Ex = nullptr;
    }
    
    // 关闭 RHI
    if (g_DynamicRHI) {
        g_DynamicRHI->Shutdown();
        g_DynamicRHI = nullptr;
    }
    
    ReleasePlatformModule();
}

LIGHTROOM_API bool ProcessImage(const char* inputPath, const char* outputPath, float brightness, float contrast) {
    if (!inputPath || !outputPath) return false;
    std::cout << "[SDK] Processing Image: " << inputPath << std::endl;
    return true;
}

LIGHTROOM_API int GetSDKVersion() {
    return 100; // v1.0.0
}

LIGHTROOM_API void* CreateRenderTarget(uint32_t width, uint32_t height) {
    if (!g_DynamicRHI || !g_D3D9ExDevice) {
        std::cerr << "[SDK] RHI or D3D9Ex device not initialized" << std::endl;
        return nullptr;
    }
    
    try {
        D3D11DynamicRHI* d3d11RHI = dynamic_cast<D3D11DynamicRHI*>(g_DynamicRHI.get());
        if (!d3d11RHI) {
            std::cerr << "[SDK] Failed to cast to D3D11DynamicRHI" << std::endl;
            return nullptr;
        }
        
        ID3D11Device* d3d11Device = d3d11RHI->GetDevice();
        if (!d3d11Device) {
            std::cerr << "[SDK] Failed to get D3D11 device" << std::endl;
            return nullptr;
        }
        
        auto info = std::make_unique<RenderTargetInfo>();
        
        // 1. 创建 D3D11 共享渲染目标纹理
        D3D11_TEXTURE2D_DESC d3d11Desc = {};
        d3d11Desc.Width = width;
        d3d11Desc.Height = height;
        d3d11Desc.MipLevels = 1;
        d3d11Desc.ArraySize = 1;
        d3d11Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d3d11Desc.SampleDesc.Count = 1;
        d3d11Desc.SampleDesc.Quality = 0;
        d3d11Desc.Usage = D3D11_USAGE_DEFAULT;
        d3d11Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        d3d11Desc.CPUAccessFlags = 0;
        d3d11Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;  // 关键：启用共享
        
        HRESULT hr = d3d11Device->CreateTexture2D(&d3d11Desc, nullptr, info->D3D11Texture.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[SDK] Failed to create D3D11 shared texture: 0x" << std::hex << hr << std::endl;
            return nullptr;
        }
        
        // 创建 D3D11 渲染目标视图
        hr = d3d11Device->CreateRenderTargetView(info->D3D11Texture.Get(), nullptr, info->D3D11RTV.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[SDK] Failed to create D3D11 RTV: 0x" << std::hex << hr << std::endl;
            return nullptr;
        }
        
        // 获取 D3D11 纹理的共享句柄
        ComPtr<IDXGIResource> dxgiResource;
        hr = info->D3D11Texture.As(&dxgiResource);
        if (FAILED(hr)) {
            std::cerr << "[SDK] Failed to query IDXGIResource: 0x" << std::hex << hr << std::endl;
            return nullptr;
        }
        
        hr = dxgiResource->GetSharedHandle(&info->D3D11SharedHandle);
        if (FAILED(hr) || !info->D3D11SharedHandle) {
            std::cerr << "[SDK] Failed to get D3D11 shared handle: 0x" << std::hex << hr << std::endl;
            return nullptr;
        }
        
        // 2. 在 D3D9Ex 中打开 D3D11 共享纹理
        // 参考 WPFDXInterop SurfaceDevice9::OpenSurface 的实现
        D3DFORMAT d3d9Format = DXGIToD3D9Format(DXGI_FORMAT_B8G8R8A8_UNORM);
        if (d3d9Format == D3DFMT_UNKNOWN) {
            std::cerr << "[SDK] Unsupported format for D3D9 interop" << std::endl;
            return nullptr;
        }
        
        // 尝试在 D3D9 中打开 D3D11 共享纹理（用于 GPU 到 GPU 拷贝）
        // 如果成功，可以使用 StretchRect 进行快速 GPU 拷贝，避免 CPU 像素拷贝
        HANDLE sharedHandle = info->D3D11SharedHandle;
        hr = g_D3D9ExDevice->CreateTexture(
            width,
            height,
            1,  // MipLevels
            D3DUSAGE_RENDERTARGET,
            d3d9Format,
            D3DPOOL_DEFAULT,
            &info->D3D9SharedTexture,
            &sharedHandle  // 传入共享句柄的地址（作为输入/输出参数）
        );
        
        if (SUCCEEDED(hr) && info->D3D9SharedTexture) {
            // 成功打开共享纹理，获取表面用于 StretchRect
            hr = info->D3D9SharedTexture->GetSurfaceLevel(0, &info->D3D9SharedSurface);
            if (SUCCEEDED(hr) && info->D3D9SharedSurface) {
                std::cout << "[SDK] Successfully opened D3D11 shared texture in D3D9Ex - will use GPU copy (StretchRect)" << std::endl;
            } else {
                std::cerr << "[SDK] Failed to get surface from shared texture: 0x" << std::hex << hr << std::endl;
                if (info->D3D9SharedTexture) {
                    info->D3D9SharedTexture->Release();
                    info->D3D9SharedTexture = nullptr;
                }
            }
        } else {
            std::cerr << "[SDK] Cannot open D3D11 shared texture in D3D9Ex (HR: 0x" << std::hex << hr << ")" << std::endl;
        }
        
        // 始终创建独立的 D3D9 共享渲染目标表面（用于 WPF D3DImage）
        // 注意：WPF D3DImage 需要可锁定的共享表面
        // 尝试使用 CreateRenderTarget，如果失败则使用 CreateTexture
        HANDLE d3d9SharedHandle = nullptr;
        hr = g_D3D9ExDevice->CreateRenderTarget(
            width,
            height,
            d3d9Format,
            D3DMULTISAMPLE_NONE,
            0,
            TRUE,  // Lockable = TRUE，允许锁定
            &info->D3D9Surface,
            &d3d9SharedHandle  // 共享句柄（可能为 nullptr，取决于驱动支持）
        );
        
        if (FAILED(hr) || !info->D3D9Surface) {
            std::cerr << "[SDK] CreateRenderTarget failed: 0x" << std::hex << hr << ", trying CreateTexture..." << std::endl;
            
            // 回退到 CreateTexture + GetSurfaceLevel
            hr = g_D3D9ExDevice->CreateTexture(
                width,
                height,
                1,
                D3DUSAGE_RENDERTARGET,
                d3d9Format,
                D3DPOOL_DEFAULT,
                &info->D3D9Texture,
                &d3d9SharedHandle  // 尝试获取共享句柄
            );
            
            if (FAILED(hr) || !info->D3D9Texture) {
                std::cerr << "[SDK] Failed to create D3D9 texture: 0x" << std::hex << hr << std::endl;
                return nullptr;
            }
            
            // 从纹理获取表面
            hr = info->D3D9Texture->GetSurfaceLevel(0, &info->D3D9Surface);
            if (FAILED(hr) || !info->D3D9Surface) {
                std::cerr << "[SDK] Failed to get D3D9 surface from texture: 0x" << std::hex << hr << std::endl;
                return nullptr;
            }
            
            std::cout << "[SDK] Created D3D9 texture and got surface (shared handle: " << d3d9SharedHandle << ")" << std::endl;
        } else {
            std::cout << "[SDK] Created D3D9 shared render target surface directly (shared handle: " << d3d9SharedHandle << ")" << std::endl;
        }
        
        // 验证表面是否可锁定（WPF D3DImage 需要）
        D3DLOCKED_RECT testLock;
        hr = info->D3D9Surface->LockRect(&testLock, nullptr, D3DLOCK_READONLY);
        if (SUCCEEDED(hr)) {
            info->D3D9Surface->UnlockRect();
            std::cout << "[SDK] D3D9 surface is lockable (required for WPF D3DImage)" << std::endl;
        } else {
            std::cerr << "[SDK] WARNING: D3D9 surface is NOT lockable! This may cause IsFrontBufferAvailable to be false. HR: 0x" << std::hex << hr << std::endl;
            std::cerr << "[SDK] Trying to create surface with different parameters..." << std::endl;
            
            // 如果表面不可锁定，尝试释放并重新创建
            if (info->D3D9Surface) {
                info->D3D9Surface->Release();
                info->D3D9Surface = nullptr;
            }
            if (info->D3D9Texture) {
                info->D3D9Texture->Release();
                info->D3D9Texture = nullptr;
            }
            
            // 尝试使用 CreateOffscreenPlainSurface（可能更适合 WPF）
            hr = g_D3D9ExDevice->CreateOffscreenPlainSurface(
                width,
                height,
                d3d9Format,
                D3DPOOL_DEFAULT,
                &info->D3D9Surface,
                &d3d9SharedHandle
            );
            
            if (FAILED(hr) || !info->D3D9Surface) {
                std::cerr << "[SDK] CreateOffscreenPlainSurface also failed: 0x" << std::hex << hr << std::endl;
                return nullptr;
            }
            
            std::cout << "[SDK] Created D3D9 offscreen plain surface (shared handle: " << d3d9SharedHandle << ")" << std::endl;
        }
        
        // 验证共享纹理是否成功打开（GPU 拷贝需要）
        if (!info->D3D9SharedSurface) {
            std::cerr << "[SDK] ERROR: Failed to open D3D11 shared texture in D3D9Ex - GPU copy will not work!" << std::endl;
            return nullptr;
        }
        
        if (!info->D3D9Surface) {
            std::cerr << "[SDK] ERROR: D3D9 surface is null!" << std::endl;
            return nullptr;
        }
        
        std::cout << "[SDK] D3D9 surface obtained successfully, ready for WPF D3DImage" << std::endl;
        
        // 存储信息
        info->Width = width;
        info->Height = height;
        
        void* handle = info.get();
        g_RenderTargets[handle] = std::move(info);
        
        std::cout << "[SDK] Created shared render target: " << width << "x" << height << std::endl;
        return handle;
    }
    catch (const std::exception& e) {
        std::cerr << "[SDK] Exception creating render target: " << e.what() << std::endl;
        return nullptr;
    }
}

LIGHTROOM_API void DestroyRenderTarget(void* renderTargetHandle) {
    if (!renderTargetHandle) return;
    
    auto it = g_RenderTargets.find(renderTargetHandle);
    if (it != g_RenderTargets.end()) {
        // RenderTargetInfo 的析构函数会自动释放资源
        g_RenderTargets.erase(it);
    }
}

LIGHTROOM_API void* GetRenderTargetSharedHandle(void* renderTargetHandle) {
    if (!renderTargetHandle || g_RenderTargets.find(renderTargetHandle) == g_RenderTargets.end()) {
        return nullptr;
    }
    
    auto& info = g_RenderTargets[renderTargetHandle];
    if (!info || !info->D3D9Surface) {
        return nullptr;
    }
    
    // 返回 D3D9 表面的指针（WPF 的 SetBackBuffer 使用表面的 IntPtr）
    // 参考 WPFDXInterop：SetBackBuffer(D3DResourceType::IDirect3DSurface9, (IntPtr)(void*)pSurface9)
    return (void*)info->D3D9Surface;
}

LIGHTROOM_API bool LoadImageToTarget(void* renderTargetHandle, const char* imagePath) {
    if (!renderTargetHandle || g_RenderTargets.find(renderTargetHandle) == g_RenderTargets.end()) {
        return false;
    }
    
    auto& info = g_RenderTargets[renderTargetHandle];
    if (!info) {
        return false;
    }
    
    try {
        D3D11DynamicRHI* d3d11RHI = dynamic_cast<D3D11DynamicRHI*>(g_DynamicRHI.get());
        if (!d3d11RHI) {
            return false;
        }
        
        ID3D11Device* d3d11Device = d3d11RHI->GetDevice();
        if (!d3d11Device) {
            return false;
        }
        
        // 加载图片
        uint32_t imageWidth, imageHeight;
        if (!LoadImageWithWIC(imagePath, d3d11Device, info->ImageTexture, info->ImageSRV, imageWidth, imageHeight)) {
            return false;
        }
        
        info->bHasImage = true;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[SDK] Exception loading image: " << e.what() << std::endl;
        return false;
    }
}

LIGHTROOM_API void RenderToTarget(void* renderTargetHandle) {
    if (!renderTargetHandle || g_RenderTargets.find(renderTargetHandle) == g_RenderTargets.end()) {
        return;
    }
    
    auto& info = g_RenderTargets[renderTargetHandle];
    if (!info || !info->D3D11RTV || !info->D3D9Surface) {
        return;
    }
    
    try {
        D3D11DynamicRHI* d3d11RHI = dynamic_cast<D3D11DynamicRHI*>(g_DynamicRHI.get());
        if (!d3d11RHI) {
            return;
        }
        
        ID3D11Device* d3d11Device = d3d11RHI->GetDevice();
        ID3D11DeviceContext* d3d11Context = d3d11RHI->GetDeviceContext();
        if (!d3d11Device || !d3d11Context) {
            return;
        }
        
        // 1. 设置渲染目标
        d3d11Context->OMSetRenderTargets(1, info->D3D11RTV.GetAddressOf(), nullptr);
        
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)info->Width, (float)info->Height, 0.0f, 1.0f };
        d3d11Context->RSSetViewports(1, &vp);
        
        // 2. 如果有图片，渲染图片到渲染目标；否则清除为黑色
        if (info->bHasImage && info->ImageTexture && info->ImageSRV) {
            D3D11_TEXTURE2D_DESC imageDesc;
            info->ImageTexture->GetDesc(&imageDesc);
            
            if (imageDesc.Width == info->Width && imageDesc.Height == info->Height) {
                // 大小匹配，直接复制
                d3d11Context->CopyResource(info->D3D11Texture.Get(), info->ImageTexture.Get());
            } else {
                // 大小不匹配，使用 shader 进行缩放渲染
                // 清除为黑色
                float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                d3d11Context->ClearRenderTargetView(info->D3D11RTV.Get(), clearColor);
                
                // 使用简单的全屏四边形 shader 渲染图片
                if (g_SimpleVS && g_SimplePS && g_InputLayout && g_VertexBuffer && g_SamplerState) {
                    // 设置 shader
                    d3d11Context->VSSetShader(g_SimpleVS.Get(), nullptr, 0);
                    d3d11Context->PSSetShader(g_SimplePS.Get(), nullptr, 0);
                    d3d11Context->IASetInputLayout(g_InputLayout.Get());
                    
                    // 设置顶点缓冲区
                    UINT stride = sizeof(SimpleVertex);
                    UINT offset = 0;
                    d3d11Context->IASetVertexBuffers(0, 1, g_VertexBuffer.GetAddressOf(), &stride, &offset);
                    d3d11Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                    
                    // 设置纹理和采样器
                    d3d11Context->PSSetShaderResources(0, 1, info->ImageSRV.GetAddressOf());
                    d3d11Context->PSSetSamplers(0, 1, g_SamplerState.GetAddressOf());
                    
                    // 禁用深度测试和混合
                    d3d11Context->OMSetDepthStencilState(nullptr, 0);
                    d3d11Context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
                    
                    // 绘制全屏四边形
                    d3d11Context->Draw(4, 0);
                    
                    // 清理状态
                    ID3D11ShaderResourceView* nullSRV = nullptr;
                    d3d11Context->PSSetShaderResources(0, 1, &nullSRV);
                } else {
                    std::cerr << "[SDK] Simple render resources not initialized, cannot render scaled image" << std::endl;
                }
            }
        } else {
            // 没有图片，清除为黑色
            float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            d3d11Context->ClearRenderTargetView(info->D3D11RTV.Get(), clearColor);
        }
        
        // 3. 确保 D3D11 渲染完成
        d3d11Context->Flush();
        
        // 4. 使用 GPU 拷贝（StretchRect）从 D3D11 共享纹理拷贝到 D3D9 表面
        if (info->D3D9SharedSurface && info->D3D9Surface) {
            RECT srcRect = { 0, 0, (LONG)info->Width, (LONG)info->Height };
            RECT dstRect = { 0, 0, (LONG)info->Width, (LONG)info->Height };
            HRESULT hr = g_D3D9ExDevice->StretchRect(
                info->D3D9SharedSurface,  // 源：从 D3D11 共享纹理打开的表面
                &srcRect,
                info->D3D9Surface,  // 目标：WPF D3DImage 使用的表面
                &dstRect,
                D3DTEXF_NONE  // 不使用过滤
            );
            
            if (FAILED(hr)) {
                std::cerr << "[SDK] StretchRect failed: 0x" << std::hex << hr << std::endl;
            }
            
            // 强制 D3D9 设备提交命令
            if (g_D3D9ExDevice) {
                g_D3D9ExDevice->Present(nullptr, nullptr, nullptr, nullptr);
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[SDK] Exception rendering to target: " << e.what() << std::endl;
    }
}

LIGHTROOM_API void ResizeRenderTarget(void* renderTargetHandle, uint32_t width, uint32_t height) {
    if (!renderTargetHandle || g_RenderTargets.find(renderTargetHandle) == g_RenderTargets.end()) {
        return;
    }
    
    auto& info = g_RenderTargets[renderTargetHandle];
    if (!info || (info->Width == width && info->Height == height)) {
        return;
    }
    
    // 调整大小需要重新创建所有资源
    // 先销毁旧的，再创建新的
    void* oldHandle = renderTargetHandle;
    DestroyRenderTarget(oldHandle);
    
    void* newHandle = CreateRenderTarget(width, height);
    if (newHandle) {
        // 更新映射（如果需要保持相同的句柄）
        // 这里简化处理，调用者需要更新句柄
    }
}

} // extern "C"
