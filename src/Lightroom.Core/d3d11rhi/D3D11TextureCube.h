#pragma once
#include "RHITextureCube.h"
#include "Common.h"
#include <d3d11.h>
#include <memory>
#include <map>
#include <vector>

namespace RenderCore
{
	class D3D11DynamicRHI;
	class D3D11Texture2D;

	class D3D11TextureCube : public RHITextureCube
	{
	public:
		D3D11TextureCube(D3D11DynamicRHI* D3D11RHI);
		virtual ~D3D11TextureCube();

		virtual bool CreateTextureCube(EPixelFormat Format, int32_t SizeX, int32_t SizeY, uint32_t NumMips,bool CreateDepth) override;
		virtual core::vec2i GetSize() const override;
		virtual uint32_t GetNumMips() const override;

		ID3D11Texture2D* GetNativeTex() const;
		std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>> GetRTVS() const;
		std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>>& GetRTVS() ;
		ID3D11ShaderResourceView* GetSRV() const;
		std::shared_ptr<D3D11Texture2D> GetDepthTex() const;

	private:
		D3D11DynamicRHI* D3D11RHI = nullptr;
		std::shared_ptr<D3D11Texture2D> Tex2D;
		std::shared_ptr<D3D11Texture2D> DepthTex;
	};
}
