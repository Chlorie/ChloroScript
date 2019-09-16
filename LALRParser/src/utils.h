#pragma once

#include <fmt/format.h>
#include <algorithm>

namespace cls::utils
{
    template <typename... Ts>
    [[noreturn]] void error(Ts&& ... args)
    {
        throw std::runtime_error(fmt::format(std::forward<Ts>(args)...));
    }

    template <typename T, typename V>
    bool contains(const T& container, const V& value)
    {
        const auto iter = std::find(container.begin(), container.end(), value);
        return iter != container.end();
    }

    // To circumvent the std::vector<bool> issue
    class Bool final
    {
    private:
        bool value_ = false;
    public:
        Bool() = default;
        Bool(const bool value) :value_(value) {}
        Bool& operator=(const bool value) { value_ = value; return *this; }
        operator bool& () { return value_; }
        operator bool() const { return value_; }
    };

    template <typename R>
    auto enumerate(R&& range)
    {
        using Begin = decltype(std::forward<R>(range).begin());
        struct EnumerateType final
        {
            struct Iterator final
            {
                size_t index = 0;
                Begin iter;
                auto operator*() const
                {
                    using Ref = decltype(*iter);
                    return std::pair<size_t, Ref>(index, *iter);
                }
                Iterator& operator++() { index++; ++iter; return *this; }
                bool operator!=(const Iterator& other) const { return iter != other.iter; }
            };
            R range;
            Iterator begin() { return { 0, range.begin() }; }
            Iterator end() { return { 0, range.end() }; }
        };
        return EnumerateType{ std::forward<R>(range) };
    }

    template <typename R>
    auto reverse(R&& range)
    {
        using Begin = decltype(std::forward<R>(range).rbegin());
        struct EnumerateType final
        {
            struct Iterator final
            {
                Begin iter;
                decltype(auto) operator*() const { return *iter; }
                Iterator& operator++() { ++iter; return *this; }
                bool operator!=(const Iterator& other) const { return iter != other.iter; }
            };
            R range;
            Iterator begin() { return { range.rbegin() }; }
            Iterator end() { return { range.rend() }; }
        };
        return EnumerateType{ std::forward<R>(range) };
    }
}
