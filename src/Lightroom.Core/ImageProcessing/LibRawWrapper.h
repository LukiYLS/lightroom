#pragma once

#include "RAWImageInfo.h"
#include <string>
#include <vector>
#include <memory>

#ifdef LIBRAW_AVAILABLE
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// 先包含 winsock2.h（如果还没有包含）
// LibRaw 会检查并避免重复包含
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <libraw/libraw.h>
#endif

namespace LightroomCore {

// LibRaw 包装类，提供简化的接口
class LibRawWrapper {
public:
    LibRawWrapper();
    ~LibRawWrapper();

    // 打开 RAW 文件
    bool OpenFile(const std::wstring& filePath);

    // 提取元数据
    bool ExtractMetadata(RAWImageInfo& outInfo);

    // 解包 RAW 数据（不进行去马赛克）
    // 返回 16-bit Bayer pattern 数据
    bool UnpackRAW(std::vector<uint16_t>& outData, uint32_t& outWidth, uint32_t& outHeight);

    // 处理 RAW 数据（解包 + 去马赛克 + 转换为 RGB）
    // 返回 8-bit RGB 数据
    bool ProcessRAW(std::vector<uint8_t>& outData, uint32_t& outWidth, uint32_t& outHeight);

    // 获取错误信息
    const char* GetError() const;

    // 检查是否已打开文件
    bool IsOpen() const;

private:
    void* m_Processor;  // LibRaw* (当 LIBRAW_AVAILABLE 时)
    void* m_Data;       // libraw_data_t* (当 LIBRAW_AVAILABLE 时)
    std::string m_LastError;
    bool m_IsOpen;
};

} // namespace LightroomCore

