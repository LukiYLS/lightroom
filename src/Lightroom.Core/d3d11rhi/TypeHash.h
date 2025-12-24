#pragma once
#include <cstdint>
#include <functional>

namespace Templates
{
    template <typename T>
    inline void HashCombine(uint32_t& Seed, const T& Value)
    {
        std::hash<T> Hasher;
        Seed ^= Hasher(Value) + 0x9e3779b9 + (Seed << 6) + (Seed >> 2);
    }

    inline uint32_t HashCombine(uint32_t A, uint32_t B)
    {
        return A ^ (B + 0x9e3779b9 + (A << 6) + (A >> 2));
    }

    template <typename T>
    inline uint32_t GetTypeHash(const T& Value)
    {
        return (uint32_t)std::hash<T>{}(Value);
    }
}













