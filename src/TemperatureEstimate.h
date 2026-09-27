#pragma once
#include <cmath>

inline double EstimateTemperature(double usage, double previous, bool previousEstimated,
                                  double idleTemperature, double fullLoadRise) {
    if (!std::isfinite(usage) || usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    double target = idleTemperature + usage * fullLoadRise / 100.0;
    return previousEstimated ? previous * 0.7 + target * 0.3 : target;
}
