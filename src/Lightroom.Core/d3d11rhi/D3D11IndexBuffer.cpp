#include "D3D11IndexBuffer.h"
#include "RHIDefinitions.h"
#include "D3D11RHI.h"
#include "Common.h"
#include <dxgiformat.h>

namespace RenderCore
{
	D3D11IndexBuffer::D3D11IndexBuffer(D3D11DynamicRHI* InD3D11RHI)
		: D3D11RHI(InD3D11RHI)
	{
	}

	D3D11IndexBuffer::~D3D11IndexBuffer()
	{
	}

	bool D3D11IndexBuffer::CreateIndexBuffer(const uint16_t* InData, int32_t InUsage, int32_t TriangleNumber)
	{
		IndexFormat = DXGI_FORMAT_R16_UINT;
		IndexCount = TriangleNumber * 3;
		Size = sizeof(uint16_t) * IndexCount;
		return CreateBuffer(InData,InUsage);
	}

	bool D3D11IndexBuffer::CreateIndexBuffer(const uint32_t* InData, int32_t InUsage, int32_t TriangleNumber)
	{
		IndexFormat = DXGI_FORMAT_R32_UINT;
		IndexCount = TriangleNumber * 3;
		Size = sizeof(uint32_t) * IndexCount;
		return CreateBuffer(InData, InUsage);
	}

	ID3D11Buffer* D3D11IndexBuffer::GetNativeBuffer() const
	{
		return Buffer.Get();
	}

	int32_t D3D11IndexBuffer::GetIndexFormat() const
	{
		return IndexFormat;
	}

	int32_t D3D11IndexBuffer::GetIndexCount() const
	{
		return IndexCount;
	}

	bool D3D11IndexBuffer::CreateBuffer(const void* InData, int32_t InUsage)
	{
		// Describe the index buffer.
		D3D11_BUFFER_DESC Desc;
		ZeroMemory(&Desc, sizeof(D3D11_BUFFER_DESC));
		Desc.ByteWidth = Size;
		Desc.Usage = (InUsage & BUF_AnyDynamic) ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		Desc.CPUAccessFlags = (InUsage & BUF_AnyDynamic) ? D3D11_CPU_ACCESS_WRITE : 0;
		Desc.MiscFlags = 0;

		if (InUsage & BUF_UnorderedAccess)
		{
			Desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		}

		if (InUsage & BUF_DrawIndirect)
		{
			Desc.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
		}

		if (InUsage & BUF_ShaderResource)
		{
			Desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
		}

		if (InUsage & BUF_Shared)
		{
			Desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
		}

		D3D11_SUBRESOURCE_DATA IndexInitData{};
		IndexInitData.pSysMem = InData;
		HRESULT hr = D3D11RHI->GetDevice()->CreateBuffer(&Desc, InData ? &IndexInitData : nullptr, Buffer.GetAddressOf());
		return SUCCEEDED(hr);
	}

}
