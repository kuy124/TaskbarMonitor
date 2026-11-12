#pragma once
#include "Common.h"
#include <string>

struct SensorTemperatures {
    double cpu = -1.0;
    double gpu = -1.0;
};

struct HardwareSensorStatus {
    SensorTemperatures temperatures;
    bool checked = false;
    bool running = false;
    bool driverInstalled = false;
    bool administrator = false;
    HRESULT error = S_OK;
    std::wstring noticesPath;
    std::wstring cpuSource;
    std::wstring gpuSource;
};

HardwareSensorStatus ReadHardwareTemperatures();
void CloseHardwareSensors();
bool CheckEmbeddedSensorBridge();
