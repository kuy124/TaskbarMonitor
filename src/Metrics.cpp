#include "Metrics.h"
#include "Config.h"

SystemMetrics g_metrics = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0, -1, 0.0, 0.0 };

static FILETIME g_prevIdleTime = {0}, g_prevKernelTime = {0}, g_prevUserTime = {0};
static PDH_HQUERY g_hPdhQuery = NULL;
static PDH_HCOUNTER g_hGpuCounter = NULL;
static PDH_HCOUNTER g_hDiskCounter = NULL;

static ULONG64 g_prevInBytes = 0;
static ULONG64 g_prevOutBytes = 0;
static ULONGLONG g_prevTickCount = 0;

static ULONGLONG FileTimeToUint64(const FILETIME& ft) {
    return (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

void InitMetrics() {
    if (PdhOpenQuery(NULL, 0, &g_hPdhQuery) == ERROR_SUCCESS) {
        PdhAddEnglishCounterW(g_hPdhQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_hGpuCounter);
        PdhAddEnglishCounterW(g_hPdhQuery, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &g_hDiskCounter);
        PdhCollectQueryData(g_hPdhQuery);
    }
}

void CleanupMetrics() {
    if (g_hPdhQuery) {
        PdhCloseQuery(g_hPdhQuery);
        g_hPdhQuery = NULL;
    }
}

void UpdateGPU() {
    if (!g_hPdhQuery || !g_hGpuCounter || !g_config.showGPU) return;

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

void UpdateSystemStats() {
    if (g_config.showProcess) {
        PERFORMANCE_INFORMATION pi = { sizeof(PERFORMANCE_INFORMATION) };
        if (GetPerformanceInfo(&pi, sizeof(pi))) {
            g_metrics.processCount = pi.ProcessCount;
        }
    }

    if (g_config.showBattery) {
        SYSTEM_POWER_STATUS sps;
        if (GetSystemPowerStatus(&sps)) {
            if (sps.BatteryLifePercent != 255 && sps.BatteryFlag != 128) {
                g_metrics.batteryPercent = sps.BatteryLifePercent;
            } else {
                g_metrics.batteryPercent = -1;
            }
        }
    }
}

void UpdateCPU() {
    if (!g_config.showCPU) return;

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
        ULONG64 totalIn = 0, totalOut = 0;

        for (ULONG i = 0; i < pIfTable->NumEntries; i++) {
            MIB_IF_ROW2* row = &pIfTable->Table[i];
            if (row->Type != IF_TYPE_SOFTWARE_LOOPBACK && row->OperStatus == IfOperStatusUp) {
                totalIn += row->InOctets;
                totalOut += row->OutOctets;
            }
        }
        FreeMibTable(pIfTable);

        ULONGLONG now = GetTickCount64();
        if (g_prevTickCount != 0 && now > g_prevTickCount) {
            double elapsedSec = (now - g_prevTickCount) / 1000.0;
            g_metrics.downloadSpeed = (double)(totalIn - g_prevInBytes) / elapsedSec;
            g_metrics.uploadSpeed = (double)(totalOut - g_prevOutBytes) / elapsedSec;
            if (g_metrics.downloadSpeed < 0) g_metrics.downloadSpeed = 0;
            if (g_metrics.uploadSpeed < 0)   g_metrics.uploadSpeed = 0;
        }

        g_prevInBytes = totalIn;
        g_prevOutBytes = totalOut;
        g_prevTickCount = now;
    }
}

void UpdateAllMetrics() {
    UpdateCPU();
    UpdateGPU();
    UpdateDisk();
    UpdateMemory();
    UpdateNetwork();
    UpdateSystemStats();
}

// Clear, explicit speed units (KB/s, MB/s, GB/s or Kb/s, Mb/s, Gb/s)
void FormatSpeed(double speedBytes, wchar_t* outBuf, size_t size) {
    if (speedBytes < 0) speedBytes = 0;

    if (g_config.netUnit == NET_UNIT_BITS) {
        double bits = speedBytes * 8.0;
        if (bits >= 1000000000.0) {
            swprintf(outBuf, size, L"%.1f Gb/s", bits / 1000000000.0);
        } else if (bits >= 1000000.0) {
            double mb = bits / 1000000.0;
            if (mb >= 100.0) swprintf(outBuf, size, L"%.0f Mb/s", mb);
            else             swprintf(outBuf, size, L"%.1f Mb/s", mb);
        } else {
            swprintf(outBuf, size, L"%.0f Kb/s", bits / 1000.0);
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