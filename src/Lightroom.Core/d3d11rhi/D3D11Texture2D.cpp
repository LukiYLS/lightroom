#include "D3D11Texture2D.h"
#include "RHIDefinitions.h"
#include "D3D11RHI.h"
#include "Common.h"
#include "D3D11RHIPrivate.h"

// #include "DirectXTex/DirectXTex.h"
// #include "tinygltf/stb_image.h"

namespace RenderCore
{

	/**
 * Creates a 2D texture optionally guarded by a structured exception handler.
 */
	bool SafeCreateTexture2D(ID3D11Device* Direct3DDevice, int32_t UEFormat, const D3D11_TEXTURE2D_DESC* TextureDesc, const D3D11_SUBRESOURCE_DATA* SubResourceData, ID3D11Texture2D** OutTexture2D)
	{
		HRESULT hResult = Direct3DDevice->CreateTexture2D(TextureDesc, SubResourceData, OutTexture2D);
		// VERIFYD3DRESULT(hResult);
		return SUCCEEDED(hResult);
	}

	D3D11Texture2D::D3D11Texture2D(D3D11DynamicRHI* InD3D11RHI)
		:D3D11RHI(InD3D11RHI)
	{
	}

	D3D11Texture2D::~D3D11Texture2D()
	{
	}

	bool D3D11Texture2D::CreateTexture2D(EPixelFormat Format, int32_t Flags, int32_t SizeX, int32_t SizeY, int32_t SizeZ, uint32_t NumMips, 
		void* InBuffer /*= nullptr*/, int32_t RowBytes /*= 0*/)
	{
		return CreateTexture2D(Format,Flags,SizeX,SizeY,SizeZ,false, NumMips, InBuffer,RowBytes);
	}

	bool D3D11Texture2D::CreateTexture2D(EPixelFormat Format, int32_t Flags, int32_t SizeX, int32_t SizeY, int32_t SizeZ, 
		bool bCubeTexture, uint32_t NumMips, void* InBuffer, size_t RowBytes)
	{
		Size.x = SizeX;
		Size.y = SizeY;
		this->NumMips = NumMips;
		this->Format = Format;

		const bool bSRGB = (Flags & TexCreate_SRGB) != 0;

		const DXGI_FORMAT PlatformResourceFormat = (DXGI_FORMAT)GPixelFormats[Format].PlatformFormat;

		uint32_t CPUAccessFlags = 0;
		D3D11_USAGE TextureUsage = D3D11_USAGE_DEFAULT;
		bool bCreateShaderResource = true;

		auto Device = D3D11RHI->GetDevice();

		uint32_t ActualMSAACount = 1;
		uint32_t ActualMSAAQuality = 0;
		if (Flags & TexCreate_MSAA)
		{
			ActualMSAAQuality = GetMaxMSAAQuality(Device, PlatformResourceFormat, ActualMSAACount);
			// 0xffffffff means not supported
			if (ActualMSAAQuality == 0xffffffff || (Flags & TexCreate_Shared) != 0)
			{
				// no MSAA
				ActualMSAACount = 1;
				ActualMSAAQuality = 0;
			}
		}

		bIsMultisampled = ActualMSAACount > 1;

		if (Flags & TexCreate_CPUReadback)
		{
			// Assert(!(Flags & TexCreate_RenderTargetable));
			// Assert(!(Flags & TexCreate_DepthStencilTargetable));
			// Assert(!(Flags & TexCreate_ShaderResource));
			// Assert(!(Flags & TexCreate_Tiled));

			CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			TextureUsage = D3D11_USAGE_STAGING;
			bCreateShaderResource = false;
		}

		if (Flags & TexCreate_CPUWritable)
		{
			// Assert(!(Flags & TexCreate_Tiled));
			CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			TextureUsage = D3D11_USAGE_STAGING;
			bCreateShaderResource = false;
		}

		// Describe the texture.
		D3D11_TEXTURE2D_DESC TextureDesc;
		ZeroMemory(&TextureDesc, sizeof(D3D11_TEXTURE2D_DESC));
		TextureDesc.Width = SizeX;
		TextureDesc.Height = SizeY;
		TextureDesc.MipLevels = NumMips;
		TextureDesc.ArraySize = SizeZ;
		TextureDesc.Format = PlatformResourceFormat;
		TextureDesc.SampleDesc.Count = ActualMSAACount;
		TextureDesc.SampleDesc.Quality = ActualMSAAQuality;
		TextureDesc.Usage = TextureUsage;
		TextureDesc.BindFlags = bCreateShaderResource ? D3D11_BIND_SHADER_RESOURCE : 0;
		TextureDesc.CPUAccessFlags = CPUAccessFlags;
		TextureDesc.MiscFlags = bCubeTexture ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

		if (Flags & TexCreate_DisableSRVCreation)
		{
			bCreateShaderResource = false;
		}

		if (Flags & TexCreate_Shared)
		{
			TextureDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
		}

		if (Flags & TexCreate_GenerateMipCapable)
		{
			// Set the flag that allows us to call GenerateMips on this texture later
			TextureDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
			TextureDesc.MipLevels = 0;
		}

		if (Flags & TexCreate_Tiled)
		{
			TextureDesc.MiscFlags |= D3D11_RESOURCE_MISC_TILED;
		}

		// Set up the texture bind flags.
		bool bCreateRTV = false;
		bool bCreateDSV = false;

		if (Flags & TexCreate_RenderTargetable || bIsMultisampled || Flags & TexCreate_GenerateMipCapable)
		{
			// Assert(!(Flags & TexCreate_DepthStencilTargetable));
			// Assert(!(Flags & TexCreate_ResolveTargetable));
			TextureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
			bCreateRTV = true;
		}
		else if (Flags & TexCreate_DepthStencilTargetable)
		{
			// Assert(!(Flags & TexCreate_RenderTargetable));
			// Assert(!(Flags & TexCreate_ResolveTargetable));
			TextureDesc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;

			bCreateDSV = true;
		}
		else if (Flags & TexCreate_ResolveTargetable)
		{
			// Assert(!(Flags & TexCreate_RenderTargetable));
			// Assert(!(Flags & TexCreate_DepthStencilTargetable));
			if (Format == PF_DepthStencil || Format == PF_ShadowDepth || Format == PF_D24)
			{
				TextureDesc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
				bCreateDSV = true;
			}
			else
			{
				TextureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
				bCreateRTV = true;
			}
		}
		 
		if (Flags & TexCreate_UAV)
		{
			TextureDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		}

		if (bCreateDSV && !(Flags & TexCreate_ShaderResource))
		{
			TextureDesc.BindFlags &= ~D3D11_BIND_SHADER_RESOURCE;
			bCreateShaderResource = false;
		}

		auto DeviceContext = D3D11RHI->GetDeviceContext();
		HRESULT hr = S_OK;
		if (Flags & TexCreate_GenerateMipCapable)
		{
			if (!SafeCreateTexture2D(Device, Format, &TextureDesc, nullptr, Tex2D.GetAddressOf()))
			{
				return false;
			}

			if (InBuffer)
			{
				DeviceContext->UpdateSubresource(Tex2D.Get(), 0, nullptr, InBuffer, (uint32_t)RowBytes, 0);
			}
		}
		else
		{
			
			size_t SlicePitch = SizeY * RowBytes;
			if (RowBytes == 0 && InBuffer != 0)
			{
				// DirectX::ComputePitch(PlatformResourceFormat, SizeX, SizeY, RowBytes, SlicePitch);
			}

			D3D11_SUBRESOURCE_DATA SubRes{};
			SubRes.pSysMem = InBuffer;
			SubRes.SysMemPitch = (uint32_t)RowBytes;
			SubRes.SysMemSlicePitch = SizeY * (uint32_t)RowBytes;

			if (!SafeCreateTexture2D(Device, Format, &TextureDesc, InBuffer ? &SubRes : nullptr, Tex2D.GetAddressOf()))
			{
				return false;
			}
		}

		if (bCreateShaderResource)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
			memset(&SRVDesc, 0, sizeof(SRVDesc));
			SRVDesc.Format = GetPlatformTextureResourceFormat(PlatformResourceFormat, Flags);

			if (bCubeTexture)
			{
				SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
				SRVDesc.TextureCube.MostDetailedMip = 0;
				SRVDesc.TextureCube.MipLevels = NumMips;
			}
			else
			{
				if (bIsMultisampled)
				{
					SRVDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2DMS;
				}
				else
				{
					SRVDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2D;
					SRVDesc.Texture2D.MostDetailedMip = 0;
					SRVDesc.Texture2D.MipLevels = NumMips;
				}
			}


			hr = Device->CreateShaderResourceView(Tex2D.Get(), &SRVDesc, TexSRV.GetAddressOf());
			if (FAILED(hr))
			{
				return false;
			}

			if ((Flags & TexCreate_GenerateMipCapable) && TexSRV)
			{
				DeviceContext->GenerateMips(TexSRV.Get());
			}

		}

		if (bCreateRTV)
		{
			if (bCubeTexture)
			{
				for (uint32_t MipIndex = 0; MipIndex < NumMips; MipIndex++)
				{
					for (uint32_t SliceIndex = 0; SliceIndex < TextureDesc.ArraySize; SliceIndex++)
					{
						D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
						memset(&RTVDesc, 0, sizeof(RTVDesc));

						RTVDesc.Format = GetPlatformTextureResourceFormat(PlatformResourceFormat, Flags);

						if (bIsMultisampled)
						{
							RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
							RTVDesc.Texture2DMSArray.FirstArraySlice = SliceIndex;
							RTVDesc.Texture2DMSArray.ArraySize = 1;
						}
						else
						{
							RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
							RTVDesc.Texture2DArray.FirstArraySlice = SliceIndex;
							RTVDesc.Texture2DArray.ArraySize = 1;
							RTVDesc.Texture2DArray.MipSlice = MipIndex;
						}

						ComPtr<ID3D11RenderTargetView> TexRTV;
						hr = Device->CreateRenderTargetView(Tex2D.Get(), &RTVDesc, TexRTV.GetAddressOf());
						TexRTVS[MipIndex].emplace_back(TexRTV);
					}
				}
			}
			else
			{
				for (uint32_t MipIndex = 0; MipIndex < NumMips; MipIndex++)
				{
					D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
					memset(&RTVDesc, 0, sizeof(RTVDesc));
					RTVDesc.Format = GetPlatformTextureResourceFormat(PlatformResourceFormat, Flags);
					if (bIsMultisampled)
					{
						RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
					}
					else
					{
						RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
					}
					RTVDesc.Texture2D.MipSlice = MipIndex;
					ComPtr<ID3D11RenderTargetView> TexRTV;
					hr = Device->CreateRenderTargetView(Tex2D.Get(), &RTVDesc, TexRTV.GetAddressOf());
					if (FAILED(hr))
					{
						return false;
					}
					TexRTVS[MipIndex].emplace_back(TexRTV);
				}
			}
		}

		if (bCreateDSV)
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc;
			memset(&DSVDesc, 0, sizeof(DSVDesc));
			DSVDesc.Format = FindDepthResourceDXGIFormat(PlatformResourceFormat);
			if (bIsMultisampled)
			{
				DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
			}
			else
			{
				DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			}

			DSVDesc.Texture2D.MipSlice = 0;
			hr = Device->CreateDepthStencilView(Tex2D.Get(), &DSVDesc, TexDSV.GetAddressOf());
			if (FAILED(hr))
			{
				return false;
			}

		}

		return true;
	}

	bool D3D11Texture2D::CreateFromFile(const std::wstring& FileName)
	{
		core::log::error("CreateFromFile not implemented (missing dependencies)");
		return false;
	}

	bool D3D11Texture2D::CreateHDRFromFile(const std::wstring& FileName)
	{
		core::log::error("CreateHDRFromFile not implemented (missing dependencies)");
		return false;
	}

	bool D3D11Texture2D::IsMultisampled() const
	{
		return bIsMultisampled;
	}

	core::vec2i D3D11Texture2D::GetSize() const
	{
		return Size;
	}

	uint32_t D3D11Texture2D::GetNumMips() const
	{
		return NumMips;
	}

	EPixelFormat D3D11Texture2D::GetPixelFormat() const
	{
		return Format;
	}

	ID3D11Texture2D* D3D11Texture2D::GetNativeTex() const
	{
		return Tex2D.Get();
	}

	ID3D11RenderTargetView* D3D11Texture2D::GetRTV() const
	{
		auto it = TexRTVS.find(0);
		if (it != TexRTVS.end() && !it->second.empty())
		{
			return it->second[0].Get();
		}
		return nullptr;
	}

	std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>> D3D11Texture2D::GetRTVS() const
	{
		return TexRTVS;
	}

	std::map < uint32_t, std::vector< ComPtr <ID3D11RenderTargetView>>>& D3D11Texture2D::GetRTVS()
	{
		return TexRTVS;
	}

	ID3D11ShaderResourceView* D3D11Texture2D::GetSRV() const
	{
		return TexSRV.Get();
	}

	ID3D11DepthStencilView* D3D11Texture2D::GetDSV() const
	{
		return TexDSV.Get();
	}

	bool D3D11Texture2D::CreateFromExistingTexture(ID3D11Texture2D* existingTexture, EPixelFormat format)
	{
		if (!existingTexture) {
			return false;
		}

		// 保存现有纹理的引用
		Tex2D = existingTexture;

		// 获取纹理描述
		D3D11_TEXTURE2D_DESC desc;
		existingTexture->GetDesc(&desc);

		// 设置成员变量
		Size.x = (int32_t)desc.Width;
		Size.y = (int32_t)desc.Height;
		NumMips = desc.MipLevels;
		Format = format;
		bIsMultisampled = desc.SampleDesc.Count > 1;

		auto Device = D3D11RHI->GetDevice();
		if (!Device) {
			return false;
		}

		HRESULT hr;

		// 创建 SRV（如果纹理支持）
		if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
			D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
			memset(&SRVDesc, 0, sizeof(SRVDesc));
			SRVDesc.Format = desc.Format;

			if (bIsMultisampled) {
				SRVDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2DMS;
			} else {
				SRVDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2D;
				SRVDesc.Texture2D.MostDetailedMip = 0;
				SRVDesc.Texture2D.MipLevels = NumMips;
			}

			hr = Device->CreateShaderResourceView(existingTexture, &SRVDesc, TexSRV.GetAddressOf());
			if (FAILED(hr)) {
				return false;
			}
		}

		// 创建 RTV（如果纹理支持）
		if (desc.BindFlags & D3D11_BIND_RENDER_TARGET) {
			for (uint32_t MipIndex = 0; MipIndex < NumMips; MipIndex++) {
				D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
				memset(&RTVDesc, 0, sizeof(RTVDesc));
				RTVDesc.Format = desc.Format;
				if (bIsMultisampled) {
					RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
				} else {
					RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
					RTVDesc.Texture2D.MipSlice = MipIndex;
				}

				ComPtr<ID3D11RenderTargetView> TexRTV;
				hr = Device->CreateRenderTargetView(existingTexture, &RTVDesc, TexRTV.GetAddressOf());
				if (FAILED(hr)) {
					return false;
				}
				TexRTVS[MipIndex].emplace_back(TexRTV);
			}
		}

		return true;
	}

}
