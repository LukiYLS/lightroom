#include "D3D11UniformBuffer.h"
#include "D3D11RHI.h"

namespace RenderCore
{
	D3D11UniformBuffer::D3D11UniformBuffer(D3D11DynamicRHI* InD3D11RHI)
		: D3D11RHI(InD3D11RHI)
	{
	}

	D3D11UniformBuffer::~D3D11UniformBuffer()
	{
	}

	bool D3D11UniformBuffer::CreateUniformBuffer(const void* Contents, uint32_t ConstantBufferSize)
	{
		Assert(core::Align(ConstantBufferSize, 16u) == ConstantBufferSize);

		D3D11_BUFFER_DESC Desc{};
		Desc.ByteWidth = ConstantBufferSize;
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.MiscFlags = 0;
		Desc.StructureByteStride = 0;

		D3D11_SUBRESOURCE_DATA ImmutableData{};
		ImmutableData.pSysMem = Contents;
		ImmutableData.SysMemPitch = ImmutableData.SysMemSlicePitch = 0;

		this->ConstantBufferSize = ConstantBufferSize;

		D3D11RHI->GetDevice()->CreateBuffer(&Desc, Contents ? &ImmutableData : nullptr, UniformBuffer.GetAddressOf());
		return UniformBuffer.Get() != nullptr;
	}

	uint32_t D3D11UniformBuffer::GetConstantBufferSize() const
	{
		return ConstantBufferSize;
	}

	ID3D11Buffer* D3D11UniformBuffer::GetNativeUniformBuffer() const
	{
		return UniformBuffer.Get();
	}

}
