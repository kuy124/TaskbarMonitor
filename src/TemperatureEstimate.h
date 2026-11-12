#pragma once
#include <cmath>
#include <cwchar>

inline double EstimateTemperature(double usage, double previous, bool previousEstimated,
                                  double idleTemperature, double fullLoadRise) {
    if (!std::isfinite(usage) || usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    double target = idleTemperature + usage * fullLoadRise / 100.0;
    return previousEstimated ? previous * 0.7 + target * 0.3 : target;
}

inline void FormatTemperature(double temperature, bool estimated, wchar_t* output, size_t size) {
    if (!std::isfinite(temperature) || temperature < 0.0) std::swprintf(output, size, L"N/A");
    else std::swprintf(output, size, L"%ls%.0f\u00b0C", estimated ? L"~" : L"", temperature);
}
