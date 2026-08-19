#include "error.h"

#include <format>
#include <stdexcept>

namespace
{
std::string wide_to_utf8(const std::wstring_view value)
{
    if (value.empty())
    {
        return {};
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (required_size <= 0)
    {
        return {};
    }

    std::string result(static_cast<std::size_t>(required_size), '\0');
    const int converted = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required_size,
        nullptr,
        nullptr);

    if (converted <= 0)
    {
        return {};
    }

    return result;
}
} // namespace

std::string format_hresult(const HRESULT hr)
{
    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(
        flags,
        nullptr,
        static_cast<DWORD>(hr),
        0,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message;
    if (length > 0 && buffer != nullptr)
    {
        message.assign(buffer, length);
        LocalFree(buffer);

        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
        {
            message.pop_back();
        }

        return wide_to_utf8(message);
    }

    return "Unknown error";
}

void check_hr(const HRESULT hr, const std::string_view context)
{
    if (FAILED(hr))
    {
        throw std::runtime_error(
            std::format("{} failed with HRESULT 0x{:08X}: {}", context, static_cast<unsigned int>(hr), format_hresult(hr)));
    }
}
