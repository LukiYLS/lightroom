#pragma once
#include "RHIIndexBuffer.h"
#include "Common.h"
#include <memory>

namespace RenderCore 
{
	class D3D11DynamicRHI;

	class D3D11IndexBuffer : public RHIIndexBuffer
	{
	public:
		D3D11IndexBuffer(D3D11DynamicRHI* D3D11RHI);
		virtual ~D3D11IndexBuffer();

		virtual bool CreateIndexBuffer(const uint16_t* InData, int32_t InUsage, int32_t TriangleNumber) override;
		virtual bool CreateIndexBuffer(const uint32_t* InData, int32_t InUsage, int32_t TriangleNumber) override;
		virtual int32_t GetIndexFormat() const override;
		virtual int32_t GetIndexCount() const override;
	public:
		ID3D11Buffer* GetNativeBuffer() const;
	private:
		bool CreateBuffer(const void* InData, int32_t InUsage);

	private:
		ComPtr<ID3D11Buffer> Buffer;
		D3D11DynamicRHI* D3D11RHI = nullptr;

		uint32_t IndexCount = 0;
		uint32_t Size = 0;
		int32_t IndexFormat = DXGI_FORMAT_R16_UINT;
	};
}
