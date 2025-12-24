#pragma once
#include <cstdint>

template<class TEnum>
class TEnumAsByte
{
public:
    typedef TEnum EnumType;

    TEnumAsByte() = default;
    TEnumAsByte(const TEnumAsByte&) = default;
    TEnumAsByte& operator=(const TEnumAsByte&) = default;

    TEnumAsByte(TEnum InValue)
        : Value((uint8_t)InValue)
    {
    }

    TEnumAsByte(int32_t InValue)
        : Value((uint8_t)InValue)
    {
    }

    TEnumAsByte(uint8_t InValue)
        : Value(InValue)
    {
    }

    operator TEnum() const
    {
        return (TEnum)Value;
    }

    TEnum GetValue() const
    {
        return (TEnum)Value;
    }

private:
    uint8_t Value;
};

template<class TEnum>
inline uint32_t GetTypeHash(const TEnumAsByte<TEnum>& Value)
{
    return (uint32_t)Value.GetValue();
}













