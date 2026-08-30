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

    int fontH = -(g_config.fontSize + 1);
    HFONT hFontLabel = CreateFontW(fontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_config.fontFamily);

    HFONT hFontValue = CreateFontW(fontH, 0, 0, 0, g_config.fontWeight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_config.fontFamily);

    SetBkMode(memDC, TRANSPARENT);

    int curX = 6;
    bool hasPrev = false;

    auto DrawDivider = [&](int x) {
        if (!g_config.showDividers) return;
        HPEN dividerPen = CreatePen(PS_SOLID, 1, g_theme.divider);
        HGDIOBJ oldDivPen = SelectObject(memDC, dividerPen);
        MoveToEx(memDC, x, 5, NULL);
        LineTo(memDC, x, MONITOR_HEIGHT - 5);
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
        TextOutW(memDC, curX, 2, netLine1, (int)wcslen(netLine1));
        SetTextColor(memDC, g_theme.download);
        TextOutW(memDC, curX, 16, netLine2, (int)wcslen(netLine2));

        curX += 66 + g_config.itemSpacing;
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
        TextOutW(memDC, curX, 2, line1Lbl, (int)wcslen(line1Lbl));
        if (wcslen(line2Lbl) > 0) TextOutW(memDC, curX, 16, line2Lbl, (int)wcslen(line2Lbl));

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        TextOutW(memDC, curX + 28, 2, line1Val, (int)wcslen(line1Val));
        if (wcslen(line2Val) > 0) TextOutW(memDC, curX + 28, 16, line2Val, (int)wcslen(line2Val));

        curX += 64 + g_config.itemSpacing;
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
        TextOutW(memDC, curX, 2, L"RAM", 3);
        TextOutW(memDC, curX, 16, L"USE", 3);

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        TextOutW(memDC, curX + 28, 2, ramVal, (int)wcslen(ramVal));
        TextOutW(memDC, curX + 28, 16, memGbVal, (int)wcslen(memGbVal));

        curX += 70 + g_config.itemSpacing;
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
        TextOutW(memDC, curX, 2, L"DSK", 3);
        TextOutW(memDC, curX, 16, diskDriveLabel, (int)wcslen(diskDriveLabel));

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        TextOutW(memDC, curX + 28, 2, dskVal, (int)wcslen(dskVal));
        TextOutW(memDC, curX + 28, 16, diskFreeVal, (int)wcslen(diskFreeVal));

        curX += 72 + g_config.itemSpacing;
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
        if (wcslen(l1Val) > 0) TextOutW(memDC, curX, 2, l1Lbl, (int)wcslen(l1Lbl));
        if (wcslen(l2Val) > 0) TextOutW(memDC, curX, 16, l2Lbl, (int)wcslen(l2Lbl));

        SelectObject(memDC, hFontValue);
        SetTextColor(memDC, g_theme.value);
        if (wcslen(l1Val) > 0) TextOutW(memDC, curX + 28, 2, l1Val, (int)wcslen(l1Val));
        if (wcslen(l2Val) > 0) TextOutW(memDC, curX + 28, 16, l2Val, (int)wcslen(l2Val));

        curX += 72 + g_config.itemSpacing;
        hasPrev = true;
    }

    BitBlt(hdc, 0, 0, g_curWidth, MONITOR_HEIGHT, memDC, 0, 0, SRCCOPY);

    DeleteObject(hFontLabel);
    DeleteObject(hFontValue);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}