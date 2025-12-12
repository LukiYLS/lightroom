#include "D3D11Texture1D.h"
#include "D3D11RHIPrivate.h"
#include "D3D11RHI.h"
#include "Common.h"

namespace RenderCore
{
	struct D3D11Texture1DP
	{
		D3D11DynamicRHI* D3D11RHI = nullptr;
		ComPtr< ID3D11ShaderResourceView> TexSRV;
		ComPtr< ID3D11Texture1D> Tex1D;
		int32_t SizeX = 0;
	};

	D3D11Texture1D::D3D11Texture1D(D3D11DynamicRHI* D3D11RHI)
		:Impl(std::make_shared<D3D11Texture1DP>())
	{
		Impl->D3D11RHI = D3D11RHI;
	}

	bool SafeCreateTexture1D(ID3D11Device* Direct3DDevice, int32_t UEFormat, const D3D11_TEXTURE1D_DESC* TextureDesc, const D3D11_SUBRESOURCE_DATA* SubResourceData, ID3D11Texture1D** OutTexture2D)
	{
#if GUARDED_TEXTURE_CREATES
		bool bDriverCrash = true;
		__try
		{
#endif // #if GUARDED_TEXTURE_CREATES
			HRESULT hr = Direct3DDevice->CreateTexture1D(TextureDesc, SubResourceData, OutTexture2D);
			if (FAILED(hr))
			{
				Assert(false);
				return false;
			}
#if GUARDED_TEXTURE_CREATES
			bDriverCrash = false;
			
		}
		__finally
		{
			if (bDriverCrash)
			{
				core::log::LOG(core::log::log_err,
					TEXT("Driver crashed while creating texture: %ux %s(0x%08x) with %u mips, PF_ %d"),
					TextureDesc->Width,
					GetD3D11TextureFormatString(TextureDesc->Format),
					(uint32_t)TextureDesc->Format,
					TextureDesc->MipLevels,
					UEFormat
				);
				return false;
			}
		}
#endif // #if GUARDED_TEXTURE_CREATES
		return true;
	}


	bool D3D11Texture1D::CreateWithData(EPixelFormat Format, int32_t Flags, int32_t SizeX, void* InBuffer, int32_t RowBytes)
	{
		const DXGI_FORMAT PlatformResourceFormat = GetPlatformTextureResourceFormat((DXGI_FORMAT)GPixelFormats[Format].PlatformFormat, Flags);

		CD3D11_TEXTURE1D_DESC texDesc(PlatformResourceFormat, SizeX, 1, 1);
		D3D11_SUBRESOURCE_DATA initData{ InBuffer, uint32_t(RowBytes)};

		auto Device = Impl->D3D11RHI->GetDevice();

		if (!SafeCreateTexture1D(Device, Format, &texDesc, &initData, Impl->Tex1D.GetAddressOf()))
		{
			return false;
		}

		if (Flags & TexCreate_ShaderResource)
		{
			return SUCCEEDED(Device->CreateShaderResourceView(Impl->Tex1D.Get(), nullptr, Impl->TexSRV.GetAddressOf()));
		}

		return true;
	}

	int32_t D3D11Texture1D::GetSize() const
	{
		return Impl->SizeX;
	}

	ID3D11Texture1D* D3D11Texture1D::GetNativeTex() const
	{
		return Impl->Tex1D.Get();
	}

	ID3D11ShaderResourceView* D3D11Texture1D::GetSRV() const
	{
		return Impl->TexSRV.Get();
	}

}