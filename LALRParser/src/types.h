#pragma once

#include <vector>
#include <string>
#include <variant>
#include <optional>

namespace cls::lalr
{
    constexpr size_t max_size = size_t(-1);

    struct TokenType final
    {
        std::string type_name;
        std::optional<std::string> enumerator;
    };

    struct Terminal final
    {
        size_t index = max_size;
        std::string variable_name;
    };

    struct NonTerminal final
    {
        size_t index = max_size;
        bool use_unique_ptr = false;
        std::string variable_name;
    };

    using Term = std::variant<Terminal, NonTerminal>;

    struct Rule final
    {
        std::string type_name;
        std::vector<Term> terms;
    };

    struct Grammar final
    {
        std::vector<TokenType> token_types;
        std::vector<std::string> non_terminals;
        std::vector<std::vector<Rule>> rules;
    };

    enum class ActionType : uint8_t { shift, reduce, accept, error };

    struct Action final
    {
        ActionType type = ActionType::error;
        size_t index = 0;
    };

    struct TableRow final
    {
        static constexpr size_t no_goto = max_size;
        std::vector<Action> actions;
        std::vector<size_t> go_to;
    };

    struct TermIndex final
    {
        size_t index = 0;
        bool is_terminal = true;
        bool operator==(const TermIndex& other) const
        {
            return index == other.index
                && is_terminal == other.is_terminal;
        }
        bool operator!=(const TermIndex& other) const { return !(*this == other); }
    };
}
