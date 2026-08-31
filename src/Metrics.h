#pragma once
#include "Common.h"

struct SystemMetrics {
    double cpuUsage;
    double cpuTemp;       // Temperature in Celsius (-1 if unsupported)
    double gpuUsage;
    double gpuTemp;       // Temperature in Celsius (-1 if unsupported)
    double memUsage;
    double memUsedGB;
    double diskUsage;
    double diskFreeGB;
    DWORD  processCount;
    int    batteryPercent;
    double uploadSpeed;    // Bytes per sec
    double downloadSpeed;  // Bytes per sec
};

extern SystemMetrics g_metrics;

void InitMetrics();
void CleanupMetrics();
void UpdateCPU();
void UpdateCPUTemp();
void UpdateGPU();
void UpdateGPUTemp();
void UpdateDisk();
void UpdateMemory();
void UpdateNetwork();
void UpdateSystemStats();
void UpdateAllMetrics();
void FormatSpeed(double speedBytes, wchar_t* outBuf, size_t size);