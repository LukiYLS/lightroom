#pragma once
#include "RHITexture2D.h"
#include "Common.h"
#include <map>
#include <vector>
#include <memory>

namespace RenderCore
{
	class D3D11DynamicRHI;

	// std::shared_ptr<uint8_t> GetImageData(const std::wstring& path,int32_t &X,int32_t &Y);

	class D3D11Texture2D : public RHITexture2D
	{
	public:
		D3D11Texture2D(D3D11DynamicRHI* D3D11RHI);
		virtual ~D3D11Texture2D();

		virtual bool CreateTexture2D(EPixelFormat Format, int32_t Flags, int32_t SizeX, int32_t SizeY, int32_t SizeZ=1, 
										uint32_t NumMips=1, void* InBuffer = nullptr, int RowBytes = 0) override;
		bool CreateTexture2D(EPixelFormat Format, int32_t Flags, int32_t SizeX, int32_t SizeY, int32_t SizeZ, bool bCubeTexture, uint32_t NumMips, void* InBuffer, size_t RowBytes);
		virtual bool CreateFromFile(const std::wstring& FileName) override;
		virtual bool CreateHDRFromFile(const std::wstring& FileName) override;
		virtual bool IsMultisampled() const override;
		virtual core::vec2i GetSize() const;
		virtual uint32_t GetNumMips() const;
		virtual EPixelFormat GetPixelFormat() const override;

		ID3D11Texture2D* GetNativeTex() const;
		ID3D11RenderTargetView* GetRTV() const;
		std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>> GetRTVS() const;
		std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>>& GetRTVS();
		ID3D11ShaderResourceView* GetSRV() const;
		ID3D11DepthStencilView* GetDSV() const;

	private:
		D3D11DynamicRHI* D3D11RHI;
		ComPtr<ID3D11Texture2D> Tex2D;
		ComPtr<ID3D11Texture2D> DepthTex;
		ComPtr<ID3D11ShaderResourceView> TexSRV;
		ComPtr<ID3D11DepthStencilView> TexDSV;

		std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>> TexRTVS;

		core::vec2i Size;
		bool bIsMultisampled{ false };
		uint32_t NumMips{ 1 };
		EPixelFormat Format;
	};
}
