#pragma once
#include "Common.h"

struct HardwareSensorStatus;
enum class SensorAccessRequest { None, SavedChoice, JustApproved };

bool IsProcessElevated();
bool ElevationRestartPending();
bool RequestElevatedRestart(HWND settingsWindow, HWND host, bool installCpuDriver = false);
void CompleteSensorAccessRequest(SensorAccessRequest& request, HWND host, const HardwareSensorStatus& status,
                                bool missingCpu, bool missingGpu);
void PollElevatedRestart(HWND host);
void CancelElevatedRestart();
bool AcceptElevatedRestart(bool& requested, bool& installCpuDriver);
