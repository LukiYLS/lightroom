#include "D3D11Shader.h"
#include "RHIDefinitions.h"
#include "D3D11RHI.h"
#include "Common.h"
#include "D3DShaderUtil.h"
#include "D3D11ReourceTraits.h"
#include <cstring>

namespace RenderCore
{
	D3D11VertexShader::D3D11VertexShader(D3D11DynamicRHI* InD3D11RHI)
		: D3D11RHI(InD3D11RHI)
	{
	}

	D3D11VertexShader::~D3D11VertexShader()
	{
	}

	bool D3D11VertexShader::CreateShader(const std::wstring& FileName, const std::string& VSMain, const RHIVertexDeclare& VertexDeclare, const std::vector<RHIShaderMacro>& MacroDefines /*= {}*/)
	{
		std::vector< D3D_SHADER_MACRO> D3DShaderMacros;
		ShaderUtil::RHIShaderMarcoToD3DShaderMacro(MacroDefines, D3DShaderMacros);

		ComPtr<ID3DBlob> ShaderCodeBlob = ShaderUtil::CreateShader(FileName, VSMain, "vs_5_0", D3DShaderMacros.data());
		if (!ShaderCodeBlob)
		{
			return false;
		}
		this->ShaderCode = ShaderCodeBlob;

		auto Device = D3D11RHI->GetDevice();
		// VERIFYD3DRESULT
		Device->CreateVertexShader(ShaderCode->GetBufferPointer(), ShaderCode->GetBufferSize(), nullptr, VertexShader.GetAddressOf());

		const auto& DescVec = VertexDeclare.GetDeclareDesc();
		if (DescVec.empty())
		{
			return VertexShader.Get() != nullptr;
		}

		// Convert VertexElementDesc to D3D11_INPUT_ELEMENT_DESC
		std::vector<D3D11_INPUT_ELEMENT_DESC> D3D11Descs;
		std::vector<std::vector<char>> SemanticNameStorage; // Store semantic names to keep them alive
		D3D11Descs.reserve(DescVec.size());
		SemanticNameStorage.reserve(DescVec.size());
		
		for (const auto& Desc : DescVec)
		{
			// Store semantic name in a local buffer
			std::vector<char> semanticName(64, '\0');
			size_t len = strlen(Desc.SemanticName);
			size_t maxLen = semanticName.size() - 1;
			size_t copyLen = (len < maxLen) ? len : maxLen;
			memcpy(semanticName.data(), Desc.SemanticName, copyLen);
			semanticName[copyLen] = '\0';
			SemanticNameStorage.push_back(std::move(semanticName));
			
			D3D11_INPUT_ELEMENT_DESC D3D11Desc = {};
			D3D11Desc.SemanticName = SemanticNameStorage.back().data();
			D3D11Desc.SemanticIndex = Desc.SemanticIndex;
			D3D11Desc.Format = Desc.Format;
			D3D11Desc.InputSlot = Desc.InputSlot;
			D3D11Desc.AlignedByteOffset = Desc.AlignedByteOffset;
			D3D11Desc.InputSlotClass = static_cast<D3D11_INPUT_CLASSIFICATION>(Desc.InputSlotClass);
			D3D11Desc.InstanceDataStepRate = Desc.InstanceDataStepRate;
			D3D11Descs.push_back(D3D11Desc);
		}

		return CreateLayout(D3D11Descs);
	}

	ID3D11VertexShader* D3D11VertexShader::GetNativeVertexShader() const
	{
		return VertexShader.Get();
	}

	ID3D11InputLayout* D3D11VertexShader::GetNativeInputLayout() const
	{
		return InputLayout.Get();
	}

	bool D3D11VertexShader::CreateLayout(const std::vector<D3D11_INPUT_ELEMENT_DESC>& ElementDescs)
	{
		if (ElementDescs.empty() )
		{
			return false;
		}

		if (!ShaderCode)
		{
			Assert(false);
			return false;
		}

		auto Device = D3D11RHI->GetDevice();

		std::vector<D3D11_INPUT_ELEMENT_DESC> D3D11ElementDescs;
		D3D11ElementDescs.resize(ElementDescs.size());

		int32_t Index = 0;
		for (const auto& Item: ElementDescs)
		{
			D3D11_INPUT_ELEMENT_DESC& ElementDesc = D3D11ElementDescs[Index++];
			ElementDesc.SemanticName = Item.SemanticName;
			ElementDesc.SemanticIndex = Item.SemanticIndex;
			ElementDesc.Format = static_cast<DXGI_FORMAT>(Item.Format);
			ElementDesc.InputSlot = Item.InputSlot;
			ElementDesc.AlignedByteOffset = Item.AlignedByteOffset;
			ElementDesc.InputSlotClass = static_cast<D3D11_INPUT_CLASSIFICATION>(Item.InputSlotClass);
			ElementDesc.InstanceDataStepRate = Item.InstanceDataStepRate;
		}

		HRESULT hr = Device->CreateInputLayout(D3D11ElementDescs.data(), (uint32_t)D3D11ElementDescs.size(), 
			ShaderCode->GetBufferPointer(), ShaderCode->GetBufferSize(), InputLayout.GetAddressOf());
		if (FAILED(hr))
		{
			core::log::error("CreateInputLayout Failed -------");
			for (const auto& Item : ElementDescs)
			{
				// core::err() << "SemanticName:" << Item.SemanticName << " SemanticIndex:" << Item.SemanticIndex << " Format:" << GetD3D11TextureFormatString(static_cast<DXGI_FORMAT>(Item.Format));
				// Simple error logging
				core::log::error(Item.SemanticName);
			}
			Assert(false);
			return false;
		}
		return true;
	}

	D3D11PixelShader::D3D11PixelShader(D3D11DynamicRHI* InD3D11RHI)
		: D3D11RHI(InD3D11RHI)
	{
	}

	D3D11PixelShader::~D3D11PixelShader()
	{
	}

	bool D3D11PixelShader::CreateShader(const std::wstring& FileName, const std::string& PSMain, const std::vector<RHIShaderMacro>& MacroDefines )
	{
		std::vector< D3D_SHADER_MACRO> D3DShaderMacros;
		ShaderUtil::RHIShaderMarcoToD3DShaderMacro(MacroDefines, D3DShaderMacros);
		auto ShaderCodeBlob = ShaderUtil::CreateShader(FileName, PSMain, "ps_5_0", D3DShaderMacros.data());
		if (!ShaderCodeBlob)
		{
			return false;
		}
		auto Device = D3D11RHI->GetDevice();
		HRESULT hr =  Device->CreatePixelShader(ShaderCodeBlob->GetBufferPointer(), ShaderCodeBlob->GetBufferSize(), nullptr, PixelShader.GetAddressOf());
		return SUCCEEDED(hr);
	}

	ID3D11PixelShader* D3D11PixelShader::GetNativePixelShader() const
	{
		return PixelShader.Get();
	}

	D3D11ComputeShader::D3D11ComputeShader(D3D11DynamicRHI* InD3D11RHI)
		: D3D11RHI(InD3D11RHI)
	{
	}

	D3D11ComputeShader::~D3D11ComputeShader()
	{
	}

	bool D3D11ComputeShader::CreateShader(const std::wstring& FileName, const std::string& CSMain, const std::vector<RHIShaderMacro>& MacroDefines)
	{
		std::vector< D3D_SHADER_MACRO> D3DShaderMacros;
		ShaderUtil::RHIShaderMarcoToD3DShaderMacro(MacroDefines, D3DShaderMacros);
		auto ShaderCodeBlob = ShaderUtil::CreateShader(FileName, CSMain, "cs_5_0", D3DShaderMacros.data());
		if (!ShaderCodeBlob)
		{
			return false;
		}
		auto Device = D3D11RHI->GetDevice();
		HRESULT hr = Device->CreateComputeShader(ShaderCodeBlob->GetBufferPointer(), ShaderCodeBlob->GetBufferSize(), nullptr, ComputeShader.GetAddressOf());
		return SUCCEEDED(hr);
	}

	ID3D11ComputeShader* D3D11ComputeShader::GetNativeComputeShader() const
	{
		return ComputeShader.Get();
	}

}
