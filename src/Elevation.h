#pragma once
#include "Common.h"

bool IsProcessElevated();
bool ElevationRestartPending();
bool RequestElevatedRestart(HWND owner, HWND host);
void PollElevatedRestart(HWND host);
void CancelElevatedRestart();
bool AcceptElevatedRestart(bool& requested);
