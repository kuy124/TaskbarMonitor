#pragma once
#include "Common.h"

struct SystemMetrics {
    double cpuUsage;
    double gpuUsage;
    double memUsage;
    double memUsedGB;
    double diskUsage;
    double diskFreeGB;
    DWORD  processCount;
    int    batteryPercent;
    double uploadSpeed;
    double downloadSpeed;
};

extern SystemMetrics g_metrics;

void InitMetrics();
void CleanupMetrics();
void UpdateCPU();
void UpdateGPU();
void UpdateDisk();
void UpdateMemory();
void UpdateNetwork();
void UpdateSystemStats();
void UpdateAllMetrics();
void FormatSpeed(double speedKB, wchar_t* outBuf, size_t size);