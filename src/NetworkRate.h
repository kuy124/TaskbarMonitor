#pragma once
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <cmath>
#include <cwchar>

inline void FormatNetworkSpeed(double bytes, bool bits, wchar_t* text, size_t size) {
    if (!std::isfinite(bytes) || bytes < 0) {
        std::swprintf(text, size, L"N/A");
        return;
    }
    const double base = bits ? 1000.0 : 1024.0;
    const wchar_t* byteUnits[] = {L"B/s", L"KB/s", L"MB/s", L"GB/s"};
    const wchar_t* bitUnits[] = {L"bps", L"Kbps", L"Mbps", L"Gbps"};
    double value = bits ? bytes * 8.0 : bytes;
    int unit = 0;
    while (value >= base && unit < 3) { value /= base; ++unit; }
    std::swprintf(text, size, unit == 0 || value >= 100 ? L"%.0f %ls" : L"%.1f %ls",
                  value, bits ? bitUnits[unit] : byteUnits[unit]);
}

struct NetworkCounters {
    std::uint64_t received, sent;
};

// Counters and time always advance together. New/restarted adapters establish
// a baseline instead of reporting all traffic since boot as a one-second burst.
class NetworkRate {
public:
    using Snapshot = std::unordered_map<std::uint64_t, NetworkCounters>;
    double download = 0, upload = 0;

    void Reset() {
        previous.clear();
        ready = false;
        download = upload = 0;
    }

    void Sample(Snapshot current, std::uint64_t now) {
        if (ready && now >= sampledAt && now - sampledAt < 100) return;
        download = upload = 0;
        if (ready && now > sampledAt) {
            double seconds = (now - sampledAt) / 1000.0;
            for (const auto& entry : current) {
                auto old = previous.find(entry.first);
                if (old == previous.end()) continue;
                const auto& counters = entry.second;
                // A reset in either direction invalidates this adapter's interval.
                if (counters.received < old->second.received || counters.sent < old->second.sent) continue;
                download += (counters.received - old->second.received) / seconds;
                upload += (counters.sent - old->second.sent) / seconds;
            }
        }
        previous = std::move(current);
        sampledAt = now;
        ready = true;
    }

private:
    Snapshot previous;
    std::uint64_t sampledAt = 0;
    bool ready = false;
};
