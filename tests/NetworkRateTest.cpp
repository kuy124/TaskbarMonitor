#include "NetworkRate.h"
#include <cassert>
#include <limits>

int main() {
    NetworkRate rate;
    rate.Sample({{1, {10000, 20000}}}, 1000);
    assert(rate.upload == 0 && rate.download == 0);
    rate.Sample({{1, {12000, 20500}}}, 2000);
    assert(rate.download == 2000 && rate.upload == 500);
    // Re-entrant/recovery calls must not erase the displayed rate or baseline.
    rate.Sample({{1, {12000, 20500}}}, 2000);
    rate.Sample({{1, {12000, 20500}}}, 2016);
    assert(rate.download == 2000 && rate.upload == 500);
    rate.Sample({{1, {16000, 21500}}}, 4000);
    assert(rate.download == 2000 && rate.upload == 500);
    // A full idle interval must still show zero, not an invented smoothed rate.
    rate.Sample({{1, {16000, 21500}}}, 5000);
    assert(rate.upload == 0 && rate.download == 0);
    // Reset, removal and reconnect establish new baselines.
    rate.Sample({{1, {0, 22000}}}, 6000);
    assert(rate.upload == 0 && rate.download == 0);
    rate.Sample({}, 7000);
    rate.Sample({{1, {900000, 800000}}}, 8000);
    assert(rate.upload == 0 && rate.download == 0);
    rate.Reset();
    rate.Sample({{1, {900000, 800000}}, {2, {1ULL << 40, 1ULL << 40}}}, 9000);
    rate.Sample({{1, {901000, 800500}}, {2, {(1ULL << 40) + 2000, (1ULL << 40) + 3000}}}, 10000);
    assert(rate.download == 3000 && rate.upload == 3500);

    wchar_t text[32];
    auto check = [&](double bytes, bool bits, const wchar_t* expected) {
        FormatNetworkSpeed(bytes, bits, text, 32);
        assert(std::wcscmp(text, expected) == 0);
    };
    check(0, false, L"0 B/s");
    check(128, false, L"128 B/s"); // previously rounded to 0 KB/s
    check(1536, false, L"1.5 KB/s");
    check(1048576, false, L"1.0 MB/s");
    check(1, true, L"8 bps");
    check(125000, true, L"1.0 Mbps");
    check(125000000, true, L"1.0 Gbps");
    check(-1, false, L"N/A");
    check(std::numeric_limits<double>::quiet_NaN(), false, L"N/A");
}
