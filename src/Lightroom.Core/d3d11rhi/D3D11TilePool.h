#pragma once
#include "RHITilePool.h"
#include "Common.h"
#include <d3d11_2.h>
#include <vector>
#include <memory>

// Ensure D3D11_2 types are available
#ifndef D3D11_TILED_RESOURCE_COORDINATE
// These should be in d3d11_2.h, but if not available, we need to include the right header
#endif

namespace RenderCore
{
	class D3D11DynamicRHI;

	struct D3D11MipInfo
	{
		D3D11_TILED_RESOURCE_COORDINATE startCoordinate;
		D3D11_TILE_REGION_SIZE regionSize;
	};

	class D3D11TilePool : public RHITilePool
	{
	public:
		D3D11TilePool(D3D11DynamicRHI* D3D11RHI);
		virtual ~D3D11TilePool();

		bool CreatePool(std::shared_ptr< RHITexture2D> TexRHI) override;
		bool UpdateTileMappings(std::shared_ptr< RHITexture2D> TexRHI) override;
		void UpdateTiles(std::shared_ptr< RHITexture2D> TexRHI, std::shared_ptr<uint8_t> Data) override;
	private:
		D3D11DynamicRHI* D3D11RHI{ nullptr };
		ComPtr<ID3D11Buffer> TiledPool;

		std::vector<D3D11_SUBRESOURCE_TILING> Tilings;
		D3D11_TILE_SHAPE TileShape;
		uint32_t NumTiles = 0;
		D3D11_PACKED_MIP_DESC PackedMipInfo;
		uint32_t SubresourceCount = 0;
		D3D11MipInfo MipInfo;
	};
}
