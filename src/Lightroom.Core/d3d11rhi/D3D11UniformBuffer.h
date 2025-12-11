#pragma once
#include "RHIUniformBuffer.h"
#include "Common.h"
#include <d3d11.h>
#include <memory>

namespace RenderCore
{
	class D3D11DynamicRHI;

	class D3D11UniformBuffer : public RHIUniformBuffer
	{
	public:
		D3D11UniformBuffer(D3D11DynamicRHI* D3D11RHI);
		virtual ~D3D11UniformBuffer();

		virtual bool CreateUniformBuffer(const void* Contents, uint32_t ConstantBufferSize) override;
		virtual uint32_t GetConstantBufferSize() const override;
		ID3D11Buffer* GetNativeUniformBuffer() const;

	private:
		D3D11DynamicRHI* D3D11RHI{ nullptr };
		ComPtr<ID3D11Buffer> UniformBuffer;
		uint32_t ConstantBufferSize{};
	};
}
