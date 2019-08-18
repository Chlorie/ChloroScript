#pragma once

#include <vector>
#include <string_view>
#include <variant>

namespace cls::lex
{
    struct Position final
    {
        size_t line = 1;
        size_t column = 1;
    };

    enum class Symbol : uint8_t
    {
        equal,
        semicolon,
        colon,
        left_paren, right_paren,
        left_brace, right_brace,
        max_value
    };

    enum class Keyword : uint8_t
    {
        int_,
        def, return_,
        max_value
    };

    enum class LexError : uint8_t
    {
        integer_literal_too_big,
        unknown_sequence,
        open_multiline_comment,
        max_value
    };

    struct Identifier final { std::string name; };
    struct Integer final { int32_t value = 0; };

    struct Token final
    {
        std::variant<
            Symbol, Keyword, Identifier, Integer,
            LexError
        > content;
        Position position;
    };

    class Lexer final
    {
    private:
        std::string_view script_;
        std::vector<Token> result_;
        size_t index_ = 0;
        Position position_;
        char current() const { return script_[index_]; }
        bool is_end() const { return script_.length() <= index_; }
        void skip_whitespace();
        void skip_enter();
        void match_identifier_or_keyword();
        void match_symbol();
        void match_integer_literal();
        void consume_error();
    public:
        explicit Lexer(const std::string_view script) :script_(script) {}
        std::vector<Token> lex();
    };
}
