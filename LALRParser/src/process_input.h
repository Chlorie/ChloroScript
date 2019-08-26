#pragma once

#include <string>
#include <vector>
#include <variant>

namespace cls::lalr
{
    struct Terminal final
    {
        const std::string* term = nullptr;
        std::string variable_name;
    };

    struct EnumTerm final { std::string name; };

    struct NonTerminal final
    {
        const std::string* term = nullptr;
        bool use_unique_ptr = false;
        std::string variable_name;
    };

    using Term = std::variant<Terminal, EnumTerm, NonTerminal>;

    struct Rule final
    {
        const std::string* non_terminal = nullptr;
        std::string type_name;
        std::vector<Term> terms;
    };

    struct Grammar final
    {
        std::vector<std::string> token_types;
        std::vector<std::string> non_terminals;
        std::vector<Rule> rules;
    };

    Grammar process_input(const std::string& text);
}
