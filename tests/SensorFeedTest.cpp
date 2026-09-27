#include "SensorFeed.h"
#include <cassert>

int main() {
    auto both = ParseSensorTemperatures(R"({"Children":[{"SensorId":"/intelcpu/0/temperature/1","Text":"CPU Package","Type":"Temperature","RawValue":61.5,"Children":[]},{"SensorId":"/gpu-nvidia/0/temperature/0","Text":"GPU Core","Type":"Temperature","RawValue":53,"Children":[]}]})");
    assert(both.cpu == 61.5 && both.gpu == 53);
    auto amd = ParseSensorTemperatures(R"({"SensorId":"/amdcpu/0/temperature/0","Type":"Temperature","RawValue":42.25})");
    assert(amd.cpu == 42.25 && amd.gpu == -1);
    assert(ParseSensorTemperatures(R"({"SensorId":"/gpu-amd/0/temperature/0","Text":"GPU Core","Type":"Temperature","RawValue":46})").gpu == 46);
    assert(ParseSensorTemperatures(R"({"SensorId":"/gpu-intel-integrated/0/temperature/0","Text":"GPU Core","Type":"Temperature","RawValue":49})").gpu == 49);
    auto bad = ParseSensorTemperatures(R"({"Children":[{"SensorId":"/intelcpu/0/temperature/0","Type":"Temperature","RawValue":null},{"SensorId":"/other/0/temperature/0","Type":"Temperature","RawValue":45}]})");
    assert(bad.cpu == -1 && bad.gpu == -1);
    assert(ParseSensorTemperatures(R"({"SensorId":"/intelcpu/0/temperature/0","Type":"Temperature","RawValue":250})").cpu == -1);
    assert(ParseSensorTemperatures(R"({"SensorId":"/intelcpu/0/temperature/0","Type":"Temperature","RawValue":0})").cpu == -1);
    assert(ParseSensorTemperatures(R"({"SensorId":"/intelcpu/0/temperature/0","Type":"Temperature","RawValue":"NaN"})").cpu == -1);
    assert(ParseSensorTemperatures(R"({"SensorId":"/intelcpu/0/temperature/0","Type":"Temperature","RawValue":-7})").cpu == -1);
    assert(ParseSensorTemperatures(R"({"SensorId":"/gpu-intel/0/temperature/0","Type":"Temperature","RawValue":52})").gpu == 52);
    assert(ParseSensorTemperatures(R"({"SensorId":"/intelcpu/0/load/0","Type":"Load","RawValue":65})").cpu == -1);
    auto priority = ParseSensorTemperatures(R"({"Children":[{"SensorId":"/intelcpu/0/temperature/0","Text":"Core #1 Distance to TjMax","Type":"Temperature","RawValue":50},{"SensorId":"/intelcpu/0/temperature/1","Text":"Core Average","Type":"Temperature","RawValue":45},{"SensorId":"/intelcpu/0/temperature/2","Text":"CPU Package","Type":"Temperature","RawValue":58}]})");
    assert(priority.cpu == 58);
    assert(ParseSensorTemperatures("").cpu == -1);
    assert(ParseSensorTemperatures(R"({"SensorId":"/intelcpu/0/temperature/0","Type":"Temperature","RawValue":52)").cpu == -1);
    assert(ParseSensorTemperatures(R"({"SensorId":"/intelcpu/0/temperature/0","Type":"Temperature","RawValue":52}garbage)").cpu == -1);
}
