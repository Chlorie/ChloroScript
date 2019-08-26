#pragma once

#include <variant>

namespace cls::utils
{
    template <typename... Fs>
    struct Overload : Fs...
    {
        template <typename... Ts>
        explicit Overload(Ts&& ... vs) :Fs(std::forward<Ts>(vs))... {}
        using Fs::operator()...;
    };
    template <typename... Fs> Overload(Fs...)->Overload<Fs...>;
}
