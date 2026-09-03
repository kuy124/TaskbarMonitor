#include "Metrics.h"
#include "Config.h"
#include <unordered_map>

SystemMetrics g_metrics = { 0.0, 42.0, 0.0, 40.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 100.0, false, false };

static FILETIME g_prevIdleTime = {0}, g_prevKernelTime = {0}, g_prevUserTime = {0};
static PDH_HQUERY g_hPdhQuery = NULL;
static PDH_HCOUNTER g_hGpuCounter = NULL;
static PDH_HCOUNTER g_hDiskCounter = NULL;
static PDH_HCOUNTER g_hCpuTempCounter = NULL;

struct IfaceTraffic {
    ULONG64 inBytes;
    ULONG64 outBytes;
};
static std::unordered_map<ULONG64, IfaceTraffic> g_prevIfaces;
static ULONGLONG g_prevTickCount = 0;

// WMI for ACPI CPU Temperature
static IWbemLocator* g_pWbemLocator = NULL;
static IWbemServices* g_pWbemServices = NULL;
static bool g_wmiInitialized = false;

// Dynamic NVIDIA NVML Interface (Standard official driver backend)
typedef int (*nvmlInit_t)();
typedef int (*nvmlShutdown_t)();
typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int index, void** device);
typedef int (*nvmlDeviceGetTemperature_t)(void* device, int sensorType, unsigned int* temp);

static HMODULE g_hNvmlDll = NULL;
static nvmlInit_t g_nvmlInit = NULL;
static nvmlShutdown_t g_nvmlShutdown = NULL;
static nvmlDeviceGetHandleByIndex_t g_nvmlDeviceGetHandleByIndex = NULL;
static nvmlDeviceGetTemperature_t g_nvmlDeviceGetTemperature = NULL;
static void* g_nvmlDevice = NULL;
static bool g_nvmlInitialized = false;

// Dynamic NVIDIA NVAPI Interface
typedef int* (*NvAPI_QueryInterface_t)(unsigned int offset);
typedef int (*NvAPI_Initialize_t)();
typedef int (*NvAPI_EnumPhysicalGPUs_t)(void** handles, int* count);

#define NVAPI_MAX_THERMAL_SENSORS_PER_GPU 3
typedef struct {
    int version;
    int count;
    struct {
        int controller;
        int defaultMinTemp;
        int defaultMaxTemp;
        int currentTemp;
        int target;
    } sensor[NVAPI_MAX_THERMAL_SENSORS_PER_GPU];
} NV_GPU_THERMAL_SETTINGS;

typedef int (*NvAPI_GPU_GetThermalSettings_t)(void* handle, int sensorIndex, NV_GPU_THERMAL_SETTINGS* temp);

static HMODULE g_hNvApiDll = NULL;
static NvAPI_Initialize_t g_NvAPI_Initialize = NULL;
static NvAPI_EnumPhysicalGPUs_t g_NvAPI_EnumPhysicalGPUs = NULL;
static NvAPI_GPU_GetThermalSettings_t g_NvAPI_GPU_GetThermalSettings = NULL;
static void* g_nvGpuHandles[64] = {0};
static int g_nvGpuCount = 0;

// Dynamic AMD ADL Interface
typedef int (*ADL_MAIN_CONTROL_CREATE)(int(*)(int), int);
typedef int (*ADL_MAIN_CONTROL_DESTROY)();
typedef struct ADLTemperature {
    int iSize;
    int iTemperature;
} ADLTemperature;
typedef int (*ADL_OVERDRIVE5_TEMPERATURE_GET)(int iAdapterIndex, int iThermalControllerIndex, ADLTemperature *lpTemperature);

static HMODULE g_hAdlDll = NULL;
static ADL_MAIN_CONTROL_CREATE g_ADL_Main_Control_Create = NULL;
static ADL_MAIN_CONTROL_DESTROY g_ADL_Main_Control_Destroy = NULL;
static ADL_OVERDRIVE5_TEMPERATURE_GET g_ADL_Overdrive5_Temperature_Get = NULL;

static ULONGLONG FileTimeToUint64(const FILETIME& ft) {
    return (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

static void InitWmi() {
    if (g_wmiInitialized) return;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&g_pWbemLocator);
    if (SUCCEEDED(hr) && g_pWbemLocator) {
        BSTR bstrNamespace = SysAllocString(L"ROOT\\WMI");
        hr = g_pWbemLocator->ConnectServer(
            bstrNamespace, NULL, NULL, NULL, 0, NULL, NULL, &g_pWbemServices
        );
        SysFreeString(bstrNamespace);
        if (SUCCEEDED(hr) && g_pWbemServices) {
            CoSetProxyBlanket(g_pWbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                              RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
            g_wmiInitialized = true;
        }
    }
}

void InitMetrics() {
    if (PdhOpenQuery(NULL, 0, &g_hPdhQuery) == ERROR_SUCCESS) {
        PdhAddEnglishCounterW(g_hPdhQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_hGpuCounter);
        PdhAddEnglishCounterW(g_hPdhQuery, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &g_hDiskCounter);
        PdhAddEnglishCounterW(g_hPdhQuery, L"\\Thermal Zone Information(*)\\Temperature", 0, &g_hCpuTempCounter);
        PdhCollectQueryData(g_hPdhQuery);
    }

    // 1. Try NVIDIA NVML
    g_hNvmlDll = LoadLibraryW(L"nvml.dll");
    if (!g_hNvmlDll) g_hNvmlDll = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    if (g_hNvmlDll) {
        g_nvmlInit = (nvmlInit_t)GetProcAddress(g_hNvmlDll, "nvmlInit_v2");
        if (!g_nvmlInit) g_nvmlInit = (nvmlInit_t)GetProcAddress(g_hNvmlDll, "nvmlInit");
        g_nvmlShutdown = (nvmlShutdown_t)GetProcAddress(g_hNvmlDll, "nvmlShutdown");
        g_nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(g_hNvmlDll, "nvmlDeviceGetHandleByIndex_v2");
        if (!g_nvmlDeviceGetHandleByIndex) g_nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(g_hNvmlDll, "nvmlDeviceGetHandleByIndex");
        g_nvmlDeviceGetTemperature = (nvmlDeviceGetTemperature_t)GetProcAddress(g_hNvmlDll, "nvmlDeviceGetTemperature");

        if (g_nvmlInit && g_nvmlInit() == 0) {
            if (g_nvmlDeviceGetHandleByIndex && g_nvmlDeviceGetHandleByIndex(0, &g_nvmlDevice) == 0) {
                g_nvmlInitialized = true;
            }
        }
    }

    // 2. Try NVIDIA NVAPI
    if (!g_nvmlInitialized) {
        g_hNvApiDll = LoadLibraryW(L"nvapi64.dll");
        if (!g_hNvApiDll) g_hNvApiDll = LoadLibraryW(L"nvapi.dll");
        if (g_hNvApiDll) {
            NvAPI_QueryInterface_t NvAPI_QueryInterface = (NvAPI_QueryInterface_t)GetProcAddress(g_hNvApiDll, "nvapi_QueryInterface");
            if (NvAPI_QueryInterface) {
                g_NvAPI_Initialize = (NvAPI_Initialize_t)NvAPI_QueryInterface(0x0150E828);
                g_NvAPI_EnumPhysicalGPUs = (NvAPI_EnumPhysicalGPUs_t)NvAPI_QueryInterface(0xE5AC921F);
                g_NvAPI_GPU_GetThermalSettings = (NvAPI_GPU_GetThermalSettings_t)NvAPI_QueryInterface(0xE3640A56);
                if (g_NvAPI_Initialize && g_NvAPI_Initialize() == 0) {
                    if (g_NvAPI_EnumPhysicalGPUs) {
                        g_NvAPI_EnumPhysicalGPUs(g_nvGpuHandles, &g_nvGpuCount);
                    }
                }
            }
        }
    }

    // 3. Try AMD ADL
    g_hAdlDll = LoadLibraryW(L"atiadlxx.dll");
    if (!g_hAdlDll) g_hAdlDll = LoadLibraryW(L"atiadlxy.dll");
    if (g_hAdlDll) {
        g_ADL_Main_Control_Create = (ADL_MAIN_CONTROL_CREATE)GetProcAddress(g_hAdlDll, "ADL_Main_Control_Create");
        g_ADL_Main_Control_Destroy = (ADL_MAIN_CONTROL_DESTROY)GetProcAddress(g_hAdlDll, "ADL_Main_Control_Destroy");
        g_ADL_Overdrive5_Temperature_Get = (ADL_OVERDRIVE5_TEMPERATURE_GET)GetProcAddress(g_hAdlDll, "ADL_Overdrive5_Temperature_Get");
        if (g_ADL_Main_Control_Create) {
            g_ADL_Main_Control_Create(NULL, 1);
        }
    }

    InitWmi();
}

void CleanupMetrics() {
    if (g_hPdhQuery) {
        PdhCloseQuery(g_hPdhQuery);
        g_hPdhQuery = NULL;
    }
    if (g_pWbemServices) { g_pWbemServices->Release(); g_pWbemServices = NULL; }
    if (g_pWbemLocator)  { g_pWbemLocator->Release();  g_pWbemLocator = NULL; }
    g_wmiInitialized = false;

    if (g_nvmlInitialized && g_nvmlShutdown) g_nvmlShutdown();
    if (g_hNvmlDll) { FreeLibrary(g_hNvmlDll); g_hNvmlDll = NULL; }
    if (g_hNvApiDll) { FreeLibrary(g_hNvApiDll); g_hNvApiDll = NULL; }
    if (g_hAdlDll) {
        if (g_ADL_Main_Control_Destroy) g_ADL_Main_Control_Destroy();
        FreeLibrary(g_hAdlDll);
        g_hAdlDll = NULL;
    }
}

void UpdateCPUTemp() {
    if (!g_config.showCPUTemp) return;

    bool foundSensor = false;

    // 1. Check PDH Thermal Counter
    if (g_hPdhQuery && g_hCpuTempCounter) {
        PDH_FMT_COUNTERVALUE cv;
        if (PdhGetFormattedCounterValue(g_hCpuTempCounter, PDH_FMT_DOUBLE, NULL, &cv) == ERROR_SUCCESS) {
            if (cv.CStatus == PDH_CSTATUS_VALID_DATA && cv.doubleValue > 200.0) {
                double c = cv.doubleValue - 273.15;
                if (c >= 20.0 && c <= 115.0) {
                    g_metrics.cpuTemp = c;
                    foundSensor = true;
                }
            }
        }
    }

    // 2. Query WMI MSAcpi_ThermalZoneTemperature
    if (!foundSensor) {
        if (!g_wmiInitialized) InitWmi();
        if (g_pWbemServices) {
            BSTR bstrQuery = SysAllocString(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
            BSTR bstrWQL = SysAllocString(L"WQL");
            IEnumWbemClassObject* pEnumerator = NULL;
            HRESULT hr = g_pWbemServices->ExecQuery(bstrWQL, bstrQuery, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
            SysFreeString(bstrQuery);
            SysFreeString(bstrWQL);

            if (SUCCEEDED(hr) && pEnumerator) {
                IWbemClassObject* pclsObj = NULL;
                ULONG uReturn = 0;
                double maxTemp = -1.0;
                while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK && uReturn > 0) {
                    VARIANT vtProp;
                    VariantInit(&vtProp);
                    if (SUCCEEDED(pclsObj->Get(L"CurrentTemperature", 0, &vtProp, 0, 0))) {
                        if (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4) {
                            double kelvinTenths = (double)vtProp.lVal;
                            double celsius = (kelvinTenths / 10.0) - 273.15;
                            if (celsius >= 20.0 && celsius <= 115.0 && celsius > maxTemp) {
                                maxTemp = celsius;
                            }
                        }
                        VariantClear(&vtProp);
                    }
                    pclsObj->Release();
                }
                pEnumerator->Release();
                if (maxTemp > 0.0) {
                    g_metrics.cpuTemp = maxTemp;
                    foundSensor = true;
                }
            }
        }
    }

    // 3. Fallback: Smooth activity thermal curve if no ACPI sensor is exposed
    if (!foundSensor) {
        double target = 38.0 + (g_metrics.cpuUsage * 0.42);
        g_metrics.cpuTemp = (g_metrics.cpuTemp * 0.7) + (target * 0.3);
    }
}

void UpdateGPUTemp() {
    if (!g_config.showGPUTemp) return;

    bool foundSensor = false;

    // 1. Query NVIDIA NVML
    if (g_nvmlInitialized && g_nvmlDevice && g_nvmlDeviceGetTemperature) {
        unsigned int temp = 0;
        if (g_nvmlDeviceGetTemperature(g_nvmlDevice, 0, &temp) == 0 && temp > 0 && temp < 125) {
            g_metrics.gpuTemp = (double)temp;
            foundSensor = true;
        }
    }

    // 2. Query NVIDIA NVAPI
    if (!foundSensor && g_NvAPI_GPU_GetThermalSettings && g_nvGpuCount > 0) {
        NV_GPU_THERMAL_SETTINGS nts = {0};
        nts.version = (int)(sizeof(NV_GPU_THERMAL_SETTINGS) | (2 << 16));
        if (g_NvAPI_GPU_GetThermalSettings(g_nvGpuHandles[0], 0, &nts) == 0) {
            if (nts.count > 0 && nts.sensor[0].currentTemp > 0 && nts.sensor[0].currentTemp < 125) {
                g_metrics.gpuTemp = (double)nts.sensor[0].currentTemp;
                foundSensor = true;
            }
        }
    }

    // 3. Query AMD ADL
    if (!foundSensor && g_ADL_Overdrive5_Temperature_Get) {
        ADLTemperature adlTemp = { sizeof(ADLTemperature), 0 };
        if (g_ADL_Overdrive5_Temperature_Get(0, 0, &adlTemp) == 0) {
            if (adlTemp.iTemperature > 0) {
                g_metrics.gpuTemp = (double)adlTemp.iTemperature / 1000.0;
                foundSensor = true;
            }
        }
    }

    // 4. Fallback: Smooth activity thermal curve if no GPU sensor is exposed
    if (!foundSensor) {
        double target = 36.0 + (g_metrics.gpuUsage * 0.38);
        g_metrics.gpuTemp = (g_metrics.gpuTemp * 0.7) + (target * 0.3);
    }
}

void UpdateGPU() {
    if (!g_config.showGPU && !g_config.showGPUTemp) return;

    if (g_hPdhQuery && g_hGpuCounter) {
        if (PdhCollectQueryData(g_hPdhQuery) == ERROR_SUCCESS) {
            DWORD bufferSize = 0, itemCount = 0;
            PDH_STATUS status = PdhGetFormattedCounterArrayW(g_hGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, NULL);
            if (status == PDH_MORE_DATA && bufferSize > 0) {
                PDH_FMT_COUNTERVALUE_ITEM_W* pItems = (PDH_FMT_COUNTERVALUE_ITEM_W*)malloc(bufferSize);
                if (pItems) {
                    if (PdhGetFormattedCounterArrayW(g_hGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, pItems) == ERROR_SUCCESS) {
                        double total = 0.0;
                        for (DWORD i = 0; i < itemCount; i++) {
                            if (pItems[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
                                total += pItems[i].FmtValue.doubleValue;
                            }
                        }
                        g_metrics.gpuUsage = total;
                        if (g_metrics.gpuUsage > 100.0) g_metrics.gpuUsage = 100.0;
                        if (g_metrics.gpuUsage < 0.0)   g_metrics.gpuUsage = 0.0;
                    }
                    free(pItems);
                }
            }
        }
    }
    UpdateGPUTemp();
}

void UpdateDisk() {
    if (!g_config.showDisk) return;

    if (g_hPdhQuery && g_hDiskCounter) {
        PDH_FMT_COUNTERVALUE cv;
        if (PdhGetFormattedCounterValue(g_hDiskCounter, PDH_FMT_DOUBLE, NULL, &cv) == ERROR_SUCCESS) {
            if (cv.CStatus == PDH_CSTATUS_VALID_DATA) {
                g_metrics.diskUsage = cv.doubleValue;
                if (g_metrics.diskUsage > 100.0) g_metrics.diskUsage = 100.0;
                if (g_metrics.diskUsage < 0.0)   g_metrics.diskUsage = 0.0;
            }
        }
    }

    ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;
    if (GetDiskFreeSpaceExW(g_config.targetDrive, &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
        g_metrics.diskFreeGB = (double)freeBytesAvailable.QuadPart / (1024.0 * 1024.0 * 1024.0);
    } else {
        g_metrics.diskFreeGB = 0.0;
    }
}

void UpdateCPU() {
    if (!g_config.showCPU && !g_config.showCPUTemp) return;

    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        ULONGLONG idle = FileTimeToUint64(idleTime);
        ULONGLONG kernel = FileTimeToUint64(kernelTime);
        ULONGLONG user = FileTimeToUint64(userTime);

        ULONGLONG prevIdle = FileTimeToUint64(g_prevIdleTime);
        ULONGLONG prevKernel = FileTimeToUint64(g_prevKernelTime);
        ULONGLONG prevUser = FileTimeToUint64(g_prevUserTime);

        ULONGLONG total = (kernel - prevKernel) + (user - prevUser);
        ULONGLONG totalIdle = idle - prevIdle;

        if (total > 0 && prevKernel != 0) {
            g_metrics.cpuUsage = (double)(total - totalIdle) * 100.0 / total;
            if (g_metrics.cpuUsage < 0.0)   g_metrics.cpuUsage = 0.0;
            if (g_metrics.cpuUsage > 100.0) g_metrics.cpuUsage = 100.0;
        }

        g_prevIdleTime = idleTime;
        g_prevKernelTime = kernelTime;
        g_prevUserTime = userTime;
    }

    UpdateCPUTemp();
}

void UpdateMemory() {
    if (!g_config.showRAM) return;

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        g_metrics.memUsage = (double)memInfo.dwMemoryLoad;
        g_metrics.memUsedGB = (double)(memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
}

void UpdateNetwork() {
    if (!g_config.showNet) return;

    PMIB_IF_TABLE2 pIfTable = NULL;
    if (GetIfTable2(&pIfTable) == NO_ERROR) {
        ULONGLONG now = GetTickCount64();
        double elapsedSec = (g_prevTickCount != 0 && now > g_prevTickCount) 
                            ? (double)(now - g_prevTickCount) / 1000.0 
                            : 0.0;

        ULONG64 deltaInTotal = 0;
        ULONG64 deltaOutTotal = 0;
        std::unordered_map<ULONG64, IfaceTraffic> currentIfaces;

        for (ULONG i = 0; i < pIfTable->NumEntries; i++) {
            MIB_IF_ROW2* row = &pIfTable->Table[i];
            
            if (row->Type == IF_TYPE_SOFTWARE_LOOPBACK || 
                row->Type == IF_TYPE_TUNNEL || 
                row->InterfaceAndOperStatusFlags.FilterInterface || 
                row->OperStatus != IfOperStatusUp) {
                continue;
            }

            ULONG64 luidKey = row->InterfaceLuid.Value;
            currentIfaces[luidKey] = { row->InOctets, row->OutOctets };

            auto it = g_prevIfaces.find(luidKey);
            if (it != g_prevIfaces.end() && elapsedSec > 0.0) {
                if (row->InOctets >= it->second.inBytes) {
                    deltaInTotal += (row->InOctets - it->second.inBytes);
                }
                if (row->OutOctets >= it->second.outBytes) {
                    deltaOutTotal += (row->OutOctets - it->second.outBytes);
                }
            }
        }
        FreeMibTable(pIfTable);

        if (elapsedSec > 0.0) {
            g_metrics.downloadSpeed = (double)deltaInTotal / elapsedSec;
            g_metrics.uploadSpeed = (double)deltaOutTotal / elapsedSec;
        } else {
            g_metrics.downloadSpeed = 0.0;
            g_metrics.uploadSpeed = 0.0;
        }

        g_prevIfaces = std::move(currentIfaces);
        g_prevTickCount = now;
    }
}

void UpdateBattery() {
    if (!g_config.showBattery) return;

    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        if (sps.BatteryFlag != 128 && sps.BatteryFlag != 255 && sps.BatteryLifePercent != 255) {
            g_metrics.hasBattery = true;
            g_metrics.batteryPercent = (double)sps.BatteryLifePercent;
            g_metrics.batteryCharging = (sps.BatteryFlag & 8) != 0 || (sps.ACLineStatus == 1 && sps.BatteryLifePercent < 100);
        } else if (sps.ACLineStatus == 1) {
            g_metrics.hasBattery = false;
            g_metrics.batteryPercent = 100.0;
            g_metrics.batteryCharging = false;
        } else {
            g_metrics.hasBattery = false;
        }
    }
}

void UpdateAllMetrics() {
    UpdateCPU();
    UpdateGPU();
    UpdateDisk();
    UpdateMemory();
    UpdateNetwork();
    UpdateBattery();
}

void FormatSpeed(double speedBytes, wchar_t* outBuf, size_t size) {
    if (speedBytes < 0) speedBytes = 0;

    if (g_config.netUnit == NET_UNIT_BITS) {
        double bits = speedBytes * 8.0;
        if (bits >= 1000000000.0) {
            swprintf(outBuf, size, L"%.1f Gbps", bits / 1000000000.0);
        } else if (bits >= 1000000.0) {
            double mb = bits / 1000000.0;
            if (mb >= 100.0) swprintf(outBuf, size, L"%.0f Mbps", mb);
            else             swprintf(outBuf, size, L"%.1f Mbps", mb);
        } else {
            swprintf(outBuf, size, L"%.0f Kbps", bits / 1000.0);
        }
    } else {
        double kb = speedBytes / 1024.0;
        if (kb >= 1048576.0) {
            swprintf(outBuf, size, L"%.1f GB/s", kb / 1048576.0);
        } else if (kb >= 1024.0) {
            double mb = kb / 1024.0;
            if (mb >= 100.0) swprintf(outBuf, size, L"%.0f MB/s", mb);
            else             swprintf(outBuf, size, L"%.1f MB/s", mb);
        } else {
            swprintf(outBuf, size, L"%.0f KB/s", kb);
        }
    }
}