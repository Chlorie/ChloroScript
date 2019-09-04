#include "functions.h"
#include <optional>
#include <fmt/format.h>
#include "static_char_set.h"
#include "utils.h"

namespace cls::lalr
{
    using namespace utils;

    namespace
    {
        class GrammarParser final
        {
        private:
            std::string_view left_text_;
            Grammar grammar_;
            size_t non_terminal_index_ = size_t(-1);
            std::string_view cut_prefix(size_t count);
            std::string_view next_symbol();
            void extract_non_terminals();
            size_t get_non_terminal_index(std::string_view name) const;
            std::optional<Term> read_term();
            std::optional<Rule> read_rule();
            void process_token_type_list();
        public:
            explicit GrammarParser(const std::string_view text) :left_text_(text) {}
            Grammar process();
        };

        constexpr StaticCharSet symbol_set =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_";
        constexpr StaticCharSet whitespace = " \t\n\r";
    }

    std::string_view GrammarParser::cut_prefix(const size_t count)
    {
        std::string_view prefix{ left_text_.data(), count };
        left_text_.remove_prefix(count);
        return prefix;
    }

    std::string_view GrammarParser::next_symbol()
    {
        while (!left_text_.empty() && whitespace.contains(left_text_[0]))
            left_text_.remove_prefix(1);
        if (left_text_.empty()) return {};
        if (!symbol_set.contains(left_text_[0])) return cut_prefix(1);
        size_t length = 0;
        while (length < left_text_.size() && symbol_set.contains(left_text_[length])) length++;
        return cut_prefix(length);
    }

    void GrammarParser::extract_non_terminals()
    {
        const std::string_view restore_point = left_text_;
        std::string_view previous;
        while (true)
        {
            const std::string_view current = next_symbol();
            if (current.empty()) break;
            if (current == ":") grammar_.non_terminals.emplace_back(previous);
            previous = current;
        }
        left_text_ = restore_point;
    }

    size_t GrammarParser::get_non_terminal_index(const std::string_view name) const
    {
        const auto iter = std::find_if(grammar_.non_terminals.begin(), grammar_.non_terminals.end(),
            [name](const std::string& str) { return name == str; });
        if (iter == grammar_.non_terminals.end()) return size_t(-1);
        return size_t(iter - grammar_.non_terminals.begin());
    }

    std::optional<Term> GrammarParser::read_term()
    {
        const std::string_view type_name = next_symbol();
        if (type_name == ";") return std::nullopt;
        if (const size_t non_terminal = get_non_terminal_index(type_name); non_terminal != size_t(-1))
        {
            std::string_view next = next_symbol();
            NonTerminal result{ non_terminal, false, {} };
            if (next == "*")
            {
                result.use_unique_ptr = true;
                next = next_symbol();
            }
            if (next != "(")
                error("Non-terminal type name \"{}\" must be followed by parentheses "
                    "enclosed variable name", grammar_.non_terminals[non_terminal]);
            result.variable_name = std::string(next_symbol());
            if (next_symbol() != ")")
                error("Variable name \"{}\" must be enclosed by parentheses", result.variable_name);
            return Term(std::move(result));
        }
        if (const auto iter = std::find_if(grammar_.token_types.begin(), grammar_.token_types.end(),
            [type_name](const TokenType& type) { return type.type_name == type_name; });
            iter != grammar_.token_types.end())
        {
            const std::string_view next = next_symbol();
            if (next == ".") // Enumerator
            {
                const std::string_view enumerator_name = next_symbol();
                const auto enum_iter = std::find_if(iter, grammar_.token_types.end(),
                    [enumerator_name](const TokenType& type) { return type.enumerator == enumerator_name; });
                if (enum_iter == grammar_.token_types.end())
                    error("Failed to find corresponding term type \"{}.{}\"", type_name, enumerator_name);
                const size_t index(enum_iter - grammar_.token_types.begin());
                return Term(Terminal{ index, {} });
            }
            const size_t index(iter - grammar_.token_types.begin());
            Terminal result{ index, {} };
            if (next != "(")
                error("Terminal non-enum type name \"{}\" must be followed by parentheses "
                    "enclosed variable name", type_name);
            result.variable_name = std::string(next_symbol());
            if (next_symbol() != ")")
                error("Variable name \"{}\" must be enclosed by parentheses", result.variable_name);
            return Term(std::move(result));
        }
        error("Failed to find corresponding term type \"{}\"", type_name);
    }

    std::optional<Rule> GrammarParser::read_rule()
    {
        const std::string_view first_symbol = next_symbol();
        if (first_symbol.empty()) return std::nullopt;
        if (first_symbol != "|")
        {
            non_terminal_index_ = get_non_terminal_index(first_symbol);
            if (next_symbol() != ":")
                error("Non-terminal type name \"{}\" must be followed by colon",
                    grammar_.non_terminals[non_terminal_index_]);
        }
        if (non_terminal_index_ == size_t(-1)) error("Missing the first alternative");
        Rule rule{ non_terminal_index_, {}, {} };
        const std::string_view restore_point = left_text_;
        if (next_symbol() == "[") // Type name for this alternative
        {
            rule.type_name = next_symbol();
            if (next_symbol() != "]")
                error("Alternative type name \"{}\" should be enclosed by brackets", rule.type_name);
        }
        else
            left_text_ = restore_point;
        while (auto term = read_term())
            rule.terms.emplace_back(std::move(*term));
        return rule;
    }

    void GrammarParser::process_token_type_list()
    {
        while (true)
        {
            const std::string symbol{ next_symbol() };
            if (symbol == "$") break; // EOS symbol
            const std::string_view next = next_symbol();
            if (next == "{") // Enum type
            {
                while (true)
                {
                    grammar_.token_types.emplace_back(
                        TokenType{ symbol, std::string(next_symbol()) });
                    const std::string_view separator = next_symbol();
                    if (separator == "}") break;
                    if (separator != ",") error("Enumerator list not finished");
                }
                if (next_symbol() != ",") error("Token type list not finished");
                continue;
            }
            if (next != "," || symbol.empty()) error("Token type list not finished");
            grammar_.token_types.emplace_back(TokenType{ symbol, {} });
        }
    }

    Grammar GrammarParser::process()
    {
        process_token_type_list();
        grammar_.non_terminals.emplace_back();
        grammar_.rules.emplace_back(Rule{ 0, "", { NonTerminal{ 1 } } });
        extract_non_terminals();
        while (auto rule = read_rule())
            grammar_.rules.emplace_back(std::move(*rule));
        return std::move(grammar_);
    }

    Grammar process_input(const std::string& text) { return GrammarParser(text).process(); }
}
