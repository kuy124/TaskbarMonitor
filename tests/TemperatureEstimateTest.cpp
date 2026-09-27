#include "TemperatureEstimate.h"
#include <cassert>
#include <limits>

int main() {
    assert(EstimateTemperature(0, -1, false, 38, 42) == 38);
    assert(EstimateTemperature(100, -1, false, 38, 42) == 80);
    assert(EstimateTemperature(50, 38, true, 38, 42) > 38);
    assert(EstimateTemperature(50, 38, true, 38, 42) < 59);
    assert(EstimateTemperature(50, 95, false, 38, 42) == 59);
    assert(EstimateTemperature(-1, -1, false, 36, 38) == 36);
    assert(EstimateTemperature(200, -1, false, 36, 38) == 74);
    assert(EstimateTemperature(std::numeric_limits<double>::quiet_NaN(), -1, false, 36, 38) == 36);
}
