#pragma once
#include "Common.h"

struct MonitorConfig {
    bool showNet;
    bool showCompute;
    bool showMemory;
    bool showDisk;
    bool showSystem;
    wchar_t targetDrive[8];
    int leftMargin;
    int refreshRateMs;
};

extern MonitorConfig g_config;
extern int g_curWidth;

void LoadConfig();
void SaveConfig();
int CalculateTotalWidth();

// Autostart Management
bool IsAutostartEnabled();
void SetAutostart(bool enable);
void CheckAndPromptAutostart(HWND hWndParent);