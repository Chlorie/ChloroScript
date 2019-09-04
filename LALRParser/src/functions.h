#pragma once

#include "types.h"

namespace cls::lalr
{
    Grammar process_input(const std::string& text);
    std::vector<TableRow> generate_table(const Grammar& grammar);
}
