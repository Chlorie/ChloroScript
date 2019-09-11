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
            if (no_dependency_count_ == 0)
                error("Class dependency graph contains cycles");
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
            bool skip_new_line_ = false;
            bool write_to_header_ = true;
            std::ofstream header_stream_; // header stream
            std::ofstream source_stream_; // Source stream
            const Grammar& grammar_;
            const std::vector<TableRow>& table_;
            std::ofstream& stream() { return write_to_header_ ? header_stream_ : source_stream_; }
            void new_line(ptrdiff_t indent = 0);
            void open_brace(bool to_new_line = true);
            void close_brace(std::string_view extra = {});
            std::vector<size_t> get_struct_define_sequence() const;
            void define_structs();
        public:
            CodeGenerator(const std::string& file_path, const Grammar& grammar,
                const std::vector<TableRow>& table) :
                header_stream_(file_path + ".h"), source_stream_(file_path + ".cpp"),
                grammar_(grammar), table_(table)
            {
                if (header_stream_.fail()) error("Failed to open text file {}", file_path);
                if (source_stream_.fail()) error("Failed to open text file {}", file_path);
            }
            void write_code();
        };

        void CodeGenerator::new_line(const ptrdiff_t indent)
        {
            indent_ += indent;
            if (!skip_new_line_) stream() << '\n' << std::string(indent_, ' ');
            skip_new_line_ = false;
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
                stream() << "struct " << name << ';';
                new_line();
            }
            new_line();
            // Define the structs
            const std::vector<size_t> define_sequence = get_struct_define_sequence();
            const auto write_term = [this](const Term& term)
            {
                std::visit(Overload
                    {
                        [this](const Terminal& t)
                        {
                            const TokenType& token = grammar_.token_types[t.index];
                            if (token.enumerator) // Enum token
                            {
                                skip_new_line_ = true;
                                return;
                            }
                            stream() << token.type_name << ' ' << t.variable_name << ';';
                        },
                        [this](const NonTerminal& t)
                        {
                            const std::string& type = grammar_.non_terminals[t.index];
                            if (t.use_unique_ptr) header_stream_ << "std::unique_ptr<";
                            stream() << type;
                            if (t.use_unique_ptr) header_stream_ << '>';
                            stream() << ' ' << t.variable_name << ';';
                        }
                    }, term);
            };
            for (const size_t i : define_sequence)
            {
                if (i == 0) continue;
                stream() << "struct " << grammar_.non_terminals[i] << " final";
                const auto& rules = grammar_.rules[i];
                if (rules.size() == 1) // Only one production
                {
                    if (rules[0].terms.size() == 1) // Only one term, output in one line
                    {
                        stream() << " { ";
                        write_term(rules[0].terms[0]);
                        stream() << " };";
                        new_line();
                    }
                    else // Multiple terms, output in separate lines
                    {
                        open_brace();
                        for (const Term& term : rules[0].terms)
                        {
                            write_term(term);
                            stream() << '\n';
                        }
                        close_brace(";");
                    }
                }
                else // Multiple productions, should use variant
                {
                    open_brace(false);
                    for (const Rule& rule : rules)
                    {
                        new_line();
                        if (rule.terms.empty()) // Epsilon production
                            stream() << "struct " << rule.type_name << " final {};";
                        else if (rule.terms.size() != 1) // Multiple terms
                        {
                            stream() << "struct " << rule.type_name << " final";
                            open_brace(false);
                            for (const Term& term : rule.terms)
                            {
                                new_line();
                                write_term(term);
                            }
                            close_brace(";");
                        }
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
                                        stream() << grammar_.token_types[t.index].type_name;
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
        }

        void CodeGenerator::write_code()
        {
            stream() << "namespace cls::parse"; // Write to header file
            open_brace();
            define_structs();
            close_brace();
            write_to_header_ = false; indent_ = 0; // Start writing into source file
        }
    }

    void generate_code(const std::string& file_path, const Grammar& grammar,
        const std::vector<TableRow>& table)
    {
        CodeGenerator(file_path, grammar, table).write_code();
    }
}
