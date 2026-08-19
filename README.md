# WUPdate - Windows Update CLI

`WUPdate` is a native C++23 command-line application for managing Windows Updates through the Windows Update Agent (WUA) COM API.

It is designed as a native replacement for PowerShell `PSWindowsUpdate`-style workflows, with no PowerShell runtime dependency and no .NET dependency.

## Highlights

- Native Windows executable
- C++23
- No PowerShell dependency
- No .NET dependency
- No ATL
- No MFC
- Uses `Microsoft::WRL::ComPtr` for COM lifetime management
- Uses only Windows SDK-provided APIs and libraries

## Build Requirements

- Visual Studio Build Tools (MSVC)
- Windows SDK
- CMake
- Ninja

## Build

From the project root:

```powershell
cmake -B build -G Ninja
cmake --build build
```

This generates a Ninja build (no Visual Studio solution required).

## Usage

```powershell
WUPdate search
WUPdate list
WUPdate history
WUPdate install
WUPdate --help
WUPdate --version
```

Stubbed (not implemented yet):

```powershell
WUPdate hide
WUPdate unhide
WUPdate uninstall
WUPdate scan
```

## Implemented Commands (v1)

### `WUPdate search`

- Searches with criteria: `IsInstalled=0`
- Displays:
  - Title
  - KB articles
  - Reboot required
  - Downloaded state

### `WUPdate list`

- Searches with criteria: `IsInstalled=1`
- Displays:
  - Title
  - KB articles
  - Last deployment change time

### `WUPdate history`

- Uses WUA history APIs
- Displays:
  - Date
  - Operation
  - Result
  - Update title

### `WUPdate install`

Workflow:

1. Search available updates (`IsInstalled=0`)
2. Create update collection
3. Download updates
4. Install updates
5. Display download/install result codes
6. Display reboot requirement
7. Display per-update result details

## Implementation Notes

- COM initialization uses RAII in `com_init`:
  - Constructor calls `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`
  - Destructor calls `CoUninitialize()`
- COM interfaces are managed with `Microsoft::WRL::ComPtr`
- WUA interfaces used directly:
  - `IUpdateSession`
  - `IUpdateSearcher`
  - `ISearchResult`
  - `IUpdateCollection`
  - `IUpdate`
  - `IUpdateDownloader`
  - `IUpdateInstaller`
- `HRESULT` failures are checked and surfaced as `std::runtime_error` using formatted messages via `std::format`
- COM-facing text is handled as `std::wstring`, converted to UTF-8 at presentation boundaries
- Console output uses UTF-8 encoding

## Code Quality

- C++23 (`set(CMAKE_CXX_STANDARD 23)`)
- RAII for resource lifetime
- No naked `new`
- No global mutable state
- Headers and implementation split by responsibility
- `/W4` enabled under MSVC
- Minimal manual CLI parsing with `std::span` and `std::string_view`

## Project Layout

```text
WUPdate/
├── CMakeLists.txt
├── README.md
└── src/
    ├── main.cpp
    ├── com_init.h
    ├── error.h
    ├── error.cpp
    ├── update_client.h
    ├── update_client.cpp
    ├── commands.h
    ├── commands.cpp
    └── models.h
```
