#include "functions.h"
#include <fstream>
#include "utils.h"
#include "overload.h"

namespace cls::lalr
{
    using namespace utils;

    namespace
    {
        /* Dependency graph */

        class DependencyGraph final
        {
        private:
            size_t no_dependency_count_ = 0;
            std::vector<std::unordered_set<size_t>> dependencies_;
        public:
            explicit DependencyGraph(const size_t size = 0) :
                no_dependency_count_(size), dependencies_(size) {}
            void add_dependency(size_t root, size_t dependency);
            std::vector<size_t> topological_traversal() const;
        };

        void DependencyGraph::add_dependency(const size_t root, const size_t dependency)
        {
            if (root == dependency)
                error("Class contains self dependency");
            if (dependencies_.size() <= root)
            {
                const size_t original_size = dependencies_.size();
                no_dependency_count_ += root + 1 - original_size;
                dependencies_.resize(root + 1);
            }
            if (dependencies_[root].empty()) no_dependency_count_--;
            dependencies_[root].insert(dependency);
        }

        std::vector<size_t> DependencyGraph::topological_traversal() const
        {
            const size_t size = dependencies_.size();
            size_t left_count = size;
            std::vector<size_t> dependency_count(size);
            std::transform(dependencies_.begin(), dependencies_.end(),
                dependency_count.begin(), [](const auto& vec) { return vec.size(); });
            std::vector<size_t> result;
            result.reserve(size);
            while (left_count > 0)
            {
                const size_t next(std::find(dependency_count.begin(),
                    dependency_count.end(), 0) - dependency_count.begin());
                if (next == size) error("Class dependency graph contains cycles");
                dependency_count[next]--; // Set to a really big value
                result.emplace_back(next);
                for (size_t i = 0; i < size; i++)
                    if (std::find(dependencies_[i].begin(), dependencies_[i].end(), next)
                        != dependencies_[i].end())
                        dependency_count[i]--;
                left_count--;
            }
            return result;
        }

        /* Code Generator */

        class CodeGenerator final
        {
        private:
            size_t indent_ = 0;
            bool write_to_header_ = true;
            std::ofstream header_stream_; // header stream
            std::ofstream source_stream_; // Source stream
            const Grammar& grammar_;
            const std::vector<TableRow>& table_;
            std::vector<std::vector<size_t>> rule_saved_term_count_;
            bool is_enum(const Term& term) const;
            std::ofstream& stream() { return write_to_header_ ? header_stream_ : source_stream_; }
            void new_line(ptrdiff_t indent = 0);
            void open_brace(bool to_new_line = true);
            void close_brace(std::string_view extra = {});
            template <typename... Ts>
            void write(Ts&& ... vs)
            {
                fmt::format_to(std::ostreambuf_iterator(stream()), std::forward<Ts>(vs)...);
            }
            std::vector<size_t> get_struct_define_sequence() const;
            void define_structs();
            void declare_parser_class();
            void define_parser_helpers();
            std::string pop_term(const Term& term, size_t offset) const;
            void define_reduce();
            void define_go_to();
            std::vector<size_t> get_token_indices() const;
            void define_parse();
        public:
            CodeGenerator(const std::string& directory, const Grammar& grammar,
                const std::vector<TableRow>& table);
            void write_code();
        };

        bool CodeGenerator::is_enum(const Term& term) const
        {
            if (const Terminal * t = std::get_if<Terminal>(&term))
                return grammar_.token_types[t->index].enumerator.has_value();
            return false;
        }

        void CodeGenerator::new_line(const ptrdiff_t indent)
        {
            indent_ += indent;
            write("\n{0:{1}}", "", indent_);
        }

        void CodeGenerator::open_brace(const bool to_new_line)
        {
            new_line();
            stream() << '{';
            if (to_new_line)
                new_line(4);
            else
                indent_ += 4;
        }

        void CodeGenerator::close_brace(const std::string_view extra)
        {
            new_line(-4);
            stream() << '}' << extra;
        }

        std::vector<size_t> CodeGenerator::get_struct_define_sequence() const
        {
            DependencyGraph graph(grammar_.non_terminals.size());
            for (const auto [nt, rules] : enumerate(grammar_.rules))
                for (const Rule& rule : rules)
                    for (const Term& term : rule.terms)
                        if (const NonTerminal * ptr = std::get_if<NonTerminal>(&term);
                            ptr && !ptr->use_unique_ptr)
                            graph.add_dependency(nt, ptr->index);
            return graph.topological_traversal();
        }

        void CodeGenerator::define_structs()
        {
            // Forward declare
            for (const std::string& name : grammar_.non_terminals)
            {
                if (name.empty()) continue;
                write("struct {};", name);
                new_line();
            }
            new_line();
            // Define the structs
            const std::vector<size_t> define_sequence = get_struct_define_sequence();
            const auto write_term = [this](const Term& term, const bool begin_with_new_line = false)
            {
                std::visit(Overload
                    {
                        [begin_with_new_line, this](const Terminal& t)
                        {
                            const TokenType& token = grammar_.token_types[t.index];
                            if (token.enumerator) return;
                            if (begin_with_new_line) new_line();
                            write("lex::{} {};", token.type_name, t.variable_name);
                        },
                        [begin_with_new_line, this](const NonTerminal& t)
                        {
                            const std::string& type = grammar_.non_terminals[t.index];
                            if (begin_with_new_line) new_line();
                            if (t.use_unique_ptr)
                                write("std::unique_ptr<{}>", type);
                            else
                                write(type);
                            write(" {};", t.variable_name);
                        }
                    }, term);
            };
            for (const size_t i : define_sequence)
            {
                if (i == 0) continue;
                write("struct {} final", grammar_.non_terminals[i]);
                const auto& rules = grammar_.rules[i];
                const auto output_class_members = [&, this](const size_t index)
                {
                    const size_t count = rule_saved_term_count_[i][index];
                    if (count == 0) // Empty class
                        stream() << " {};";
                    else if (count == 1) // Only one member
                    {
                        stream() << " { ";
                        for (const auto& term : rules[index].terms)
                            if (!is_enum(term))
                            {
                                write_term(term);
                                break;
                            }
                        stream() << " };";
                    }
                    else // Multiple terms, output in separate lines
                    {
                        open_brace(false);
                        for (const Term& term : rules[index].terms)
                            write_term(term, true);
                        close_brace(";");
                    }
                    new_line();
                };
                if (rules.size() == 1) // Only one production
                    output_class_members(0);
                else // Multiple productions, should use variant
                {
                    open_brace();
                    for (const auto [j, rule] : enumerate(rules))
                    {
                        if (rule.type_name.empty()) continue;
                        write("struct {} final", rule.type_name);
                        output_class_members(j);
                    }
                    stream() << "std::variant<";
                    for (const auto [j, rule] : enumerate(rules))
                    {
                        if (j != 0) stream() << ", ";
                        if (rule.terms.size() == 1)
                            std::visit(Overload
                                {
                                    [this](const Terminal& t)
                                    {
                                        write("lex::{}", grammar_.token_types[t.index].type_name);
                                    },
                                    [this](const NonTerminal& t)
                                    {
                                        stream() << grammar_.non_terminals[t.index];
                                    }
                                }, rule.terms[0]);
                        else
                            stream() << rule.type_name;
                    }
                    stream() << "> value;";
                    close_brace(";");
                    new_line();
                }
            }
            stream() << "using ASTNode = std::variant<";
            for (const std::string& nt : grammar_.non_terminals)
                if (!nt.empty())
                    stream() << nt << ", ";
            stream() << "lex::Token>;";
            new_line();
        }

        void CodeGenerator::declare_parser_class()
        {
            stream() << R"code(
    class Parser final
    {
    private:
        std::vector<lex::Token> tokens_;
        size_t input_position_ = 0;
        std::vector<size_t> state_stack_{ 0 };
        std::vector<ASTNode> node_stack_;

        template <typename T>
        T move_top(const size_t offset = 0) { return std::get<T>(std::move(*(node_stack_.end() - offset - 1))); }

        template <typename T>
        T move_top_token(const size_t offset = 0) { return std::get<T>(move_top<lex::Token>(offset).content); }

        template <typename T>
        auto make_unique_from_top(const size_t offset = 0) { return std::make_unique<T>(move_top<T>(offset)); }

        template <size_t N>
        auto& current_token() { return std::get<N>(tokens_[input_position_].content); }

        void error() const;
        void pop_n(size_t n);
        size_t current_token_type() const;
        size_t current_node_type() const;
        void shift(size_t new_state);
        void reduce(size_t rule);
        void go_to();
    public:
        explicit Parser(std::vector<lex::Token>&& tokens) :tokens_(std::move(tokens)) {}
        )code";
            write("{} parse();\n    }};", grammar_.non_terminals[1]);
        }

        void CodeGenerator::define_parser_helpers()
        {
            stream() << R"code(
    void Parser::pop_n(const size_t n)
    {
        node_stack_.erase(node_stack_.end() - n - 1, node_stack_.end() - 1);
        state_stack_.erase(state_stack_.end() - n, state_stack_.end());
    }

    void Parser::error() const
    {
        const auto [line, column] = tokens_[input_position_].position;
        throw std::runtime_error(fmt::format("Parsing error at line {}, column {}", line, column));
    }

    size_t Parser::current_token_type() const { return tokens_[input_position_].content.index(); }

    size_t Parser::current_node_type() const { return node_stack_.back().index(); }

    void Parser::shift(const size_t new_state)
    {
        node_stack_.emplace_back(std::move(tokens_[input_position_]));
        state_stack_.emplace_back(new_state);
        input_position_++;
    }

    )code";
        }

        std::string CodeGenerator::pop_term(const Term& term, const size_t offset) const
        {
            return std::visit(Overload
                {
                    [offset, this](const Terminal& t)
                    {
                        return fmt::format("move_top_token<{}>({})",
                            grammar_.token_types[t.index].type_name, offset);
                    },
                    [offset, this](const NonTerminal& t)
                    {
                        return fmt::format("{}<{}>({})",
                            t.use_unique_ptr ? "make_unique_from_top" : "move_top",
                            grammar_.non_terminals[t.index], offset);
                    }
                }, term);
        }

        void CodeGenerator::define_reduce()
        {
            stream() << "void Parser::reduce(const size_t rule)";
            open_brace();
            stream() << "using namespace lex;"; new_line();
            stream() << "switch (rule)";
            open_brace();
            size_t index = 1;
            for (const auto [i, rules] : enumerate(grammar_.rules))
            {
                if (i == 0) continue;
                const std::string& nt_name = grammar_.non_terminals[i];
                for (const auto [j, rule] : enumerate(rules))
                {
                    write("case {}:", index);
                    open_brace();
                    const size_t out_term_count = rule_saved_term_count_[i][j];
                    if (out_term_count == 0) // No terms to output
                    {
                        write("node_stack_.emplace_back({}", nt_name);
                        if (!rule.type_name.empty()) write("{{ {}::{}", nt_name, rule.type_name);
                        write("{{}}{});", rule.type_name.empty() ? "" : " }");
                    }
                    else if (out_term_count == 1) // Only one term to output
                    {
                        const auto iter = std::find_if(rule.terms.begin(), rule.terms.end(),
                            [this](const Term& t) { return !is_enum(t); });
                        write("node_stack_.emplace_back({}{{ {} }});",
                            nt_name, pop_term(*iter, rule.terms.end() - iter - 1));
                    }
                    else // Two or more terms to pop
                    {
                        write("node_stack_.emplace_back({}", nt_name);
                        if (!rule.type_name.empty()) write("{{ {}::{}", nt_name, rule.type_name);
                        open_brace(false);
                        bool first = true;
                        for (const auto [k, term] : enumerate(rule.terms))
                        {
                            if (is_enum(term)) continue;
                            if (!first) stream() << ',';
                            first = false;
                            new_line();
                            stream() << pop_term(term, rule.terms.size() - 1 - k);
                        }
                        close_brace(rule.type_name.empty() ? ");" : " });");
                    }
                    new_line();
                    write("pop_n({}); break;", rule.terms.size());
                    close_brace(); new_line();
                    index++;
                }
            }
            stream() << "default: error();";
            close_brace(); new_line();
            stream() << "go_to();";
            close_brace();
            new_line(); new_line();
        }

        void CodeGenerator::define_go_to()
        {
            stream() << "void Parser::go_to()";
            open_brace();
            stream() << "switch (state_stack_.back())";
            open_brace();
            for (const auto [i, row] : enumerate(table_))
            {
                if (std::all_of(row.go_to.begin(), row.go_to.end(),
                    [](const size_t v) { return v == TableRow::no_goto; })) continue;
                write("case {}: switch (current_node_type())", i);
                open_brace();
                for (const auto [j, v] : enumerate(row.go_to))
                {
                    if (v == TableRow::no_goto) continue;
                    write("case {}: state_stack_.emplace_back({}); return;", j - 1, v);
                    new_line();
                }
                stream() << "default: error();";
                close_brace(); new_line();
            }
            stream() << "default: error();";
            close_brace();
            close_brace();
            new_line(); new_line();
        }

        std::vector<size_t> CodeGenerator::get_token_indices() const
        {
            std::vector<size_t> result(grammar_.token_types.size());
            std::string enum_type;
            size_t index = size_t(-1);
            for (const auto [i, type] : enumerate(grammar_.token_types))
            {
                if (enum_type.empty() || !type.enumerator || enum_type != type.type_name)
                {
                    if (type.enumerator)
                        enum_type = type.type_name;
                    else
                        enum_type = "";
                    index++;
                }
                result[i] = index;
            }
            return result;
        }

        void CodeGenerator::define_parse()
        {
            const auto default_error = [this]()
            {
                stream() << "default: error();";
                close_brace(); new_line();
            };
            const std::vector<size_t> token_indices = get_token_indices();
            const std::string& return_type = grammar_.non_terminals[1];
            write("{} Parser::parse()", return_type);
            open_brace();
            stream() << "using namespace lex;"; new_line();
            stream() << "while (true)"; new_line(4);
            stream() << "switch (state_stack_.back())";
            open_brace();
            for (const auto [i, row] : enumerate(table_))
            {
                write("case {}: switch (current_token_type())", i);
                open_brace();
                size_t prev_index = max_size;
                for (const auto [j, action] : enumerate(row.actions))
                {
                    if (action.type == ActionType::error) continue;
                    const TokenType& type = grammar_.token_types[j];
                    const size_t index = token_indices[j];
                    if (type.enumerator) // Enum
                    {
                        if (prev_index != index) // Write switch opening for the enum
                        {
                            if (prev_index != max_size) default_error();
                            write("case {0}: switch (current_token<{0}>())", index);
                            open_brace();
                        }
                        prev_index = index;
                        write("case {}::{}: ", type.type_name, *type.enumerator);
                    }
                    else // Other tokens
                    {
                        if (prev_index != max_size) default_error();
                        prev_index = max_size;
                        write("case {}: ", token_indices[j]);
                    }
                    switch (action.type)
                    {
                        case ActionType::shift: write("shift({}); continue;", action.index); break;
                        case ActionType::reduce: write("reduce({}); continue;", action.index); break;
                        case ActionType::accept: write("return move_top<{}>();", return_type); break;
                        default: error("Unknown action type");
                    }
                    new_line();
                }
                if (prev_index != max_size) default_error();
                default_error();
            }
            stream() << "default: error();";
            close_brace();
            indent_ -= 4;
            close_brace();
        }

        CodeGenerator::CodeGenerator(const std::string& directory, const Grammar& grammar,
            const std::vector<TableRow>& table) :
            header_stream_(directory + "parser.h"), source_stream_(directory + "parser.cpp"),
            grammar_(grammar), table_(table)
        {
            if (header_stream_.fail()) error("Failed to open text file {}", directory);
            if (source_stream_.fail()) error("Failed to open text file {}", directory);
            for (const auto& rules : grammar_.rules)
            {
                auto& count = rule_saved_term_count_.emplace_back();
                std::transform(rules.begin(), rules.end(), std::back_inserter(count),
                    [this](const Rule& rule)
                {
                    return std::count_if(rule.terms.begin(), rule.terms.end(),
                        [this](const Term& term) { return !is_enum(term); });
                });
            }
        }

        void CodeGenerator::write_code()
        {
            stream() << R"(#pragma once

#include <memory>
#include "lexer.h"

namespace cls::parse)"; // Write to header file
            open_brace();
            define_structs();
            declare_parser_class();
            close_brace();
            new_line();

            write_to_header_ = false; indent_ = 0; // Start writing into source file
            stream() << R"(#include "parser.h"
#include <stdexcept>
#include <fmt/format.h>

namespace cls::parse)";
            open_brace(false);
            define_parser_helpers();
            define_reduce();
            define_go_to();
            define_parse();
            close_brace();
            new_line();
        }
    }

    void generate_code(const std::string& file_path, const Grammar& grammar,
        const std::vector<TableRow>& table)
    {
        CodeGenerator(file_path, grammar, table).write_code();
    }
}
