#include <fmt/format.h>
#include "src/lexer.h"
#include "src/parser.h"

int main()  // NOLINT
{
    auto tokens = cls::lex::Lexer(R"script(

global_var: int = 0;
def func(arg: int): int
{
    local_var: int = 1;
}

)script").lex();
    try
    {
        auto ast = cls::parse::Parser(std::move(tokens)).parse();
        return 0;
    }
    catch (const std::exception& e)
    {
        fmt::print("Exception: {}", e.what());
    }
    return 0;
}
