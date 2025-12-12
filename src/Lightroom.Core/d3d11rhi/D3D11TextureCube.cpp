#include "D3D11TextureCube.h"
#include "D3D11Texture2D.h"
#include "D3D11RHI.h"

namespace RenderCore
{
	D3D11TextureCube::D3D11TextureCube(D3D11DynamicRHI* InD3D11RHI)
		: D3D11RHI(InD3D11RHI)
	{
		Tex2D = std::make_shared<D3D11Texture2D>(D3D11RHI);
		DepthTex = std::make_shared<D3D11Texture2D>(D3D11RHI);
	}

	D3D11TextureCube::~D3D11TextureCube()
	{
	}

	bool D3D11TextureCube::CreateTextureCube(EPixelFormat Format, int32_t SizeX, int32_t SizeY, uint32_t NumMips, bool CreateDepth)
	{
		bool Ret =  Tex2D->CreateTexture2D(Format, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_GenerateMipCapable, SizeX, SizeY, 6,true, NumMips,nullptr,0);
		if (CreateDepth)
		{
			Ret &= DepthTex->CreateTexture2D(RenderCore::PF_DepthStencil, ETextureCreateFlags::TexCreate_DepthStencilTargetable, SizeX, SizeY);
		}
		
		return Ret;
	}

	core::vec2i D3D11TextureCube::GetSize() const
	{
		return Tex2D->GetSize();
	}

	uint32_t D3D11TextureCube::GetNumMips() const
	{
		return Tex2D->GetNumMips();
	}

	ID3D11Texture2D* D3D11TextureCube::GetNativeTex() const
	{
		return Tex2D->GetNativeTex();
	}

	std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>> D3D11TextureCube::GetRTVS() const
	{
		return Tex2D->GetRTVS();
	}

	std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>>& D3D11TextureCube::GetRTVS()
	{
		return Tex2D->GetRTVS();
	}

	ID3D11ShaderResourceView* D3D11TextureCube::GetSRV() const
	{
		return Tex2D->GetSRV();
	}

	std::shared_ptr<D3D11Texture2D> D3D11TextureCube::GetDepthTex() const
	{
		return DepthTex;
	}

}
