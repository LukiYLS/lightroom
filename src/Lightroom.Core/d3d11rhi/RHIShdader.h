#pragma once
#include "Common.h"
#include "RHIDefinitions.h"
#include "EnumAsByte.h"
#include "TypeHash.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <map>

namespace RenderCore
{
	enum EShaderFrequency : uint8_t;

	struct RHIShaderMacro
	{
		std::string Name;
		std::string Definition;
		RHIShaderMacro(const std::string& InName, const std::string& InDefinition)
			: Name(InName)
			, Definition(InDefinition)
		{}
	};

	struct RHIVertexElement
	{
		uint8_t StreamIndex;
		uint8_t Offset;
		TEnumAsByte<EVertexType> Type;
		uint8_t AttributeIndex;
		uint16_t Stride;
		uint16_t InstanceStepRate;

		RHIVertexElement() {}
		RHIVertexElement(uint8_t InStreamIndex, uint8_t InOffset, EVertexType InType, uint8_t InAttributeIndex, uint16_t InStride, uint16_t InInstanceStepRate = 0)
			: StreamIndex(InStreamIndex)
			, Offset(InOffset)
			, Type(InType)
			, AttributeIndex(InAttributeIndex)
			, Stride(InStride)
			, InstanceStepRate(InInstanceStepRate)
		{}
		
		friend uint32_t GetTypeHash(const RHIVertexElement& Element)
		{
			uint32_t Hash = Element.StreamIndex;
			Templates::HashCombine(Hash, Element.Offset);
			Templates::HashCombine(Hash, (uint8_t)Element.Type.GetValue());
			Templates::HashCombine(Hash, Element.AttributeIndex);
			Templates::HashCombine(Hash, Element.Stride);
			Templates::HashCombine(Hash, Element.InstanceStepRate);
			return Hash;
		}
	};

	struct VertexElementDesc
	{
		char SemanticName[64];
		uint32_t SemanticIndex;
		DXGI_FORMAT Format;
		uint32_t InputSlot;
		uint32_t AlignedByteOffset;
		uint32_t InputSlotClass;
		uint32_t InstanceDataStepRate;
	};

	struct VertexDeclareInput
	{
		EVertexElementType InElementType;
		uint32_t InAttributeIndex;
		bool bUseInstanceIndex;
	};

	struct RHIVertexDeclare
	{
	public:
		RHIVertexDeclare();
		RHIVertexDeclare(const std::vector<RHIVertexElement>& InElements)
			: Elements(InElements)
		{
		}

		std::vector<uint8_t> GetDeclareDescBytes() const
		{
			std::vector<uint8_t> Desc;
			Desc.resize(Elements.size() * sizeof(RHIVertexElement));
			memcpy(Desc.data(), Elements.data(), Desc.size());
			return Desc;
		}

		const std::vector<VertexElementDesc>& GetDeclareDesc() const;
		void CreateDeclare(const std::vector<VertexDeclareInput>& Inputs);
		void AppendDeclareInput(const VertexDeclareInput& DeclareInput);
		uint32_t GetHash() const;

	public:
		std::vector<RHIVertexElement> Elements;
		std::vector<VertexElementDesc> Decs;
	};

	class RHIShader
	{
	public:
		RHIShader() = default;
		virtual ~RHIShader() {}
		EShaderFrequency GetFrequency() const { return Frequency; }

	protected:
		EShaderFrequency Frequency;
	};

	class RHIVertexShader : public RHIShader
	{
	public:
		RHIVertexShader() { Frequency = SF_Vertex; }
		virtual ~RHIVertexShader() {}
		virtual bool CreateShader(const std::wstring& FileName, const std::string& VSMain, const RHIVertexDeclare& VertexDeclare, const std::vector<RHIShaderMacro>& MacroDefines) = 0;
	};

	class RHIPixelShader : public RHIShader
	{
	public:
		RHIPixelShader() { Frequency = SF_Pixel; }
		virtual ~RHIPixelShader() {}
		virtual bool CreateShader(const std::wstring& FileName, const std::string& PSMain, const std::vector<RHIShaderMacro>& MacroDefines) = 0;
	};

	class RHIComputeShader : public RHIShader
	{
	public:
		RHIComputeShader() { Frequency = SF_Compute; }
		virtual ~RHIComputeShader() {}
		virtual bool CreateShader(const std::wstring& FileName, const std::string& CSMain, const std::vector<RHIShaderMacro>& MacroDefines) = 0;
	};

	struct RHIShaderCache
	{
		std::unordered_map<size_t, std::shared_ptr<RHIVertexShader>> VertexShaderCache;
		std::unordered_map<size_t, std::shared_ptr<RHIPixelShader>> PixelShaderCache;
		std::unordered_map<size_t, std::shared_ptr<RHIComputeShader>> ComputeShaderCache;
	};
}
