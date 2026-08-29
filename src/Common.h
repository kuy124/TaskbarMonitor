#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <dwmapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <tchar.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_EXCLUDED_FROM_PEEK
#define DWMWA_EXCLUDED_FROM_PEEK 12
#endif

#define MONITOR_HEIGHT 30
#define COLOR_TRANSPARENT_KEY RGB(1, 1, 1)

#define TIMER_METRICS 1
#define TIMER_FAST_SYNC 2
#define WM_TRAYICON (WM_USER + 1)

// Menu Commands
#define ID_TRAY_SETTINGS 200
#define ID_TRAY_EXIT     201
#define ID_TRAY_TASKMGR  202
#define ID_TRAY_REFRESH  203

// GUI Settings Controls
#define IDC_CHK_NET      1001
#define IDC_CHK_COMPUTE  1002
#define IDC_CHK_MEMORY   1003
#define IDC_CHK_DISK     1004
#define IDC_CHK_SYSTEM   1005
#define IDC_COMBO_DRIVE  1006
#define IDC_COMBO_MARGIN 1007
#define IDC_COMBO_RATE   1008
#define IDC_BTN_SAVE     1009
#define IDC_BTN_CANCEL   1010