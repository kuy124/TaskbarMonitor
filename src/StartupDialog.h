#pragma once

#include "Common.h"
#include "Config.h"

bool ShowAutostartDialog(HINSTANCE hInstance, HWND hOwnerWnd, const AutostartInfo& initialInfo);
// Only a new Yes authorizes installing the sensor driver as part of setup.
bool AskForTemperatureChoice(HWND owner);
