#include "HardwareSensors.h"
#include <mscoree.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <vector>

namespace {
struct EmbeddedFile {
    std::wstring name;
    const BYTE* data;
    DWORD size;
};
std::wstring bundleDirectory;
std::vector<HANDLE> bundleLocks;
ICLRRuntimeHost* runtime = nullptr;
HMODULE runtimeModule = nullptr;

BOOL CALLBACK CollectFile(HMODULE module, LPCWSTR, LPWSTR name, LONG_PTR parameter) {
    auto& files = *reinterpret_cast<std::vector<EmbeddedFile>*>(parameter);
    if (!IS_INTRESOURCE(name)) return TRUE;
    ULONG_PTR id = reinterpret_cast<ULONG_PTR>(name);
    if (id < 2500 || id > 2510) return TRUE;
    wchar_t filename[80];
    if (!LoadStringW(module, static_cast<UINT>(id), filename, _countof(filename)) || files.size() >= 16) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    for (const wchar_t* p = filename; *p; ++p) {
        if (!iswalnum(*p) && *p != L'.' && *p != L'-') { SetLastError(ERROR_INVALID_NAME); return FALSE; }
    }
    HRSRC resource = FindResourceW(module, name, RT_RCDATA);
    DWORD size = resource ? SizeofResource(module, resource) : 0;
    const BYTE* data = resource ? static_cast<const BYTE*>(LockResource(LoadResource(module, resource))) : nullptr;
    if (!data || !size || size > 20 * 1024 * 1024) { SetLastError(ERROR_INVALID_DATA); return FALSE; }
    files.push_back({filename, data, size});
    return TRUE;
}

HRESULT Fingerprint(const std::vector<EmbeddedFile>& files, std::wstring& result) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return HRESULT_FROM_WIN32(GetLastError());
    HRESULT error = S_OK;
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) error = HRESULT_FROM_WIN32(GetLastError());
    for (const auto& file : files) {
        if (FAILED(error)) break;
        if (!CryptHashData(hash, reinterpret_cast<const BYTE*>(file.name.c_str()),
                           static_cast<DWORD>((file.name.size() + 1) * sizeof(wchar_t)), 0) ||
            !CryptHashData(hash, file.data, file.size, 0)) error = HRESULT_FROM_WIN32(GetLastError());
    }
    BYTE digest[32];
    DWORD size = sizeof(digest);
    if (SUCCEEDED(error) && !CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0))
        error = HRESULT_FROM_WIN32(GetLastError());
    if (hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (SUCCEEDED(error)) {
        const wchar_t* hex = L"0123456789abcdef";
        for (BYTE byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    }
    return error;
}

HRESULT Directory(const std::wstring& path) {
    if (!CreateDirectoryW(path.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    // Lock the directories too: verifying files alone does not pin their loading paths.
    HANDLE directory = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (directory == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    BY_HANDLE_FILE_INFORMATION info = {};
    HRESULT error = S_OK;
    if (!GetFileInformationByHandle(directory, &info)) error = HRESULT_FROM_WIN32(GetLastError());
    else if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        error = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    if (FAILED(error)) CloseHandle(directory);
    else bundleLocks.push_back(directory);
    return error;
}

HRESULT WriteNewFile(const std::wstring& path, const EmbeddedFile& resource) {
    GUID guid;
    if (FAILED(CoCreateGuid(&guid))) return E_FAIL;
    wchar_t unique[40];
    StringFromGUID2(guid, unique, _countof(unique));
    std::wstring temporary = path + unique + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    DWORD written = 0;
    HRESULT error = S_OK;
    if (!WriteFile(file, resource.data, resource.size, &written, NULL) || written != resource.size || !FlushFileBuffers(file))
        error = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_WRITE_FAULT);
    CloseHandle(file);
    if (SUCCEEDED(error) && !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DWORD code = GetLastError();
        if (code != ERROR_ALREADY_EXISTS && code != ERROR_FILE_EXISTS) error = HRESULT_FROM_WIN32(code);
    }
    DeleteFileW(temporary.c_str()); // Only the uniquely named temporary file created above.
    return error;
}

HRESULT LockAndCheck(const std::wstring& path, const EmbeddedFile& resource, HANDLE& file) {
    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) {
        HRESULT error = WriteNewFile(path, resource);
        if (FAILED(error)) return error;
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    }
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    BY_HANDLE_FILE_INFORMATION info = {};
    if (!GetFileInformationByHandle(file, &info)) return HRESULT_FROM_WIN32(GetLastError());
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    if (info.nFileSizeHigh || info.nFileSizeLow != resource.size) return HRESULT_FROM_WIN32(ERROR_CRC);
    BYTE buffer[4096];
    DWORD position = 0;
    while (position < resource.size) {
        DWORD length = std::min<DWORD>(sizeof(buffer), resource.size - position), read = 0;
        if (!ReadFile(file, buffer, length, &read, NULL)) return HRESULT_FROM_WIN32(GetLastError());
        if (read != length || std::memcmp(buffer, resource.data + position, length) != 0)
            return HRESULT_FROM_WIN32(ERROR_CRC);
        position += length;
    }
    return S_OK;
}

void UnlockBundle() {
    for (HANDLE file : bundleLocks) CloseHandle(file);
    bundleLocks.clear();
}

HRESULT PrepareBundle() {
    if (!bundleLocks.empty()) return S_OK;
    HMODULE module = GetModuleHandleW(NULL);
    std::vector<EmbeddedFile> files;
    if (!EnumResourceNamesW(module, RT_RCDATA, CollectFile, reinterpret_cast<LONG_PTR>(&files)))
        return HRESULT_FROM_WIN32(GetLastError());
    std::wstring fingerprint;
    HRESULT error = Fingerprint(files, fingerprint);
    if (FAILED(error)) return error;
    wchar_t applicationData[MAX_PATH];
    error = SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, applicationData);
    if (FAILED(error)) return error;
    bundleDirectory = std::wstring(applicationData) + L"\\TaskbarMonitor";
    if (FAILED(error = Directory(bundleDirectory))) { UnlockBundle(); return error; }
    bundleDirectory += L"\\Sensors";
    if (FAILED(error = Directory(bundleDirectory))) { UnlockBundle(); return error; }
    // ponytail: keep content-addressed caches for running copies; prune old versions if disk usage matters.
    bundleDirectory += L"\\" + fingerprint;
    if (FAILED(error = Directory(bundleDirectory))) { UnlockBundle(); return error; }
    for (const auto& resource : files) {
        HANDLE file = INVALID_HANDLE_VALUE;
        error = LockAndCheck(bundleDirectory + L"\\" + resource.name, resource, file);
        if (FAILED(error)) {
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            UnlockBundle();
            return error;
        }
        bundleLocks.push_back(file);
    }
    return S_OK;
}

HRESULT Call(LPCWSTR method, LPCWSTR argument, DWORD& value) {
    std::wstring bridge = bundleDirectory + L"\\TaskbarMonitor.Sensors.dll";
    return runtime->ExecuteInDefaultAppDomain(bridge.c_str(), L"TaskbarMonitor.Sensors.Bootstrap", method, argument, &value);
}

HRESULT StartRuntime() {
    if (runtime) return S_OK;
    HRESULT error = PrepareBundle();
    if (FAILED(error)) return error;
    if (!runtimeModule) runtimeModule = LoadLibraryExW(L"mscoree.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!runtimeModule) return HRESULT_FROM_WIN32(GetLastError());
    using Bind = HRESULT (STDAPICALLTYPE*)(LPCWSTR, LPCWSTR, DWORD, REFCLSID, REFIID, LPVOID*);
    auto bind = reinterpret_cast<Bind>(GetProcAddress(runtimeModule, "CorBindToRuntimeEx"));
    if (!bind) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    const GUID clsid = {0x90f1a06e,0x7712,0x4762,{0x86,0xb5,0x7a,0x5e,0xba,0x6b,0xdb,0x02}};
    const GUID iid = {0x90f1a06c,0x7712,0x4762,{0x86,0xb5,0x7a,0x5e,0xba,0x6b,0xdb,0x02}};
    error = bind(L"v4.0.30319", L"wks", 0, clsid, iid, reinterpret_cast<void**>(&runtime));
    if (SUCCEEDED(error)) error = runtime->Start();
    DWORD unused = 0;
    if (SUCCEEDED(error)) error = Call(L"Initialize", bundleDirectory.c_str(), unused);
    if (FAILED(error) && runtime) { runtime->Release(); runtime = nullptr; }
    return error;
}
}

HardwareSensorStatus ReadHardwareTemperatures() {
    HardwareSensorStatus result;
    result.checked = true;
    result.error = StartRuntime();
    if (!bundleDirectory.empty()) result.noticesPath = bundleDirectory + L"\\ThirdParty-Licenses.txt";
    if (FAILED(result.error)) return result;
    DWORD flags = 0, temperatures = 0, sources = 0;
    result.error = Call(L"Status", L"", flags);
    result.driverInstalled = (flags & 1) != 0;
    result.administrator = (flags & 2) != 0;
    if (SUCCEEDED(result.error)) result.error = Call(L"Read", L"", temperatures);
    if (SUCCEEDED(result.error)) result.error = Call(L"Sources", L"", sources);
    if (FAILED(result.error)) return result;
    DWORD cpu = temperatures & 65535, gpu = temperatures >> 16;
    DWORD cpuSource = sources & 255, gpuSource = sources >> 8;
    if (cpuSource > 9 || gpuSource > 9 || (cpu != 65535) != (cpuSource != 0) || (gpu != 65535) != (gpuSource != 0)) {
        result.error = E_INVALIDARG;
        return result;
    }
    if ((cpu != 65535 && cpu > 1500) || (gpu != 65535 && gpu > 1500)) { result.error = E_INVALIDARG; return result; }
    result.temperatures.cpu = cpu == 65535 ? -1.0 : cpu / 10.0;
    result.temperatures.gpu = gpu == 65535 ? -1.0 : gpu / 10.0;
    const wchar_t* cpuSources[] = { L"", L"CPU core / CCD (hottest)", L"Core Average", L"CPU (Tctl/Tdie)", L"CPU (Tdie)", L"CPU Package", L"Motherboard / EC CPU (fallback)", L"AIDA64 shared memory", L"External hardware monitor (WMI)", L"HWiNFO shared memory" };
    const wchar_t* gpuSources[] = { L"", L"GPU Hot Spot", L"GPU Core", L"Motherboard / EC GPU (fallback)", L"Windows GPU driver (WDDM)", L"Intel IGCL temperature sensor", L"Intel Level Zero Sysman", L"AIDA64 shared memory", L"External hardware monitor (WMI)", L"HWiNFO shared memory" };
    result.cpuSource = cpuSources[cpuSource];
    result.gpuSource = gpuSources[gpuSource];
    result.running = true;
    return result;
}

bool CheckEmbeddedSensorBridge() {
    if (FAILED(StartRuntime())) return false;
    DWORD value = 0;
    return SUCCEEDED(Call(L"SelfCheck", L"", value)) && value == (615u | (530u << 16));
}

void CloseHardwareSensors() {
    if (runtime) {
        DWORD ignored = 0;
        Call(L"Close", L"", ignored);
        runtime->Release();
        runtime = nullptr;
    }
    UnlockBundle();
    // The CLR owns live assemblies. Do not Stop it or unload mscoree.dll before process exit.
}
