#include "Elevation.h"
#include <shlobj.h>
#include <cerrno>
#include <string>
#include <vector>

namespace {
HANDLE readyEvent = NULL, childProcess = NULL;
HWND requestOwner = NULL;
ULONGLONG requestStarted = 0;

std::wstring EventName(DWORD process, const wchar_t* nonce) {
    return L"Local\\TaskbarMonitor.Elevation." + std::to_wstring(process) + L"." + nonce;
}

bool SameUser(HANDLE parent) {
    auto User = [](HANDLE process, std::vector<BYTE>& data) {
        HANDLE token = NULL;
        if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return false;
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, NULL, 0, &size);
        data.resize(size);
        bool ok = size && GetTokenInformation(token, TokenUser, data.data(), size, &size);
        CloseHandle(token);
        return ok;
    };
    std::vector<BYTE> mine, theirs;
    return User(GetCurrentProcess(), mine) && User(parent, theirs) &&
           EqualSid(reinterpret_cast<TOKEN_USER*>(mine.data())->User.Sid,
                    reinterpret_cast<TOKEN_USER*>(theirs.data())->User.Sid);
}
}

bool IsProcessElevated() { return IsUserAnAdmin() != FALSE; }
bool ElevationRestartPending() { return readyEvent != NULL; }

void CancelElevatedRestart() {
    if (childProcess) CloseHandle(childProcess);
    if (readyEvent) CloseHandle(readyEvent);
    childProcess = readyEvent = NULL;
    requestOwner = NULL;
}

bool RequestElevatedRestart(HWND owner, HWND host) {
    if (IsProcessElevated() || ElevationRestartPending()) return false;
    wchar_t executable[MAX_PATH], nonce[40];
    DWORD length = GetModuleFileNameW(NULL, executable, _countof(executable));
    GUID id;
    if (!length || length == _countof(executable) || FAILED(CoCreateGuid(&id)) ||
        !StringFromGUID2(id, nonce, _countof(nonce))) return false;
    std::wstring name = EventName(GetCurrentProcessId(), nonce);
    readyEvent = CreateEventW(NULL, TRUE, FALSE, name.c_str());
    if (!readyEvent || GetLastError() == ERROR_ALREADY_EXISTS) { CancelElevatedRestart(); return false; }
    std::wstring arguments = L"--elevated-handoff " + std::to_wstring(GetCurrentProcessId()) +
                             L" " + nonce + L" --settings --temperatures --autostart";
    SHELLEXECUTEINFOW launch = { sizeof(launch) };
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    launch.hwnd = owner;
    launch.lpVerb = L"runas";
    launch.lpFile = executable;
    launch.lpParameters = arguments.c_str();
    launch.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch) || !launch.hProcess) {
        DWORD error = GetLastError();
        CancelElevatedRestart();
        if (error != ERROR_CANCELLED)
            MessageBoxW(owner, L"Could not request administrator access. The monitor is still running.", L"TaskbarMonitor", MB_OK | MB_ICONWARNING);
        return false;
    }
    childProcess = launch.hProcess;
    requestOwner = owner;
    requestStarted = GetTickCount64();
    if (!SetTimer(host, TIMER_ELEVATION, 100, NULL)) { CancelElevatedRestart(); return false; }
    return true;
}

void PollElevatedRestart(HWND host) {
    if (!childProcess) { KillTimer(host, TIMER_ELEVATION); return; }
    DWORD child = WaitForSingleObject(childProcess, 0);
    bool ready = WaitForSingleObject(readyEvent, 0) == WAIT_OBJECT_0;
    if (child == WAIT_TIMEOUT && ready) {
        CancelElevatedRestart();
        KillTimer(host, TIMER_ELEVATION);
        PostMessageW(host, WM_CLOSE, 0, 0);
    } else if (child != WAIT_TIMEOUT || GetTickCount64() - requestStarted >= 15000) {
        HWND owner = IsWindow(requestOwner) ? requestOwner : host;
        CancelElevatedRestart();
        KillTimer(host, TIMER_ELEVATION);
        MessageBoxW(owner, L"The elevated restart did not complete. The current monitor was kept running.\n\nUse the same Windows account when approving UAC.", L"TaskbarMonitor", MB_OK | MB_ICONWARNING);
    }
}

bool AcceptElevatedRestart(bool& requested) {
    requested = false;
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return false;
    int handoff = -1;
    for (int i = 1; i < count; ++i) {
        if (wcscmp(arguments[i], L"--elevated-handoff") == 0) {
            if (handoff != -1) { LocalFree(arguments); return false; }
            handoff = i;
        }
    }
    if (handoff == -1) { LocalFree(arguments); return true; }
    requested = true;
    if (handoff + 2 >= count || !IsProcessElevated()) { LocalFree(arguments); return false; }
    wchar_t* end = NULL;
    errno = 0;
    unsigned long process = wcstoul(arguments[handoff + 1], &end, 10);
    GUID id;
    wchar_t nonce[40];
    bool valid = !errno && end && !*end && process && process != GetCurrentProcessId() &&
                 SUCCEEDED(CLSIDFromString(arguments[handoff + 2], &id)) &&
                 StringFromGUID2(id, nonce, _countof(nonce)) &&
                 _wcsicmp(nonce, arguments[handoff + 2]) == 0;
    std::wstring name = valid ? EventName(static_cast<DWORD>(process), nonce) : L"";
    LocalFree(arguments);
    if (!valid) return false;
    HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(process));
    if (!parent) return false;
    wchar_t mine[MAX_PATH], theirs[MAX_PATH];
    DWORD size = _countof(theirs), length = GetModuleFileNameW(NULL, mine, _countof(mine));
    valid = length && length < _countof(mine) && QueryFullProcessImageNameW(parent, 0, theirs, &size) &&
            _wcsicmp(mine, theirs) == 0 && SameUser(parent);
    HANDLE ready = valid ? OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str()) : NULL;
    bool accepted = ready && SetEvent(ready) && WaitForSingleObject(parent, 30000) == WAIT_OBJECT_0;
    if (ready) CloseHandle(ready);
    CloseHandle(parent);
    return accepted;
}
