#pragma once

#include <windows.h>

#include <string>
#include <string_view>

std::string format_hresult(HRESULT hr);
void check_hr(HRESULT hr, std::string_view context = "COM call");
