#pragma once
#include "RHIVertexBuffer.h"
#include "Common.h"
#include <d3d11.h>
#include <memory>

namespace RenderCore
{
	class D3D11DynamicRHI;

	class D3D11VertexBuffer : public RHIVertexBuffer
	{
	public:
		D3D11VertexBuffer(D3D11DynamicRHI* D3D11RHI);
		virtual ~D3D11VertexBuffer();

		virtual bool CreateVertexBuffer(const void* InData, EBufferUsageFlags InUsage, int32_t StrideByteWidth, int32_t Count) override;
		virtual void UpdateVertexBUffer(const void* InData, int32_t nVertex, int32_t sizePerVertex) override;
		virtual int32_t GetStride() const override;
		virtual int32_t GetCount() const override;

		ID3D11Buffer* GetNativeBuffer() const;

	private:
		int32_t Stride = 0;
		int32_t Count = 0;
		ComPtr<ID3D11Buffer> Buffer;
		D3D11DynamicRHI* D3D11RHI = nullptr;
	};
}
