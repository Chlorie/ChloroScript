#include "lexer.h"
#include <array>
#include <charconv>
#include "utils/static_char_set.h"

namespace cls::lex
{
    using namespace std::literals;

    namespace
    {
        template <typename T>
        using Strings = std::array<std::string_view, size_t(T::max_value)>;

        constexpr Strings<Symbol> symbols{ "=", ";", ":", "(", ")", "{", "}" };
        constexpr Strings<Keyword> keywords{ "int", "def", "return" };

        constexpr utils::StaticCharSet alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        constexpr utils::StaticCharSet digits = "0123456789";
        constexpr utils::StaticCharSet identifier_first = alpha | "_";
        constexpr utils::StaticCharSet identifier_rest = identifier_first | digits;
        constexpr utils::StaticCharSet error_recover_point = " \t\r\n!@#$%^&*()-+=[]{}|\\:;\"'<,>./?";
    }

    void Lexer::skip_whitespace()
    {
        while (!is_end())
        {
            switch (current())
            {
                case ' ': position_.column++; break;
                case '\t': position_.column += 4; break;
                default: return;
            }
            index_++;
        }
    }

    void Lexer::skip_enter()
    {
        while (!is_end())
        {
            switch (current())
            {
                case '\n': index_++; break;
                case '\r': index_++; if (!is_end() && current() == '\n') index_++; break;
                default: return;
            }
            position_.column = 1;
            position_.line++;
        }
    }

    void Lexer::match_identifier_or_keyword()
    {
        if (is_end()) return;
        if (!identifier_first.contains(current())) return;
        const size_t start_index = index_++;
        while (identifier_rest.contains(current())) index_++;
        const size_t length = index_ - start_index;
        const std::string_view identifier = script_.substr(start_index, length);
        for (size_t i = 0; i < size_t(Keyword::max_value); i++)
        {
            const std::string_view& keyword = keywords[i];
            if (identifier == keyword)
            {
                result_.push_back({ Keyword(i), position_ });
                position_.column += length;
                return;
            }
        }
        result_.push_back({ Identifier{ std::string(identifier) }, position_ });
        position_.column += length;
    }

    void Lexer::match_symbol()
    {
        if (is_end()) return;
        for (size_t i = 0; i < size_t(Symbol::max_value); i++)
        {
            const std::string_view& symbol = symbols[i];
            if (script_.substr(index_, symbol.length()) == symbol)
            {
                result_.push_back({ Symbol(i), position_ });
                index_ += symbol.length();
                position_.column += symbol.length();
                return;
            }
        }
    }

    void Lexer::match_integer_literal()
    {
        if (is_end() || !digits.contains(current())) return;
        const size_t start_index = index_;
        while (!is_end() && digits.contains(current())) index_++;
        int32_t value = 0;
        const auto [ptr, ec] = std::from_chars(&script_[start_index], &script_[index_], value);
        if (ec != std::errc{})
            result_.push_back({ LexError::integer_literal_too_big, position_ });
        else
            result_.push_back({ Integer{value}, position_ });
        position_.column += index_ - start_index;
    }

    void Lexer::consume_error()
    {
        const size_t start_index = index_;
        while (!is_end() && !error_recover_point.contains(current())) index_++;
        result_.push_back({ LexError::unknown_sequence, position_ });
        position_.column += index_ - start_index;
    }

    std::vector<Token> Lexer::lex()
    {
        size_t last_index = 0;
        while (!is_end())
        {
            skip_whitespace();
            skip_enter();
            match_identifier_or_keyword();
            match_symbol();
            match_integer_literal();
            if (index_ == last_index) consume_error();
            last_index = index_;
        }
        return std::move(result_);
    }
}
