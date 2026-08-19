#include "com_init.h"
#include "commands.h"

#include <windows.h>

#include <exception>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    try
    {
        com_init init;

        std::vector<std::string_view> args;
        args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
        for (int index = 1; index < argc; ++index)
        {
            args.emplace_back(argv[index]);
        }

        // detect global flags
        bool verbose = false;
        std::vector<std::string_view> filtered;
        filtered.reserve(args.size());
        for (const auto &a : args) {
            if (a == "--verbose" || a == "-v") { verbose = true; }
            else { filtered.push_back(a); }
        }

        return run_command(std::span<const std::string_view>(filtered.data(), filtered.size()), verbose);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
