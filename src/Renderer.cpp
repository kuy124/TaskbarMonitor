#include "Renderer.h"
#include "Config.h"
#include "Theme.h"
#include "Metrics.h"
#include "TaskbarSync.h"
#include <algorithm>
#include <vector>

struct MetricRow {
    wchar_t label[16];
    wchar_t value[32];
    // Widest value this row can show. Reserving it keeps the layout from
    // shifting while the readings update.
    const wchar_t* sample;
    COLORREF colLabel;
    COLORREF colValue;
};

struct MetricColumn {
    bool isNet;
    wchar_t netLine1[36];
    wchar_t netLine2[36];
    const wchar_t* netSample;
    MetricRow row1;
    MetricRow row2;
    bool hasRow2;
};

void RenderOverlay(HWND hWnd, HDC hdc) {
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    if (dpiY == 0) dpiY = 96;
    RECT client = {};
    GetClientRect(hWnd, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;
    if (width <= 0) width = g_curWidth;
    if (height <= 0) height = g_monitorHeight;
    // Two readable rows must fit inside the taskbar band, even at high DPI.
    int fontHeight = std::min(MulDiv(g_config.fontSize, dpiY, 72), std::max(8, (height - 4) / 2));

    HFONT hFontLabel = CreateFontW(fontHeight, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_config.fontFamily);

    HFONT hFontValue = CreateFontW(fontHeight, 0, 0, 0, g_config.fontWeight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_config.fontFamily);

    HDC measureDC = CreateCompatibleDC(hdc);
    HGDIOBJ oldMFont = SelectObject(measureDC, hFontValue);
    TEXTMETRIC tm;
    GetTextMetricsW(measureDC, &tm);

    // Two rows, vertically centred in the taskbar band. The vertical offset
    // nudges the block without moving the window out of the taskbar.
    int blockTop = (height - 2 * (int)tm.tmHeight) / 2 + g_config.offsetY;
    blockTop = std::clamp(blockTop, 0, std::max(0, height - 2 * (int)tm.tmHeight));
    int row1Y = blockTop;
    int row2Y = blockTop + tm.tmHeight;

    std::vector<MetricColumn> columns;

    // 1. Network Column with unambiguous U / D labeling
    if (g_config.showNet) {
        MetricColumn col;
        col.isNet = true;
        col.hasRow2 = true;
        wchar_t upSpeed[24], dnSpeed[24];
        FormatSpeed(g_metrics.uploadSpeed, upSpeed, 24);
        FormatSpeed(g_metrics.downloadSpeed, dnSpeed, 24);
        swprintf(col.netLine1, 36, L"▲ U  %ls", upSpeed);
        swprintf(col.netLine2, 36, L"▼ D  %ls", dnSpeed);
        col.netSample = g_config.netUnit == NET_UNIT_BITS ? L"▲ U  999.9 Mbps"
                                                        : L"▲ U  999.9 MB/s";
        columns.push_back(col);
    }

    // 2. CPU / GPU Load Column (%)
    if (g_config.showCPU || g_config.showGPU) {
        MetricColumn col;
        col.isNet = false;
        if (g_config.showCPU && g_config.showGPU) {
            col.hasRow2 = true;
            wcscpy_s(col.row1.label, L"CPU");
            swprintf(col.row1.value, 32, L"%.0f%%", g_metrics.cpuUsage);
            col.row1.sample = L"100%";
            col.row1.colLabel = g_theme.label;
            col.row1.colValue = g_theme.value;

            wcscpy_s(col.row2.label, L"GPU");
            if (g_metrics.gpuUsage < 0) wcscpy_s(col.row2.value, L"N/A");
            else swprintf(col.row2.value, 32, L"%.0f%%", g_metrics.gpuUsage);
            col.row2.sample = L"100%";
            col.row2.colLabel = g_theme.label;
            col.row2.colValue = g_theme.value;
        } else if (g_config.showCPU) {
            col.hasRow2 = false;
            wcscpy_s(col.row1.label, L"CPU");
            swprintf(col.row1.value, 32, L"%.0f%%", g_metrics.cpuUsage);
            col.row1.sample = L"100%";
            col.row1.colLabel = g_theme.label;
            col.row1.colValue = g_theme.value;
        } else {
            col.hasRow2 = false;
            wcscpy_s(col.row1.label, L"GPU");
            if (g_metrics.gpuUsage < 0) wcscpy_s(col.row1.value, L"N/A");
            else swprintf(col.row1.value, 32, L"%.0f%%", g_metrics.gpuUsage);
            col.row1.sample = L"100%";
            col.row1.colLabel = g_theme.label;
            col.row1.colValue = g_theme.value;
        }
        columns.push_back(col);
    }

    // 3. CPU & GPU Heat Column (°C)
    if (g_config.showCPUTemp || g_config.showGPUTemp) {
        MetricColumn col;
        col.isNet = false;
        if (g_config.showCPUTemp && g_config.showGPUTemp) {
            col.hasRow2 = true;
            wcscpy_s(col.row1.label, L"CPU");
            if (g_metrics.cpuTemp < 0) wcscpy_s(col.row1.value, L"N/A");
            else swprintf(col.row1.value, 32, L"%.0f°C", g_metrics.cpuTemp);
            col.row1.sample = L"100°C";
            col.row1.colLabel = g_theme.label;
            col.row1.colValue = g_theme.value;

            wcscpy_s(col.row2.label, L"GPU");
            if (g_metrics.gpuTemp < 0) wcscpy_s(col.row2.value, L"N/A");
            else swprintf(col.row2.value, 32, L"%.0f°C", g_metrics.gpuTemp);
            col.row2.sample = L"100°C";
            col.row2.colLabel = g_theme.label;
            col.row2.colValue = g_theme.value;
        } else if (g_config.showCPUTemp) {
            col.hasRow2 = false;
            wcscpy_s(col.row1.label, L"CPU");
            if (g_metrics.cpuTemp < 0) wcscpy_s(col.row1.value, L"N/A");
            else swprintf(col.row1.value, 32, L"%.0f°C", g_metrics.cpuTemp);
            col.row1.sample = L"100°C";
            col.row1.colLabel = g_theme.label;
            col.row1.colValue = g_theme.value;
        } else {
            col.hasRow2 = false;
            wcscpy_s(col.row1.label, L"GPU");
            if (g_metrics.gpuTemp < 0) wcscpy_s(col.row1.value, L"N/A");
            else swprintf(col.row1.value, 32, L"%.0f°C", g_metrics.gpuTemp);
            col.row1.sample = L"100°C";
            col.row1.colLabel = g_theme.label;
            col.row1.colValue = g_theme.value;
        }
        columns.push_back(col);
    }

    // 4. RAM Column
    if (g_config.showRAM) {
        MetricColumn col;
        col.isNet = false;
        col.hasRow2 = true;
        wcscpy_s(col.row1.label, L"RAM");
        swprintf(col.row1.value, 32, L"%.0f%%", g_metrics.memUsage);
        col.row1.sample = L"100%";
        col.row1.colLabel = g_theme.label;
        col.row1.colValue = g_theme.value;

        wcscpy_s(col.row2.label, L"USE");
        swprintf(col.row2.value, 32, L"%.1fG", g_metrics.memUsedGB);
        col.row2.sample = L"99.9G";
        col.row2.colLabel = g_theme.label;
        col.row2.colValue = g_theme.value;
        columns.push_back(col);
    }

    // 5. Disk Column
    if (g_config.showDisk) {
        MetricColumn col;
        col.isNet = false;
        col.hasRow2 = true;
        wcscpy_s(col.row1.label, L"DSK");
        if (g_metrics.diskUsage < 0) wcscpy_s(col.row1.value, L"N/A");
        else swprintf(col.row1.value, 32, L"%.0f%%", g_metrics.diskUsage);
        col.row1.sample = L"100%";
        col.row1.colLabel = g_theme.label;
        col.row1.colValue = g_theme.value;

        // Matches the Windows shell's own "C: 412 GB free" wording.
        swprintf(col.row2.label, 16, L"%c:", g_config.targetDrive[0]);
        if (g_metrics.diskFreeGB < 0) wcscpy_s(col.row2.value, L"N/A");
        else if (g_metrics.diskFreeGB >= 1000.0) swprintf(col.row2.value, 32, L"%.1fT", g_metrics.diskFreeGB / 1024.0);
        else swprintf(col.row2.value, 32, L"%.0fG", g_metrics.diskFreeGB);
        col.row2.sample = L"999G";
        col.row2.colLabel = g_theme.label;
        col.row2.colValue = g_theme.value;
        columns.push_back(col);
    }

    // 6. Optional System Metrics (Battery & Uptime)
    std::vector<MetricRow> sysRows;
    if (g_config.showBattery) {
        MetricRow r;
        wcscpy_s(r.label, L"BAT");
        if (g_metrics.hasBattery) {
            if (g_metrics.batteryCharging) {
                swprintf(r.value, 32, L"⚡%.0f%%", g_metrics.batteryPercent);
            } else {
                swprintf(r.value, 32, L"%.0f%%", g_metrics.batteryPercent);
            }
        } else {
            swprintf(r.value, 32, L"AC");
        }
        r.sample = L"⚡99%";
        r.colLabel = g_theme.label;
        r.colValue = g_theme.value;
        sysRows.push_back(r);
    }

    if (g_config.showUptime) {
        MetricRow r;
        wcscpy_s(r.label, L"UP");
        ULONGLONG totalSecs = GetTickCount64() / 1000;
        swprintf(r.value, 32, L"%lluh%02llum", totalSecs / 3600, (totalSecs % 3600) / 60);
        r.sample = L"999h59m";
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

    // Exact dynamic text measurement. Each row is anchored right after its own
    // label, so a value never drifts away from the label that names it.
    const int LABEL_GAP = 5;
    const int EDGE = 4;
    std::vector<int> colWidths;
    int calculatedTotalWidth = EDGE * 2;

    for (size_t i = 0; i < columns.size(); i++) {
        int w = 0;
        if (columns[i].isNet) {
            SIZE sz1, sz2, reserve = {};
            GetTextExtentPoint32W(measureDC, columns[i].netLine1, (int)wcslen(columns[i].netLine1), &sz1);
            GetTextExtentPoint32W(measureDC, columns[i].netLine2, (int)wcslen(columns[i].netLine2), &sz2);
            GetTextExtentPoint32W(measureDC, columns[i].netSample, (int)wcslen(columns[i].netSample), &reserve);
            w = std::max(sz1.cx, sz2.cx);
            w = std::max(w, static_cast<int>(reserve.cx));
            w += EDGE;
        } else {
            SelectObject(measureDC, hFontLabel);
            SIZE szL1 = {0}, szL2 = {0};
            GetTextExtentPoint32W(measureDC, columns[i].row1.label, (int)wcslen(columns[i].row1.label), &szL1);
            if (columns[i].hasRow2) {
                GetTextExtentPoint32W(measureDC, columns[i].row2.label, (int)wcslen(columns[i].row2.label), &szL2);
            }
            SelectObject(measureDC, hFontValue);
            SIZE szV1 = {0}, szV2 = {0}, r1 = {0}, r2 = {0};
            GetTextExtentPoint32W(measureDC, columns[i].row1.value, (int)wcslen(columns[i].row1.value), &szV1);
            GetTextExtentPoint32W(measureDC, columns[i].row1.sample, (int)wcslen(columns[i].row1.sample), &r1);
            if (columns[i].hasRow2) {
                GetTextExtentPoint32W(measureDC, columns[i].row2.value, (int)wcslen(columns[i].row2.value), &szV2);
                GetTextExtentPoint32W(measureDC, columns[i].row2.sample, (int)wcslen(columns[i].row2.sample), &r2);
            }
            // One shared value column keeps the two rows of a reading aligned.
            int valueW = std::max(std::max((int)szV1.cx, (int)r1.cx),
                                  std::max((int)szV2.cx, (int)r2.cx));
            w = std::max(szL1.cx, szL2.cx) + LABEL_GAP + valueW + EDGE;
        }

        colWidths.push_back(w);
        calculatedTotalWidth += w;
        if (i + 1 < columns.size()) {
            calculatedTotalWidth += g_config.itemSpacing;
        }
    }
    if (calculatedTotalWidth < 40) calculatedTotalWidth = 40;

    if (calculatedTotalWidth != g_curWidth) {
        g_curWidth = calculatedTotalWidth;
        // Do not reparent or query Explorer from inside WM_PAINT.
        PostMessageW(hWnd, WM_TASKBAR_SYNC, 0, 0);
    }

    SelectObject(measureDC, oldMFont);
    DeleteDC(measureDC);

    // Double-buffered GDI Drawing
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, g_curWidth, height);
    HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

    COLORREF fillBg = g_config.transparentBg ? g_transparentKey : g_theme.background;
    HBRUSH bgBrush = CreateSolidBrush(fillBg);
    RECT clientRect = { 0, 0, g_curWidth, height };
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(memDC, TRANSPARENT);

    auto DrawDivider = [&](int x) {
        if (!g_config.showDividers) return;
        int divTop = row1Y - 1;
        if (divTop < 0) divTop = 0;
        int divBottom = row2Y + tm.tmHeight;
        if (divBottom >= height) divBottom = height - 1;
        if (divBottom <= divTop) return;
        HPEN dividerPen = CreatePen(PS_SOLID, 1, g_theme.divider);
        HGDIOBJ oldDivPen = SelectObject(memDC, dividerPen);
        MoveToEx(memDC, x, divTop, NULL);
        LineTo(memDC, x, divBottom);
        SelectObject(memDC, oldDivPen);
        DeleteObject(dividerPen);
    };

    int curX = EDGE;
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
            // The value sits directly after its label. Right-aligning inside a
            // reserved slot would push short values away from their labels.
            SelectObject(memDC, hFontLabel);
            SIZE szL1 = {0}, szL2 = {0};
            GetTextExtentPoint32W(memDC, columns[i].row1.label, (int)wcslen(columns[i].row1.label), &szL1);
            if (columns[i].hasRow2) {
                GetTextExtentPoint32W(memDC, columns[i].row2.label, (int)wcslen(columns[i].row2.label), &szL2);
            }
            int labelW = std::max(szL1.cx, szL2.cx);
            int valX = curX + labelW + LABEL_GAP;

            auto DrawRow = [&](const MetricRow& row, int y) {
                SelectObject(memDC, hFontLabel);
                SetTextColor(memDC, row.colLabel);
                TextOutW(memDC, curX, y, row.label, (int)wcslen(row.label));
                SelectObject(memDC, hFontValue);
                SetTextColor(memDC, row.colValue);
                TextOutW(memDC, valX, y, row.value, (int)wcslen(row.value));
            };
            DrawRow(columns[i].row1, row1Y);
            if (columns[i].hasRow2) {
                DrawRow(columns[i].row2, row2Y);
            }
        }

        curX += colWidths[i] + g_config.itemSpacing;
    }

    BitBlt(hdc, 0, 0, g_curWidth, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    DeleteObject(hFontLabel);
    DeleteObject(hFontValue);
}
