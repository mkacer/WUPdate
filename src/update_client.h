#pragma once

#include "models.h"

#include <vector>

class UpdateClient
{
public:
    explicit UpdateClient(bool verbose = false) noexcept;

    std::vector<UpdateInfo> search_available_updates() const;
    std::vector<InstalledUpdateInfo> list_installed_updates() const;
    std::vector<HistoryEntry> get_history() const;
    InstallSummary install_available_updates() const;
    InstallSummary install_selected_updates(const std::vector<int>& selected_indices) const;

    // Settings operations
    void settings_check() const;
    // confirm: must be true to proceed; restart_service: attempt to restart wuauserv after reset
    void settings_reset(bool confirm, bool restart_service = true) const;

private:
    bool verbose_ = false;
};
