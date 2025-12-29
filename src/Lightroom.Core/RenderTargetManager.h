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
		// 缓冲区资源结构
		struct BufferResources {
			// --- RHI 层资源 (供渲染引擎使用) ---
			std::shared_ptr<RenderCore::RHIRenderTarget> RHIRenderTarget;
			std::shared_ptr<RenderCore::RHITexture2D> RHITexture;

			// --- Native DX11 资源 (供互操作使用) ---
			Microsoft::WRL::ComPtr<ID3D11Texture2D> D3D11SharedTexture;
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> D3D11SharedRTV;
			HANDLE D3D11SharedHandle = nullptr;

			// --- Native DX9 资源 (供 WPF 使用) ---
			Microsoft::WRL::ComPtr<IDirect3DSurface9> D3D9SharedSurface;

			uint32_t Width = 0;
			uint32_t Height = 0;
		};

		struct RenderTargetInfo {
			uint32_t Width = 0;
			uint32_t Height = 0;

			// 双缓冲策略：
			// Front Buffer: 固定给WPF使用，永远不变（除非resize）
			// Back Buffer: 内部渲染缓冲区，可以轮换（0或1）
			BufferResources FrontBuffer;  // WPF使用的固定缓冲区
			BufferResources BackBuffers[2];  // 内部渲染缓冲区（双缓冲）
			int32_t CurrentBackBuffer = 0;  // 当前使用的Back Buffer索引（0或1）

			// 向后兼容：提供当前活动缓冲区的访问（指向Front Buffer）
			std::shared_ptr<RenderCore::RHIRenderTarget> RHIRenderTarget;
			std::shared_ptr<RenderCore::RHITexture2D> RHITexture;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> D3D11SharedTexture;
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> D3D11SharedRTV;
			HANDLE D3D11SharedHandle = nullptr;
			Microsoft::WRL::ComPtr<IDirect3DSurface9> D3D9SharedSurface;

			// 更新活动缓冲区的引用（指向Front Buffer）
			void UpdateActiveReferences() {
				RHIRenderTarget = FrontBuffer.RHIRenderTarget;
				RHITexture = FrontBuffer.RHITexture;
				D3D11SharedTexture = FrontBuffer.D3D11SharedTexture;
				D3D11SharedRTV = FrontBuffer.D3D11SharedRTV;
				D3D11SharedHandle = FrontBuffer.D3D11SharedHandle;
				D3D9SharedSurface = FrontBuffer.D3D9SharedSurface;
			}

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
		// 注意：这个指针永远指向Front Buffer，不会改变（除非resize）
		void* GetD3D9SharedHandle(void* handle);

		// 获取 RHI 纹理 (用于传递给渲染管线)
		// 注意：返回的是Back Buffer的纹理，用于渲染
		std::shared_ptr<RenderCore::RHITexture2D> GetRHITexture(void* handle);

		// 获取 DX11 Shared Handle (主要用于调试或扩展)
		HANDLE GetD3D11SharedHandle(void* handle);

		// 获取下一个Back Buffer用于渲染（双缓冲轮换）
		// 返回Back Buffer的纹理，如果失败返回nullptr
		std::shared_ptr<RenderCore::RHITexture2D> AcquireNextRenderBuffer(void* handle);

		// 将Back Buffer的内容复制到Front Buffer，并等待GPU完成
		// 返回是否成功
		bool PresentBackBuffer(void* handle);

	private:
		std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
		D3D9Interop* m_D3D9Interop;

		// 使用 unique_ptr 管理 RenderTargetInfo 内存
		std::unordered_map<void*, std::unique_ptr<RenderTargetInfo>> m_RenderTargets;

		// 私有辅助函数：初始化或重建具体资源
		bool InitResources(RenderTargetInfo* info, uint32_t width, uint32_t height);
		
		// 初始化单个缓冲区资源
		bool InitBufferResources(BufferResources* buffer, uint32_t width, uint32_t height);
		
		// 从旧缓冲区复制内容到新缓冲区（用于resize时保持画面）
		bool CopyBufferContent(BufferResources* srcBuffer, BufferResources* dstBuffer);
	};

} // namespace LightroomCore