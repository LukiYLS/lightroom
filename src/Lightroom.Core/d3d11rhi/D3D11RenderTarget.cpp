#include "D3D11RenderTarget.h"
#include "D3D11RHI.h"
#include "D3D11Texture2D.h"
#include "D3D11CommandContext.h"

namespace RenderCore
{
	D3D11RenderTarget::D3D11RenderTarget(D3D11DynamicRHI* InD3D11RHI)
		: D3D11RHI(InD3D11RHI)
	{
		
	}

	D3D11RenderTarget::~D3D11RenderTarget()
	{
	}


	bool D3D11RenderTarget::Create(EPixelFormat Format, int32_t SizeX, int32_t SizeY, uint32_t NumMips, bool IsMultiSampled, bool CreateDepth)
	{
		Size = core::vec2i(SizeX, SizeY);
		Tex2D = std::make_shared<D3D11Texture2D>(D3D11RHI);
		int32_t Flags = ETextureCreateFlags::TexCreate_RenderTargetable | ETextureCreateFlags::TexCreate_ShaderResource;
		//if (NumMips > 1)
		//{
		//	Flags |= TexCreate_GenerateMipCapable;
		//}
		if (IsMultiSampled)
		{
			Flags |= ETextureCreateFlags::TexCreate_MSAA;
		}

		if (!Tex2D->CreateTexture2D(Format, Flags,SizeX,SizeY,1, NumMips))
		{
			return false;
		}
		if (CreateDepth)
		{
			DepthTex = std::make_shared<D3D11Texture2D>(D3D11RHI);
			Flags = ETextureCreateFlags::TexCreate_DepthStencilTargetable;
			if (IsMultiSampled)
			{
				Flags |= ETextureCreateFlags::TexCreate_MSAA;
			}

			return DepthTex->CreateTexture2D(EPixelFormat::PF_DepthStencil, Flags, SizeX, SizeY);
		}
		return true;
	}

	core::vec2i D3D11RenderTarget::GetSize() const
	{
		return Size;
	}

	void D3D11RenderTarget::Bind()
	{
		D3D11RHI->GetDefaultCommandContext()->SetRenderTarget(Tex2D,DepthTex);
	}

	void D3D11RenderTarget::UnBind()
	{
		D3D11RHI->GetDefaultCommandContext()->SetRenderTarget(nullptr, nullptr);
	}

	ID3D11Texture2D* D3D11RenderTarget::GetNativeTex() const
	{
		if (!Tex2D)
		{
			return nullptr;
		}
		return Tex2D->GetNativeTex();
	}

	ID3D11RenderTargetView* D3D11RenderTarget::GetRTV() const
	{
		if (!Tex2D)
		{
			return nullptr;
		}
		return Tex2D->GetRTV();
	}

	ID3D11ShaderResourceView* D3D11RenderTarget::GetSRV() const
	{
		if (!Tex2D)
		{
			return nullptr;
		}
		return Tex2D->GetSRV();
	}

	ID3D11DepthStencilView* D3D11RenderTarget::GetDSV() const
	{
		if (!DepthTex)
		{
			return nullptr;
		}
		return DepthTex->GetDSV();
	}

	std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>> D3D11RenderTarget::GetRTVS() const
	{
		return Tex2D->GetRTVS();
	}

	std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>>& D3D11RenderTarget::GetRTVS()
	{
		return Tex2D->GetRTVS();
	}

	std::shared_ptr< RenderCore::RHITexture2D> D3D11RenderTarget::GetTex() const
	{
		return Tex2D;
	}

}
