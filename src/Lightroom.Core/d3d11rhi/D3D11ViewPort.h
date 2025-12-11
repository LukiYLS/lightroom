#pragma once
#include "RHIViewPort.h"
#include "Common.h"
#include "D3D11Texture2D.h"
#include <memory>

namespace RenderCore
{
	class D3D11DynamicRHI;

	class D3D11ViewPort : public RHIViewPort
	{
	public:
		D3D11ViewPort(D3D11DynamicRHI *D3D11RHI, HWND InWindowHandle, uint32_t InSizeX, uint32_t InSizeY);
		virtual ~D3D11ViewPort();

		virtual void* GetNativeSwapChain() const;
		virtual void* GetNativeBackBufferTexture() const;
		virtual void* GetNativeBackBufferRT() const;

		virtual void SetRenderTarget() override;
		virtual void Prepare() override;
		virtual void Clear(const core::FLinearColor& Color) override;
		virtual void Present() override;
		virtual void Resize(uint32_t InSizeX, uint32_t InSizeY, bool bInIsFullscreen) override;
		core::vec2u GetSize() const;
	private:
		DXGI_MODE_DESC SetupDXGI_MODE_DESC() const;
		void GetSwapChainSurface();
		uint32_t GetSwapChainFlags() const;

	private:
		D3D11DynamicRHI* D3D11RHI;
		uint32_t SizeX;
		uint32_t SizeY;
		uint32_t BackBufferCount;
		HWND WindowHandle = nullptr;
		EPixelFormat PixelFormat = PF_B8G8R8A8;
		
		ComPtr<IDXGISwapChain> SwapChain;
		ComPtr<ID3D11RenderTargetView> BackBufferRenderTargetView;
		ComPtr<ID3D11Texture2D> BackBufferResource;
		std::shared_ptr<D3D11Texture2D> DepthSRV;
		bool bAllowTearing = false;

		static uint32_t GSwapChainFlags;
	};
}
