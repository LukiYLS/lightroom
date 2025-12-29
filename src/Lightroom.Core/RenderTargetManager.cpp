#include "RenderTargetManager.h"
#include "d3d11rhi/D3D11RHI.h"
#include "d3d11rhi/D3D11Texture2D.h"
#include "d3d11rhi/D3D11RenderTarget.h"
#include "d3d11rhi/RHIRenderTarget.h"
#include "d3d11rhi/Common.h"
#include <dxgi.h>
#include <wrl/client.h>
#include <iostream>
#include <thread>
#include <chrono>

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
	// 核心逻辑：初始化单个缓冲区资源
	// -----------------------------------------------------------------------------
	bool RenderTargetManager::InitBufferResources(BufferResources* buffer, uint32_t width, uint32_t height, bool createD3D9Surface) {
		if (!m_RHI) {
			return false;
		}

		// Front Buffer需要D3D9 Surface，需要检查D3D9Interop
		if (createD3D9Surface) {
			if (!m_D3D9Interop || !m_D3D9Interop->IsInitialized()) {
				return false;
			}
		}

		auto* d3d11RHI = dynamic_cast<D3D11DynamicRHI*>(m_RHI.get());
		if (!d3d11RHI) return false;

		ID3D11Device* d3d11Device = d3d11RHI->GetDevice();
		if (!d3d11Device) return false;

		// 创建 D3D11 纹理
		// Back Buffer不需要共享，所以不需要D3D11_RESOURCE_MISC_SHARED标志
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
		
		// 只有Front Buffer需要共享（给WPF使用）
		if (createD3D9Surface) {
			desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
		} else {
			desc.MiscFlags = 0;
		}

		HRESULT hr = d3d11Device->CreateTexture2D(&desc, nullptr, buffer->D3D11SharedTexture.ReleaseAndGetAddressOf());
		if (FAILED(hr)) return false;

		// 创建原生 RTV
		hr = d3d11Device->CreateRenderTargetView(buffer->D3D11SharedTexture.Get(), nullptr, buffer->D3D11SharedRTV.ReleaseAndGetAddressOf());
		if (FAILED(hr)) return false;

		// 只有Front Buffer需要共享句柄和D3D9 Surface
		if (createD3D9Surface) {
			ComPtr<IDXGIResource> dxgiResource;
			hr = buffer->D3D11SharedTexture.As(&dxgiResource);
			if (FAILED(hr)) return false;

			hr = dxgiResource->GetSharedHandle(&buffer->D3D11SharedHandle);
			if (FAILED(hr) || !buffer->D3D11SharedHandle) return false;

			// D3D9 互操作：打开共享句柄
			buffer->D3D9SharedSurface.Reset();
			if (!m_D3D9Interop->CreateSurfaceFromSharedHandle(
				buffer->D3D11SharedHandle,
				width,
				height,
				buffer->D3D9SharedSurface.GetAddressOf())) {
				return false;
			}
		} else {
			// Back Buffer不需要共享句柄和D3D9 Surface
			buffer->D3D11SharedHandle = nullptr;
			buffer->D3D9SharedSurface.Reset();
		}

		auto d3d11RenderTarget = std::make_shared<D3D11RenderTarget>(d3d11RHI);

		if (!d3d11RenderTarget->CreateFromExistingTexture(
			buffer->D3D11SharedTexture.Get(),
			EPixelFormat::PF_B8G8R8A8)) {
			return false;
		}

		buffer->RHIRenderTarget = d3d11RenderTarget;
		buffer->RHITexture = buffer->RHIRenderTarget->GetTex();

		buffer->Width = width;
		buffer->Height = height;

		return true;
	}

	// -----------------------------------------------------------------------------
	// 从旧缓冲区复制内容到新缓冲区（用于resize时保持画面）
	// 注意：这是一个优化，用于在resize时避免画面变黑
	// 如果尺寸不同，只复制重叠区域；实际渲染会在resize后立即进行
	// -----------------------------------------------------------------------------
	bool RenderTargetManager::CopyBufferContent(BufferResources* srcBuffer, BufferResources* dstBuffer) {
		if (!srcBuffer || !dstBuffer || !srcBuffer->D3D11SharedTexture || !dstBuffer->D3D11SharedTexture) {
			return false;
		}

		auto* d3d11RHI = dynamic_cast<D3D11DynamicRHI*>(m_RHI.get());
		if (!d3d11RHI) return false;

		ID3D11DeviceContext* context = d3d11RHI->GetDeviceContext();
		if (!context) return false;

		// 使用GPU复制纹理内容
		uint32_t srcWidth = srcBuffer->Width;
		uint32_t srcHeight = srcBuffer->Height;
		uint32_t dstWidth = dstBuffer->Width;
		uint32_t dstHeight = dstBuffer->Height;

		if (srcWidth == dstWidth && srcHeight == dstHeight) {
			// 尺寸相同，直接复制整个纹理
			context->CopyResource(dstBuffer->D3D11SharedTexture.Get(), srcBuffer->D3D11SharedTexture.Get());
		} else {
			// 尺寸不同，复制重叠区域（作为临时内容，避免完全黑屏）
			// 注意：实际渲染会在resize后立即进行，所以这只是过渡
			uint32_t copyWidth = std::min(srcWidth, dstWidth);
			uint32_t copyHeight = std::min(srcHeight, dstHeight);
			
			if (copyWidth > 0 && copyHeight > 0) {
				D3D11_BOX srcBox = {};
				srcBox.left = 0;
				srcBox.top = 0;
				srcBox.front = 0;
				srcBox.right = copyWidth;
				srcBox.bottom = copyHeight;
				srcBox.back = 1;

				context->CopySubresourceRegion(
					dstBuffer->D3D11SharedTexture.Get(), 0,
					0, 0, 0,
					srcBuffer->D3D11SharedTexture.Get(), 0,
					&srcBox
				);
			}
		}

		// 不调用 Flush()，避免阻塞
		// CopyResource/CopySubresourceRegion 命令已经提交到命令队列，GPU 会异步执行
		return true;
	}

	// -----------------------------------------------------------------------------
	// 初始化资源：Front Buffer（给WPF）+ Back Buffers（内部渲染）
	// -----------------------------------------------------------------------------
	bool RenderTargetManager::InitResources(RenderTargetInfo* info, uint32_t width, uint32_t height) {
		// 初始化Front Buffer（固定给WPF使用）
		if (!InitBufferResources(&info->FrontBuffer, width, height)) {
			return false;
		}

		// 初始化两个Back Buffer（用于渲染，不需要D3D9 Surface）
		// 注意：AcquireNextRenderBuffer会先切换缓冲区，所以需要初始化两个
		for (int32_t i = 0; i < 2; ++i) {
			if (!InitBufferResources(&info->BackBuffers[i], width, height, false)) {
				// 如果失败，清理Front Buffer和已创建的Back Buffers
				info->FrontBuffer = BufferResources();
				for (int32_t j = 0; j < i; ++j) {
					info->BackBuffers[j] = BufferResources();
				}
				return false;
			}
		}

		info->Width = width;
		info->Height = height;
		info->CurrentBackBuffer = 0;
		info->UpdateActiveReferences();

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

		// 保存旧Front Buffer的内容（用于复制）
		// 重要：必须先移动到临时变量，因为InitBufferResources会覆盖原内容
		BufferResources oldFront = std::move(info->FrontBuffer);
		bool hasContent = oldFront.D3D11SharedTexture != nullptr && 
		                  oldFront.Width > 0 && oldFront.Height > 0;

		// 重新初始化Front Buffer（WPF使用的固定缓冲区）
		// 注意：这里会改变WPF的Surface指针，但resize时这是必要的
		if (!InitBufferResources(&info->FrontBuffer, width, height)) {
			return false;
		}

		// 重新初始化所有Back Buffers（不需要D3D9 Surface）
		for (int32_t i = 0; i < 2; ++i) {
			if (!InitBufferResources(&info->BackBuffers[i], width, height, false)) {
				// 如果失败，清理已创建的缓冲区
				info->FrontBuffer = BufferResources();
				for (int32_t j = 0; j < i; ++j) {
					info->BackBuffers[j] = BufferResources();
				}
				return false;
			}
		}

		// 如果旧缓冲区有内容，复制到新Front Buffer（避免resize时变黑）
		// 注意：CopyBufferContent 是异步的，不需要等待完成
		// 实际渲染会在resize后立即进行，所以这只是过渡内容
		if (hasContent) {
			CopyBufferContent(&oldFront, &info->FrontBuffer);
			// 不等待GPU完成，避免阻塞
			// 后续的渲染会覆盖这个临时内容
		}

		info->Width = width;
		info->Height = height;
		info->CurrentBackBuffer = 0;
		info->UpdateActiveReferences();

		return true;
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
			// 返回Front Buffer的Surface指针（固定给WPF使用）
			return info->FrontBuffer.D3D9SharedSurface.Get();
		}
		return nullptr;
	}

	std::shared_ptr<RHITexture2D> RenderTargetManager::GetRHITexture(void* handle) {
		// 返回当前Back Buffer的纹理（不切换，用于读取等操作）
		auto* info = GetRenderTargetInfo(handle);
		if (!info) {
			return nullptr;
		}
		
		// 返回当前Back Buffer的纹理
		BufferResources* backBuffer = &info->BackBuffers[info->CurrentBackBuffer];
		if (!backBuffer->D3D11SharedTexture) {
			return nullptr;
		}
		
		return backBuffer->RHITexture;
	}

	HANDLE RenderTargetManager::GetD3D11SharedHandle(void* handle) {
		auto* info = GetRenderTargetInfo(handle);
		return info ? info->FrontBuffer.D3D11SharedHandle : nullptr;
	}

	std::shared_ptr<RHITexture2D> RenderTargetManager::AcquireNextRenderBuffer(void* handle) {
		auto* info = GetRenderTargetInfo(handle);
		if (!info) {
			return nullptr;
		}

		// 双缓冲轮换：切换到下一个Back Buffer
		info->CurrentBackBuffer = (info->CurrentBackBuffer + 1) % 2;
		
		// 确保Back Buffer已初始化（Back Buffer不需要D3D9 Surface）
		BufferResources* backBuffer = &info->BackBuffers[info->CurrentBackBuffer];
		if (!backBuffer->D3D11SharedTexture || 
		    backBuffer->Width != info->Width ||
		    backBuffer->Height != info->Height) {
			// 需要初始化Back Buffer（不需要D3D9 Surface）
			if (!InitBufferResources(backBuffer, info->Width, info->Height, false)) {
				return nullptr;
			}
		}

		return backBuffer->RHITexture;
	}

	bool RenderTargetManager::PresentBackBuffer(void* handle) {
		auto* info = GetRenderTargetInfo(handle);
		if (!info) {
			return false;
		}

		BufferResources* backBuffer = &info->BackBuffers[info->CurrentBackBuffer];
		if (!backBuffer->D3D11SharedTexture || !info->FrontBuffer.D3D11SharedTexture) {
			return false;
		}

		auto* d3d11RHI = dynamic_cast<D3D11DynamicRHI*>(m_RHI.get());
		if (!d3d11RHI) return false;

		ID3D11DeviceContext* context = d3d11RHI->GetDeviceContext();
		if (!context) return false;

		// 将Back Buffer的内容复制到Front Buffer
		// 注意：CopyResource 是异步的，不需要等待完成
		// WPF 会在需要时读取 Front Buffer，GPU 会异步执行复制
		context->CopyResource(
			info->FrontBuffer.D3D11SharedTexture.Get(),
			backBuffer->D3D11SharedTexture.Get()
		);

		// 不调用 Flush()，避免阻塞
		// CopyResource 命令已经提交到命令队列，GPU 会异步执行
		// WPF 的 D3DImage 会在需要时自动等待 GPU 完成

		return true;
	}

} // namespace LightroomCore

