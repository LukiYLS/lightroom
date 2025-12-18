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
#include <vector>
#include <iostream>
#include <algorithm>

#include "ImageExporter.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include "../d3d11rhi/D3D11CommandContext.h"
#include "../d3d11rhi/RHIDefinitions.h"
#include <d3d11.h>
#include <wrl/client.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")

namespace LightroomCore {

ImageExporter::ImageExporter(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : m_RHI(rhi)
{
}

ImageExporter::~ImageExporter() {
}

bool ImageExporter::SaveImageDataWithWIC(const std::string& filePath,
	const uint8_t* imageData,
	uint32_t width,
	uint32_t height,
	uint32_t stride,
	ExportFormat format,
	uint32_t quality) {
	Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
	if (FAILED(hr)) return false;

	// 1. 处理文件路径 (这里保留你原有的转码逻辑，省略以节省篇幅)
	std::wstring widePath;
	int pathLen = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
	widePath.resize(pathLen);
	MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, &widePath[0], pathLen);


	Microsoft::WRL::ComPtr<IWICStream> stream;
	hr = wicFactory->CreateStream(&stream);
	if (FAILED(hr)) return false;
	hr = stream->InitializeFromFilename(widePath.c_str(), GENERIC_WRITE);
	if (FAILED(hr)) return false;

	// 2. 创建编码器
	GUID containerFormat = (format == ExportFormat::PNG) ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg;
	Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
	hr = wicFactory->CreateEncoder(containerFormat, nullptr, &encoder);
	if (FAILED(hr)) return false;
	hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
	if (FAILED(hr)) return false;

	// 3. 创建帧
	Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frameEncoder;
	Microsoft::WRL::ComPtr<IPropertyBag2> propertyBag;
	hr = encoder->CreateNewFrame(&frameEncoder, &propertyBag);
	if (FAILED(hr)) return false;

	// 设置 JPEG 质量
	if (format == ExportFormat::JPEG && propertyBag) {
		PROPBAG2 option = { 0 };
		option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
		VARIANT varValue;
		VariantInit(&varValue);
		varValue.vt = VT_R4;
		varValue.fltVal = quality / 100.0f;
		propertyBag->Write(1, &option, &varValue);
	}

	hr = frameEncoder->Initialize(propertyBag.Get());
	if (FAILED(hr)) return false;

	// 4. 创建一个 WIC Bitmap 包装我们的内存数据
	Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap;
	hr = wicFactory->CreateBitmapFromMemory(
		width,
		height,
		GUID_WICPixelFormat32bppBGRA,
		stride,
		stride * height,
		const_cast<uint8_t*>(imageData),
		&wicBitmap
	);
	if (FAILED(hr)) {
		std::cerr << "Failed to create WIC Bitmap from memory: 0x" << std::hex << hr << std::endl;
		return false;
	}

	// 5. 【关键修复】使用 WriteSource，让 WIC 自动处理格式转换 (32bpp -> 24bpp 等)
	// 这样即使导出 JPEG，WIC 也会自动丢弃 Alpha 通道，而不会导致错位条纹
	hr = frameEncoder->SetSize(width, height);
	if (FAILED(hr)) return false;

	// 这里的 pixelFormat 只是建议，WriteSource 会内部处理转换
	WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
	hr = frameEncoder->SetPixelFormat(&pixelFormat);

	// 写入数据源
	hr = frameEncoder->WriteSource(wicBitmap.Get(), nullptr);
	if (FAILED(hr)) {
		std::cerr << "Failed to WriteSource: 0x" << std::hex << hr << std::endl;
		return false;
	}

	hr = frameEncoder->Commit();
	if (FAILED(hr)) return false;
	hr = encoder->Commit();
	if (FAILED(hr)) return false;

	return true;
}

bool ImageExporter::ReadD3D11TextureData(ID3D11Texture2D* d3d11Texture,
	uint32_t& outWidth, uint32_t& outHeight,
	std::vector<uint8_t>& outData, uint32_t& outStride) {

	if (!d3d11Texture) return false;

	// 1. 获取源纹理信息
	D3D11_TEXTURE2D_DESC srcDesc;
	d3d11Texture->GetDesc(&srcDesc);
	outWidth = srcDesc.Width;
	outHeight = srcDesc.Height;

	Microsoft::WRL::ComPtr<ID3D11Device> device;
	d3d11Texture->GetDevice(&device);
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	device->GetImmediateContext(&context);

	// 2. 准备源数据指针
	// 如果是 MSAA (多重采样) 纹理，不能直接 CopyResource 到 Staging，必须先 Resolve
	ID3D11Texture2D* pSourceTexture = d3d11Texture;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> pResolvedTexture;

	if (srcDesc.SampleDesc.Count > 1) {
		D3D11_TEXTURE2D_DESC resolveDesc = srcDesc;
		resolveDesc.SampleDesc.Count = 1;
		resolveDesc.SampleDesc.Quality = 0;
		resolveDesc.Usage = D3D11_USAGE_DEFAULT;
		resolveDesc.BindFlags = 0;
		resolveDesc.CPUAccessFlags = 0;

		// 修正 Typeless 格式 (Resolve 必须指定具体格式)
		DXGI_FORMAT resolveFormat = srcDesc.Format;
		if (resolveFormat == DXGI_FORMAT_R8G8B8A8_TYPELESS) resolveFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		if (resolveFormat == DXGI_FORMAT_B8G8R8A8_TYPELESS) resolveFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

		if (FAILED(device->CreateTexture2D(&resolveDesc, nullptr, &pResolvedTexture))) return false;

		context->ResolveSubresource(pResolvedTexture.Get(), 0, d3d11Texture, 0, resolveFormat);
		pSourceTexture = pResolvedTexture.Get();
	}

	// 3. 创建 Staging 纹理 (用于 CPU 读取)
	// 直接使用源纹理的格式，不做中间转换，效率最高
	D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
	stagingDesc.SampleDesc.Count = 1; // Staging 必须是单采样
	stagingDesc.SampleDesc.Quality = 0;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0; // 清除 Shared 等标志

	// 再次修正 Typeless，Staging 最好也是具体格式
	if (stagingDesc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	if (stagingDesc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS) stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTex;
	if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &stagingTex))) return false;

	// 4. 拷贝数据 (GPU -> CPU Staging)
	context->CopyResource(stagingTex.Get(), pSourceTexture);

	// 5. 映射内存
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

	// 6. 数据搬运
	outStride = outWidth * 4;
	outData.resize(outStride * outHeight);

	uint8_t* dst = outData.data();
	uint8_t* src = reinterpret_cast<uint8_t*>(mapped.pData);

	// 检查是否需要交换红蓝通道 (RGBA -> BGRA)
	// 通常 RenderTarget 是 RGBA，而 WIC Bitmap 最好是 BGRA
	bool isRGBA = (srcDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || srcDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
	for (int y = 0; y < (int)outHeight; ++y) {
		uint8_t* rowSrc = src + (y * mapped.RowPitch);
		uint8_t* rowDst = dst + (y * outStride);

		if (isRGBA) {
			for (uint32_t x = 0; x < outWidth; ++x) {
				rowDst[x * 4 + 0] = rowSrc[x * 4 + 2]; // B
				rowDst[x * 4 + 1] = rowSrc[x * 4 + 1]; // G
				rowDst[x * 4 + 2] = rowSrc[x * 4 + 0]; // R
				rowDst[x * 4 + 3] = rowSrc[x * 4 + 3]; // A
			}
		}
		else {
			// 如果已经是 BGRA，直接内存拷贝这一行
			memcpy(rowDst, rowSrc, outStride);
		}
	}

	context->Unmap(stagingTex.Get(), 0);
	return true;
}
} // namespace LightroomCore

 