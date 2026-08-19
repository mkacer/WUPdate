#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct UpdateInfo
{
    std::wstring title;
    std::vector<std::wstring> kb_articles;
    bool reboot_required{false};
    bool downloaded{false};
};

struct InstalledUpdateInfo
{
    std::wstring title;
    std::vector<std::wstring> kb_articles;
    SYSTEMTIME last_deployment_change_time{};
};

struct HistoryEntry
{
    SYSTEMTIME date{};
    long operation{0};
    long result_code{0};
    std::wstring title;
};

struct InstallResultInfo
{
    std::wstring title;
    long result_code{0};
    long hresult{0};
    bool reboot_required{false};
};

struct InstallSummary
{
    long download_result_code{0};
    long install_result_code{0};
    bool reboot_required{false};
    std::vector<InstallResultInfo> updates;
};
