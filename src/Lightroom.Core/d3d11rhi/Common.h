#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <random>
#include <chrono>
#include <Windows.h>
#include <wrl/client.h>
#include <iostream>
#include <d3d11.h>
#include <d3d11_2.h>
#include <dxgi.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>
#include <unordered_map>
#include <map>
#include <cassert>

using Microsoft::WRL::ComPtr;

// Stub for math namespace
namespace math {
    inline void RandInit(uint32_t seed) { srand(seed); }
    inline void SRandInit(uint32_t seed) { srand(seed); }
    template<typename T>
    inline T Min(T a, T b) { return (std::min)(a, b); }
    template<typename T>
    inline T Max(T a, T b) { return (std::max)(a, b); }
    template<typename T>
    inline T Clamp(T X, T Min, T Max)
    {
        return X < Min ? Min : X < Max ? X : Max;
    }
    inline int32_t FloorToInt(float F)
    {
        return (int32_t)floorf(F);
    }
}

// Stub for core namespace
namespace core {
    inline uint32_t Cycles() {
        return (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    }
    
    // Simple string conversion (Windows specific wide to multi-byte)
    inline std::string ucs2_u8(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    struct Crc {
        static uint32_t MemCrc32(const void* Data, int32_t Length) {
            // Simple hash for now
            uint32_t hash = 0;
            const char* p = (const char*)Data;
            for (int i = 0; i < Length; ++i) hash = hash * 31 + p[i];
            return hash;
        }
        static uint32_t HashState(const void* Data, int32_t Length, uint32_t Crc) {
             return Crc ^ MemCrc32(Data, Length);
        }
        static uint32_t HashState(const uint8_t* Data, size_t Length, uint32_t Crc) {
             return Crc ^ MemCrc32(Data, (int32_t)Length);
        }
    };
    
    inline size_t HashString(const std::string& str) {
        return std::hash<std::string>{}(str);
    }
    
    struct FLinearColor {
        float R, G, B, A;
        FLinearColor(float r, float g, float b, float a) : R(r), G(g), B(b), A(a) {}
        FLinearColor() : R(0), G(0), B(0), A(0) {}
    };

    struct FColor {
        uint8_t R, G, B, A;
        FColor(uint32_t InColor) {
            R = (InColor >> 24) & 0xFF;
            G = (InColor >> 16) & 0xFF;
            B = (InColor >> 8) & 0xFF;
            A = InColor & 0xFF;
        }
        operator FLinearColor() const {
            return FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
        }
    };

    struct vec2 {
        float x, y;
        vec2(float _x = 0, float _y = 0) : x(_x), y(_y) {}
    };

    struct vec2i {
        int32_t x, y;
        vec2i(int32_t _x = 0, int32_t _y = 0) : x(_x), y(_y) {}
    };

    struct vec2u {
        uint32_t x, y;
        vec2u(uint32_t _x = 0, uint32_t _y = 0) : x(_x), y(_y) {}
    };

    struct vec4 {
        float x, y, z, w;
        vec4(float _x = 0, float _y = 0, float _z = 0, float _w = 0) : x(_x), y(_y), z(_z), w(_w) {}
    };

    struct vec4u {
        uint32_t x, y, z, w;
        vec4u(uint32_t _x = 0, uint32_t _y = 0, uint32_t _z = 0, uint32_t _w = 0) : x(_x), y(_y), z(_z), w(_w) {}
        uint32_t left() const { return x; }
        uint32_t top() const { return y; }
        uint32_t right() const { return z; }
        uint32_t bottom() const { return w; }
    };

    // Logger stub
    namespace log {
        enum log_e { log_inf, log_err };
        inline void info(const char* msg) { std::cout << "[INFO] " << msg << std::endl; }
        inline void error(const char* msg) { std::cerr << "[ERROR] " << msg << std::endl; }
        inline std::ostream& err() { return std::cerr; }
        inline std::ostream& inf() { return std::cout; }
        inline void LOG(log_e level, const wchar_t* format, ...) {
            // Simple stub for now - va_list not implemented
            (void)level;
            std::wcout << L"[LOG] " << format << std::endl;
        }
        inline std::string format(const std::string& prefix, const std::string& value) {
            return prefix + value;
        }
    }
    
    // CommandLine stub
    class CommandLine {
    public:
        static CommandLine& Get() {
            static CommandLine instance;
            return instance;
        }
        bool GetName(const char* name) {
            // Stub - always return false
            return false;
        }
    };
    
    inline std::ostream& inf() { return std::cout; }
    
    // WIDE2 macro for string literals
    #define WIDE2(str) L##str

    template<typename T>
    inline T Align(T Val, T Alignment)
    {
        return (Val + Alignment - 1) & ~(Alignment - 1);
    }
}

namespace win32 {
	inline void DoAssert(bool success, const wchar_t* file_name, int line) {
        if (!success) {
            std::wcerr << L"Assertion failed at " << file_name << L":" << line << std::endl;
            // __debugbreak();
        }
    }
    
    // Stub for platform_memory.h
    struct Win32MemoryConstants {
        static Win32MemoryConstants GetConstants() { return {}; }
        int64_t TotalPhysicalGB = 16; // Default to 16GB
    };
}

#ifndef WFILE
#define WFILE _CRT_WIDE(__FILE__)
#endif

#ifdef _DEBUG
#define Assert(s) win32::DoAssert((s), WFILE, __LINE__)
#else
#define Assert(s)
#endif

template <typename T, uint32_t N>
char(&UE4ArrayCountHelper(const T(&)[N]))[N + 1];

// Number of elements in an array.
#define UE_ARRAY_COUNT( array ) (sizeof(UE4ArrayCountHelper(array)) - 1)
#define VERIFYD3DRESULT(x) {HRESULT hr = x; if (FAILED(hr)) { Assert(false);}}

#define C_P(Name) D3D11DynamicRHIPrivate *d = d_ptr;
