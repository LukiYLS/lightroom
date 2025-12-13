#pragma once

#include "RAWImageInfo.h"
#include <string>
#include <vector>
#include <memory>

// LibRaw 前向声明
// 如果 LibRaw 已集成，取消注释以下行
// #define LIBRAW_AVAILABLE

#ifdef LIBRAW_AVAILABLE
// 防止 Winsock 冲突 - 必须在包含任何 Windows 头文件之前
// LibRaw 的头文件会在 Windows 下包含 winsock2.h
// 我们需要确保在包含 LibRaw 之前，winsock2.h 已经被正确包含

// 方案：在项目级别定义 WIN32_LEAN_AND_MEAN 和 NOMINMAX
// 这样所有文件在包含 windows.h 之前都会先定义这些宏
// LibRaw 的头文件会检查这些宏并正确包含 winsock2.h

// 注意：LibRaw 的头文件 libraw_datastream.h 和 internal/defines.h 
// 会在 Windows 下包含 winsock2.h，但它们会检查 LIBRAW_NO_WINSOCK2
// 如果定义了 LIBRAW_NO_WINSOCK2，则不会包含 winsock2.h

// 我们选择不定义 LIBRAW_NO_WINSOCK2，让 LibRaw 包含 winsock2.h
// 但我们需要确保在包含 LibRaw 之前，windows.h 还没有被包含
// 或者如果已经被包含，需要确保 winsock2.h 在 windows.h 之前

// 最佳方案：在包含 LibRaw 之前，先包含 winsock2.h
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

// 然后包含 windows.h（如果需要，但 WIN32_LEAN_AND_MEAN 会阻止它包含 winsock.h）
#include <windows.h>

// 现在可以安全地包含 LibRaw 头文件
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

