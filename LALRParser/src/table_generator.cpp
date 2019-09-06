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
        struct MergeResult final
        {
            size_t merged_index = 0;
            bool updated = true;
        };

        struct Item final
        {
            size_t rule = 0;
            size_t dot = 0;
            std::unordered_set<size_t> lookahead;
            // Check if two items are the same LR(0) item
            bool lr0_equals(const Item& other) const { return rule == other.rule && dot == other.dot; }
            // Insert current item into a item set, lookahead set may be moved away
            MergeResult merge_into(std::vector<Item>& item_set) &&
            {
                const auto iter = std::find_if(item_set.begin(), item_set.end(),
                    [this](const Item& it) { return lr0_equals(it); });
                if (iter == item_set.end())
                {
                    item_set.emplace_back(Item{ rule, dot, std::move(lookahead) });
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
            static constexpr size_t epsilon = size_t(-1);
            std::vector<std::vector<Item>> item_sets_;
            std::vector<std::vector<Transition>> transitions_;
            const Grammar* grammar_;
            std::vector<std::unordered_set<size_t>> first_;
            std::vector<TableRow> table_;
            MergeResult merge_set(std::vector<Item>&& item_set);
            void apply_closure(std::vector<Item>& item_set) const;
            void compute_item_sets();
            void generate_table_impl();
        public:
            explicit TableGenerator(const Grammar& grammar) :grammar_(&grammar) {}
            std::vector<TableRow> generate_table();
        };

        MergeResult TableGenerator::merge_set(std::vector<Item>&& item_set)
        {
            for (size_t i = 0; i < item_sets_.size(); i++) // Test each existing item set
            {
                std::vector<Item>& target = item_sets_[i];
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
                const Rule& rule = grammar_->rules[item.rule];
                if (rule.terms.size() == item.dot // A -> BCD.
                    || std::holds_alternative<Terminal>(rule.terms[item.dot])) // A -> B.cD
                {
                    finished[index] = true;
                    continue;
                }
                const size_t nt_index = std::get<NonTerminal>(rule.terms[item.dot]).index; // A -> B.C
                std::unordered_set<size_t> lookahead = item.lookahead;
                for (size_t i = item.dot + 1; i < rule.terms.size(); i++)
                {
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
                    if (!contains(first_[nt.index], epsilon)) break;
                }
                finished[index] = true;
                for (size_t i = 0; i < grammar_->rules.size(); i++)
                    if (grammar_->rules[i].non_terminal_index == nt_index)
                    {
                        const MergeResult merge_result = Item{ i, 0, lookahead }.merge_into(item_set);
                        if (merge_result.merged_index == finished.size())
                            finished.emplace_back(false);
                        else if (merge_result.updated)
                            finished[merge_result.merged_index] = false;
                    }
            }
        }

        void TableGenerator::compute_item_sets()
        {
            const auto get_index = [](const Term& term)
            {
                return std::visit(Overload
                    {
                        [](const Terminal& t) { return TermIndex{ t.index, true }; },
                        [](const NonTerminal& t) { return TermIndex{ t.index, false }; },
                    }, term);
            };
            // Get the first item in: S' -> .S, $
            auto& first_item_set = item_sets_.emplace_back();
            Item first_item{ 0, 0,  { grammar_->token_types.size() } };
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
                // Skip reducing items
                for (size_t i = 0; i < processed.size(); i++)
                {
                    const Item& item = item_sets_[index][i];
                    const Rule& rule = grammar_->rules[item.rule];
                    if (rule.terms.size() == item.dot) processed[i] = true;
                }
                finished[index] = true;
                while (true)
                {
                    const auto iter = std::find(processed.begin(), processed.end(), false);
                    if (iter == processed.end()) break;
                    size_t i(iter - processed.begin());
                    const Item& primary_item = item_sets_[index][i];
                    const Rule& primary_rule = grammar_->rules[primary_item.rule];
                    if (primary_rule.terms.size() == primary_item.dot) continue; // No more shift
                    const TermIndex next_term = get_index(primary_rule.terms[primary_item.dot]);
                    std::vector<Item> new_set;
                    for (; i < processed.size(); i++)
                        if (!processed[i])
                        {
                            const Item& item = item_sets_[index][i];
                            const Rule& rule = grammar_->rules[item.rule];
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

        void TableGenerator::generate_table_impl()
        {

        }

        std::vector<TableRow> TableGenerator::generate_table()
        {
            first_ = compute_first_set(*grammar_);
            compute_item_sets();
            generate_table_impl();
            return std::move(table_);
        }
    }

    std::vector<TableRow> generate_table(const Grammar& grammar) { return TableGenerator(grammar).generate_table(); }
}
