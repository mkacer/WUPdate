#include "commands.h"

#include "update_client.h"

#include <windows.h>

#include <format>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr std::string_view k_version = "0.1.0";

std::string to_utf8(const std::wstring_view value)
{
    if (value.empty())
    {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (required <= 0)
    {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required,
        nullptr,
        nullptr);
    return result;
}

std::string yes_no(const bool value)
{
    return value ? "Yes" : "No";
}

std::string format_system_time(const SYSTEMTIME& time)
{
    return std::format(
        "{:04}-{:02}-{:02} {:02}:{:02}",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute);
}

std::string join_kb_articles(const std::vector<std::wstring>& kb_articles)
{
    if (kb_articles.empty())
    {
        return "-";
    }

    std::string joined;
    for (std::size_t index = 0; index < kb_articles.size(); ++index)
    {
        if (index > 0)
        {
            joined += ", ";
        }

        const std::string kb = to_utf8(kb_articles[index]);
        if (kb.rfind("KB", 0) == 0)
        {
            joined += kb;
        }
        else
        {
            joined += "KB" + kb;
        }
    }

    return joined;
}

std::string operation_to_string(const long operation)
{
    switch (operation)
    {
    case 1:
        return "Install";
    case 2:
        return "Uninstall";
    default:
        return std::format("Other({})", operation);
    }
}

std::string result_code_to_string(const long result_code)
{
    switch (result_code)
    {
    case 0:
        return "NotStarted";
    case 1:
        return "InProgress";
    case 2:
        return "Succeeded";
    case 3:
        return "SucceededWithErrors";
    case 4:
        return "Failed";
    case 5:
        return "Aborted";
    default:
        return std::format("Unknown({})", result_code);
    }
}

void print_separator()
{
    std::cout << "--------------------------------------------------------------------------------\n";
}

void print_help()
{
    std::cout << "WUPdate - Windows Update CLI\n\n";
    std::cout << "Usage:\n";
    std::cout << "  WUPdate [--verbose] search\n";
    std::cout << "  WUPdate [--verbose] list\n";
    std::cout << "  WUPdate [--verbose] history\n";
    std::cout << "  WUPdate [--verbose] install [--all]\n";
    std::cout << "  WUPdate settings check\n";
    std::cout << "  WUPdate settings reset --confirm\n";
    std::cout << "  WUPdate --help\n";
    std::cout << "  WUPdate --version\n";
    std::cout << "Global flags:\n";
    std::cout << "  --verbose    Show more verbose progress and diagnostics\n";
}

int run_search(bool verbose)
{
    const UpdateClient client(verbose);
    const std::vector<UpdateInfo> updates = client.search_available_updates();

    std::cout << "Available Updates\n\n";
    print_separator();
    std::cout << std::format("{:<12} {:<11} {:<7} {}\n", "KB", "Downloaded", "Reboot", "Title");
    print_separator();

    for (const UpdateInfo& update : updates)
    {
        std::cout << std::format(
            "{:<12} {:<11} {:<7} {}\n",
            join_kb_articles(update.kb_articles),
            yes_no(update.downloaded),
            yes_no(update.reboot_required),
            to_utf8(update.title));
    }

    if (updates.empty())
    {
        std::cout << "No available updates.\n";
    }

    return 0;
}

int run_list(bool verbose)
{
    const UpdateClient client(verbose);
    const std::vector<InstalledUpdateInfo> updates = client.list_installed_updates();

    std::cout << "Installed Updates\n\n";
    print_separator();
    std::cout << std::format("{:<20} {:<16} {}\n", "Last Changed", "KB", "Title");
    print_separator();

    for (const InstalledUpdateInfo& update : updates)
    {
        std::cout << std::format(
            "{:<20} {:<16} {}\n",
            format_system_time(update.last_deployment_change_time),
            join_kb_articles(update.kb_articles),
            to_utf8(update.title));
    }

    if (updates.empty())
    {
        std::cout << "No installed updates found.\n";
    }

    return 0;
}

int run_history(bool verbose)
{
    const UpdateClient client(verbose);
    const std::vector<HistoryEntry> history = client.get_history();

    std::cout << "Update History\n\n";
    print_separator();
    std::cout << std::format("{:<20} {:<10} {:<20} {}\n", "Date", "Operation", "Result", "Title");
    print_separator();

    for (const HistoryEntry& entry : history)
    {
        std::cout << std::format(
            "{:<20} {:<10} {:<20} {}\n",
            format_system_time(entry.date),
            operation_to_string(entry.operation),
            result_code_to_string(entry.result_code),
            to_utf8(entry.title));
    }

    if (history.empty())
    {
        std::cout << "No update history entries found.\n";
    }

    return 0;
}

int run_install(bool verbose, const std::span<const std::string_view> args)
{
    const UpdateClient client(verbose);

    bool install_all = false;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--all")
        {
            install_all = true;
            continue;
        }
        if (args[i] == "--interactive" || args[i] == "-i")
        {
            continue; // accepted for backwards compatibility
        }

        std::cerr << std::format("Unknown install option: {}\n", args[i]);
        return 1;
    }

    if (install_all) {
        std::cout << "Searching for available updates...\n";
        const InstallSummary summary = client.install_available_updates();
        if (summary.updates.empty())
        {
            std::cout << "No available updates to install.\n";
            return 0;
        }

        std::cout << std::format("Found {} update(s).\n", summary.updates.size());
        std::cout << "Downloading updates...\n";
        std::cout << std::format("Download result: {}\n", result_code_to_string(summary.download_result_code));

        std::cout << "Installing updates...\n";
        std::cout << std::format("Install result: {}\n", result_code_to_string(summary.install_result_code));
        std::cout << std::format("Reboot required: {}\n\n", yes_no(summary.reboot_required));

        print_separator();
        std::cout << std::format("{:<10} {:<12} {:<7} {}\n", "Result", "HRESULT", "Reboot", "Title");
        print_separator();

        for (const InstallResultInfo& item : summary.updates)
        {
            std::cout << std::format(
                "{:<10} 0x{:08X} {:<7} {}\n",
                result_code_to_string(item.result_code),
                static_cast<unsigned long>(item.hresult),
                yes_no(item.reboot_required),
                to_utf8(item.title));
        }

        return 0;
    }

    // Interactive-by-default mode: prompt per update unless --all is passed.
    const std::vector<UpdateInfo> updates = client.search_available_updates();
    if (updates.empty()) {
        std::cout << "No available updates to select.\n";
        return 0;
    }

    std::vector<int> indices;
    indices.reserve(updates.size());
    std::cout << std::format("Found {} update(s). Review each update:\n", updates.size());

    for (std::size_t i = 0; i < updates.size(); ++i)
    {
        const UpdateInfo& update = updates[i];
        std::cout << std::format("[{:>2}/{}] {} (KB: {}, reboot: {})\n",
            i + 1,
            updates.size(),
            to_utf8(update.title),
            join_kb_articles(update.kb_articles),
            yes_no(update.reboot_required));

        while (true)
        {
            std::cout << "Install this update? [y/N]: ";
            std::string line;
            if (!std::getline(std::cin, line))
            {
                std::cerr << "Failed to read input.\n";
                return 1;
            }

            std::string normalized;
            normalized.reserve(line.size());
            for (char ch : line)
            {
                if (!std::isspace(static_cast<unsigned char>(ch)))
                {
                    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                }
            }

            if (normalized.empty() || normalized == "n" || normalized == "no")
            {
                break;
            }
            if (normalized == "y" || normalized == "yes")
            {
                indices.push_back(static_cast<int>(i));
                break;
            }

            std::cout << "Please answer 'y' or 'n'.\n";
        }
    }

    if (indices.empty())
    {
        std::cout << "No updates selected for installation.\n";
        return 0;
    }

    std::cout << std::format("Proceed to install {} selected update(s)? [y/N]: ", indices.size());
    std::string confirmation;
    if (!std::getline(std::cin, confirmation))
    {
        std::cerr << "Failed to read input.\n";
        return 1;
    }
    std::string normalized_confirmation;
    normalized_confirmation.reserve(confirmation.size());
    for (char ch : confirmation)
    {
        if (!std::isspace(static_cast<unsigned char>(ch)))
        {
            normalized_confirmation.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    if (normalized_confirmation != "y" && normalized_confirmation != "yes")
    {
        std::cout << "Installation canceled.\n";
        return 0;
    }

    const InstallSummary summary = client.install_selected_updates(indices);

    std::cout << std::format("Install result: {}\n", result_code_to_string(summary.install_result_code));
    std::cout << std::format("Reboot required: {}\n\n", yes_no(summary.reboot_required));
    print_separator();
    std::cout << std::format("{:<10} {:<12} {:<7} {}\n", "Result", "HRESULT", "Reboot", "Title");
    print_separator();
    for (const InstallResultInfo& item : summary.updates) {
        std::cout << std::format("{:<10} 0x{:08X} {:<7} {}\n", result_code_to_string(item.result_code), static_cast<unsigned long>(item.hresult), yes_no(item.reboot_required), to_utf8(item.title));
    }

    return 0;
}

int run_not_implemented(const std::string_view command)
{
    std::cout << std::format("{}: Not implemented\n", command);
    return 0;
}
} // namespace

int run_command(const std::span<const std::string_view> args, bool verbose)
{
    if (args.empty() || args[0] == "--help")
    {
        print_help();
        return 0;
    }

    if (args[0] == "--version")
    {
        std::cout << k_version << '\n';
        return 0;
    }

    if (args[0] == "search")
    {
        return run_search(verbose);
    }

    if (args[0] == "list")
    {
        return run_list(verbose);
    }

    if (args[0] == "history")
    {
        return run_history(verbose);
    }

    if (args[0] == "install")
    {
            return run_install(verbose, args);
    }

    if (args[0] == "settings")
    {
        if (args.size() == 1)
        {
            std::cout << "settings check    - show Windows Update related settings (policies and current)\n";
            std::cout << "settings reset --confirm - reset common Windows Update policies (requires admin)\n";
            return 0;
        }

        if (args[1] == "check")
        {
            UpdateClient client(verbose);
            client.settings_check();
            return 0;
        }

        if (args[1] == "reset")
        {
            if (args.size() > 2 && args[2] == "--confirm")
            {
                UpdateClient client(verbose);
                try {
                    client.settings_reset(true);
                } catch (const std::exception& ex) {
                    std::cerr << "Failed to reset settings: " << ex.what() << '\n';
                    return 1;
                }
                return 0;
            }

            std::cerr << "settings reset is potentially destructive. To proceed, pass: settings reset --confirm\n";
            return 1;
        }

        std::cerr << "Unknown settings subcommand: " << args[1] << '\n';
        return 1;
    }

    if (args[0] == "hide" || args[0] == "unhide" || args[0] == "uninstall" || args[0] == "scan")
    {
        return run_not_implemented(args[0]);
    }

    std::cerr << std::format("Unknown command: {}\n\n", args[0]);
    print_help();
    return 1;
}
