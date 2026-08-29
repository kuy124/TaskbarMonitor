#include "Renderer.h"
#include "Config.h"
#include "Theme.h"
#include "Metrics.h"

void RenderOverlay(HWND hWnd, HDC hdc) {
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, g_curWidth, MONITOR_HEIGHT);
    HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

    HBRUSH bgBrush = CreateSolidBrush(COLOR_TRANSPARENT_KEY);
    RECT clientRect = { 0, 0, g_curWidth, MONITOR_HEIGHT };
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    HFONT hFontLabel = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    HFONT hFontValue = CreateFontW(-11, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");

    SetBkMode(memDC, TRANSPARENT);

    wchar_t upSpeed[16], dnSpeed[16], netLine1[32], netLine2[32];
    FormatSpeed(g_metrics.uploadSpeed, upSpeed, 16);
    FormatSpeed(g_metrics.downloadSpeed, dnSpeed, 16);
    swprintf(netLine1, 32, L"▲ %ls", upSpeed);
    swprintf(netLine2, 32, L"▼ %ls", dnSpeed);

    wchar_t cpuVal[16], gpuVal[16];
    swprintf(cpuVal, 16, L"%2.0f%%", g_metrics.cpuUsage);
    swprintf(gpuVal, 16, L"%2.0f%%", g_metrics.gpuUsage);

    wchar_t ramVal[16], memGbVal[16];
    swprintf(ramVal, 16, L"%2.0f%%", g_metrics.memUsage);
    swprintf(memGbVal, 16, L"%.1fG", g_metrics.memUsedGB);

    wchar_t dskVal[16], diskFreeVal[16], diskDriveLabel[8];
    swprintf(dskVal, 16, L"%2.0f%%", g_metrics.diskUsage);
    swprintf(diskDriveLabel, 8, L"%c:", g_config.targetDrive[0]);
    if (g_metrics.diskFreeGB >= 1000.0) swprintf(diskFreeVal, 16, L"%.1fT", g_metrics.diskFreeGB / 1024.0);
    else swprintf(diskFreeVal, 16, L"%.0fG", g_metrics.diskFreeGB);

    wchar_t prcVal[16], sysVal2[16];
    swprintf(prcVal, 16, L"%lu", g_metrics.processCount);
    if (g_metrics.batteryPercent >= 0) {
        swprintf(sysVal2, 16, L"%d%%", g_metrics.batteryPercent);
    } else {
        ULONGLONG totalSecs = GetTickCount64() / 1000;
        swprintf(sysVal2, 16, L"%lluh%02llum", totalSecs / 3600, (totalSecs % 3600) / 60);
    }

    int curX = 6;
    bool hasPrev = false;

    auto DrawDivider = [&](int x) {
        HPEN dividerPen = CreatePen(PS_SOLID, 1, g_theme.divider);
        HGDIOBJ oldDivPen = SelectObject(memDC, dividerPen);
        MoveToEx(memDC, x, 6, NULL);
        LineTo(memDC, x, MONITOR_HEIGHT - 6);
        SelectObject(memDC, oldDivPen);
        DeleteObject(dividerPen);
    };

    if (g_config.showNet) {
        if (hasPrev) { DrawDivider(curX - 6); }
        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.upload);
        TextOutW(memDC, curX, 2, netLine1, (int)wcslen(netLine1));
        SetTextColor(memDC, g_theme.download);
        TextOutW(memDC, curX, 15, netLine2, (int)wcslen(netLine2));
        curX += 66 + 12;
        hasPrev = true;
    }

    if (g_config.showCompute) {
        if (hasPrev) { DrawDivider(curX - 6); }
        SelectObject(memDC, hFontLabel);
        SetTextColor(memDC, g_theme.label);
        TextOutW(memDC, curX, 2, L"CPU", 3);
        TextOutW(memDC, curX, 15, L"GPU", 3);

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        TextOutW(memDC, curX + 28, 2, cpuVal, (int)wcslen(cpuVal));
        TextOutW(memDC, curX + 28, 15, gpuVal, (int)wcslen(gpuVal));
        curX += 64 + 12;
        hasPrev = true;
    }

    if (g_config.showMemory) {
        if (hasPrev) { DrawDivider(curX - 6); }
        SelectObject(memDC, hFontLabel);
        SetTextColor(memDC, g_theme.label);
        TextOutW(memDC, curX, 2, L"RAM", 3);
        TextOutW(memDC, curX, 15, L"USE", 3);

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        TextOutW(memDC, curX + 28, 2, ramVal, (int)wcslen(ramVal));
        TextOutW(memDC, curX + 28, 15, memGbVal, (int)wcslen(memGbVal));
        curX += 72 + 12;
        hasPrev = true;
    }

    if (g_config.showDisk) {
        if (hasPrev) { DrawDivider(curX - 6); }
        SelectObject(memDC, hFontLabel);
        SetTextColor(memDC, g_theme.label);
        TextOutW(memDC, curX, 2, L"DSK", 3);
        TextOutW(memDC, curX, 15, diskDriveLabel, (int)wcslen(diskDriveLabel));

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        TextOutW(memDC, curX + 28, 2, dskVal, (int)wcslen(dskVal));
        TextOutW(memDC, curX + 28, 15, diskFreeVal, (int)wcslen(diskFreeVal));
        curX += 72 + 12;
        hasPrev = true;
    }

    if (g_config.showSystem) {
        if (hasPrev) { DrawDivider(curX - 6); }
        SelectObject(memDC, hFontLabel);
        SetTextColor(memDC, g_theme.label);
        TextOutW(memDC, curX, 2, L"PRC", 3);
        TextOutW(memDC, curX, 15, (g_metrics.batteryPercent >= 0 ? L"BAT" : L"UP"), (g_metrics.batteryPercent >= 0 ? 3 : 2));

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        TextOutW(memDC, curX + 28, 2, prcVal, (int)wcslen(prcVal));
        TextOutW(memDC, curX + 28, 15, sysVal2, (int)wcslen(sysVal2));
        curX += 72 + 12;
        hasPrev = true;
    }

    BitBlt(hdc, 0, 0, g_curWidth, MONITOR_HEIGHT, memDC, 0, 0, SRCCOPY);

    DeleteObject(hFontLabel);
    DeleteObject(hFontValue);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}