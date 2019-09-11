#include <fmt/format.h>
#include <fstream>
#include <chrono>
#include <stack>
#include "src/functions.h"

namespace cls::lex
{
    struct Position final { size_t line = 0, column = 0; };

    enum class Symbol : uint8_t { plus };
    struct Identifier final { std::string name; };

    struct Token final
    {
        std::variant<Symbol, Identifier, std::monostate> content;
        Position position;
    };
}

/*
 *  Symbol Identifier $
 *
 *  Program: Expression(expr);
 *  Expression: [BinOp] Expression*(expr) Symbol.plus Term(term);
 *            | Term(term);
 *  Term: Identifier(ident);
 *
 *  [0] S'-> S
 *  [1] S -> E
 *  [2] E -> E + T
 *  [3] E -> T
 *  [4] T -> id
 *
 *         action            goto
 *      +    id   $    S'   S    E    T
 *  0        s4             1    2    3
 *  1             acc
 *  2   s5        r1
 *  3   r3        r3
 *  4   r4        r4
 *  5        s4                       6
 *  6   r2        r2
 */

namespace cls::parse
{
    struct T final { lex::Identifier id; };
    struct E final
    {
        struct BinOp final
        {
            std::unique_ptr<E> expr;
            lex::Symbol symbol;
            T term;
        };
        std::variant<BinOp, T> expr;
    };
    struct S final { E expr; };

    using ASTNode = std::variant<S, E, T, lex::Token>;

    class Parser final
    {
    private:
        enum TokenType
        {
            t_symbol,
            t_identifier,
            t_end_of_stream
        };

        enum NonTerminalType
        {
            n_s,
            n_e,
            n_t
        };

        std::vector<lex::Token> tokens_;
        size_t input_position_ = 0;
        std::stack<size_t> state_stack_;
        std::stack<ASTNode> node_stack_;

        template <typename T>
        T move_and_pop()
        {
            T value = std::get<T>(std::move(node_stack_.top()));
            node_stack_.pop();
            state_stack_.pop();
            return value;
        }

        void error() const
        {
            const auto [line, column] = tokens_[input_position_].position;
            throw std::runtime_error(
                fmt::format("Parsing error at line {}, column {}", line, column));
        }

        size_t current_token_type() const { return tokens_[input_position_].content.index(); }

        size_t current_node_type() const { return node_stack_.top().index(); }

        template <size_t N>
        auto& current_token() { return std::get<N>(tokens_[input_position_].content); }

        void shift(const size_t new_state)
        {
            node_stack_.emplace(std::move(tokens_[input_position_]));
            state_stack_.push(new_state);
            input_position_++;
        }

        void reduce(const size_t rule)
        {
            using namespace lex;
            switch (rule)
            {
                case 1:
                {
                    node_stack_.emplace(S{ move_and_pop<E>() });
                    break;
                }
                case 2:
                {
                    T term = move_and_pop<T>();
                    const Symbol symbol = std::get<Symbol>(move_and_pop<Token>().content);
                    E::BinOp result
                    {
                        std::make_unique<E>(move_and_pop<E>()),
                        symbol,
                        std::move(term)
                    };
                    node_stack_.emplace(E{ { std::move(result) } });
                    break;
                }
                case 3:
                {
                    node_stack_.emplace(E{ move_and_pop<T>() });
                    break;
                }
                case 4:
                {
                    node_stack_.emplace(T{ std::get<Identifier>(move_and_pop<Token>().content) });
                    break;
                }
                default: error();
            }
            go_to();
        }

        void go_to()
        {
            switch (state_stack_.top())
            {
                case 0: switch (current_node_type())
                {
                    case n_s: state_stack_.push(1); return;
                    case n_e: state_stack_.push(2); return;
                    case n_t: state_stack_.push(3); return;
                    default: error();
                }
                case 5: switch (current_node_type())
                {
                    case n_t: state_stack_.push(6); return;
                    default: error();
                }
                default: error();
            }
        }

    public:
        explicit Parser(std::vector<lex::Token>&& tokens) :
            tokens_(std::move(tokens))
        {
            state_stack_.push(0);
        }

        S parse()
        {
            using namespace lex;
            while (true)
                switch (state_stack_.top())
                {
                    case 0: switch (current_token_type())
                    {
                        case t_identifier: shift(4); continue;
                        default: error();
                    }
                    case 1: switch (current_token_type())
                    {
                        case t_end_of_stream: return move_and_pop<S>(); // accept
                        default: error();
                    }
                    case 2: switch (current_token_type())
                    {
                        case t_symbol: switch (current_token<t_symbol>())
                        {
                            case Symbol::plus: shift(5); continue;
                            default: error();
                        }
                        case t_end_of_stream: reduce(1); continue;
                        default: error();
                    }
                    case 3: switch (current_token_type())
                    {
                        case t_symbol: switch (current_token<t_symbol>())
                        {
                            case Symbol::plus: reduce(3); continue;
                            default: error();
                        }
                        case t_end_of_stream: reduce(3); continue;
                        default: error();
                    }
                    case 4: switch (current_token_type())
                    {
                        case t_symbol: switch (current_token<t_symbol>())
                        {
                            case Symbol::plus: reduce(4); continue;
                            default: error();
                        }
                        case t_end_of_stream: reduce(4); continue;
                        default: error();
                    }
                    case 5: switch (current_token_type())
                    {
                        case t_identifier: shift(4); continue;
                        default: error();
                    }
                    case 6: switch (current_token_type())
                    {
                        case t_symbol: switch (current_token<t_symbol>())
                        {
                            case Symbol::plus: reduce(2); continue;
                            default: error();
                        }
                        case t_end_of_stream: reduce(2); continue;
                        default: error();
                    }
                    default: error();
                }
        }
    };
}

int main(const int argc, const char** argv)
{
    using namespace std::literals;
    using namespace cls::lex;
    using namespace cls::lalr;
    //using namespace cls::parse;

    using Clock = std::chrono::high_resolution_clock;

    if (argc != 3)
    {
        fmt::print("Usage: LALRParser.exe grammar_path output_path\n");
        return 1;
    }

    try
    {
        const auto start = Clock::now();

        std::ifstream stream(argv[1]);
        std::string file, line;
        while (std::getline(stream, line)) file += line + '\n';
        const Grammar grammar = process_input(file);
        const std::vector<TableRow>& table = generate_table(grammar);
        generate_code(argv[2], grammar, table);

        const auto us = (Clock::now() - start) / 1us;
        fmt::print("Completed - Elapsed {}us\n", us);
        return 0;
    }
    catch (const std::runtime_error& e)
    {
        fmt::print("{}", e.what());
    }

    //Parser parser({
    //    { Identifier{ "x" }, {} },
    //    { Symbol::plus, {} },
    //    { Identifier{ "y" }, {} },
    //    { Symbol::plus, {} },
    //    { Identifier{ "z" }, {} },
    //    { std::monostate{}, {} }
    //    });
    //auto ast = parser.parse();

    return 0;
}
