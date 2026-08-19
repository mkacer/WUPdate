#pragma once

#include <span>
#include <string_view>

int run_command(std::span<const std::string_view> args, bool verbose = false);
