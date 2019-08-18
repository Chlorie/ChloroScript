#include <fmt/format.h>
#include "src/lexer.h"
#include "src/utils/overload.h"

int main()
{
    using namespace cls::lex;
    Lexer lexer(R"(
def entry(): int
{
    i: int = 1;
    { i = 0; }
    return i;
})");
    for (auto&& token : lexer.lex())
        std::visit(cls::utils::Overload
            {
                [](const Symbol value) { fmt::print("Symbol: {}\n", size_t(value)); },
                [](const Keyword value) { fmt::print("Keyword: {}\n", size_t(value)); },
                [](const Identifier& value) { fmt::print("Identifier: {}\n", value.name); },
                [](const Integer value) { fmt::print("Integer: {}\n", value.value); },
                [](const LexError value) { fmt::print("LexError: {}\n", size_t(value)); },
            }, token.content);
    return 0;
}
