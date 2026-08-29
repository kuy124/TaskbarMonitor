#pragma once
#include "Common.h"

extern HWND g_hTaskbar;

void InitTaskbarHooks(HWND hWnd);
void CleanupTaskbarHooks();
void SyncWithTaskbar(HWND hWnd);