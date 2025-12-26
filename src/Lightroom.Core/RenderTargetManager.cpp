#include "RenderTargetManager.h"
#include "d3d11rhi/D3D11RHI.h"
#include "d3d11rhi/D3D11Texture2D.h"
#include "d3d11rhi/D3D11RenderTarget.h"
#include "d3d11rhi/RHIRenderTarget.h"
#include "d3d11rhi/Common.h"
#include <dxgi.h>
#include <wrl/client.h>
#include <iostream>

using namespace RenderCore;

namespace LightroomCore {

	RenderTargetManager::RenderTargetManager(std::shared_ptr<RenderCore::DynamicRHI> rhi, D3D9Interop* d3d9Interop)
		: m_RHI(rhi)
		, m_D3D9Interop(d3d9Interop)
	{
	}

	RenderTargetManager::~RenderTargetManager() {
		m_RenderTargets.clear();
	}

	// -----------------------------------------------------------------------------
	// 核心逻辑：资源初始化
	// -----------------------------------------------------------------------------
	bool RenderTargetManager::InitResources(RenderTargetInfo* info, uint32_t width, uint32_t height) {
		if (!m_RHI || !m_D3D9Interop || !m_D3D9Interop->IsInitialized()) {
			return false;
		}

		auto* d3d11RHI = dynamic_cast<D3D11DynamicRHI*>(m_RHI.get());
		if (!d3d11RHI) return false;

		ID3D11Device* d3d11Device = d3d11RHI->GetDevice();
		if (!d3d11Device) return false;

		// 创建支持共享的 D3D11 纹理
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

		HRESULT hr = d3d11Device->CreateTexture2D(&desc, nullptr, info->D3D11SharedTexture.ReleaseAndGetAddressOf());
		if (FAILED(hr)) return false;

		// 创建原生 RTV (可选，如果 RHI 内部会创建则此处为辅助)
		hr = d3d11Device->CreateRenderTargetView(info->D3D11SharedTexture.Get(), nullptr, info->D3D11SharedRTV.ReleaseAndGetAddressOf());
		if (FAILED(hr)) return false;

		ComPtr<IDXGIResource> dxgiResource;
		hr = info->D3D11SharedTexture.As(&dxgiResource);
		if (FAILED(hr)) return false;

		hr = dxgiResource->GetSharedHandle(&info->D3D11SharedHandle);
		if (FAILED(hr) || !info->D3D11SharedHandle) return false;

		auto d3d11RenderTarget = std::make_shared<D3D11RenderTarget>(d3d11RHI);

		if (!d3d11RenderTarget->CreateFromExistingTexture(
			info->D3D11SharedTexture.Get(),
			EPixelFormat::PF_B8G8R8A8)) {
			return false;
		}

		info->RHIRenderTarget = d3d11RenderTarget;
		info->RHITexture = info->RHIRenderTarget->GetTex();

		// D3D9 互操作：打开共享句柄
		info->D3D9SharedSurface.Reset();

		// 注意：假设 D3D9Interop 接口已更新为接受 IDirect3DSurface9** (ComPtr地址)
		if (!m_D3D9Interop->CreateSurfaceFromSharedHandle(
			info->D3D11SharedHandle,
			width,
			height,
			info->D3D9SharedSurface.GetAddressOf())) {
			return false;
		}

		info->Width = width;
		info->Height = height;

		return true;
	}

	void* RenderTargetManager::CreateRenderTarget(uint32_t width, uint32_t height) {
		try {
			auto info = std::make_unique<RenderTargetInfo>();

			if (!InitResources(info.get(), width, height)) {
				return nullptr;
			}

			void* handle = info.get();
			m_RenderTargets[handle] = std::move(info);
			return handle;
		}
		catch (...) {
			return nullptr;
		}
	}

	void RenderTargetManager::DestroyRenderTarget(void* handle) {
		if (handle) {
			m_RenderTargets.erase(handle);
		}
	}

	bool RenderTargetManager::ResizeRenderTarget(void* handle, uint32_t width, uint32_t height) {
		auto* info = GetRenderTargetInfo(handle);
		if (!info) return false;

		if (info->Width == width && info->Height == height) {
			return true;
		}

		// 清理 RHI 引用，确保重建时不被占用
		info->RHITexture.reset();
		info->RHIRenderTarget.reset();
		return InitResources(info, width, height);
	}

	RenderTargetManager::RenderTargetInfo* RenderTargetManager::GetRenderTargetInfo(void* handle) {
		auto it = m_RenderTargets.find(handle);
		if (it != m_RenderTargets.end()) {
			return it->second.get();
		}
		return nullptr;
	}

	void* RenderTargetManager::GetD3D9SharedHandle(void* handle) {
		auto* info = GetRenderTargetInfo(handle);
		if (info) {
			// 返回 Surface 的原始指针给 WPF 使用
			return info->D3D9SharedSurface.Get();
		}
		return nullptr;
	}

	std::shared_ptr<RHITexture2D> RenderTargetManager::GetRHITexture(void* handle) {
		auto* info = GetRenderTargetInfo(handle);
		return info ? info->RHITexture : nullptr;
	}

	HANDLE RenderTargetManager::GetD3D11SharedHandle(void* handle) {
		auto* info = GetRenderTargetInfo(handle);
		return info ? info->D3D11SharedHandle : nullptr;
	}

} // namespace LightroomCore

