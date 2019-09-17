#include "functions.h"
#include <unordered_set>
#include <algorithm>
#include <numeric>
#include "utils.h"
#include "overload.h"

namespace cls::lalr
{
    using namespace utils;

    namespace
    {
        TermIndex get_index(const Term& term)
        {
            return std::visit(Overload
                {
                    [](const Terminal& t) { return TermIndex{ t.index, true }; },
                    [](const NonTerminal& t) { return TermIndex{ t.index, false }; },
                }, term);
        }

        std::string action_to_string(const Action& action)
        {
            char ch;
            switch (action.type)
            {
                case ActionType::shift: ch = 's'; break;
                case ActionType::reduce: ch = 'r'; break;
                case ActionType::accept: return "accept";
                case ActionType::error: ch = 'e'; break;
                default: return {};
            }
            return fmt::format("{}{}", ch, action.index);
        }

        struct MergeResult final
        {
            size_t merged_index = 0;
            bool updated = true;
        };

        struct Item final
        {
            size_t non_terminal = 0;
            size_t rule = 0;
            size_t dot = 0;
            std::unordered_set<size_t> lookahead;
            // Check if two items are the same LR(0) item
            bool lr0_equals(const Item& other) const
            {
                return non_terminal == other.non_terminal &&
                    rule == other.rule &&
                    dot == other.dot;
            }
            // Insert current item into a item set, lookahead set may be moved away
            MergeResult merge_into(std::vector<Item>& item_set) &&
            {
                const auto iter = std::find_if(item_set.begin(), item_set.end(),
                    [this](const Item& it) { return lr0_equals(it); });
                if (iter == item_set.end())
                {
                    item_set.emplace_back(Item{ non_terminal, rule, dot,
                        std::move(lookahead) });
                    return { item_set.size() - 1, true };
                }
                auto& target = iter->lookahead;
                bool updated = false;
                for (const size_t token : lookahead)
                    if (target.insert(token).second)
                        updated = true;
                const size_t merged_index(iter - item_set.begin());
                return { merged_index, updated };
            }
        };

        struct Transition final
        {
            TermIndex term;
            size_t dest_index = 0;
        };

        class TableGenerator final
        {
        private:
            static constexpr size_t epsilon = max_size;
            std::vector<std::vector<Item>> item_sets_;
            std::vector<std::vector<Transition>> transitions_;
            const Grammar& grammar_;
            std::vector<size_t> rule_total_;
            std::vector<std::unordered_set<size_t>> first_;
            std::vector<TableRow> table_;
            std::string error_msg_;
            const Rule& rule_of(const Item& item) const;
            MergeResult merge_set(std::vector<Item>&& item_set);
            void apply_closure(std::vector<Item>& item_set) const;
            bool is_reduce(const Item& item) const;
            void compute_item_sets();
            void initialize_table();
            std::string term_to_string(const TermIndex& term) const;
            std::string item_set_to_string(const std::vector<Item>& item_set) const;
            void fill_reduce();
            void fill_shift();
        public:
            explicit TableGenerator(const Grammar& grammar);
            std::vector<TableRow> generate_table();
        };

        const Rule& TableGenerator::rule_of(const Item& item) const
        {
            return grammar_.rules[item.non_terminal][item.rule];
        }

        MergeResult TableGenerator::merge_set(std::vector<Item>&& item_set)
        {
            for (const auto [i, target] : enumerate(item_sets_)) // Test each existing item set
            {
                if (!std::is_permutation(target.begin(), target.end(),
                    item_set.begin(), item_set.end(),
                    [](const Item& lhs, const Item& rhs) { return lhs.lr0_equals(rhs); }))
                    continue; // Not the same LR0 item set
                bool updated = false;
                for (Item& item : item_set)
                {
                    auto& match = *std::find_if(target.begin(), target.end(),
                        [&](const Item& it) { return it.lr0_equals(item); });
                    for (const size_t token : item.lookahead)
                        if (match.lookahead.insert(token).second) // Insert lookahead tokens
                            updated = true;
                }
                return { i, updated }; // Return insertion index
            }
            item_sets_.emplace_back(std::move(item_set)); // No match, add new item set
            return { item_sets_.size() - 1, true };
        }

        void TableGenerator::apply_closure(std::vector<Item>& item_set) const
        {
            std::vector<Bool> finished(item_set.size(), false);
            while (true)
            {
                const auto unfinished_iter = std::find(finished.begin(), finished.end(), false);
                if (unfinished_iter == finished.end()) break;
                const size_t index(unfinished_iter - finished.begin());
                const Item& item = item_set[index];
                const Rule& rule = rule_of(item);
                if (rule.terms.size() == item.dot // A -> BCD.
                    || std::holds_alternative<Terminal>(rule.terms[item.dot])) // A -> B.cD
                {
                    finished[index] = true;
                    continue;
                }
                const size_t nt_index = std::get<NonTerminal>(rule.terms[item.dot]).index; // A -> B.C
                std::unordered_set<size_t> lookahead;
                bool all_nullable = true;
                for (size_t i = item.dot + 1; i < rule.terms.size(); i++)
                {
                    all_nullable = false;
                    const Term& term = rule.terms[i];
                    if (const Terminal * t = std::get_if<Terminal>(&term))
                    {
                        lookahead.insert(t->index);
                        break;
                    }
                    const NonTerminal& nt = std::get<NonTerminal>(term);
                    for (const size_t token : first_[nt.index])
                        if (token != epsilon)
                            lookahead.insert(token);
                        else
                            all_nullable = true;
                    if (!all_nullable) break;
                }
                if (all_nullable)
                    for (const size_t token : item.lookahead)
                        lookahead.insert(token);
                finished[index] = true;
                for (const auto [i, r] : enumerate(grammar_.rules[nt_index]))
                {
                    const MergeResult merge_result =
                        Item{ nt_index, i, 0, lookahead }.merge_into(item_set);
                    if (merge_result.merged_index == finished.size())
                        finished.emplace_back(false);
                    else if (merge_result.updated)
                        finished[merge_result.merged_index] = false;
                }
            }
        }

        bool TableGenerator::is_reduce(const Item& item) const { return rule_of(item).terms.size() == item.dot; }

        void TableGenerator::compute_item_sets()
        {
            auto& first_item_set = item_sets_.emplace_back();
            Item first_item{ 0, 0, 0,  { grammar_.token_types.size() - 1 } };
            first_item_set.emplace_back(std::move(first_item));
            apply_closure(first_item_set);
            transitions_.emplace_back();
            std::vector<Bool> finished{ false };
            while (true)
            {
                const auto unfinished_iter = std::find(finished.begin(), finished.end(), false);
                if (unfinished_iter == finished.end()) break;
                const size_t index(unfinished_iter - finished.begin());
                std::vector<Bool> processed(item_sets_[index].size());
                std::transform(item_sets_[index].begin(), item_sets_[index].end(),
                    processed.begin(), [this](const Item& item) { return is_reduce(item); });
                finished[index] = true;
                while (true)
                {
                    const auto iter = std::find(processed.begin(), processed.end(), false);
                    if (iter == processed.end()) break;
                    size_t i(iter - processed.begin());
                    const Item& primary_item = item_sets_[index][i];
                    const Rule& primary_rule = grammar_.rules[primary_item.non_terminal][primary_item.rule];
                    if (primary_rule.terms.size() == primary_item.dot) continue; // No more shift
                    const TermIndex next_term = get_index(primary_rule.terms[primary_item.dot]);
                    std::vector<Item> new_set;
                    for (; i < processed.size(); i++)
                        if (!processed[i])
                        {
                            const Item& item = item_sets_[index][i];
                            const Rule& rule = grammar_.rules[item.non_terminal][item.rule];
                            if (get_index(rule.terms[item.dot]) != next_term) continue;
                            Item new_item = item;
                            new_item.dot++;
                            std::move(new_item).merge_into(new_set);
                            processed[i] = true;
                        }
                    apply_closure(new_set);
                    const MergeResult merge_result = merge_set(std::move(new_set));
                    transitions_[index].emplace_back(Transition{ next_term, merge_result.merged_index });
                    if (merge_result.merged_index == finished.size())
                    {
                        transitions_.emplace_back();
                        finished.emplace_back(false);
                    }
                    else if (merge_result.updated)
                        finished[merge_result.merged_index] = false;
                }
            }
        }

        void TableGenerator::initialize_table()
        {
            table_.resize(item_sets_.size());
            for (TableRow& row : table_)
            {
                row.actions.resize(grammar_.token_types.size());
                row.go_to = std::vector<size_t>(grammar_.non_terminals.size(), TableRow::no_goto);
            }
        }

        std::string TableGenerator::term_to_string(const TermIndex& term) const
        {
            const auto [index, is_terminal] = term;
            if (is_terminal)
            {
                const TokenType& token_type = grammar_.token_types[index];
                if (token_type.enumerator)
                    return fmt::format("{}.{}", token_type.type_name, *token_type.enumerator);
                return token_type.type_name;
            }
            return grammar_.non_terminals[index];
        }

        std::string TableGenerator::item_set_to_string(const std::vector<Item>& item_set) const
        {
            std::string result;
            for (const Item& item : item_set)
            {
                const Rule& rule = rule_of(item);
                result += fmt::format("  {} ->", grammar_.non_terminals[item.non_terminal]);
                for (const auto [i, term] : enumerate(rule.terms))
                {
                    if (i == item.dot) result += " .";
                    result += ' ';
                    result += term_to_string(get_index(term));
                }
                result += ", ";
                for (const auto [i, token] : enumerate(item.lookahead))
                {
                    if (i != 0) result += '/';
                    result += term_to_string({ token, true });
                }
                result += '\n';
            }
            return result;
        }

        void TableGenerator::fill_reduce()
        {
            for (const auto [i, item_set] : enumerate(std::as_const(item_sets_)))
                for (const Item& item : item_set)
                {
                    if (!is_reduce(item)) continue;
                    for (const size_t token : item.lookahead)
                    {
                        const Action new_action = item.non_terminal == 0 ?
                            Action{ ActionType::accept, 0 } :
                            Action{ ActionType::reduce, item.rule + rule_total_[item.non_terminal] };
                        Action& action = table_[i].actions[token];
                        if (action.type != ActionType::error) // R-R conflict
                            error_msg_ += fmt::format("Reduce-reduce conflict in item set "
                                "I{}:\n{}when parsing token {}, conflicting actions are "
                                "{}, {}\n\n", i, item_set_to_string(item_sets_[i]),
                                term_to_string({ token, true }),
                                action_to_string(action), action_to_string(new_action));
                        action = new_action;
                    }
                }
        }

        void TableGenerator::fill_shift()
        {
            for (const auto [i, transitions] : enumerate(std::as_const(transitions_)))
                for (const Transition& transition : transitions)
                {
                    if (!transition.term.is_terminal) // Goto
                        table_[i].go_to[transition.term.index] = transition.dest_index;
                    else // Shift
                    {
                        const size_t token = transition.term.index;
                        const Action new_action{ ActionType::shift, transition.dest_index };
                        Action& action = table_[i].actions[token];
                        if (new_action == action) continue;
                        if (action.type != ActionType::error) // S-R conflict
                            error_msg_ += fmt::format("Shift-reduce conflict in item set "
                                "I{}:\n{}when parsing token {}, conflicting actions are "
                                "{}, {}\n\n", i, item_set_to_string(item_sets_[i]),
                                term_to_string({ token, true }),
                                action_to_string(action), action_to_string(new_action));
                        action = new_action;
                    }
                }
        }

        TableGenerator::TableGenerator(const Grammar& grammar) :
            grammar_(grammar)
        {
            std::exclusive_scan(grammar_.rules.begin(), grammar_.rules.end(),
                std::back_inserter(rule_total_), 0,
                [](const size_t lhs, const auto& rhs) { return lhs + rhs.size(); });
        }

        std::vector<TableRow> TableGenerator::generate_table()
        {
            first_ = compute_first_set(grammar_);
            compute_item_sets();
            initialize_table();
            fill_reduce();
            fill_shift();
            if (!error_msg_.empty()) error("{}", std::move(error_msg_));
            return std::move(table_);
        }
    }

    std::vector<TableRow> generate_table(const Grammar& grammar) { return TableGenerator(grammar).generate_table(); }
}
