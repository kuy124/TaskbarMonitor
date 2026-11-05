#include "Updater.h"
#include "Config.h"
#include <string>

void CheckForUpdates() {
    static HANDLE worker = NULL;
    if (!g_config.autoUpdate) return;
    if (worker) {
        if (WaitForSingleObject(worker, 0) == WAIT_TIMEOUT) return;
        CloseHandle(worker);
        worker = NULL;
    }
    HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(201), RT_RCDATA);
    if (!resource) return;
    HGLOBAL data = LoadResource(NULL, resource);
    if (!data) return;
    wchar_t temp[MAX_PATH], script[MAX_PATH], executable[MAX_PATH], powershell[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, executable, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    if (!GetSystemDirectoryW(powershell, MAX_PATH)) return;
    length = GetTempPathW(MAX_PATH, temp);
    if (!length || length >= MAX_PATH || !GetTempFileNameW(temp, L"tmu", 0, script)) return;
    std::wstring scriptPath = std::wstring(script) + L".ps1";
    if (!MoveFileW(script, scriptPath.c_str())) { DeleteFileW(script); return; }
    HANDLE file = CreateFileW(scriptPath.c_str(), GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0, size = SizeofResource(NULL, resource);
    bool saved = file != INVALID_HANDLE_VALUE && WriteFile(file, LockResource(data), size, &written, NULL) && written == size;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!saved) { DeleteFileW(scriptPath.c_str()); return; }
    std::wstring command = L"\"" + std::wstring(powershell) + L"\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + scriptPath + L"\" -Executable \"" + executable + L"\" -AppProcessId " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process = {};
    if (CreateProcessW(NULL, &command[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
        worker = process.hProcess;
        CloseHandle(process.hThread);
    } else DeleteFileW(scriptPath.c_str());
}
