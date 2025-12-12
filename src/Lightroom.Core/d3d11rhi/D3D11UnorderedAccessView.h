#pragma once
#include "RHIUnorderedAccessView.h"
#include "Common.h"
#include <d3d11.h>
#include <memory>

namespace RenderCore
{
	class D3D11DynamicRHI;

	class D3D11UnorderedAccessView : public RHIUnorderedAccessView
	{
	public:
		D3D11UnorderedAccessView(D3D11DynamicRHI* D3D11RHI);
		~D3D11UnorderedAccessView();

		virtual bool CreateFromTexture(std::shared_ptr<RHITexture2D> Tex2D, uint32_t MipLevel) override;
		virtual std::shared_ptr<RHITexture2D> GetTexture2D() const override;
		ComPtr<ID3D11UnorderedAccessView> GetNativeUAV() const;

	private:
		D3D11DynamicRHI* D3D11RHI;
		ComPtr<ID3D11UnorderedAccessView> UnorderedAccessView;
		std::shared_ptr<RHITexture2D> Tex2D;
	};
}
