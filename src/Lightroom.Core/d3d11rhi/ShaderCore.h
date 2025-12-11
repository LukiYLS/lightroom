#pragma once
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include "Common.h"

namespace RenderCore
{
	enum class EShaderParameterType : uint8_t
	{
		LooseData = 0,
		UniformBuffer = 1,
		Sampler = 2,
		SRV = 3,
		UAV = 4,
		Num = 5
	};

	struct FShaderParameterMap
	{
		std::map<std::string, uint32_t> ParameterMap;
		
		void AddParameter(const std::string& Name, uint32_t BufferIndex, uint32_t BaseIndex, uint32_t Size)
		{
			// Simple mapping for now
			ParameterMap[Name] = BaseIndex;
		}
		
		void AddParameterAllocation(const std::string& Name, uint32_t BufferIndex, uint32_t BaseIndex, uint32_t Size, EShaderParameterType Type)
		{
			// Simple mapping for now
			ParameterMap[Name] = BaseIndex;
			(void)BufferIndex;
			(void)Size;
			(void)Type;
		}
		
		bool ContainsParameter(const std::string& Name) const
		{
			return ParameterMap.find(Name) != ParameterMap.end();
		}
	};

	struct FShaderCompilerOutput
	{
		FShaderParameterMap ParameterMap;
		std::vector<uint8_t> ShaderCode;
		std::vector<std::string> Errors;
		bool bSucceeded = false;
	};
}
