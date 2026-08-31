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
#include <commdlg.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <dwmapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <shellapi.h>
#include <wbemidl.h>
#include <stdio.h>
#include <tchar.h>
#include <math.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_EXCLUDED_FROM_PEEK
#define DWMWA_EXCLUDED_FROM_PEEK 12
#endif

#define MONITOR_HEIGHT 36
#define COLOR_TRANSPARENT_KEY RGB(1, 1, 1)

#define TIMER_METRICS 1
#define WM_TRAYICON (WM_USER + 1)

// Menu Commands
#define ID_TRAY_SETTINGS     200
#define ID_TRAY_EXIT         201
#define ID_TRAY_TASKMGR      202
#define ID_TRAY_REFRESH      203
#define ID_TRAY_CLICKTHROUGH 204

// GUI Settings Control IDs
#define IDC_TABCONTROL       1000
#define IDC_CHK_NET          1001
#define IDC_CHK_CPU          1002
#define IDC_CHK_GPU          1003
#define IDC_CHK_RAM          1004
#define IDC_CHK_DISK         1005
#define IDC_CHK_BATTERY      1006
#define IDC_CHK_UPTIME       1007
#define IDC_CHK_PROCESS      1008
#define IDC_CHK_CPUTEMP      1009
#define IDC_CHK_GPUTEMP      1017

#define IDC_COMBO_DRIVE      1010
#define IDC_COMBO_NETUNIT    1011
#define IDC_COMBO_ALIGN      1012
#define IDC_EDIT_OFFSETX     1013
#define IDC_EDIT_OFFSETY     1014
#define IDC_EDIT_SPACING     1015
#define IDC_CHK_DIVIDERS     1016

#define IDC_COMBO_FONT       1020
#define IDC_EDIT_FONTSIZE    1021
#define IDC_COMBO_FONTWEIGHT 1022

#define IDC_COMBO_THEME      1030
#define IDC_BTN_COL_LABEL    1031
#define IDC_BTN_COL_VALUE    1032
#define IDC_BTN_COL_UP       1033
#define IDC_BTN_COL_DOWN     1034
#define IDC_BTN_COL_DIV      1035
#define IDC_BTN_COL_BG       1036
#define IDC_CHK_TRANS_BG     1037
#define IDC_CHK_AUTOCONTRAST 1038

#define IDC_EDIT_RATE        1040
#define IDC_CHK_AUTOSTART    1041
#define IDC_CHK_CLICKTHROUGH 1042

#define IDC_BTN_APPLY        1050
#define IDC_BTN_SAVE         1051
#define IDC_BTN_CANCEL       1052
#define IDC_BTN_DEFAULTS     1053