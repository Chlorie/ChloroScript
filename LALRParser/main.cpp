#include <fmt/format.h>
#include <fstream>
#include <chrono>
#include "src/functions.h"

int main(const int argc, const char** argv)
{
    using namespace std::literals;
    using namespace cls::lalr;
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
    return 0;
}
