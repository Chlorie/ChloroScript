#include "process_input.h"
#include <optional>
#include <fmt/format.h>
#include "static_char_set.h"

namespace cls::lalr
{
    namespace
    {
        constexpr utils::StaticCharSet symbol_set =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_";
        constexpr utils::StaticCharSet whitespace = " \t\n\r";
    }

    class GrammarBuilder final
    {
    private:
        std::string_view left_text_;
        Grammar grammar_;
        const std::string* non_terminal_ptr_ = nullptr;
        template <typename... Ts>
        [[noreturn]] void error(Ts&& ... args) const
        {
            throw std::runtime_error(fmt::format(std::forward<Ts>(args)...));
        }
        std::string_view cut_prefix(size_t count);
        std::string_view next_symbol();
        void extract_non_terminals();
        const std::string* find_non_terminal(std::string_view name) const;
        const std::string* find_terminal(std::string_view name) const;
        std::optional<Term> read_term();
        std::optional<Rule> read_rule();
        void process_token_type_list();
    public:
        explicit GrammarBuilder(const std::string_view text) :left_text_(text) {}
        Grammar process();
    };

    std::string_view GrammarBuilder::cut_prefix(const size_t count)
    {
        std::string_view prefix{ left_text_.data(), count };
        left_text_.remove_prefix(count);
        return prefix;
    }

    std::string_view GrammarBuilder::next_symbol()
    {
        while (!left_text_.empty() && whitespace.contains(left_text_[0]))
            left_text_.remove_prefix(1);
        if (left_text_.empty()) return {};
        if (!symbol_set.contains(left_text_[0])) return cut_prefix(1);
        size_t length = 0;
        while (length < left_text_.size() && symbol_set.contains(left_text_[length])) length++;
        return cut_prefix(length);
    }

    void GrammarBuilder::extract_non_terminals()
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

    const std::string* GrammarBuilder::find_non_terminal(const std::string_view name) const
    {
        const auto iter = std::find_if(grammar_.non_terminals.begin(), grammar_.non_terminals.end(),
            [name](const std::string& str) { return name == str; });
        if (iter == grammar_.non_terminals.end()) return nullptr;
        return &*iter;
    }

    const std::string* GrammarBuilder::find_terminal(const std::string_view name) const
    {
        const auto iter = std::find_if(grammar_.token_types.begin(), grammar_.token_types.end(),
            [name](const std::string& str) { return name == str; });
        if (iter == grammar_.token_types.end()) return nullptr;
        return &*iter;
    }

    std::optional<Term> GrammarBuilder::read_term()
    {
        const std::string_view type_name = next_symbol();
        if (type_name == ";") return std::nullopt;
        if (const std::string * non_terminal = find_non_terminal(type_name))
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
                    "enclosed variable name", *non_terminal);
            result.variable_name = std::string(next_symbol());
            if (next_symbol() != ")")
                error("Variable name \"{}\" must be enclosed by parentheses", result.variable_name);
            return Term(std::move(result));
        }
        if (const std::string * terminal = find_terminal(type_name))
        {
            const std::string_view next = next_symbol();
            if (next == ".") // Enum
            {
                EnumTerm result{ *terminal + "::" + std::string(next_symbol()) };
                return Term(std::move(result));
            }
            Terminal result{ terminal, {} };
            if (next != "(")
                error("Terminal non-enum type name \"{}\" must be followed by parentheses "
                    "enclosed variable name", *terminal);
            result.variable_name = std::string(next_symbol());
            if (next_symbol() != ")")
                error("Variable name \"{}\" must be enclosed by parentheses", result.variable_name);
            return Term(std::move(result));
        }
        error("Failed to find corresponding term type \"{}\"", type_name);
    }

    std::optional<Rule> GrammarBuilder::read_rule()
    {
        const std::string_view first_symbol = next_symbol();
        if (first_symbol.empty()) return std::nullopt;
        if (first_symbol != "|")
        {
            non_terminal_ptr_ = find_non_terminal(first_symbol);
            if (next_symbol() != ":")
                error("Non-terminal type name \"{}\" must be followed by colon", *non_terminal_ptr_);
        }
        if (!non_terminal_ptr_) error("Missing the first alternative");
        Rule rule{ non_terminal_ptr_, {}, {} };
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

    void GrammarBuilder::process_token_type_list()
    {
        while (true)
        {
            const std::string_view symbol = next_symbol();
            if (symbol.empty()) error("Token type list not finished");
            if (symbol == "$") break; // EOS symbol
            grammar_.token_types.emplace_back(symbol);
        }
    }

    Grammar GrammarBuilder::process()
    {
        process_token_type_list();
        extract_non_terminals();
        while (auto rule = read_rule())
            grammar_.rules.emplace_back(std::move(*rule));
        return std::move(grammar_);
    }

    Grammar process_input(const std::string& text) { return GrammarBuilder(text).process(); }
}
