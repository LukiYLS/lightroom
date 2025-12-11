#pragma once
#include "RHIShdader.h"
#include "Common.h"
#include <d3d11.h>
#include <memory>
#include <vector>
#include <string>

namespace RenderCore
{
	class D3D11DynamicRHI;

	class D3D11VertexShader : public RHIVertexShader
	{
	public:
		D3D11VertexShader(D3D11DynamicRHI* D3D11RHI);
		virtual ~D3D11VertexShader();

		bool CreateShader(const std::wstring& FileName, const std::string& VSMain, const RHIVertexDeclare& VertexDeclare, const std::vector<RHIShaderMacro>& MacroDefines) override;
		ID3D11VertexShader* GetNativeVertexShader() const;
		ID3D11InputLayout* GetNativeInputLayout() const;
	private:
		bool CreateLayout(const std::vector<D3D11_INPUT_ELEMENT_DESC>& ElementDescs);
	private:
		D3D11DynamicRHI* D3D11RHI = nullptr;
		ComPtr<ID3DBlob> ShaderCode;
		ComPtr<ID3D11VertexShader> VertexShader;
		ComPtr<ID3D11InputLayout> InputLayout;
	};

	class D3D11PixelShader : public RHIPixelShader
	{
	public:
		D3D11PixelShader(D3D11DynamicRHI* D3D11RHI);
		virtual ~D3D11PixelShader();

		bool CreateShader(const std::wstring& FileName, const std::string& PSMain, const std::vector<RHIShaderMacro>& MacroDefines) override;
		ID3D11PixelShader* GetNativePixelShader() const;
	private:
		D3D11DynamicRHI* D3D11RHI = nullptr;
		ComPtr<ID3D11PixelShader> PixelShader;
	};

	class D3D11ComputeShader : public RHIComputeShader
	{
	public:
		D3D11ComputeShader(D3D11DynamicRHI* D3D11RHI);
		virtual ~D3D11ComputeShader();

		bool CreateShader(const std::wstring& FileName, const std::string& CSMain, const std::vector<RHIShaderMacro>& MacroDefines) override;
		ID3D11ComputeShader* GetNativeComputeShader() const;
	private:
		D3D11DynamicRHI* D3D11RHI = nullptr;
		ComPtr<ID3D11ComputeShader> ComputeShader;
	};
}
