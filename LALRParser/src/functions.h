#pragma once

#include <unordered_set>
#include "types.h"

namespace cls::lalr
{
    Grammar process_input(const std::string& text);
    std::vector<std::unordered_set<size_t>> compute_first_set(const Grammar& grammar);
    std::vector<TableRow> generate_table(const Grammar& grammar);
    void generate_code(const std::string& file_path, const Grammar& grammar,
        const std::vector<TableRow>& table);
}
