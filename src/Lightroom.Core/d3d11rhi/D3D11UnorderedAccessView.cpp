 #include "D3D11UnorderedAccessView.h"
#include "RHI.h"
#include "D3D11RHI.h"
#include "D3D11ReourceTraits.h"
#include "D3D11Texture2D.h"
#include "D3D11RHIPrivate.h"

namespace RenderCore
{
	D3D11UnorderedAccessView::D3D11UnorderedAccessView(D3D11DynamicRHI* InD3D11RHI)
		: D3D11RHI(InD3D11RHI)
	{
	}

	D3D11UnorderedAccessView::~D3D11UnorderedAccessView()
	{
	}

	bool D3D11UnorderedAccessView::CreateFromTexture(std::shared_ptr<RHITexture2D> Tex2D, uint32_t MipLevel)
	{
		this->Tex2D = Tex2D;
		auto D3D11Tex = RHIResourceCast(Tex2D.get());
		D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc{};
		UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		UAVDesc.Texture2D.MipSlice = MipLevel;
		UAVDesc.Format = FindShaderResourceDXGIFormat((DXGI_FORMAT)GPixelFormats[Tex2D->GetPixelFormat()].PlatformFormat, false);
		auto Device = D3D11RHI->GetDevice();
		// VERIFYD3DRESULT
		Device->CreateUnorderedAccessView(D3D11Tex->GetNativeTex(), &UAVDesc, UnorderedAccessView.GetAddressOf());
		return UnorderedAccessView.Get() != nullptr;
	}

	std::shared_ptr<RenderCore::RHITexture2D> D3D11UnorderedAccessView::GetTexture2D() const
	{
		return Tex2D;
	}

	ComPtr<ID3D11UnorderedAccessView> D3D11UnorderedAccessView::GetNativeUAV() const
	{
		return UnorderedAccessView;
	}

}
