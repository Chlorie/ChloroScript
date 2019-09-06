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
}
