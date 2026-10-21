#pragma once
#include "Common.h"

extern HWND g_hTaskbar;

void InitTaskbarHooks(HWND hWnd);
void CleanupTaskbarHooks();
void ShutdownTaskbarHooks();
void SyncWithTaskbar(HWND hWnd);
void TickTaskbarPlacement(HWND hWnd);
bool IsAttachedToTaskbar(HWND hWnd, HWND taskbar);
