#pragma once
#include "Common.h"
#include "HardwareSensors.h"

struct SystemMetrics {
    double cpuUsage;
    double cpuTemp;       // Temperature in Celsius
    double gpuUsage;
    double gpuTemp;       // Temperature in Celsius
    double memUsage;
    double memUsedGB;
    double diskUsage;
    double diskFreeGB;
    double uploadSpeed;    // Bytes per sec
    double downloadSpeed;  // Bytes per sec
    double batteryPercent; // 0 - 100
    bool batteryCharging;
    bool hasBattery;
    bool cpuTempEstimated;
    bool gpuTempEstimated;
};

extern SystemMetrics g_metrics;

bool InitMetrics(HWND notifyWindow);
void CleanupMetrics();
void UpdateAllMetrics();
SystemMetrics GetLatestMetrics();
bool RequestHardwareSensorRefresh();
HardwareSensorStatus GetHardwareSensorStatus();
void FormatSpeed(double speedBytes, wchar_t* outBuf, size_t size);
