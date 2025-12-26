#pragma once

#include "d3d11rhi/DynamicRHI.h"
#include "d3d11rhi/D3D11RHI.h" 
#include "d3d11rhi/D3D11RenderTarget.h"
#include "D3D9Interop.h"

#include <memory>
#include <unordered_map>
#include <cstdint>
#include <wrl/client.h>
#include <d3d11.h>
#include <d3d9.h>

namespace LightroomCore {

	class RenderTargetManager {
	public:
		struct RenderTargetInfo {
			uint32_t Width = 0;
			uint32_t Height = 0;

			// --- RHI 层资源 (供渲染引擎使用) ---
			std::shared_ptr<RenderCore::RHIRenderTarget> RHIRenderTarget;
			std::shared_ptr<RenderCore::RHITexture2D> RHITexture;

			// --- Native DX11 资源 (供互操作使用) ---
			Microsoft::WRL::ComPtr<ID3D11Texture2D> D3D11SharedTexture;
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> D3D11SharedRTV;
			HANDLE D3D11SharedHandle = nullptr;

			// --- Native DX9 资源 (供 WPF 使用) ---
			Microsoft::WRL::ComPtr<IDirect3DSurface9> D3D9SharedSurface;

			RenderTargetInfo() = default;
			~RenderTargetInfo() = default;
		};

		RenderTargetManager(std::shared_ptr<RenderCore::DynamicRHI> rhi, D3D9Interop* d3d9Interop);
		~RenderTargetManager();

		// 创建渲染目标，返回不透明句柄 (即 RenderTargetInfo 指针)
		void* CreateRenderTarget(uint32_t width, uint32_t height);

		// 销毁渲染目标
		void DestroyRenderTarget(void* handle);

		// 调整渲染目标大小
		bool ResizeRenderTarget(void* handle, uint32_t width, uint32_t height);

		// 获取渲染目标信息结构体
		RenderTargetInfo* GetRenderTargetInfo(void* handle);

		// 获取 D3D9 Surface 指针 (用于 WPF D3DImage.SetBackBuffer)
		void* GetD3D9SharedHandle(void* handle);

		// 获取 RHI 纹理 (用于传递给渲染管线)
		std::shared_ptr<RenderCore::RHITexture2D> GetRHITexture(void* handle);

		// 获取 DX11 Shared Handle (主要用于调试或扩展)
		HANDLE GetD3D11SharedHandle(void* handle);

	private:
		std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
		D3D9Interop* m_D3D9Interop;

		// 使用 unique_ptr 管理 RenderTargetInfo 内存
		std::unordered_map<void*, std::unique_ptr<RenderTargetInfo>> m_RenderTargets;

		// 私有辅助函数：初始化或重建具体资源
		bool InitResources(RenderTargetInfo* info, uint32_t width, uint32_t height);
	};

} // namespace LightroomCore