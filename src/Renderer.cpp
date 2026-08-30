#include "Renderer.h"
#include "Config.h"
#include "Theme.h"
#include "Metrics.h"
#include "TaskbarSync.h"
#include <vector>

struct MetricRow {
    wchar_t label[16];
    wchar_t value[32];
    COLORREF colLabel;
    COLORREF colValue;
};

struct MetricColumn {
    bool isNet;
    wchar_t netLine1[36];
    wchar_t netLine2[36];
    MetricRow row1;
    MetricRow row2;
    bool hasRow2;
};

void RenderOverlay(HWND hWnd, HDC hdc) {
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    if (dpiY == 0) dpiY = 96;
    int fontHeight = -MulDiv(g_config.fontSize, dpiY, 72);

    HFONT hFontLabel = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_config.fontFamily);

    HFONT hFontValue = CreateFontW(fontHeight, 0, 0, 0, g_config.fontWeight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_config.fontFamily);

    HDC measureDC = CreateCompatibleDC(hdc);
    HGDIOBJ oldMFont = SelectObject(measureDC, hFontValue);
    TEXTMETRIC tm;
    GetTextMetricsW(measureDC, &tm);

    int row1Y = (MONITOR_HEIGHT / 2) - tm.tmHeight + 1;
    int row2Y = (MONITOR_HEIGHT / 2);
    if (row1Y < 0) row1Y = 0;
    int centerY = (MONITOR_HEIGHT - tm.tmHeight) / 2;

    std::vector<MetricColumn> columns;

    // Network Column (Clean arrow + full speed units)
    if (g_config.showNet) {
        MetricColumn col;
        col.isNet = true;
        col.hasRow2 = true;
        wchar_t upSpeed[24], dnSpeed[24];
        FormatSpeed(g_metrics.uploadSpeed, upSpeed, 24);
        FormatSpeed(g_metrics.downloadSpeed, dnSpeed, 24);
        swprintf(col.netLine1, 36, L"▲ %ls", upSpeed);
        swprintf(col.netLine2, 36, L"▼ %ls", dnSpeed);
        columns.push_back(col);
    }

    // CPU / GPU Column
    if (g_config.showCPU || g_config.showGPU) {
        MetricColumn col;
        col.isNet = false;
        if (g_config.showCPU && g_config.showGPU) {
            col.hasRow2 = true;
            wcscpy_s(col.row1.label, L"CPU");
            swprintf(col.row1.value, 32, L"%2.0f%%", g_metrics.cpuUsage);
            col.row1.colLabel = g_theme.label;
            col.row1.colValue = g_theme.value;

            wcscpy_s(col.row2.label, L"GPU");
            swprintf(col.row2.value, 32, L"%2.0f%%", g_metrics.gpuUsage);
            col.row2.colLabel = g_theme.label;
            col.row2.colValue = g_theme.value;
        } else if (g_config.showCPU) {
            col.hasRow2 = false;
            wcscpy_s(col.row1.label, L"CPU");
            swprintf(col.row1.value, 32, L"%2.0f%%", g_metrics.cpuUsage);
            col.row1.colLabel = g_theme.label;
            col.row1.colValue = g_theme.value;
        } else {
            col.hasRow2 = false;
            wcscpy_s(col.row1.label, L"GPU");
            swprintf(col.row1.value, 32, L"%2.0f%%", g_metrics.gpuUsage);
            col.row1.colLabel = g_theme.label;
            col.row1.colValue = g_theme.value;
        }
        columns.push_back(col);
    }

    // RAM Column
    if (g_config.showRAM) {
        MetricColumn col;
        col.isNet = false;
        col.hasRow2 = true;
        wcscpy_s(col.row1.label, L"RAM");
        swprintf(col.row1.value, 32, L"%2.0f%%", g_metrics.memUsage);
        col.row1.colLabel = g_theme.label;
        col.row1.colValue = g_theme.value;

        wcscpy_s(col.row2.label, L"USE");
        swprintf(col.row2.value, 32, L"%.1fG", g_metrics.memUsedGB);
        col.row2.colLabel = g_theme.label;
        col.row2.colValue = g_theme.value;
        columns.push_back(col);
    }

    // Disk Column
    if (g_config.showDisk) {
        MetricColumn col;
        col.isNet = false;
        col.hasRow2 = true;
        wcscpy_s(col.row1.label, L"DSK");
        swprintf(col.row1.value, 32, L"%2.0f%%", g_metrics.diskUsage);
        col.row1.colLabel = g_theme.label;
        col.row1.colValue = g_theme.value;

        swprintf(col.row2.label, 16, L"%c:", g_config.targetDrive[0]);
        if (g_metrics.diskFreeGB >= 1000.0) swprintf(col.row2.value, 32, L"%.1fT", g_metrics.diskFreeGB / 1024.0);
        else swprintf(col.row2.value, 32, L"%.0fG", g_metrics.diskFreeGB);
        col.row2.colLabel = g_theme.label;
        col.row2.colValue = g_theme.value;
        columns.push_back(col);
    }

    // Dynamic System Metrics
    std::vector<MetricRow> sysRows;
    if (g_config.showProcess) {
        MetricRow r;
        wcscpy_s(r.label, L"PRC");
        swprintf(r.value, 32, L"%lu", g_metrics.processCount);
        r.colLabel = g_theme.label;
        r.colValue = g_theme.value;
        sysRows.push_back(r);
    }
    if (g_config.showBattery && g_metrics.batteryPercent >= 0) {
        MetricRow r;
        wcscpy_s(r.label, L"BAT");
        swprintf(r.value, 32, L"%d%%", g_metrics.batteryPercent);
        r.colLabel = g_theme.label;
        r.colValue = g_theme.value;
        sysRows.push_back(r);
    }
    if (g_config.showUptime) {
        MetricRow r;
        wcscpy_s(r.label, L"UP");
        ULONGLONG totalSecs = GetTickCount64() / 1000;
        swprintf(r.value, 32, L"%lluh%02llum", totalSecs / 3600, (totalSecs % 3600) / 60);
        r.colLabel = g_theme.label;
        r.colValue = g_theme.value;
        sysRows.push_back(r);
    }

    for (size_t i = 0; i < sysRows.size(); i += 2) {
        MetricColumn col;
        col.isNet = false;
        col.row1 = sysRows[i];
        if (i + 1 < sysRows.size()) {
            col.hasRow2 = true;
            col.row2 = sysRows[i + 1];
        } else {
            col.hasRow2 = false;
        }
        columns.push_back(col);
    }

    // Exact dynamic text measurement
    std::vector<int> colWidths;
    int calculatedTotalWidth = 14;

    for (size_t i = 0; i < columns.size(); i++) {
        int w = 0;
        if (columns[i].isNet) {
            SIZE sz1, sz2;
            GetTextExtentPoint32W(measureDC, columns[i].netLine1, (int)wcslen(columns[i].netLine1), &sz1);
            GetTextExtentPoint32W(measureDC, columns[i].netLine2, (int)wcslen(columns[i].netLine2), &sz2);
            w = (sz1.cx > sz2.cx ? sz1.cx : sz2.cx) + 6;
        } else {
            SelectObject(measureDC, hFontLabel);
            SIZE szL1, szL2 = {0};
            GetTextExtentPoint32W(measureDC, columns[i].row1.label, (int)wcslen(columns[i].row1.label), &szL1);
            if (columns[i].hasRow2) {
                GetTextExtentPoint32W(measureDC, columns[i].row2.label, (int)wcslen(columns[i].row2.label), &szL2);
            }
            int maxLblW = (szL1.cx > szL2.cx) ? szL1.cx : szL2.cx;

            SelectObject(measureDC, hFontValue);
            SIZE szV1, szV2 = {0};
            GetTextExtentPoint32W(measureDC, columns[i].row1.value, (int)wcslen(columns[i].row1.value), &szV1);
            if (columns[i].hasRow2) {
                GetTextExtentPoint32W(measureDC, columns[i].row2.value, (int)wcslen(columns[i].row2.value), &szV2);
            }
            int maxValW = (szV1.cx > szV2.cx) ? szV1.cx : szV2.cx;

            w = maxLblW + 6 + maxValW + 4;
        }

        colWidths.push_back(w);
        calculatedTotalWidth += w;
        if (i + 1 < columns.size()) {
            calculatedTotalWidth += g_config.itemSpacing;
        }
    }
    calculatedTotalWidth += 8;
    if (calculatedTotalWidth < 40) calculatedTotalWidth = 40;

    if (calculatedTotalWidth != g_curWidth) {
        g_curWidth = calculatedTotalWidth;
        SyncWithTaskbar(hWnd);
    }

    SelectObject(measureDC, oldMFont);
    DeleteDC(measureDC);

    // Double-buffered GDI Drawing
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, g_curWidth, MONITOR_HEIGHT);
    HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

    COLORREF fillBg = g_config.transparentBg ? COLOR_TRANSPARENT_KEY : g_theme.background;
    HBRUSH bgBrush = CreateSolidBrush(fillBg);
    RECT clientRect = { 0, 0, g_curWidth, MONITOR_HEIGHT };
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(memDC, TRANSPARENT);

    auto DrawDivider = [&](int x) {
        if (!g_config.showDividers) return;
        HPEN dividerPen = CreatePen(PS_SOLID, 1, g_theme.divider);
        HGDIOBJ oldDivPen = SelectObject(memDC, dividerPen);
        MoveToEx(memDC, x, 5, NULL);
        LineTo(memDC, x, MONITOR_HEIGHT - 5);
        SelectObject(memDC, oldDivPen);
        DeleteObject(dividerPen);
    };

    int curX = 8;
    for (size_t i = 0; i < columns.size(); i++) {
        if (i > 0) {
            DrawDivider(curX - (g_config.itemSpacing / 2));
        }

        if (columns[i].isNet) {
            SelectObject(memDC, hFontValue);
            SetTextColor(memDC, g_theme.upload);
            TextOutW(memDC, curX, row1Y, columns[i].netLine1, (int)wcslen(columns[i].netLine1));
            SetTextColor(memDC, g_theme.download);
            TextOutW(memDC, curX, row2Y, columns[i].netLine2, (int)wcslen(columns[i].netLine2));
        } else {
            SelectObject(memDC, hFontLabel);
            SIZE szL1, szL2 = {0};
            GetTextExtentPoint32W(memDC, columns[i].row1.label, (int)wcslen(columns[i].row1.label), &szL1);
            if (columns[i].hasRow2) {
                GetTextExtentPoint32W(memDC, columns[i].row2.label, (int)wcslen(columns[i].row2.label), &szL2);
            }
            int maxLblW = (szL1.cx > szL2.cx) ? szL1.cx : szL2.cx;
            int valX = curX + maxLblW + 6;

            if (columns[i].hasRow2) {
                SelectObject(memDC, hFontLabel);
                SetTextColor(memDC, columns[i].row1.colLabel);
                TextOutW(memDC, curX, row1Y, columns[i].row1.label, (int)wcslen(columns[i].row1.label));
                SetTextColor(memDC, columns[i].row2.colLabel);
                TextOutW(memDC, curX, row2Y, columns[i].row2.label, (int)wcslen(columns[i].row2.label));

                SelectObject(memDC, hFontValue);
                SetTextColor(memDC, columns[i].row1.colValue);
                TextOutW(memDC, valX, row1Y, columns[i].row1.value, (int)wcslen(columns[i].row1.value));
                SetTextColor(memDC, columns[i].row2.colValue);
                TextOutW(memDC, valX, row2Y, columns[i].row2.value, (int)wcslen(columns[i].row2.value));
            } else {
                SelectObject(memDC, hFontLabel);
                SetTextColor(memDC, columns[i].row1.colLabel);
                TextOutW(memDC, curX, centerY, columns[i].row1.label, (int)wcslen(columns[i].row1.label));

                SelectObject(memDC, hFontValue);
                SetTextColor(memDC, columns[i].row1.colValue);
                TextOutW(memDC, valX, centerY, columns[i].row1.value, (int)wcslen(columns[i].row1.value));
            }
        }

        curX += colWidths[i] + g_config.itemSpacing;
    }

    BitBlt(hdc, 0, 0, g_curWidth, MONITOR_HEIGHT, memDC, 0, 0, SRCCOPY);

    DeleteObject(hFontLabel);
    DeleteObject(hFontValue);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}