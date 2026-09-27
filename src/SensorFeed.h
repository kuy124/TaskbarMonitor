#pragma once
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

struct SensorTemperatures { double cpu = -1.0, gpu = -1.0; };

class SensorFeedParser {
public:
    explicit SensorFeedParser(const std::string& text) : text_(text) {}

    SensorTemperatures Parse() {
        Space();
        if (text_.size() >= 1024 * 1024 || pos_ == text_.size() || text_[pos_] != '{' || !Value(0)) return {};
        Space();
        return pos_ == text_.size() ? temperatures_ : SensorTemperatures{};
    }

private:
    void Space() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r')) ++pos_;
    }

    bool Literal(const char* literal) {
        for (size_t i = 0; literal[i]; ++i) {
            if (pos_ == text_.size() || text_[pos_++] != literal[i]) return false;
        }
        return true;
    }

    bool String(std::string& out) {
        if (pos_ == text_.size() || text_[pos_++] != '"') return false;
        while (pos_ < text_.size()) {
            unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (pos_ == text_.size()) return false;
            char escape = text_[pos_++];
            if (escape == 'u') {
                for (int i = 0; i < 4; ++i) {
                    if (pos_ == text_.size() || !std::isxdigit(static_cast<unsigned char>(text_[pos_++]))) return false;
                }
                continue;
            }
            if (escape == '"' || escape == '\\' || escape == '/' || escape == 'b' ||
                escape == 'f' || escape == 'n' || escape == 'r' || escape == 't') out += escape;
            else return false;
        }
        return false;
    }

    bool Number(double& out) {
        size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        if (pos_ == text_.size()) return false;
        if (text_[pos_] == '0') ++pos_;
        else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        } else return false;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            size_t digits = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (digits == pos_) return false;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            size_t digits = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (digits == pos_) return false;
        }
        out = std::strtod(text_.c_str() + start, nullptr);
        return true;
    }

    bool Value(int depth) {
        if (depth > 32) return false;
        Space();
        if (pos_ == text_.size()) return false;
        char c = text_[pos_];
        if (c == '{') {
            ++pos_;
            std::string id, type, name;
            double raw = -1.0;
            bool hasRaw = false;
            Space();
            if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
            for (;;) {
                std::string key;
                if (!String(key)) return false;
                Space();
                if (pos_ == text_.size() || text_[pos_++] != ':') return false;
                Space();
                if ((key == "SensorId" || key == "Type" || key == "Text") &&
                    pos_ < text_.size() && text_[pos_] == '"') {
                    std::string value;
                    if (!String(value)) return false;
                    if (key == "SensorId") id = value;
                    else if (key == "Type") type = value;
                    else name = value;
                } else if (key == "RawValue" && pos_ < text_.size() &&
                           (text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9'))) {
                    if (!Number(raw)) return false;
                    hasRaw = true;
                } else if (!Value(depth + 1)) return false;
                Space();
                if (pos_ == text_.size()) return false;
                if (text_[pos_] == '}') { ++pos_; break; }
                if (text_[pos_++] != ',') return false;
                Space();
            }
            if (type == "Temperature" && hasRaw && std::isfinite(raw) && raw >= 1.0 && raw <= 120.0) {
                if ((id.rfind("/intelcpu/", 0) == 0 || id.rfind("/amdcpu/", 0) == 0 ||
                     id.rfind("/genericcpu/", 0) == 0) && name.find("Distance") == std::string::npos) {
                    int priority = name == "CPU Package" ? 3 : (name == "Core Max" ? 2 : 1);
                    if (priority > cpuPriority_) { temperatures_.cpu = raw; cpuPriority_ = priority; }
                }
                if (id.rfind("/gpu-nvidia/", 0) == 0 || id.rfind("/gpu-amd/", 0) == 0 ||
                    id.rfind("/gpu-intel/", 0) == 0 || id.rfind("/gpu-intel-integrated/", 0) == 0) {
                    int priority = name.find("Core") != std::string::npos ? 2 : 1;
                    if (priority > gpuPriority_) { temperatures_.gpu = raw; gpuPriority_ = priority; }
                }
            }
            return true;
        }
        if (c == '[') {
            ++pos_;
            Space();
            if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
            for (;;) {
                if (!Value(depth + 1)) return false;
                Space();
                if (pos_ == text_.size()) return false;
                if (text_[pos_] == ']') { ++pos_; return true; }
                if (text_[pos_++] != ',') return false;
            }
        }
        if (c == '"') { std::string ignored; return String(ignored); }
        if (c == 't') return Literal("true");
        if (c == 'f') return Literal("false");
        if (c == 'n') return Literal("null");
        double ignored = 0;
        return Number(ignored);
    }

    const std::string& text_;
    size_t pos_ = 0;
    SensorTemperatures temperatures_;
    int cpuPriority_ = 0, gpuPriority_ = 0;
};

inline SensorTemperatures ParseSensorTemperatures(const std::string& json) {
    return SensorFeedParser(json).Parse();
}
