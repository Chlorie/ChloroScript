#pragma once

#include <fmt/format.h>

namespace cls::utils
{
    template <typename... Ts>
    [[noreturn]] void error(Ts&& ... args)
    {
        throw std::runtime_error(fmt::format(std::forward<Ts>(args)...));
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
