#include "update_client.h"

#include <wuapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <optional>
#include <iostream>
#include <windows.h>

#include "error.h"
#include <chrono>

UpdateClient::UpdateClient(bool verbose) noexcept : verbose_(verbose) {}

using Microsoft::WRL::ComPtr;

// Local helpers used for verbose output inside this translation unit.
static std::string to_utf8_local(const std::wstring& w)
{
    if (w.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s; s.resize(size);
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), size, nullptr, nullptr);
    return s;
}

static std::string join_kb_articles_local(const std::vector<std::wstring>& kb_articles)
{
    if (kb_articles.empty()) return "-";
    std::string joined;
    for (size_t i = 0; i < kb_articles.size(); ++i) {
        if (i) joined += ", ";
        const std::wstring& w = kb_articles[i];
        std::string s = to_utf8_local(w);
        if (s.rfind("KB", 0) == 0) joined += s; else joined += std::string("KB") + s;
    }
    return joined;
}

namespace
{
class bstr_value
{
public:
    explicit bstr_value(const std::wstring_view text)
    {
        value_ = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
        if (value_ == nullptr)
        {
            throw std::runtime_error("SysAllocStringLen failed");
        }
    }

    ~bstr_value()
    {
        SysFreeString(value_);
    }

    BSTR get() const
    {
        return value_;
    }

    bstr_value(const bstr_value&) = delete;
    bstr_value& operator=(const bstr_value&) = delete;

private:
    BSTR value_{nullptr};
};

std::wstring from_bstr(BSTR value)
{
    if (value == nullptr)
    {
        return {};
    }

    return std::wstring(value, SysStringLen(value));
}

std::vector<std::wstring> get_kb_articles(IUpdate* update)
{
    ComPtr<IStringCollection> kb_collection;
    check_hr(update->get_KBArticleIDs(kb_collection.GetAddressOf()), "IUpdate::get_KBArticleIDs");

    LONG count = 0;
    check_hr(kb_collection->get_Count(&count), "IStringCollection::get_Count");

    std::vector<std::wstring> kb_articles;
    kb_articles.reserve(static_cast<std::size_t>(count));

    for (LONG index = 0; index < count; ++index)
    {
        BSTR value = nullptr;
        check_hr(kb_collection->get_Item(index, &value), "IStringCollection::get_Item");
        kb_articles.push_back(from_bstr(value));
        SysFreeString(value);
    }

    return kb_articles;
}

ComPtr<IUpdateSession> create_session()
{
    CLSID session_clsid{};
    check_hr(CLSIDFromProgID(L"Microsoft.Update.Session", &session_clsid), "CLSIDFromProgID(Microsoft.Update.Session)");

    ComPtr<IUpdateSession> session;
    check_hr(
        CoCreateInstance(
            session_clsid,
            nullptr,
            CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
            __uuidof(IUpdateSession),
            reinterpret_cast<void**>(session.GetAddressOf())),
        "CoCreateInstance(IUpdateSession)");

    return session;
}

ComPtr<IUpdateSearcher> create_searcher(IUpdateSession* session)
{
    ComPtr<IUpdateSearcher> searcher;
    check_hr(session->CreateUpdateSearcher(searcher.GetAddressOf()), "IUpdateSession::CreateUpdateSearcher");
    return searcher;
}

ComPtr<IUpdateCollection> search_collection(const std::wstring_view criteria)
{
    const ComPtr<IUpdateSession> session = create_session();
    const ComPtr<IUpdateSearcher> searcher = create_searcher(session.Get());

    bstr_value query(criteria);
    ComPtr<ISearchResult> result;
    check_hr(searcher->Search(query.get(), result.GetAddressOf()), "IUpdateSearcher::Search");

    ComPtr<IUpdateCollection> updates;
    check_hr(result->get_Updates(updates.GetAddressOf()), "ISearchResult::get_Updates");
    return updates;
}

ComPtr<IUpdateCollection> create_update_collection()
{
    CLSID collection_clsid{};
    check_hr(CLSIDFromProgID(L"Microsoft.Update.UpdateColl", &collection_clsid), "CLSIDFromProgID(Microsoft.Update.UpdateColl)");

    ComPtr<IUpdateCollection> collection;
    check_hr(
        CoCreateInstance(
            collection_clsid,
            nullptr,
            CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
            __uuidof(IUpdateCollection),
            reinterpret_cast<void**>(collection.GetAddressOf())),
        "CoCreateInstance(IUpdateCollection)");

    return collection;
}

std::wstring get_update_title(IUpdate* update)
{
    BSTR title = nullptr;
    check_hr(update->get_Title(&title), "IUpdate::get_Title");
    const std::wstring value = from_bstr(title);
    SysFreeString(title);
    return value;
}
} // namespace

std::vector<UpdateInfo> UpdateClient::search_available_updates() const
{
    if (verbose_) std::cout << "[verbose] Starting search: IsInstalled=0\n";
    const auto t0 = std::chrono::steady_clock::now();

    const ComPtr<IUpdateCollection> updates = search_collection(L"IsInstalled=0");

    LONG count = 0;
    check_hr(updates->get_Count(&count), "IUpdateCollection::get_Count");

    if (verbose_) std::cout << "[verbose] Search returned candidate count: " << count << "\n";

    std::vector<UpdateInfo> result;
    result.reserve(static_cast<std::size_t>(count));

    for (LONG index = 0; index < count; ++index)
    {
        ComPtr<IUpdate> update;
        check_hr(updates->get_Item(index, update.GetAddressOf()), "IUpdateCollection::get_Item");

#if defined(_MSC_VER)
        ComPtr<IUpdate2> update2;
        check_hr(update.As(&update2), "QueryInterface(IUpdate2)");

        VARIANT_BOOL reboot_required = VARIANT_FALSE;
        check_hr(update2->get_RebootRequired(&reboot_required), "IUpdate2::get_RebootRequired");
#else
        VARIANT_BOOL reboot_required = VARIANT_FALSE;
#endif

        VARIANT_BOOL downloaded = VARIANT_FALSE;
        check_hr(update->get_IsDownloaded(&downloaded), "IUpdate::get_IsDownloaded");

        UpdateInfo info{};
        info.title = get_update_title(update.Get());
        info.kb_articles = get_kb_articles(update.Get());
        info.reboot_required = reboot_required == VARIANT_TRUE;
        info.downloaded = downloaded == VARIANT_TRUE;

        if (verbose_) {
                    std::cout << "[verbose] Found update: " << to_utf8_local(info.title) << " KBs=" << join_kb_articles_local(info.kb_articles) << " downloaded=" << (info.downloaded ? "1" : "0") << " reboot=" << (info.reboot_required ? "1" : "0") << "\n";
        }

        result.push_back(std::move(info));
    }

    if (verbose_) {
        const auto t1 = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << "[verbose] Search completed in " << ms << " ms\n";
    }

    return result;
}

std::vector<InstalledUpdateInfo> UpdateClient::list_installed_updates() const
{
    const ComPtr<IUpdateCollection> updates = search_collection(L"IsInstalled=1");

    LONG count = 0;
    check_hr(updates->get_Count(&count), "IUpdateCollection::get_Count");

    std::vector<InstalledUpdateInfo> result;
    result.reserve(static_cast<std::size_t>(count));

    for (LONG index = 0; index < count; ++index)
    {
        ComPtr<IUpdate> update;
        check_hr(updates->get_Item(index, update.GetAddressOf()), "IUpdateCollection::get_Item");

        DATE deployment_time = 0;
        check_hr(update->get_LastDeploymentChangeTime(&deployment_time), "IUpdate::get_LastDeploymentChangeTime");

        InstalledUpdateInfo info{};
        info.title = get_update_title(update.Get());
        info.kb_articles = get_kb_articles(update.Get());
        if (VariantTimeToSystemTime(deployment_time, &info.last_deployment_change_time) == 0)
        {
            ZeroMemory(&info.last_deployment_change_time, sizeof(info.last_deployment_change_time));
        }
        result.push_back(std::move(info));
    }

    return result;
}

std::vector<HistoryEntry> UpdateClient::get_history() const
{
    const ComPtr<IUpdateSession> session = create_session();
    const ComPtr<IUpdateSearcher> searcher = create_searcher(session.Get());

    LONG total = 0;
    check_hr(searcher->GetTotalHistoryCount(&total), "IUpdateSearcher::GetTotalHistoryCount");
    if (total <= 0)
    {
        return {};
    }

    ComPtr<IUpdateHistoryEntryCollection> history;
    check_hr(searcher->QueryHistory(0, total, history.GetAddressOf()), "IUpdateSearcher::QueryHistory");

    LONG count = 0;
    check_hr(history->get_Count(&count), "IUpdateHistoryEntryCollection::get_Count");

    std::vector<HistoryEntry> entries;
    entries.reserve(static_cast<std::size_t>(count));

    for (LONG index = 0; index < count; ++index)
    {
        ComPtr<IUpdateHistoryEntry> entry;
        check_hr(history->get_Item(index, entry.GetAddressOf()), "IUpdateHistoryEntryCollection::get_Item");

        DATE date_value = 0;
        check_hr(entry->get_Date(&date_value), "IUpdateHistoryEntry::get_Date");

        BSTR title = nullptr;
        check_hr(entry->get_Title(&title), "IUpdateHistoryEntry::get_Title");

        UpdateOperation operation = uoInstallation;
        check_hr(entry->get_Operation(&operation), "IUpdateHistoryEntry::get_Operation");

        OperationResultCode result_code = orcNotStarted;
        check_hr(entry->get_ResultCode(&result_code), "IUpdateHistoryEntry::get_ResultCode");

        HistoryEntry item{};
        if (VariantTimeToSystemTime(date_value, &item.date) == 0)
        {
            ZeroMemory(&item.date, sizeof(item.date));
        }
        item.operation = static_cast<long>(operation);
        item.result_code = static_cast<long>(result_code);
        item.title = from_bstr(title);
        SysFreeString(title);
        entries.push_back(std::move(item));
    }

    return entries;
}

InstallSummary UpdateClient::install_available_updates() const
{
    if (verbose_) std::cout << "[verbose] install: searching for available updates (IsInstalled=0)\n";
    const auto t_start = std::chrono::steady_clock::now();

    const ComPtr<IUpdateSession> session = create_session();
    const ComPtr<IUpdateSearcher> searcher = create_searcher(session.Get());

    bstr_value search_query(L"IsInstalled=0");
    ComPtr<ISearchResult> search_result;
    check_hr(searcher->Search(search_query.get(), search_result.GetAddressOf()), "IUpdateSearcher::Search");

    ComPtr<IUpdateCollection> available_updates;
    check_hr(search_result->get_Updates(available_updates.GetAddressOf()), "ISearchResult::get_Updates");

    LONG available_count = 0;
    check_hr(available_updates->get_Count(&available_count), "IUpdateCollection::get_Count");
    if (available_count == 0)
    {
        if (verbose_) std::cout << "[verbose] No available updates found.\n";
        return {};
    }

    if (verbose_) std::cout << "[verbose] " << available_count << " updates available. Building install collection.\n";

    const ComPtr<IUpdateCollection> install_collection = create_update_collection();
    for (LONG index = 0; index < available_count; ++index)
    {
        ComPtr<IUpdate> update;
        check_hr(available_updates->get_Item(index, update.GetAddressOf()), "IUpdateCollection::get_Item");

        BSTR title = nullptr;
        check_hr(update->get_Title(&title), "IUpdate::get_Title");
        const std::wstring wtitle = from_bstr(title);
        SysFreeString(title);

        VARIANT_BOOL eula_accepted = VARIANT_FALSE;
        check_hr(update->get_EulaAccepted(&eula_accepted), "IUpdate::get_EulaAccepted");
        if (eula_accepted != VARIANT_TRUE)
        {
            check_hr(update->AcceptEula(), "IUpdate::AcceptEula");
            if (verbose_) std::cout << "[verbose] Accepted EULA for: " << to_utf8_local(wtitle) << "\n";
        }

        if (verbose_) std::cout << "[verbose] Adding to install collection: " << to_utf8_local(wtitle) << "\n";

        LONG added_index = 0;
        check_hr(install_collection->Add(update.Get(), &added_index), "IUpdateCollection::Add");
    }

    if (verbose_) std::cout << "[verbose] Creating downloader and starting download...\n";
    ComPtr<IUpdateDownloader> downloader;
    check_hr(session->CreateUpdateDownloader(downloader.GetAddressOf()), "IUpdateSession::CreateUpdateDownloader");
    check_hr(downloader->put_Updates(install_collection.Get()), "IUpdateDownloader::put_Updates");

    const auto t_download_start = std::chrono::steady_clock::now();
    ComPtr<IDownloadResult> download_result;
    check_hr(downloader->Download(download_result.GetAddressOf()), "IUpdateDownloader::Download");
    const auto t_download_end = std::chrono::steady_clock::now();

    OperationResultCode download_code = orcNotStarted;
    check_hr(download_result->get_ResultCode(&download_code), "IDownloadResult::get_ResultCode");
    if (verbose_) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_download_end - t_download_start).count();
        std::cout << "[verbose] Download finished (" << ms << " ms). Result: " << static_cast<int>(download_code) << "\n";
    }

    if (verbose_) std::cout << "[verbose] Creating installer and starting install...\n";
    ComPtr<IUpdateInstaller> installer;
    check_hr(session->CreateUpdateInstaller(installer.GetAddressOf()), "IUpdateSession::CreateUpdateInstaller");
    check_hr(installer->put_Updates(install_collection.Get()), "IUpdateInstaller::put_Updates");

    const auto t_install_start = std::chrono::steady_clock::now();
    ComPtr<IInstallationResult> installation_result;
    check_hr(installer->Install(installation_result.GetAddressOf()), "IUpdateInstaller::Install");
    const auto t_install_end = std::chrono::steady_clock::now();

    OperationResultCode install_code = orcNotStarted;
    check_hr(installation_result->get_ResultCode(&install_code), "IInstallationResult::get_ResultCode");

    VARIANT_BOOL reboot_required = VARIANT_FALSE;
    check_hr(installation_result->get_RebootRequired(&reboot_required), "IInstallationResult::get_RebootRequired");

    LONG installed_count = 0;
    check_hr(install_collection->get_Count(&installed_count), "IUpdateCollection::get_Count");

    InstallSummary summary{};
    summary.download_result_code = static_cast<long>(download_code);
    summary.install_result_code = static_cast<long>(install_code);
    summary.reboot_required = reboot_required == VARIANT_TRUE;
    summary.updates.reserve(static_cast<std::size_t>(installed_count));

    for (LONG index = 0; index < installed_count; ++index)
    {
        ComPtr<IUpdate> update;
        check_hr(install_collection->get_Item(index, update.GetAddressOf()), "IUpdateCollection::get_Item");

        ComPtr<IUpdateInstallationResult> update_result;
        check_hr(installation_result->GetUpdateResult(index, update_result.GetAddressOf()), "IInstallationResult::GetUpdateResult");

        OperationResultCode result_code = orcNotStarted;
        check_hr(update_result->get_ResultCode(&result_code), "IUpdateInstallationResult::get_ResultCode");

        HRESULT update_hr = S_OK;
        check_hr(update_result->get_HResult(&update_hr), "IUpdateInstallationResult::get_HResult");

        VARIANT_BOOL update_reboot_required = VARIANT_FALSE;
        check_hr(update_result->get_RebootRequired(&update_reboot_required), "IUpdateInstallationResult::get_RebootRequired");

        InstallResultInfo item{};
        item.title = get_update_title(update.Get());
        item.result_code = static_cast<long>(result_code);
        item.hresult = static_cast<long>(update_hr);
        item.reboot_required = update_reboot_required == VARIANT_TRUE;
        summary.updates.push_back(std::move(item));

        if (verbose_) {
                    std::cout << "[verbose] Update installed: " << to_utf8_local(summary.updates.back().title) << " result=" << result_code << " hr=0x" << std::hex << summary.updates.back().hresult << std::dec << " reboot=" << (summary.updates.back().reboot_required ? "1" : "0") << "\n";
        }
    }

    if (verbose_) {
        const auto t_end = std::chrono::steady_clock::now();
        const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
        const auto install_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_install_end - t_install_start).count();
        const auto download_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_download_end - t_download_start).count();
        std::cout << "[verbose] Install flow completed: total=" << total_ms << " ms, download=" << download_ms << " ms, install=" << install_ms << " ms\n";
    }

    return summary;
}

InstallSummary UpdateClient::install_selected_updates(const std::vector<int>& selected_indices) const
{
    if (verbose_) std::cout << "[verbose] install_selected: searching for available updates (IsInstalled=0)\n";

    const ComPtr<IUpdateSession> session = create_session();
    const ComPtr<IUpdateSearcher> searcher = create_searcher(session.Get());

    bstr_value search_query(L"IsInstalled=0");
    ComPtr<ISearchResult> search_result;
    check_hr(searcher->Search(search_query.get(), search_result.GetAddressOf()), "IUpdateSearcher::Search");

    ComPtr<IUpdateCollection> available_updates;
    check_hr(search_result->get_Updates(available_updates.GetAddressOf()), "ISearchResult::get_Updates");

    LONG available_count = 0;
    check_hr(available_updates->get_Count(&available_count), "IUpdateCollection::get_Count");
    if (available_count == 0)
    {
        return {};
    }

    const ComPtr<IUpdateCollection> install_collection = create_update_collection();

    for (int sel : selected_indices)
    {
        if (sel < 0 || sel >= available_count) {
            std::cout << "Skipping invalid index: " << sel << "\n";
            continue;
        }

        ComPtr<IUpdate> update;
        check_hr(available_updates->get_Item(static_cast<LONG>(sel), update.GetAddressOf()), "IUpdateCollection::get_Item");

        VARIANT_BOOL eula_accepted = VARIANT_FALSE;
        check_hr(update->get_EulaAccepted(&eula_accepted), "IUpdate::get_EulaAccepted");
        if (eula_accepted != VARIANT_TRUE)
        {
            check_hr(update->AcceptEula(), "IUpdate::AcceptEula");
            if (verbose_) {
                BSTR t = nullptr; update->get_Title(&t); std::wstring wt(t, SysStringLen(t)); SysFreeString(t);
                std::cout << "[verbose] Accepted EULA for: " << to_utf8_local(wt) << "\n";
            }
        }

        LONG added_index = 0;
        check_hr(install_collection->Add(update.Get(), &added_index), "IUpdateCollection::Add");
    }

    ComPtr<IUpdateDownloader> downloader;
    check_hr(session->CreateUpdateDownloader(downloader.GetAddressOf()), "IUpdateSession::CreateUpdateDownloader");
    check_hr(downloader->put_Updates(install_collection.Get()), "IUpdateDownloader::put_Updates");

    ComPtr<IDownloadResult> download_result;
    check_hr(downloader->Download(download_result.GetAddressOf()), "IUpdateDownloader::Download");

    OperationResultCode download_code = orcNotStarted;
    check_hr(download_result->get_ResultCode(&download_code), "IDownloadResult::get_ResultCode");

    ComPtr<IUpdateInstaller> installer;
    check_hr(session->CreateUpdateInstaller(installer.GetAddressOf()), "IUpdateSession::CreateUpdateInstaller");
    check_hr(installer->put_Updates(install_collection.Get()), "IUpdateInstaller::put_Updates");

    ComPtr<IInstallationResult> installation_result;
    check_hr(installer->Install(installation_result.GetAddressOf()), "IUpdateInstaller::Install");

    OperationResultCode install_code = orcNotStarted;
    check_hr(installation_result->get_ResultCode(&install_code), "IInstallationResult::get_ResultCode");

    VARIANT_BOOL reboot_required = VARIANT_FALSE;
    check_hr(installation_result->get_RebootRequired(&reboot_required), "IInstallationResult::get_RebootRequired");

    LONG installed_count = 0;
    check_hr(install_collection->get_Count(&installed_count), "IUpdateCollection::get_Count");

    InstallSummary summary{};
    summary.download_result_code = static_cast<long>(download_code);
    summary.install_result_code = static_cast<long>(install_code);
    summary.reboot_required = reboot_required == VARIANT_TRUE;
    summary.updates.reserve(static_cast<std::size_t>(installed_count));

    for (LONG index = 0; index < installed_count; ++index)
    {
        ComPtr<IUpdate> update;
        check_hr(install_collection->get_Item(index, update.GetAddressOf()), "IUpdateCollection::get_Item");

        ComPtr<IUpdateInstallationResult> update_result;
        check_hr(installation_result->GetUpdateResult(index, update_result.GetAddressOf()), "IInstallationResult::GetUpdateResult");

        OperationResultCode result_code = orcNotStarted;
        check_hr(update_result->get_ResultCode(&result_code), "IUpdateInstallationResult::get_ResultCode");

        HRESULT update_hr = S_OK;
        check_hr(update_result->get_HResult(&update_hr), "IUpdateInstallationResult::get_HResult");

        VARIANT_BOOL update_reboot_required = VARIANT_FALSE;
        check_hr(update_result->get_RebootRequired(&update_reboot_required), "IUpdateInstallationResult::get_RebootRequired");

        InstallResultInfo item{};
        item.title = get_update_title(update.Get());
        item.result_code = static_cast<long>(result_code);
        item.hresult = static_cast<long>(update_hr);
        item.reboot_required = update_reboot_required == VARIANT_TRUE;
        summary.updates.push_back(std::move(item));

        if (verbose_) {
            std::cout << "[verbose] Update installed: " << to_utf8_local(summary.updates.back().title) << " result=" << result_code << " hr=0x" << std::hex << summary.updates.back().hresult << std::dec << " reboot=" << (summary.updates.back().reboot_required ? "1" : "0") << "\n";
        }
    }

    return summary;
}

// --- Settings operations ---------------------------------------------------

namespace {
    std::string to_utf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int size = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s; s.resize(size);
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), size, nullptr, nullptr);
        return s;
    }

    std::wstring read_reg_string(HKEY root, const std::wstring& subkey, const std::wstring& valueName)
    {
        HKEY hKey{};
        if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return L"";
        }
        DWORD type = 0;
        DWORD cb = 0;
        LONG rc = RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type, nullptr, &cb);
        if (rc != ERROR_SUCCESS || type != REG_SZ) { RegCloseKey(hKey); return L""; }
        std::wstring buf; buf.resize(cb / sizeof(wchar_t));
        rc = RegQueryValueExW(hKey, valueName.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(buf.data()), &cb);
        RegCloseKey(hKey);
        if (rc != ERROR_SUCCESS) return L"";
        if (!buf.empty() && buf.back() == L'\0') buf.pop_back();
        return buf;
    }

    std::optional<DWORD> read_reg_dword(HKEY root, const std::wstring& subkey, const std::wstring& valueName)
    {
        HKEY hKey{};
        if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return std::nullopt;
        }
        DWORD type = 0;
        DWORD cb = sizeof(DWORD);
        DWORD v = 0;
        LONG rc = RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(&v), &cb);
        RegCloseKey(hKey);
        if (rc != ERROR_SUCCESS || (type != REG_DWORD && type != REG_QWORD)) return std::nullopt;
        return v;
    }
}

void UpdateClient::settings_check() const
{
    std::wcout << L"Windows Update settings check:\n";

    std::wstring policiesWU = L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate";
    std::wstring policiesAU = L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU";

    auto print = [&](const std::wstring& label, const std::wstring& value) {
        std::cout << to_utf8(label) << ": " << to_utf8(value.empty() ? L"<not set>" : value) << "\n";
    };

    auto wuserver = read_reg_string(HKEY_LOCAL_MACHINE, policiesWU, L"WUServer");
    auto wustatus = read_reg_string(HKEY_LOCAL_MACHINE, policiesWU, L"WUStatusServer");
    auto usewuserver = read_reg_dword(HKEY_LOCAL_MACHINE, policiesAU, L"UseWUServer");

    print(L"Policy: WUServer", wuserver);
    print(L"Policy: WUStatusServer", wustatus);
    if (usewuserver) {
        print(L"Policy: UseWUServer", std::to_wstring(*usewuserver));
    } else {
        print(L"Policy: UseWUServer", L"<not set>");
    }

    std::wstring au = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update";
    auto auOptions = read_reg_dword(HKEY_LOCAL_MACHINE, au, L"AUOptions");
    if (auOptions) print(L"Auto Update: AUOptions", std::to_wstring(*auOptions)); else print(L"Auto Update: AUOptions", L"<not set>");

    // Service info
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, GENERIC_READ);
    if (scm) {
        SC_HANDLE svc = OpenServiceW(scm, L"wuauserv", SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
        if (svc) {
            SERVICE_STATUS_PROCESS ssp{};
            DWORD needed = 0;
            if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) {
                std::wcout << L"Service wuauserv state: " << ssp.dwCurrentState << L"\n";
            }
            CloseServiceHandle(svc);
        }
        CloseServiceHandle(scm);
    }
}

static void restart_service_by_name(const std::wstring& serviceName)
{
    const std::string service_name_utf8 = to_utf8(serviceName);

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        std::cout << "[settings] Failed to open SCM\n";
        return;
    }
    SC_HANDLE svc = OpenServiceW(scm, serviceName.c_str(), SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) {
        std::cout << "[settings] Service not found or insufficient privileges: " << service_name_utf8 << "\n";
        CloseServiceHandle(scm);
        return;
    }

    SERVICE_STATUS_PROCESS ssp{};
    DWORD needed = 0;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) {
        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            std::cout << "[settings] Service " << service_name_utf8 << " is already stopped\n";
        } else {
            // request stop
            SERVICE_STATUS status{};
            if (ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
                std::cout << "[settings] Sent stop to service " << service_name_utf8 << "\n";
                // wait for stop
                for (int i = 0; i < 30; ++i) {
                    if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) break;
                    if (ssp.dwCurrentState == SERVICE_STOPPED) break;
                    Sleep(500);
                }
            } else {
                std::cout << "[settings] Failed to send stop to service\n";
            }
        }
    }

    // start service
    if (StartServiceW(svc, 0, nullptr)) {
        std::cout << "[settings] Service start requested\n";
        for (int i = 0; i < 30; ++i) {
            if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) break;
            if (ssp.dwCurrentState == SERVICE_RUNNING) break;
            Sleep(500);
        }
        if (ssp.dwCurrentState == SERVICE_RUNNING) {
            std::cout << "[settings] Service running\n";
        } else {
            std::cout << "[settings] Service did not reach running state\n";
        }
    } else {
        std::cout << "[settings] StartService failed or service already running\n";
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

void UpdateClient::settings_reset(bool confirm, bool restart_service) const
{
    if (!confirm) {
        throw std::runtime_error("settings_reset requires confirmation. Call with confirm=true to proceed.");
    }

    LSTATUS rc = RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate");
    if (rc == ERROR_SUCCESS) {
        std::cout << "Deleted policy key: Policies\\Microsoft\\Windows\\WindowsUpdate\n";
    } else if (rc == ERROR_FILE_NOT_FOUND) {
        std::cout << "Policy key not present\n";
    } else {
        std::cout << "Failed to delete policy key: " << rc << "\n";
    }

    rc = RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU");
    if (rc == ERROR_SUCCESS) {
        std::cout << "Deleted policy key: Policies\\Microsoft\\Windows\\WindowsUpdate\\AU\n";
    }

    if (restart_service) {
        try {
            restart_service_by_name(L"wuauserv");
        } catch (...) {
            std::cout << "[settings] Exception while attempting to restart wuauserv\n";
        }
    }

    std::cout << "Settings reset attempt complete. A reboot may be required for some changes to take effect.\n";
}
