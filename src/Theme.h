#pragma once
#include "Common.h"

struct ThemeColors {
    COLORREF divider;
    COLORREF label;
    COLORREF value;
    COLORREF upload;
    COLORREF download;
};

extern ThemeColors g_theme;

void UpdateThemeColors();