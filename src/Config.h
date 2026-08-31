#pragma once
#include "Common.h"

enum TaskbarAlignment {
    ALIGN_LEFT = 0,
    ALIGN_RIGHT = 1,
    ALIGN_CENTER = 2,
    ALIGN_CUSTOM = 3
};

enum ThemeMode {
    THEME_AUTO = 0,
    THEME_DARK = 1,
    THEME_LIGHT = 2,
    THEME_CUSTOM = 3
};

enum NetUnit {
    NET_UNIT_BYTES = 0, // KB/s, MB/s
    NET_UNIT_BITS = 1   // Kbps, Mbps
};

struct MonitorConfig {
    // Metric Visibility
    bool showNet;
    bool showCPU;
    bool showCPUTemp;
    bool showGPU;
    bool showGPUTemp;
    bool showRAM;
    bool showDisk;
    bool showBattery;
    bool showUptime;
    bool showProcess;

    // Storage
    wchar_t targetDrive[8];

    // Layout & Positioning
    int alignment;        // TaskbarAlignment
    int offsetX;          // X offset in pixels
    int offsetY;          // Y offset in pixels
    int itemSpacing;      // Spacing between columns
    bool showDividers;    // Show vertical line between columns

    // Typography
    wchar_t fontFamily[64];
    int fontSize;         // Point size (8 - 24)
    int fontWeight;       // FW_NORMAL, FW_MEDIUM, FW_SEMIBOLD, FW_BOLD

    // Units & Performance
    int refreshRateMs;    // Polling rate in ms (100 - 10000)
    int netUnit;          // NetUnit
    bool clickThrough;    // Mouse clicks pass through
    bool runAtStartup;

    // Theme & Colors
    int themeMode;        // ThemeMode
    bool transparentBg;   // True: transparent color-key, False: tinted background
    bool autoContrast;    // Automatically adjust font colors for high readability
    COLORREF colLabel;
    COLORREF colValue;
    COLORREF colNetUp;
    COLORREF colNetDown;
    COLORREF colDivider;
    COLORREF colBackground;
};

extern MonitorConfig g_config;
extern int g_curWidth;

void SetDefaults();
void LoadConfig();
void SaveConfig();
int CalculateTotalWidth(HDC hdc = NULL);

bool IsAutostartEnabled();
void SetAutostart(bool enable);