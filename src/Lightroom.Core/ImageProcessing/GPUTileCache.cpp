#include "GPUTileCache.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include <algorithm>
#include <Windows.h>
#include <sstream>
#include <iomanip>

namespace LightroomCore {

GPUTileCache::GPUTileCache()
    : m_D3D11RHI(nullptr)
    , m_TotalSlots(0)
    , m_UsedSlotCount(0)
    , m_HitCount(0)
    , m_MissCount(0)
    , m_TimeCounter(0) {
}

GPUTileCache::~GPUTileCache() {
    Shutdown();
}

bool GPUTileCache::Initialize(std::shared_ptr<RenderCore::DynamicRHI> rhi, uint32_t poolSizeMB) {
    if (!rhi) {
        return false;
    }
    
    m_RHI = rhi;
    m_D3D11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(rhi.get());
    if (!m_D3D11RHI) {
        return false;
    }
    
    poolSizeMB = std::max(512u, std::min(2048u, poolSizeMB));
    
    uint32_t requestedSlots = CalculateSlotCount(poolSizeMB);
    
    uint32_t actualSlots = requestedSlots;
    if (!CreateTextureArray(actualSlots)) {
        return false;
    }
    
    m_TotalSlots = actualSlots;
    
    m_Slots.resize(m_TotalSlots);
    for (uint32_t i = 0; i < m_TotalSlots; ++i) {
        m_Slots[i].PhysicalIndex = i;
        m_Slots[i].bInUse = false;
        m_Slots[i].LastAccessTime = 0;
    }
    
    m_UsedSlotCount = 0;
    m_HitCount = 0;
    m_MissCount = 0;
    m_TimeCounter = 0;
    
    std::ostringstream oss;
    oss << "[GPUTileCache] Initialized with " << m_TotalSlots 
        << " slots (" << poolSizeMB << " MB)\n";
    OutputDebugStringA(oss.str().c_str());
    
    return true;
}

void GPUTileCache::Shutdown() {
    m_SRV.Reset();
    m_TextureArray.Reset();
    m_LogicalToPhysical.clear();
    m_LRUList.clear();
    m_Slots.clear();
    m_UsedSlotCount = 0;
    m_D3D11RHI = nullptr;
    m_RHI.reset();
}

uint32_t GPUTileCache::CalculateSlotCount(uint32_t poolSizeMB) const {
    uint64_t poolSizeBytes = static_cast<uint64_t>(poolSizeMB) * 1024 * 1024;
    uint32_t slotCount = static_cast<uint32_t>(poolSizeBytes / TILE_SIZE_BYTES);
    
    return std::max(1u, slotCount);
}

bool GPUTileCache::CreateTextureArray(uint32_t& slotCount) {
    if (!m_D3D11RHI || slotCount == 0) {
        return false;
    }
    
    ID3D11Device* device = m_D3D11RHI->GetDevice();
    if (!device) {
        return false;
    }
    
    const uint32_t MAX_ARRAY_SIZE = 2048;
    if (slotCount > MAX_ARRAY_SIZE) {
        std::ostringstream oss;
        oss << "[GPUTileCache] Slot count (" << slotCount 
            << ") exceeds D3D11 limit (" << MAX_ARRAY_SIZE 
            << "), clamping to limit\n";
        OutputDebugStringA(oss.str().c_str());
        slotCount = MAX_ARRAY_SIZE;
    }
    
    D3D11_FEATURE_DATA_THREADING threadingCaps = {};
    HRESULT hrCheck = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threadingCaps, sizeof(threadingCaps));
    
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = TILE_SIZE;
    desc.Height = TILE_SIZE;
    desc.MipLevels = 1;
    desc.ArraySize = slotCount;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, m_TextureArray.GetAddressOf());
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[GPUTileCache] Failed to create Texture2DArray:\n";
        oss << "  HRESULT: 0x" << std::hex << hr << std::dec << "\n";
        oss << "  ArraySize: " << slotCount << "\n";
        oss << "  Width: " << desc.Width << ", Height: " << desc.Height << "\n";
        oss << "  Format: DXGI_FORMAT_B8G8R8A8_UNORM\n";
        
        if (hr == E_OUTOFMEMORY) {
            oss << "  Error: Out of memory. Try reducing pool size.\n";
        } else if (hr == E_INVALIDARG) {
            oss << "  Error: Invalid argument. ArraySize may be too large or unsupported.\n";
        } else if (hr == DXGI_ERROR_UNSUPPORTED) {
            oss << "  Error: Format or size not supported by device.\n";
        }
        OutputDebugStringA(oss.str().c_str());
        
        return false;
    }
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = slotCount;
    
    hr = device->CreateShaderResourceView(m_TextureArray.Get(), &srvDesc, m_SRV.GetAddressOf());
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[GPUTileCache] Failed to create SRV: 0x" 
            << std::hex << hr << std::dec << "\n";
        OutputDebugStringA(oss.str().c_str());
        m_TextureArray.Reset();
        return false;
    }
    
    return true;
}

int32_t GPUTileCache::RequestTile(const TileCoordinate& coord, const uint8_t* tileData) {
    if (!m_TextureArray) {
        return -1;
    }
    
    auto it = m_LogicalToPhysical.find(coord);
    if (it != m_LogicalToPhysical.end()) {
        uint32_t slotIndex = it->second;
        if (slotIndex < m_Slots.size() && m_Slots[slotIndex].bInUse) {
            m_HitCount++;
            UpdateAccessTime(slotIndex);
            return static_cast<int32_t>(slotIndex);
        }
    }
    
    m_MissCount++;
    
    int32_t slotIndex = FindFreeSlot();
    if (slotIndex < 0) {
        slotIndex = FindLRUSlot();
        if (slotIndex < 0) {
            return -1;
        }
        
        // 移除旧映射
        TileCoordinate oldCoord = m_Slots[slotIndex].LogicalCoord;
        m_LogicalToPhysical.erase(oldCoord);
    } else {
        m_UsedSlotCount++;
    }
    
    m_Slots[slotIndex].LogicalCoord = coord;
    m_Slots[slotIndex].bInUse = true;
    UpdateAccessTime(slotIndex);
    
    m_LogicalToPhysical[coord] = static_cast<uint32_t>(slotIndex);
    
    if (tileData) {
        UpdateTileData(static_cast<uint32_t>(slotIndex), tileData);
    }
    
    return slotIndex;
}

void GPUTileCache::ReleaseTile(const TileCoordinate& coord) {
    auto it = m_LogicalToPhysical.find(coord);
    if (it != m_LogicalToPhysical.end()) {
        uint32_t slotIndex = it->second;
        if (slotIndex < m_Slots.size()) {
            m_Slots[slotIndex].bInUse = false;
            m_LogicalToPhysical.erase(it);
            m_UsedSlotCount--;
            
            m_LRUList.remove(slotIndex);
        }
    }
}

int32_t GPUTileCache::GetTileSlotIndex(const TileCoordinate& coord) const {
    auto it = m_LogicalToPhysical.find(coord);
    if (it != m_LogicalToPhysical.end()) {
        uint32_t slotIndex = it->second;
        if (slotIndex < m_Slots.size() && m_Slots[slotIndex].bInUse) {
            return static_cast<int32_t>(slotIndex);
        }
    }
    
    return -1;
}

int32_t GPUTileCache::GetTilePhysicalIndex(const TileCoordinate& coord) const {
    // 与GetTileSlotIndex相同，提供更清晰的命名
    return GetTileSlotIndex(coord);
}

bool GPUTileCache::UpdateTileData(uint32_t slotIndex, const uint8_t* tileData) {
    if (!m_TextureArray || !tileData || slotIndex >= m_TotalSlots) {
        return false;
    }
    
    if (!m_D3D11RHI) {
        return false;
    }
    
    ID3D11DeviceContext* context = m_D3D11RHI->GetDeviceContext();
    if (!context) {
        return false;
    }
    
    D3D11_BOX box;
    box.left = 0;
    box.top = 0;
    box.front = 0;
    box.right = TILE_SIZE;
    box.bottom = TILE_SIZE;
    box.back = 1;
    
    UINT subresource = D3D11CalcSubresource(0, slotIndex, 1);
    context->UpdateSubresource(
        m_TextureArray.Get(),
        subresource,
        &box,
        tileData,
        TILE_SIZE * BYTES_PER_PIXEL,  // RowPitch
        0                              // DepthPitch
    );
    
    return true;
}

int32_t GPUTileCache::FindFreeSlot() {
    for (uint32_t i = 0; i < m_Slots.size(); ++i) {
        if (!m_Slots[i].bInUse) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

int32_t GPUTileCache::FindLRUSlot() {
    if (!m_LRUList.empty()) {
        uint32_t slotIndex = m_LRUList.front();
        m_LRUList.pop_front();
        return static_cast<int32_t>(slotIndex);
    }
    
    uint64_t oldestTime = UINT64_MAX;
    int32_t oldestSlot = -1;
    
    for (uint32_t i = 0; i < m_Slots.size(); ++i) {
        if (m_Slots[i].bInUse && m_Slots[i].LastAccessTime < oldestTime) {
            oldestTime = m_Slots[i].LastAccessTime;
            oldestSlot = static_cast<int32_t>(i);
        }
    }
    
    return oldestSlot;
}

void GPUTileCache::UpdateAccessTime(uint32_t slotIndex) {
    if (slotIndex >= m_Slots.size()) {
        return;
    }
    
    uint64_t currentTime = ++m_TimeCounter;
    m_Slots[slotIndex].LastAccessTime = currentTime;
    
m_LRUList.remove(slotIndex);
    m_LRUList.push_back(slotIndex);
}

void GPUTileCache::SetPoolSize(uint32_t poolSizeMB) {
    Shutdown();
    Initialize(m_RHI, poolSizeMB);
}

void GPUTileCache::BatchRequestTiles(const std::vector<TileCoordinate>& visibleTiles,
                                     std::vector<TileCoordinate>& outTilesToLoad,
                                     std::vector<std::pair<TileCoordinate, int32_t>>& outTilesInGPU) {
    outTilesToLoad.clear();
    outTilesInGPU.clear();
    
    if (!m_TextureArray) {
        // 如果TextureArray未初始化，所有tiles都需要加载
        outTilesToLoad = visibleTiles;
        return;
    }
    
    for (const auto& coord : visibleTiles) {
        // 检查是否已在GPU中
        auto it = m_LogicalToPhysical.find(coord);
        if (it != m_LogicalToPhysical.end()) {
            uint32_t slotIndex = it->second;
            if (slotIndex < m_Slots.size() && m_Slots[slotIndex].bInUse) {
                // 已在GPU中，更新访问时间
                m_HitCount++;
                UpdateAccessTime(slotIndex);
                outTilesInGPU.push_back({coord, static_cast<int32_t>(slotIndex)});
                continue;
            }
        }
        
        // 不在GPU中，需要加载
        m_MissCount++;
        outTilesToLoad.push_back(coord);
    }
}

uint32_t GPUTileCache::BatchUpdateTileData(const std::vector<std::pair<TileCoordinate, const uint8_t*>>& tilesToUpdate) {
    if (!m_D3D11RHI || tilesToUpdate.empty()) {
        return 0;
    }
    
    ID3D11DeviceContext* context = m_D3D11RHI->GetDeviceContext();
    if (!context) {
        return 0;
    }
    
    uint32_t successCount = 0;
    
    // 准备上传数据的box
    D3D11_BOX box;
    box.left = 0;
    box.top = 0;
    box.front = 0;
    box.right = TILE_SIZE;
    box.bottom = TILE_SIZE;
    box.back = 1;
    
    // 批量更新tiles
    for (const auto& pair : tilesToUpdate) {
        const TileCoordinate& coord = pair.first;
        const uint8_t* tileData = pair.second;
        
        if (!tileData) {
            continue;
        }
        
        // 查找物理插槽索引
        auto it = m_LogicalToPhysical.find(coord);
        if (it == m_LogicalToPhysical.end()) {
            // 如果还没有分配插槽，先分配一个
            int32_t slotIndex = RequestTile(coord, tileData);
            if (slotIndex >= 0) {
                successCount++;
            }
            continue;
        }
        
        uint32_t slotIndex = it->second;
        if (slotIndex >= m_Slots.size() || !m_Slots[slotIndex].bInUse) {
            continue;
        }
        
        // 使用UpdateSubresource上传数据到指定的数组切片
        UINT subresource = D3D11CalcSubresource(0, slotIndex, 1);
        context->UpdateSubresource(
            m_TextureArray.Get(),
            subresource,
            &box,
            tileData,
            TILE_SIZE * BYTES_PER_PIXEL,  // RowPitch
            0                              // DepthPitch
        );
        
        successCount++;
    }
    
    return successCount;
}

} // namespace LightroomCore
