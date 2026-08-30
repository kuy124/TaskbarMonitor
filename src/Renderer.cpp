#include "Renderer.h"
#include "Config.h"
#include "Theme.h"
#include "Metrics.h"

void RenderOverlay(HWND hWnd, HDC hdc) {
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, g_curWidth, MONITOR_HEIGHT);
    HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

    // Background Render
    COLORREF fillBg = g_config.transparentBg ? COLOR_TRANSPARENT_KEY : g_theme.background;
    HBRUSH bgBrush = CreateSolidBrush(fillBg);
    RECT clientRect = { 0, 0, g_curWidth, MONITOR_HEIGHT };
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    // True High-DPI Point-to-Pixel Font Scaling
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    if (dpiY == 0) dpiY = 96;
    int fontHeight = -MulDiv(g_config.fontSize, dpiY, 72);

    HFONT hFontLabel = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_config.fontFamily);

    HFONT hFontValue = CreateFontW(fontHeight, 0, 0, 0, g_config.fontWeight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_config.fontFamily);

    SetBkMode(memDC, TRANSPARENT);

    // Calculate dynamic vertical row positions using actual font text metrics to prevent clipping
    SelectObject(memDC, hFontValue);
    TEXTMETRIC tm;
    GetTextMetricsW(memDC, &tm);

    int row1Y = (MONITOR_HEIGHT / 2) - tm.tmHeight + 1;
    int row2Y = (MONITOR_HEIGHT / 2);
    if (row1Y < 0) row1Y = 0;

    int curX = 6;
    bool hasPrev = false;

    auto DrawDivider = [&](int x) {
        if (!g_config.showDividers) return;
        HPEN dividerPen = CreatePen(PS_SOLID, 1, g_theme.divider);
        HGDIOBJ oldDivPen = SelectObject(memDC, dividerPen);
        MoveToEx(memDC, x, 4, NULL);
        LineTo(memDC, x, MONITOR_HEIGHT - 4);
        SelectObject(memDC, oldDivPen);
        DeleteObject(dividerPen);
    };

    // 1. Network
    if (g_config.showNet) {
        if (hasPrev) { DrawDivider(curX - (g_config.itemSpacing / 2)); }
        wchar_t upSpeed[16], dnSpeed[16], netLine1[32], netLine2[32];
        FormatSpeed(g_metrics.uploadSpeed, upSpeed, 16);
        FormatSpeed(g_metrics.downloadSpeed, dnSpeed, 16);
        swprintf(netLine1, 32, L"▲%ls", upSpeed);
        swprintf(netLine2, 32, L"▼%ls", dnSpeed);

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.upload);
        TextOutW(memDC, curX, row1Y, netLine1, (int)wcslen(netLine1));
        SetTextColor(memDC, g_theme.download);
        TextOutW(memDC, curX, row2Y, netLine2, (int)wcslen(netLine2));

        SIZE sz1, sz2;
        GetTextExtentPoint32W(memDC, netLine1, (int)wcslen(netLine1), &sz1);
        GetTextExtentPoint32W(memDC, netLine2, (int)wcslen(netLine2), &sz2);
        int blockW = (sz1.cx > sz2.cx ? sz1.cx : sz2.cx) + 4;

        curX += blockW + g_config.itemSpacing;
        hasPrev = true;
    }

    // 2. CPU / GPU
    if (g_config.showCPU || g_config.showGPU) {
        if (hasPrev) { DrawDivider(curX - (g_config.itemSpacing / 2)); }
        wchar_t line1Lbl[8] = L"CPU", line2Lbl[8] = L"GPU";
        wchar_t line1Val[16] = {0}, line2Val[16] = {0};

        if (g_config.showCPU && g_config.showGPU) {
            swprintf(line1Val, 16, L"%2.0f%%", g_metrics.cpuUsage);
            swprintf(line2Val, 16, L"%2.0f%%", g_metrics.gpuUsage);
        } else if (g_config.showCPU) {
            swprintf(line1Val, 16, L"%2.0f%%", g_metrics.cpuUsage);
            wcscpy_s(line2Lbl, L"");
        } else {
            wcscpy_s(line1Lbl, L"GPU");
            swprintf(line1Val, 16, L"%2.0f%%", g_metrics.gpuUsage);
            wcscpy_s(line2Lbl, L"");
        }

        SelectObject(memDC, hFontLabel);
        SetTextColor(memDC, g_theme.label);
        TextOutW(memDC, curX, row1Y, line1Lbl, (int)wcslen(line1Lbl));
        if (wcslen(line2Lbl) > 0) TextOutW(memDC, curX, row2Y, line2Lbl, (int)wcslen(line2Lbl));

        SIZE szLbl;
        GetTextExtentPoint32W(memDC, L"CPU", 3, &szLbl);
        int valX = curX + szLbl.cx + 5;

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        TextOutW(memDC, valX, row1Y, line1Val, (int)wcslen(line1Val));
        if (wcslen(line2Val) > 0) TextOutW(memDC, valX, row2Y, line2Val, (int)wcslen(line2Val));

        SIZE szVal;
        GetTextExtentPoint32W(memDC, L"100%", 4, &szVal);
        curX = valX + szVal.cx + 4 + g_config.itemSpacing;
        hasPrev = true;
    }

    // 3. RAM
    if (g_config.showRAM) {
        if (hasPrev) { DrawDivider(curX - (g_config.itemSpacing / 2)); }
        wchar_t ramVal[16], memGbVal[16];
        swprintf(ramVal, 16, L"%2.0f%%", g_metrics.memUsage);
        swprintf(memGbVal, 16, L"%.1fG", g_metrics.memUsedGB);

        SelectObject(memDC, hFontLabel);
        SetTextColor(memDC, g_theme.label);
        TextOutW(memDC, curX, row1Y, L"RAM", 3);
        TextOutW(memDC, curX, row2Y, L"USE", 3);

        SIZE szLbl;
        GetTextExtentPoint32W(memDC, L"RAM", 3, &szLbl);
        int valX = curX + szLbl.cx + 5;

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        TextOutW(memDC, valX, row1Y, ramVal, (int)wcslen(ramVal));
        TextOutW(memDC, valX, row2Y, memGbVal, (int)wcslen(memGbVal));

        SIZE szVal1, szVal2;
        GetTextExtentPoint32W(memDC, ramVal, (int)wcslen(ramVal), &szVal1);
        GetTextExtentPoint32W(memDC, memGbVal, (int)wcslen(memGbVal), &szVal2);
        int maxValW = szVal1.cx > szVal2.cx ? szVal1.cx : szVal2.cx;

        curX = valX + maxValW + 4 + g_config.itemSpacing;
        hasPrev = true;
    }

    // 4. Disk
    if (g_config.showDisk) {
        if (hasPrev) { DrawDivider(curX - (g_config.itemSpacing / 2)); }
        wchar_t dskVal[16], diskFreeVal[16], diskDriveLabel[8];
        swprintf(dskVal, 16, L"%2.0f%%", g_metrics.diskUsage);
        swprintf(diskDriveLabel, 8, L"%c:", g_config.targetDrive[0]);
        if (g_metrics.diskFreeGB >= 1000.0) swprintf(diskFreeVal, 16, L"%.1fT", g_metrics.diskFreeGB / 1024.0);
        else swprintf(diskFreeVal, 16, L"%.0fG", g_metrics.diskFreeGB);

        SelectObject(memDC, hFontLabel);
        SetTextColor(memDC, g_theme.label);
        TextOutW(memDC, curX, row1Y, L"DSK", 3);
        TextOutW(memDC, curX, row2Y, diskDriveLabel, (int)wcslen(diskDriveLabel));

        SIZE szLbl;
        GetTextExtentPoint32W(memDC, L"DSK", 3, &szLbl);
        int valX = curX + szLbl.cx + 5;

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        TextOutW(memDC, valX, row1Y, dskVal, (int)wcslen(dskVal));
        TextOutW(memDC, valX, row2Y, diskFreeVal, (int)wcslen(diskFreeVal));

        SIZE szVal1, szVal2;
        GetTextExtentPoint32W(memDC, dskVal, (int)wcslen(dskVal), &szVal1);
        GetTextExtentPoint32W(memDC, diskFreeVal, (int)wcslen(diskFreeVal), &szVal2);
        int maxValW = szVal1.cx > szVal2.cx ? szVal1.cx : szVal2.cx;

        curX = valX + maxValW + 4 + g_config.itemSpacing;
        hasPrev = true;
    }

    // 5. System Stats (Process / Battery / Uptime)
    if (g_config.showProcess || g_config.showBattery || g_config.showUptime) {
        if (hasPrev) { DrawDivider(curX - (g_config.itemSpacing / 2)); }

        wchar_t l1Lbl[8] = L"PRC", l2Lbl[8] = L"UP";
        wchar_t l1Val[16] = {0}, l2Val[16] = {0};

        if (g_config.showProcess) {
            swprintf(l1Val, 16, L"%lu", g_metrics.processCount);
        } else if (g_config.showBattery && g_metrics.batteryPercent >= 0) {
            wcscpy_s(l1Lbl, L"BAT");
            swprintf(l1Val, 16, L"%d%%", g_metrics.batteryPercent);
        }

        if (g_config.showBattery && g_metrics.batteryPercent >= 0 && wcscmp(l1Lbl, L"BAT") != 0) {
            wcscpy_s(l2Lbl, L"BAT");
            swprintf(l2Val, 16, L"%d%%", g_metrics.batteryPercent);
        } else if (g_config.showUptime) {
            wcscpy_s(l2Lbl, L"UP");
            ULONGLONG totalSecs = GetTickCount64() / 1000;
            swprintf(l2Val, 16, L"%lluh%02llum", totalSecs / 3600, (totalSecs % 3600) / 60);
        }

        SelectObject(memDC, hFontLabel);
        SetTextColor(memDC, g_theme.label);
        if (wcslen(l1Val) > 0) TextOutW(memDC, curX, row1Y, l1Lbl, (int)wcslen(l1Lbl));
        if (wcslen(l2Val) > 0) TextOutW(memDC, curX, row2Y, l2Lbl, (int)wcslen(l2Lbl));

        SIZE szLbl;
        GetTextExtentPoint32W(memDC, L"PRC", 3, &szLbl);
        int valX = curX + szLbl.cx + 5;

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        if (wcslen(l1Val) > 0) TextOutW(memDC, valX, row1Y, l1Val, (int)wcslen(l1Val));
        if (wcslen(l2Val) > 0) TextOutW(memDC, valX, row2Y, l2Val, (int)wcslen(l2Val));

        SIZE szVal1, szVal2;
        GetTextExtentPoint32W(memDC, l1Val, (int)wcslen(l1Val), &szVal1);
        GetTextExtentPoint32W(memDC, l2Val, (int)wcslen(l2Val), &szVal2);
        int maxValW = szVal1.cx > szVal2.cx ? szVal1.cx : szVal2.cx;

        curX = valX + maxValW + 4 + g_config.itemSpacing;
        hasPrev = true;
    }

    BitBlt(hdc, 0, 0, g_curWidth, MONITOR_HEIGHT, memDC, 0, 0, SRCCOPY);

    DeleteObject(hFontLabel);
    DeleteObject(hFontValue);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}