#pragma once

#include <array>

namespace cls::utils
{
    // This is a set of characters, not a character set in encoding
    class StaticCharSet final
    {
    private:
        std::array<bool, 256> data_{};
    public:
        constexpr StaticCharSet() = default;
        constexpr StaticCharSet(const char* chars)
        {
            while (*chars)
            {
                data_[size_t(*chars)] = true;
                chars++;
            }
        }
        constexpr bool contains(const char ch) const { return data_[size_t(ch)]; }
        constexpr StaticCharSet& operator|=(const StaticCharSet& other)
        {
            for (size_t i = 0; i < 256; i++)
                data_[i] |= other.data_[i];
            return *this;
        }
        constexpr StaticCharSet& operator&=(const StaticCharSet& other)
        {
            for (size_t i = 0; i < 256; i++)
                data_[i] &= other.data_[i];
            return *this;
        }
        constexpr StaticCharSet operator|(StaticCharSet other) const { return other |= *this; }
        constexpr StaticCharSet operator&(StaticCharSet other) const { return other &= *this; }
        constexpr StaticCharSet operator!() const
        {
            StaticCharSet result(*this);
            for (bool& v : result.data_) v = !v;
            return result;
        }
    };
}
