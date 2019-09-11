#include "functions.h"
#include <unordered_set>
#include <numeric>
#include "overload.h"
#include "utils.h"

namespace cls::lalr
{
    using namespace utils;

    namespace
    {
        template <typename T>
        auto erase_by_indices(std::vector<T>& vector, const std::vector<size_t>& indices)
        {
            // Indices should be sorted
            auto index_iter = indices.begin();
            auto empty_space = vector.begin();
            for (const auto [i, elem] : enumerate(vector))
            {
                if (index_iter == indices.end() || i != *index_iter)
                    * empty_space++ = std::move(elem);
                else
                    ++index_iter;
            }
            vector.erase(empty_space, vector.end());
        }

        class SetGenerator final
        {
        private:
            static constexpr size_t epsilon = size_t(-1);
            // rules_[i][j][k]: k-th term of j-th production of i-th non-terminal
            std::vector<std::vector<std::vector<TermIndex>>> rules_;
            size_t rule_count_ = 0;
            size_t original_non_terminal_count_ = 0;
            size_t eos_index_ = 0;
            std::vector<std::unordered_set<size_t>> first_;
            void eliminate_direct_left_recursion(size_t index);
            void eliminate_all_left_recursion();
            void compute_first_impl();
        public:
            explicit SetGenerator(const Grammar& grammar);
            auto compute_first();
        };

        void SetGenerator::eliminate_direct_left_recursion(const size_t index)
        {
            const auto is_directly_left_recursive = [index](const std::vector<TermIndex>& rule)
            {
                if (rule.empty()) return false;
                const bool result = rule[0] == TermIndex{ index, false };
                return result;
            };
            if (!std::any_of(rules_[index].begin(), rules_[index].end(),
                is_directly_left_recursive)) return;
            const size_t helper_term_index = rules_.size();
            auto& helper_rules = rules_.emplace_back(); // Rules that produce A'
            auto& rules = rules_[index]; // Rules that produce index-th non-terminal
            // Following algorithm modified from std::remove_if
            auto empty_space = rules.begin();
            for (auto iter = rules.begin(); iter != rules.end(); ++iter)
            {
                auto& rule = *iter;
                if (is_directly_left_recursive(rule)) // A -> A b
                {
                    if (rule.size() == 1) // A -> A
                        error("Self recursive production occurred during direct left recursion "
                            "elimination of non-terminal #{}", index);
                    // A' -> b A'
                    auto& helper_rule = helper_rules.emplace_back(std::move(rule));
                    helper_rule.erase(helper_rule.begin());
                    helper_rule.emplace_back(TermIndex{ helper_term_index, false });
                }
                else // A -> b
                {
                    rule.emplace_back(TermIndex{ helper_term_index, false }); // A -> b A'
                    *empty_space++ = std::move(rule);
                }
            }
            rules.erase(empty_space, rules.end()); // Remove-erase idiom
            helper_rules.emplace_back(); // A' -> epsilon
        }

        void SetGenerator::eliminate_all_left_recursion()
        {
            eliminate_direct_left_recursion(0);
            for (size_t i = 1; i < rules_.size(); i++) // For every non-terminal
            {
                auto& rules = rules_[i];
                std::vector<size_t> to_remove;
                for (size_t j = 0; j < i; j++) // Substitute all non-terminal j in i's productions
                {
                    const TermIndex term_j{ j, false };
                    const size_t original_count = rules.size();
                    for (size_t k = 0; k < original_count; k++)
                    {
                        if (!contains(rules[k], term_j)) continue;
                        to_remove.emplace_back(k);
                        rules.resize(rules.size() + rules_[j].size());
                        auto& rule = rules[k];
                        auto prev = rule.begin();
                        while (true)
                        {
                            const auto current = std::find(prev, rule.end(), term_j);
                            std::for_each(rules.begin() + original_count, rules.end(),
                                [&](auto& new_rule) { std::copy(prev, current, std::back_inserter(new_rule)); });
                            if (current == rule.end()) break;
                            for (size_t l = 0; l < rules_[j].size(); l++)
                                std::copy(rules_[j][l].begin(), rules_[j][l].end(),
                                    std::back_inserter(rules[l + original_count]));
                            prev = current + 1;
                        }
                    }
                    erase_by_indices(rules, to_remove);
                    to_remove.clear();
                }
                eliminate_direct_left_recursion(i);
            }
        }

        void SetGenerator::compute_first_impl()
        {
            first_.resize(rules_.size());
            std::vector<size_t> traverse_stack;
            std::vector<Bool> finished(rules_.size());
            // Compute FIRST set recursively
            const auto recurse = [&, this](const auto& self, const size_t nt_index) -> void
            {
                const auto add_subset = [&, this](const size_t other_nt)
                {
                    if (other_nt == nt_index)
                        error("Grammar still contains left recursion");
                    if (!finished[other_nt]) // Other FIRST set is not ready yet
                    {
                        if (contains(traverse_stack, other_nt))
                            error("Compute graph of FOLLOW contains cycles");
                        self(self, other_nt); // Compute FIRST of the other non-terminal
                    }
                    // Insert all terminals in FIRST(other) except for epsilon into FIRST(this)
                    for (const size_t term : first_[other_nt])
                        if (term != epsilon)
                            first_[nt_index].insert(term);
                };
                traverse_stack.emplace_back(nt_index);
                for (const auto& rule : rules_[nt_index])
                {
                    if (rule.empty()) // A -> epsilon
                        first_[nt_index].insert(epsilon); // FIRST(A) += { epsilon }
                    bool all_epsilon = true;
                    for (const TermIndex& term : rule) // A -> Bi...
                    {
                        if (term.is_terminal) // A -> Bi...cDi...
                        {
                            first_[nt_index].insert(term.index); // FIRST(A) += { c }
                            all_epsilon = false;
                            break;
                        }
                        const size_t other = term.index; // A -> Bi...
                        add_subset(other); // FIRST(A) += FIRST(Bi) \ { epsilon }
                        // Epsilon not in FIRST(Bi)
                        if (first_[other].find(epsilon) == first_[other].end())
                        {
                            all_epsilon = false;
                            break;
                        }
                    }
                    if (all_epsilon) // A -> Bi... and all FIRST(Bi) contains epsilon
                        first_[nt_index].insert(epsilon);
                }
                finished[nt_index] = true;
                traverse_stack.pop_back();
            };
            for (const auto [i, v] : enumerate(finished))
                if (!v)
                    recurse(recurse, i); // Recursively compute FIRST set of all non-terminals
        }

        SetGenerator::SetGenerator(const Grammar& grammar)
        {
            eos_index_ = grammar.token_types.size();
            original_non_terminal_count_ = grammar.non_terminals.size();
            rules_.resize(original_non_terminal_count_);
            for (const auto [index, rules] : enumerate(grammar.rules))
                for (const Rule& rule : rules)
                {
                    auto& new_rule = rules_[index].emplace_back();
                    std::transform(rule.terms.begin(), rule.terms.end(),
                        std::back_inserter(new_rule),
                        [](const Term& term) { return std::visit(Overload
                            {
                                [](const Terminal& t) { return TermIndex{ t.index, true }; },
                                [](const NonTerminal& t) { return TermIndex{ t.index, false }; }
                            }, term); });
                }
        }

        auto SetGenerator::compute_first()
        {
            eliminate_all_left_recursion();
            rule_count_ = std::accumulate(rules_.begin(), rules_.end(), 0,
                [](const size_t part, const auto& vec) { return part + vec.size(); });
            compute_first_impl();
            first_.resize(original_non_terminal_count_);
            return std::move(first_);
        }
    }

    std::vector<std::unordered_set<size_t>> compute_first_set(const Grammar& grammar)
    {
        SetGenerator gen(grammar);
        return gen.compute_first();
    }
}
