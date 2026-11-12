#include "Metrics.h"
#include "Config.h"
#include "TemperatureEstimate.h"
#include "NetworkRate.h"
#include "GpuUsage.h"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <system_error>

SystemMetrics g_metrics = { 0.0, -1.0, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 100.0, false, false, false, false };

namespace sampling {
// Only the worker touches these copies or the sensor handles below.
static MonitorConfig g_config;
static SystemMetrics g_metrics = ::g_metrics;

static FILETIME g_prevIdleTime = {0}, g_prevKernelTime = {0}, g_prevUserTime = {0};
static PDH_HQUERY g_hPdhQuery = NULL;
static PDH_HCOUNTER g_hGpuCounter = NULL;
static PDH_HCOUNTER g_hDiskCounter = NULL;

static NetworkRate g_networkRate;
static bool g_pdhReady = false;
static ULONGLONG g_lastSensorPoll = 0;
static SensorTemperatures g_localTemperatures;
static HardwareSensorStatus g_sensorStatus;

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

void InitMetrics() {
    g_prevIdleTime = g_prevKernelTime = g_prevUserTime = {};
    g_networkRate.Reset();
    g_lastSensorPoll = 0;
    g_localTemperatures = {};
    g_sensorStatus = {};
    g_nvmlInitialized = false;
    g_nvmlDevice = NULL;
    g_nvGpuCount = 0;
    if (PdhOpenQuery(NULL, 0, &g_hPdhQuery) == ERROR_SUCCESS) {
        PdhAddEnglishCounterW(g_hPdhQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_hGpuCounter);
        PdhAddEnglishCounterW(g_hPdhQuery, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &g_hDiskCounter);
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

}

void CleanupMetrics() {
    if (g_hPdhQuery) {
        PdhCloseQuery(g_hPdhQuery);
        g_hPdhQuery = NULL;
        g_hGpuCounter = g_hDiskCounter = NULL;
    }

    if (g_nvmlInitialized && g_nvmlShutdown) g_nvmlShutdown();
    g_nvmlInitialized = false;
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

    if (g_config.useLibreHardware && g_lastSensorPoll != 0 && g_localTemperatures.cpu >= 0.0) {
        g_metrics.cpuTemp = g_localTemperatures.cpu;
        g_metrics.cpuTempEstimated = false;
        return;
    }

    // ACPI thermal zones are not identified CPU sensors; never label them as a CPU reading.
    g_metrics.cpuTemp = EstimateTemperature(g_metrics.cpuUsage, g_metrics.cpuTemp,
                                            g_metrics.cpuTempEstimated, 38.0, 42.0);
    g_metrics.cpuTempEstimated = true;
}

void UpdateGPUTemp() {
    if (!g_config.showGPUTemp) return;

    if (g_config.useLibreHardware && g_lastSensorPoll != 0 && g_localTemperatures.gpu >= 0.0) {
        g_metrics.gpuTemp = g_localTemperatures.gpu;
        g_metrics.gpuTempEstimated = false;
        return;
    }

    bool foundSensor = false;

    // 1. Query NVIDIA NVML
    if (g_nvmlInitialized && g_nvmlDevice && g_nvmlDeviceGetTemperature) {
        unsigned int temp = 0;
        if (g_nvmlDeviceGetTemperature(g_nvmlDevice, 0, &temp) == 0 && temp > 0 && temp < 125) {
            g_metrics.gpuTemp = (double)temp;
            g_metrics.gpuTempEstimated = false;
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
                g_metrics.gpuTempEstimated = false;
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
                g_metrics.gpuTempEstimated = false;
                foundSensor = true;
            }
        }
    }

    if (!foundSensor) {
        g_metrics.gpuTemp = EstimateTemperature(g_metrics.gpuUsage, g_metrics.gpuTemp,
                                                g_metrics.gpuTempEstimated, 36.0, 38.0);
        g_metrics.gpuTempEstimated = true;
    }
}

void UpdateGPU() {
    if (!g_config.showGPU && !g_config.showGPUTemp) return;

    g_metrics.gpuUsage = -1.0;
    if (g_pdhReady && g_hGpuCounter) {
            DWORD bufferSize = 0, itemCount = 0;
            PDH_STATUS status = PdhGetFormattedCounterArrayW(g_hGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, NULL);
            if (static_cast<DWORD>(status) == PDH_MORE_DATA && bufferSize > 0) {
                PDH_FMT_COUNTERVALUE_ITEM_W* pItems = (PDH_FMT_COUNTERVALUE_ITEM_W*)malloc(bufferSize);
                if (pItems) {
                    if (PdhGetFormattedCounterArrayW(g_hGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, pItems) == ERROR_SUCCESS) {
                        GpuUsage usage;
                        for (DWORD i = 0; i < itemCount; i++) {
                            if (pItems[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
                                pItems[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) {
                                usage.Add(pItems[i].szName, pItems[i].FmtValue.doubleValue);
                            }
                        }
                        g_metrics.gpuUsage = usage.Usage();
                    }
                    free(pItems);
                }
            }
    }
    UpdateGPUTemp();
}

void UpdateDisk() {
    if (!g_config.showDisk) return;

    g_metrics.diskUsage = -1.0;
    if (g_pdhReady && g_hDiskCounter) {
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
        g_metrics.diskFreeGB = -1.0;
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
    if (!g_config.showNet) {
        g_networkRate.Reset();
        return;
    }

    PMIB_IF_TABLE2 pIfTable = NULL;
    if (GetIfTable2(&pIfTable) == NO_ERROR) {
        ULONGLONG now = GetTickCount64();
        NetworkRate::Snapshot currentIfaces;

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

        }
        FreeMibTable(pIfTable);

        g_networkRate.Sample(std::move(currentIfaces), now);
        g_metrics.downloadSpeed = g_networkRate.download;
        g_metrics.uploadSpeed = g_networkRate.upload;
    } else {
        g_metrics.downloadSpeed = -1.0;
        g_metrics.uploadSpeed = -1.0;
        g_networkRate.Reset();
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
    g_pdhReady = g_hPdhQuery && PdhCollectQueryData(g_hPdhQuery) == ERROR_SUCCESS;
    ULONGLONG now = GetTickCount64();
    if (g_config.useLibreHardware) {
        if (g_lastSensorPoll == 0 || now - g_lastSensorPoll >= 2000) {
            g_lastSensorPoll = now;
            if (g_config.showCPUTemp || g_config.showGPUTemp) {
                g_sensorStatus = ReadHardwareTemperatures();
                g_localTemperatures = g_sensorStatus.temperatures;
            } else g_localTemperatures = {};
        }
    } else {
        g_lastSensorPoll = 0;
        g_localTemperatures = {};
    }
    UpdateCPU();
    UpdateGPU();
    UpdateDisk();
    UpdateMemory();
    UpdateNetwork();
    UpdateBattery();
}

// ponytail: one worker coalesces refresh requests; split backends if sampling latency demands it.
static std::mutex g_mutex;
static std::condition_variable g_wake;
static std::thread g_thread;
static MonitorConfig g_pendingConfig;
static SystemMetrics g_snapshot = ::g_metrics;
static HardwareSensorStatus g_sensorSnapshot;
static bool g_requested = false;
static bool g_stop = false;
static bool g_probeRequested = false;
static DWORD g_threadId = 0;
static HWND g_notifyWindow = NULL;

static void Run() {
    HRESULT com = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool cancellable = SUCCEEDED(com) && SUCCEEDED(CoEnableCallCancellation(NULL));
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_threadId = GetCurrentThreadId();
    }
    InitMetrics();
    for (;;) {
        MonitorConfig config;
        bool sample;
        bool probe;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_wake.wait(lock, [] { return g_stop || g_requested || g_probeRequested; });
            if (g_stop) break;
            config = g_pendingConfig;
            sample = g_requested;
            probe = g_probeRequested;
            g_requested = false;
            g_probeRequested = false;
        }
        if (sample) {
            g_config = config;
            UpdateAllMetrics();
        }
        if (probe) g_sensorStatus = ReadHardwareTemperatures();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_snapshot = g_metrics;
            if (!g_probeRequested) g_sensorSnapshot = g_sensorStatus;
        }
        if (g_notifyWindow) PostMessageW(g_notifyWindow, WM_METRICS_READY, 0, 0);
    }
    CleanupMetrics();
    CloseHardwareSensors();
    if (cancellable) CoDisableCallCancellation(NULL);
    if (SUCCEEDED(com)) CoUninitialize();
}
} // namespace sampling

bool InitMetrics(HWND notifyWindow) {
    std::lock_guard<std::mutex> lock(sampling::g_mutex);
    if (sampling::g_thread.joinable()) return true;
    sampling::g_pendingConfig = g_config;
    sampling::g_config = g_config;
    sampling::g_metrics = sampling::g_snapshot = g_metrics;
    sampling::g_sensorSnapshot = {};
    sampling::g_notifyWindow = notifyWindow;
    sampling::g_threadId = 0;
    sampling::g_probeRequested = false;
    sampling::g_stop = false;
    sampling::g_requested = true;
    try { sampling::g_thread = std::thread(sampling::Run); }
    catch (const std::system_error&) { return false; }
    return true;
}

void CleanupMetrics() {
    DWORD threadId;
    {
        std::lock_guard<std::mutex> lock(sampling::g_mutex);
        sampling::g_stop = true;
        threadId = sampling::g_threadId;
    }
    sampling::g_wake.notify_one();
    if (threadId) CoCancelCall(threadId, 0);
    if (sampling::g_thread.joinable()) sampling::g_thread.join();
}

void UpdateAllMetrics() {
    std::lock_guard<std::mutex> lock(sampling::g_mutex);
    if (!sampling::g_thread.joinable()) return;
    sampling::g_pendingConfig = g_config;
    sampling::g_requested = true;
    sampling::g_wake.notify_one();
}

SystemMetrics GetLatestMetrics() {
    std::lock_guard<std::mutex> lock(sampling::g_mutex);
    return sampling::g_snapshot;
}

bool RequestHardwareSensorRefresh() {
    std::lock_guard<std::mutex> lock(sampling::g_mutex);
    if (!sampling::g_thread.joinable()) return false;
    sampling::g_probeRequested = true;
    sampling::g_sensorSnapshot = {};
    sampling::g_wake.notify_one();
    return true;
}

HardwareSensorStatus GetHardwareSensorStatus() {
    std::lock_guard<std::mutex> lock(sampling::g_mutex);
    return sampling::g_sensorSnapshot;
}

void FormatSpeed(double speedBytes, wchar_t* outBuf, size_t size) {
    FormatNetworkSpeed(speedBytes, g_config.netUnit == NET_UNIT_BITS, outBuf, size);
}
