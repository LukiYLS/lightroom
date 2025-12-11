#include "D3D11ViewPort.h"
#include "D3D11RHI.h"
#include "D3D11RHIPrivate.h"
#include "Common.h"

namespace RenderCore
{
	static DXGI_SWAP_EFFECT GSwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	static DXGI_SCALING GSwapScaling = DXGI_SCALING_STRETCH;
	static uint32_t GSwapChainBufferCount = 1;
	static int32_t GD3D11UseAllowTearing = 1;

	uint32_t D3D11ViewPort::GSwapChainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	D3D11ViewPort::D3D11ViewPort(D3D11DynamicRHI* InD3D11RHI, HWND InWindowHandle, uint32_t InSizeX, uint32_t InSizeY)
		: D3D11RHI(InD3D11RHI), WindowHandle(InWindowHandle), SizeX(InSizeX), SizeY(InSizeY)
	{ 
		ComPtr<IDXGIDevice> DXGIDevice;
		D3D11RHI->GetDevice()->QueryInterface(IID_IDXGIDevice, (void**)DXGIDevice.GetAddressOf());

		{
			GSwapEffect = DXGI_SWAP_EFFECT_DISCARD;
			GSwapScaling = DXGI_SCALING_STRETCH;
			GSwapChainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
			IDXGIFactory1* Factory1 = D3D11RHI->GetFactory();
			ComPtr<IDXGIFactory5> Factory5;

			if (GD3D11UseAllowTearing)
			{
				//https://devblogs.microsoft.com/directx/dxgi-flip-model/#:~:text=DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING%20can%20enable%20even%20lower%20latency%20than%20the,using%20the%20DXGI_SCALING%20property%20set%20during%20swapchain%20creation.
				if (S_OK == Factory1->QueryInterface(IID_PPV_ARGS(Factory5.GetAddressOf())))
				{
					UINT AllowTearing = 0;
					if (S_OK == Factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &AllowTearing, sizeof(UINT)) && AllowTearing != 0)
					{
						GSwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
						GSwapScaling = DXGI_SCALING_NONE;
						GSwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
						bAllowTearing = true;
						GSwapChainBufferCount = 2;
					}
				}
			}
		}


		{
			BackBufferCount = GSwapChainBufferCount;

			// Create the swapchain.
			DXGI_SWAP_CHAIN_DESC SwapChainDesc;
			ZeroMemory(&SwapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));

			SwapChainDesc.BufferDesc = SetupDXGI_MODE_DESC();
			// MSAA Sample count
			SwapChainDesc.SampleDesc.Count = 1;
			SwapChainDesc.SampleDesc.Quality = 0;
			SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
			// 1:single buffering, 2:double buffering, 3:triple buffering
			SwapChainDesc.BufferCount = BackBufferCount;
			SwapChainDesc.OutputWindow = InWindowHandle;
			SwapChainDesc.Windowed = TRUE;
			// DXGI_SWAP_EFFECT_DISCARD / DXGI_SWAP_EFFECT_SEQUENTIAL
			SwapChainDesc.SwapEffect = GSwapEffect;
			SwapChainDesc.Flags = GSwapChainFlags;

			HRESULT CreateSwapChainResult = D3D11RHI->GetFactory()->CreateSwapChain(D3D11RHI->GetDevice(), &SwapChainDesc, SwapChain.GetAddressOf());
			if (CreateSwapChainResult == E_INVALIDARG)
			{

				core::log::info("CreateSwapChain invalid arguments");
			}
			// VERIFYD3DRESULT(CreateSwapChainResult);
			GetSwapChainSurface();
		}

		DepthSRV = std::make_shared<D3D11Texture2D>(D3D11RHI);
		DepthSRV->CreateTexture2D(RenderCore::PF_DepthStencil, ETextureCreateFlags::TexCreate_DepthStencilTargetable , InSizeX, InSizeY);

		// ImGui_ImplWin32_Init(InWindowHandle);
	}

	D3D11ViewPort::~D3D11ViewPort()
	{
		
	}

	void* D3D11ViewPort::GetNativeSwapChain() const
	{
		return SwapChain.Get();
	}

	void* D3D11ViewPort::GetNativeBackBufferTexture() const
	{
		return BackBufferResource.Get();
	}

	void* D3D11ViewPort::GetNativeBackBufferRT() const
	{
		return BackBufferRenderTargetView.Get();
	}

	void D3D11ViewPort::SetRenderTarget()
	{
		ID3D11RenderTargetView* rtv = BackBufferRenderTargetView.Get();
		D3D11RHI->GetDeviceContext()->OMSetRenderTargets(1, &rtv, DepthSRV->GetDSV());
	}

	void D3D11ViewPort::Prepare()
	{
		// ImGui_ImplDX11_NewFrame();
		// ImGui_ImplWin32_NewFrame();
		// ImGui::NewFrame();
	}

	void D3D11ViewPort::Clear(const core::FLinearColor& Color)
	{
		D3D11RHI->GetDeviceContext()->ClearRenderTargetView(BackBufferRenderTargetView.Get(), &Color.R);
		D3D11RHI->GetDeviceContext()->ClearDepthStencilView(DepthSRV->GetDSV(), D3D11_CLEAR_DEPTH, 1.0f, 0.f);
	}

	void D3D11ViewPort::Present()
	{
		// ImGui::Render();
		// ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		D3D11RHI->GetDefaultCommandContext()->RHIEndDrawing();
		if (SwapChain)
		{
			// Present the back buffer to the viewport window.
			uint32_t Flags = 0;
			if ((GetSwapChainFlags() & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0 )
			{
				Flags |= DXGI_PRESENT_ALLOW_TEARING;
			}
			auto Result = SwapChain->Present(0, Flags);

			//Impl->SwapChain->Present(0, 0);
		}
	}

	void D3D11ViewPort::Resize(uint32_t InSizeX, uint32_t InSizeY, bool bInIsFullscreen)
	{
		D3D11RHI->ClearState();
		D3D11RHI->GetDeviceContext()->Flush();
		BackBufferResource.Reset();
		BackBufferRenderTargetView.Reset();

		if (SizeX != InSizeX || SizeY != InSizeY )
		{
			SizeX = InSizeX;
			SizeY = InSizeY;

			// Resize the swap chain.

			const UINT SwapChainFlags = GetSwapChainFlags();
			const DXGI_FORMAT RenderTargetFormat = GetRenderTargetFormat(PixelFormat);
			
			// Resize all existing buffers, don't change count
			SwapChain->ResizeBuffers(0, SizeX, SizeY, RenderTargetFormat, SwapChainFlags);

			if (bInIsFullscreen)
			{
				DXGI_MODE_DESC BufferDesc = SetupDXGI_MODE_DESC();

				if (FAILED(SwapChain->ResizeTarget(&BufferDesc)))
				{
					//ResetSwapChainInternal(true);
					SwapChain->ResizeBuffers(0, SizeX, SizeY, RenderTargetFormat, SwapChainFlags);

				}
			}
			GetSwapChainSurface();
			DepthSRV = std::make_shared<D3D11Texture2D>(D3D11RHI);
			DepthSRV->CreateTexture2D(RenderCore::PF_DepthStencil, ETextureCreateFlags::TexCreate_DepthStencilTargetable, InSizeX, InSizeY);
		}
	}

	core::vec2u D3D11ViewPort::GetSize() const
	{
		return core::vec2u(SizeX, SizeY);
	}

	DXGI_MODE_DESC D3D11ViewPort::SetupDXGI_MODE_DESC() const
	{
		DXGI_MODE_DESC Ret;
		ZeroMemory(&Ret, sizeof(Ret));
		Ret.Width = SizeX;
		Ret.Height = SizeY;
		Ret.RefreshRate.Numerator = 60;	
		Ret.RefreshRate.Denominator = 1;	
		Ret.Format = GetRenderTargetFormat(PixelFormat);
		//Ret.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		//Ret.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

		return Ret;
	}

	void D3D11ViewPort::GetSwapChainSurface()
	{
		SwapChain->GetBuffer(0, IID_ID3D11Texture2D, (void**)BackBufferResource.GetAddressOf());

		D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
		RTVDesc.Format = DXGI_FORMAT_UNKNOWN;
		RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		RTVDesc.Texture2D.MipSlice = 0;
		D3D11RHI->GetDevice()->CreateRenderTargetView(BackBufferResource.Get(), &RTVDesc, BackBufferRenderTargetView.GetAddressOf());
	}

	uint32_t D3D11ViewPort::GetSwapChainFlags() const
	{
		uint32_t SwapChainFlags = GSwapChainFlags;

		// Ensure AllowTearing consistency or ResizeBuffers will fail with E_INVALIDARG
		if ( bAllowTearing != !!(SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING))
		{
			SwapChainFlags ^= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		}

		return SwapChainFlags;
	}


}
