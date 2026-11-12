#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

// Processes share engines; independent engines and adapters run in parallel.
class GpuUsage {
    std::map<std::wstring, double> engines;
public:
    void Add(const wchar_t* instance, double usage) {
        if (!instance || !std::isfinite(usage) || usage < 0.0) return;
        std::wstring name(instance);
        size_t gpu = name.find(L"luid_");
        if (gpu == std::wstring::npos) return;
        size_t physical = name.find(L"_phys_", gpu);
        size_t engine = name.find(L"_eng_", gpu);
        if (physical == std::wstring::npos || engine == std::wstring::npos || physical >= engine) return;
        size_t end = name.find(L'_', engine + 5);
        if (end == std::wstring::npos) end = name.size();
        if (end == engine + 5 || !std::all_of(name.begin() + engine + 5, name.begin() + end,
                [](wchar_t c) { return c >= L'0' && c <= L'9'; })) return;
        engines[name.substr(gpu, end - gpu)] += usage;
    }

    double Usage() const {
        double busiest = -1.0;
        for (const auto& engine : engines) busiest = std::max(busiest, engine.second);
        return std::min(100.0, busiest);
    }
};
