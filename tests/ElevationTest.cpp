#include "Elevation.h"
#include <shlobj.h>
#include <cassert>
#include <string>

enum LaunchMode { Cancel, Fail, ChildFails, Restart };
static LaunchMode mode = Cancel;
static bool elevated = false, closed = false;
static int warnings = 0, launches = 0, installQuestions = 0;
static int installAnswer = IDNO;
static std::wstring completionName;
static std::wstring lastArguments;
static BOOL FakeIsUserAnAdmin() { return elevated; }
static int FakeMessageBox(HWND, LPCWSTR, LPCWSTR, UINT type) {
    if ((type & MB_TYPEMASK) == MB_YESNO) { ++installQuestions; return installAnswer; }
    ++warnings;
    return IDOK;
}
static BOOL FakeShellExecute(SHELLEXECUTEINFOW* launch) {
    assert(ElevationRestartPending()); // Prevent re-entry while UAC itself is pending.
    assert(wcscmp(launch->lpVerb, elevated ? L"open" : L"runas") == 0);
    ++launches;
    lastArguments = launch->lpParameters;
    if (mode == Cancel || mode == Fail) {
        SetLastError(mode == Cancel ? ERROR_CANCELLED : ERROR_ACCESS_DENIED);
        return FALSE;
    }
    std::wstring command = L"\"" + std::wstring(launch->lpFile) + L"\" ";
    command += mode == ChildFails ? L"--test-failure"
        : std::wstring(launch->lpParameters) + L" --test-complete " + completionName;
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(launch->lpFile, command.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &process)) return FALSE;
    CloseHandle(process.hThread);
    launch->hProcess = process.hProcess;
    return TRUE;
}
#define IsUserAnAdmin FakeIsUserAnAdmin
#define MessageBoxW FakeMessageBox
#define ShellExecuteExW FakeShellExecute
#include "../src/Elevation.cpp"
#undef IsUserAnAdmin
#undef MessageBoxW
#undef ShellExecuteExW

static void CheckAutomaticSensorAccess(HWND host) {
    HardwareSensorStatus status;
    status.directCpuSupported = true;
    SensorAccessRequest request = SensorAccessRequest::JustApproved;
    int before = launches;
    CompleteSensorAccessRequest(request, host, status, true, true);
    assert(request == SensorAccessRequest::JustApproved && launches == before);
    status.checked = status.running = true;
    CompleteSensorAccessRequest(request, host, status, true, true);
    assert(request == SensorAccessRequest::None && launches == before + 1 && installQuestions == 0);
    assert(lastArguments.find(L"--install-cpu-driver") != std::wstring::npos && lastArguments.find(L"--settings") == std::wstring::npos);
    CompleteSensorAccessRequest(request, host, status, true, true); // Another sample after UAC was cancelled.
    assert(launches == before + 1);

    auto check = [&](SensorAccessRequest consent, bool missingCpu, bool missingGpu) {
        int previous = launches;
        CompleteSensorAccessRequest(consent, host, status, missingCpu, missingGpu);
        assert(consent == SensorAccessRequest::None);
        return launches != previous;
    };
    assert(!check(SensorAccessRequest::JustApproved, false, false)); // All selected readings already work.
    assert(!check(SensorAccessRequest::JustApproved, false, true)); // No driver path for this GPU.
    status.driverGpuSupported = true;
    assert(check(SensorAccessRequest::JustApproved, false, true));
    assert(!check(SensorAccessRequest::SavedChoice, true, false) && installQuestions == 1);
    installAnswer = IDYES;
    assert(check(SensorAccessRequest::SavedChoice, true, false) && installQuestions == 2);
    status.driverInstalled = true;
    assert(check(SensorAccessRequest::SavedChoice, true, false) && installQuestions == 2);
    assert(lastArguments.find(L"--install-cpu-driver") == std::wstring::npos);
    status.administrator = elevated = true;
    assert(!check(SensorAccessRequest::JustApproved, true, true));
    status.driverInstalled = false;
    assert(check(SensorAccessRequest::JustApproved, true, true));
    elevated = false;
}

static LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_TIMER && wParam == TIMER_ELEVATION) { PollElevatedRestart(window); return 0; }
    if (message == WM_CLOSE) { closed = true; PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}
static void PumpRestart() {
    ULONGLONG start = GetTickCount64();
    while (ElevationRestartPending() || !closed) {
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (!ElevationRestartPending()) break;
        assert(GetTickCount64() - start < 10000);
        Sleep(10);
    }
}
static HANDLE Launch(const std::wstring& arguments) {
    wchar_t executable[MAX_PATH];
    assert(GetModuleFileNameW(NULL, executable, _countof(executable)));
    std::wstring command = L"\"" + std::wstring(executable) + L"\" " + arguments;
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process = {};
    assert(CreateProcessW(executable, command.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW,
                          NULL, NULL, &startup, &process));
    CloseHandle(process.hThread);
    return process.hProcess;
}
static void CheckExit(HANDLE process) {
    assert(WaitForSingleObject(process, 15000) == WAIT_OBJECT_0);
    DWORD code = 0;
    assert(GetExitCodeProcess(process, &code) && code == 0);
    CloseHandle(process);
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--test-failure") return 42;
    if (argc > 1 && std::string(argv[1]) == "--elevated-handoff") {
        // Simulate an elevated token only in this test; never display UAC.
        elevated = true;
        bool requested = false, installCpuDriver = false;
        if (argc > 3 && std::string(argv[3]) == "invalid") {
            assert(!AcceptElevatedRestart(requested, installCpuDriver) && requested && !installCpuDriver);
            return 0;
        }
        assert(AcceptElevatedRestart(requested, installCpuDriver) && requested && installCpuDriver);
        for (int i = 4; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--test-complete") {
                std::string name = argv[i + 1];
                std::wstring wide(name.begin(), name.end());
                HANDLE done = OpenEventW(EVENT_MODIFY_STATE, FALSE, wide.c_str());
                assert(done && SetEvent(done));
                CloseHandle(done);
            }
        }
        return 0;
    }
    bool requested = true, installCpuDriver = true;
    assert(AcceptElevatedRestart(requested, installCpuDriver) && !requested && !installCpuDriver);
    if (argc > 1 && std::string(argv[1]) == "--install-cpu-driver") return 0;
    if (argc > 2 && std::string(argv[1]) == "--test-monitor") {
        std::string name = argv[2];
        completionName.assign(name.begin(), name.end());
        WNDCLASSW window = {};
        window.lpfnWndProc = HostProc;
        window.hInstance = GetModuleHandleW(NULL);
        window.lpszClassName = L"TaskbarMonitorElevationTest";
        assert(RegisterClassW(&window));
        HWND host = CreateWindowW(window.lpszClassName, L"", 0, 0, 0, 1, 1, NULL, NULL, window.hInstance, NULL);
        assert(host);
        CheckAutomaticSensorAccess(host);
        assert(!RequestElevatedRestart(host, host) && !closed && warnings == 0);
        assert(lastArguments.find(L"--temperatures") != std::wstring::npos);
        assert(!RequestElevatedRestart(NULL, host, true));
        assert(lastArguments.find(L"--install-cpu-driver") != std::wstring::npos);
        assert(lastArguments.find(L"--settings") == std::wstring::npos &&
               lastArguments.find(L"--temperatures") == std::wstring::npos);
        mode = Fail;
        assert(!RequestElevatedRestart(host, host) && !closed && warnings == 1);
        mode = ChildFails;
        assert(RequestElevatedRestart(host, host));
        assert(!RequestElevatedRestart(host, host));
        PumpRestart();
        assert(!closed && warnings == 2);
        elevated = true;
        assert(!RequestElevatedRestart(host, host));
        elevated = false;
        mode = Restart;
        assert(RequestElevatedRestart(NULL, host, true));
        PumpRestart();
        assert(closed && warnings == 2);
        CancelElevatedRestart();
        DestroyWindow(host);
        return 0;
    }
    CheckExit(Launch(L"--elevated-handoff " + std::to_wstring(GetCurrentProcessId()) + L" invalid --install-cpu-driver"));
    CheckExit(Launch(L"--install-cpu-driver"));
    GUID id;
    wchar_t nonce[40];
    assert(SUCCEEDED(CoCreateGuid(&id)) && StringFromGUID2(id, nonce, _countof(nonce)));
    completionName = L"Local\\TaskbarMonitor.ElevationTest." + std::wstring(nonce);
    HANDLE done = CreateEventW(NULL, TRUE, FALSE, completionName.c_str());
    assert(done);
    CheckExit(Launch(L"--test-monitor " + completionName));
    assert(WaitForSingleObject(done, 15000) == WAIT_OBJECT_0);
    CloseHandle(done);
}
