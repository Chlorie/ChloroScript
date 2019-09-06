#pragma once

#include <vector>
#include <string>
#include <variant>
#include <optional>

namespace cls::lalr
{
    struct TokenType final
    {
        std::string type_name;
        std::optional<std::string> enumerator;
    };

    struct Terminal final
    {
        size_t index = size_t(-1);
        std::string variable_name;
    };

    struct NonTerminal final
    {
        size_t index = size_t(-1);
        bool use_unique_ptr = false;
        std::string variable_name;
    };

    using Term = std::variant<Terminal, NonTerminal>;

    struct Rule final
    {
        size_t non_terminal_index = size_t(-1);
        std::string type_name;
        std::vector<Term> terms;
    };

    struct Grammar final
    {
        std::vector<TokenType> token_types;
        std::vector<std::string> non_terminals;
        std::vector<Rule> rules;
    };

    enum class ActionType : uint8_t { shift, reduce, accept, error };

    struct Action final
    {
        ActionType type = ActionType::error;
        size_t index = 0;
    };

    struct TableRow final
    {
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
