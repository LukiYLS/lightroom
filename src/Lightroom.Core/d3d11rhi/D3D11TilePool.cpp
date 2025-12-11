#include "D3D11TilePool.h"
#include "D3D11RHI.h"
#include "D3D11Texture2D.h"
#include "D3D11ReourceTraits.h"
#include <algorithm>
#include <cmath>

namespace RenderCore
{
	static const UINT TileSizeInBytes = 0x10000;

	D3D11TilePool::D3D11TilePool(D3D11DynamicRHI* InD3D11RHI)
		: D3D11RHI(InD3D11RHI)
	{
	}

	D3D11TilePool::~D3D11TilePool()
	{
	}

	std::vector<UINT8> GenerateTextureData(UINT TextureWidth,UINT TextureHeight,  UINT firstMip, UINT mipCount,uint8_t R,uint8_t G,uint8_t B)
	{
		const UINT TexturePixelSizeInBytes = 4;
		// Determine the size of the data required by the mips(s).
		UINT dataSize = (TextureWidth >> firstMip) * (TextureHeight >> firstMip) * TexturePixelSizeInBytes;
		if (mipCount > 1)
		{
			// If generating more than 1 mip, double the size of the texture allocation
			// (you will never need more than this).
			dataSize *= 2;
		}
		std::vector<UINT8> data(dataSize);
		UINT8* pData = &data[0];

		UINT index = 0;
		for (UINT n = 0; n < mipCount; n++)
		{
			const UINT currentMip = firstMip + n;
			const UINT width = TextureWidth >> currentMip;
			const UINT height = TextureHeight >> currentMip;
			const UINT rowPitch = width * TexturePixelSizeInBytes;
		const UINT cellPitch = (std::max)(rowPitch >> 3, TexturePixelSizeInBytes);    // The width of a cell in the checkboard texture.
		const UINT cellHeight = (std::max)(height >> 3, (UINT)1);                        // The height of a cell in the checkerboard texture.
			const UINT textureSize = rowPitch * height;

			for (UINT m = 0; m < textureSize; m += TexturePixelSizeInBytes)
			{
				UINT x = m % rowPitch;
				UINT y = m / rowPitch;
				UINT i = x / cellPitch;
				UINT j = y / cellHeight;

				pData[index++] = R;    // R
				pData[index++] = G;    // G
				pData[index++] = B;    // B
				pData[index++] = 0xff;    // A
			}
		}
		return data;
	}

	bool D3D11TilePool::CreatePool(std::shared_ptr< RHITexture2D> TexRHI)
	{
		D3D11Texture2D* Tex2d = RHIResourceCast(TexRHI.get());
		if (!Tex2d->GetNativeTex())
		{
			return false;
		}
		D3D11_TEXTURE2D_DESC TexDesc{};
		Tex2d->GetNativeTex()->GetDesc(&TexDesc);

		if (!(TexDesc.MiscFlags & D3D11_RESOURCE_MISC_TILED))
		{
			return false;
		}

		SubresourceCount = TexDesc.MipLevels;
		Tilings.resize(SubresourceCount);
		D3D11RHI->GetDevice2()->GetResourceTiling(Tex2d->GetNativeTex(), &NumTiles, &PackedMipInfo, &TileShape, &SubresourceCount, 0, &Tilings[0]);

		MipInfo.startCoordinate = { 0,0,0,0 };
		MipInfo.regionSize.bUseBox = FALSE;
		MipInfo.regionSize.Width = Tilings[0].WidthInTiles;
		MipInfo.regionSize.Height = Tilings[0].HeightInTiles;
		MipInfo.regionSize.Depth = Tilings[0].DepthInTiles;
		MipInfo.regionSize.NumTiles = Tilings[0].WidthInTiles * Tilings[0].HeightInTiles * Tilings[0].DepthInTiles;

		// Create the tile pool.
		D3D11_BUFFER_DESC tilePoolDesc;
		ZeroMemory(&tilePoolDesc, sizeof(tilePoolDesc));
		tilePoolDesc.ByteWidth = TileSizeInBytes * MipInfo.regionSize.NumTiles;
		tilePoolDesc.Usage = D3D11_USAGE_DEFAULT;
		tilePoolDesc.MiscFlags = D3D11_RESOURCE_MISC_TILE_POOL;

		auto Device = D3D11RHI->GetDevice2();
		HRESULT hr = Device->CreateBuffer(&tilePoolDesc, nullptr, TiledPool.GetAddressOf());
		// TiledPool.is_valid() -> TiledPool.Get() != nullptr or TiledPool (operator bool)
		return SUCCEEDED(hr) && TiledPool;
	}

	bool D3D11TilePool::UpdateTileMappings(std::shared_ptr< RHITexture2D> TexRHI)
	{
		D3D11Texture2D* Tex2d = RHIResourceCast(TexRHI.get());
		if (!Tex2d->GetNativeTex() || !TiledPool)
		{
			return false;
		}
		D3D11_TEXTURE2D_DESC TexDesc{};
		Tex2d->GetNativeTex()->GetDesc(&TexDesc);

		if (!(TexDesc.MiscFlags & D3D11_RESOURCE_MISC_TILED))
		{
			return false;
		}
		
		uint32_t RangeFlags = 0;
		uint32_t StartOffset = 0;

		auto Context2 = D3D11RHI->GetDeviceContext2();
		//UINT tileNum = MipInfo.regionSize.NumTiles;

		HRESULT hr = Context2->UpdateTileMappings(
			Tex2d->GetNativeTex(),
			1,
			&MipInfo.startCoordinate,
			&MipInfo.regionSize,
			TiledPool.Get(),
			1,
			&RangeFlags,
			&StartOffset,
			nullptr,
			0);

		return SUCCEEDED(hr);
	}

	std::vector<uint8_t> GetTileData(uint8_t* StartCopy, uint32_t ImageWidth, uint32_t TileWidth,uint32_t TileHeight)
	{
		std::vector<uint8_t> Data;
		Data.resize(128 * 128 * 4);
		memset(Data.data(), 0, Data.size());
		uint8_t* DestBuf = Data.data();
		for (uint32_t index = 0; index < TileHeight; ++index)
		{
			memcpy(DestBuf, StartCopy, TileWidth * 4);
			DestBuf += 128 * 4;
			StartCopy += ImageWidth * 4;
	
		}
		return Data;
	}


	void D3D11TilePool::UpdateTiles(std::shared_ptr< RHITexture2D> TexRHI, std::shared_ptr<uint8_t> Data)
	{
		D3D11Texture2D* Tex2d = RHIResourceCast(TexRHI.get());
		if (!Tex2d->GetNativeTex() || !TiledPool)
		{
			return;
		}
		D3D11_TEXTURE2D_DESC TexDesc{};
		Tex2d->GetNativeTex()->GetDesc(&TexDesc);

		if (!(TexDesc.MiscFlags & D3D11_RESOURCE_MISC_TILED))
		{
			return;
		}

		auto Context2 = D3D11RHI->GetDeviceContext2();

		D3D11_TILED_RESOURCE_COORDINATE startCoordinate;
		ZeroMemory(&startCoordinate, sizeof(startCoordinate));
		D3D11_TILE_REGION_SIZE regionSize;
		ZeroMemory(&regionSize, sizeof(regionSize));
		regionSize.NumTiles = MipInfo.regionSize.NumTiles;


		std::vector<uint8_t> TileBuf;
		TileBuf.resize(TileSizeInBytes* regionSize.NumTiles);
		memset(TileBuf.data(), 0, TileBuf.size());
		uint8_t* DestTileBuf = TileBuf.data();

		int32_t wCount = (int32_t)std::floor((double)TexDesc.Width / TileShape.WidthInTexels);
		int32_t hCount = (int32_t)std::floor((double)TexDesc.Height / TileShape.HeightInTexels);


		uint8_t* ReadBuf = Data.get();

		for (int32_t hIndex = 0; hIndex < hCount; ++hIndex)
		{
			uint8_t* LineReadBuf = ReadBuf;
			uint8_t* LineWrite = DestTileBuf;
			for (int32_t wIndex = 0; wIndex < wCount; ++wIndex)
			{
				int32_t width = 128;
				if ((wIndex + 1) * 128 > (int32_t)TexDesc.Width)
					width = (wIndex + 1) * 128 - TexDesc.Width;
				int32_t height = 128;
				if ((hIndex + 1) * 128 > (int32_t)TexDesc.Height)
					height = (hIndex + 1) * 128 - (int32_t)TexDesc.Height;
				auto Buf = GetTileData(LineReadBuf, TexDesc.Width, width, height);
				memcpy(LineWrite, Buf.data(), Buf.size());
				LineWrite += Buf.size();
				LineReadBuf += width * 4;
			}
			ReadBuf += TexDesc.Width * 4 * TileShape.HeightInTexels;
			DestTileBuf += TexDesc.Width * 4 * TileShape.HeightInTexels;
		}

		//auto Buf1 = GenerateTextureData(128, 128, 0, 1, 255, 0, 0);
		//memcpy(DestTileBuf, Buf1.data(), Buf1.size());
		//DestTileBuf += Buf1.size();
		//Buf1 = GenerateTextureData(128, 128, 0, 1, 0, 255, 0);
		//memcpy(DestTileBuf, Buf1.data(), Buf1.size());
		//DestTileBuf += Buf1.size();
		//Buf1 = GenerateTextureData(128, 128, 0, 1, 0, 0, 255);
		//memcpy(DestTileBuf, Buf1.data(), Buf1.size());

		Context2->UpdateTiles(
			Tex2d->GetNativeTex(),
			&startCoordinate,
			&regionSize,
			TileBuf.data(),
			0);
	}

}
