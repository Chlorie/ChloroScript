#pragma once

namespace cls::utils
{
    template <typename... Fs> struct Overload : Fs... { using Fs::operator()...; };
    template <typename... Fs> Overload(Fs...)->Overload<Fs...>;
}
