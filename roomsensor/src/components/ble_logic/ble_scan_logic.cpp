#include "ble_scan_logic.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

namespace ble {

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static std::string trim(const std::string& s) {
    size_t start = 0;
    size_t end = s.size();
    while (start < end && is_space(s[start])) ++start;
    while (end > start && is_space(s[end - 1])) --end;
    return s.substr(start, end - start);
}

bool parse_address(const char* text, Address& out) {
    if (text == nullptr) return false;
    std::string s = trim(text);

    // Either 12 hex digits, or 6 pairs separated by one ':' or '-' throughout
    Address addr{};
    if (s.size() == 12) {
        for (size_t i = 0; i < 6; ++i) {
            int hi = hex_value(s[2 * i]);
            int lo = hex_value(s[2 * i + 1]);
            if (hi < 0 || lo < 0) return false;
            addr[i] = (uint8_t)((hi << 4) | lo);
        }
    } else if (s.size() == 17) {
        char sep = s[2];
        if (sep != ':' && sep != '-') return false;
        for (size_t i = 0; i < 6; ++i) {
            int hi = hex_value(s[3 * i]);
            int lo = hex_value(s[3 * i + 1]);
            if (hi < 0 || lo < 0) return false;
            if (i < 5 && s[3 * i + 2] != sep) return false;
            addr[i] = (uint8_t)((hi << 4) | lo);
        }
    } else {
        return false;
    }
    out = addr;
    return true;
}

std::string format_address(const Address& addr) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    return std::string(buf);
}

Address address_from_le(const uint8_t* le) {
    Address addr{};
    for (size_t i = 0; i < 6; ++i) addr[i] = le[5 - i];
    return addr;
}

const char* describe_address_type(uint8_t addr_type, const Address& addr) {
    switch (addr_type) {
        case 0:
        case 2:
            return "public";
        case 1:
        case 3:
            switch (addr[0] >> 6) {
                case 0x3: return "random_static";
                case 0x1: return "random_private";
                case 0x0: return "random_nonresolvable";
                default: return "random";
            }
        default:
            return "unknown";
    }
}

bool parse_track_list(const char* text, std::vector<TrackedDevice>& out, std::string* error) {
    std::vector<TrackedDevice> devices;
    std::string s = text ? text : "";

    size_t pos = 0;
    while (pos <= s.size()) {
        size_t end = s.find_first_of(",;\n", pos);
        if (end == std::string::npos) end = s.size();
        std::string entry = trim(s.substr(pos, end - pos));
        pos = end + 1;
        if (entry.empty()) continue;

        std::string addr_text = entry;
        std::string name;
        size_t eq = entry.find('=');
        if (eq != std::string::npos) {
            addr_text = trim(entry.substr(0, eq));
            name = trim(entry.substr(eq + 1));
        }

        TrackedDevice device;
        if (!parse_address(addr_text.c_str(), device.addr)) {
            if (error) *error = "invalid address '" + addr_text + "'";
            return false;
        }
        if (name.find('=') != std::string::npos) {
            if (error) *error = "name for " + format_address(device.addr) + " contains '='";
            return false;
        }
        if (name.size() > kMaxNameLen) {
            if (error) *error = "name '" + name + "' is longer than " + std::to_string(kMaxNameLen) + " characters";
            return false;
        }
        device.name = name.empty() ? format_address(device.addr) : name;

        auto existing = std::find_if(devices.begin(), devices.end(),
                                     [&](const TrackedDevice& d) { return d.addr == device.addr; });
        if (existing != devices.end()) {
            existing->name = device.name;
            continue;
        }
        if (devices.size() >= kMaxTracked) {
            if (error) *error = "more than " + std::to_string(kMaxTracked) + " tracked devices";
            return false;
        }
        devices.push_back(device);
    }

    out = devices;
    return true;
}

std::string format_track_list(const std::vector<TrackedDevice>& devices) {
    std::string out;
    for (const TrackedDevice& d : devices) {
        if (!out.empty()) out += ',';
        out += format_address(d.addr);
        out += '=';
        out += d.name;
    }
    return out;
}

void Advertisement::add_uuid(uint16_t uuid) {
    for (uint8_t i = 0; i < uuid_count; ++i) {
        if (uuids16[i] == uuid) return;
    }
    if (uuid_count < kMaxUuids) uuids16[uuid_count++] = uuid;
}

void Advertisement::merge(const Advertisement& other) {
    if (name[0] == '\0' && other.name[0] != '\0') {
        memcpy(name, other.name, sizeof(name));
    }
    for (uint8_t i = 0; i < other.uuid_count; ++i) add_uuid(other.uuids16[i]);
    if (!has_manufacturer && other.has_manufacturer) {
        has_manufacturer = true;
        manufacturer_id = other.manufacturer_id;
    }
    if (!has_tx_power && other.has_tx_power) {
        has_tx_power = true;
        tx_power = other.tx_power;
    }
}

// Advertising data types (Bluetooth Assigned Numbers, "Common Data Types")
static constexpr uint8_t kAdIncomplete16 = 0x02;
static constexpr uint8_t kAdComplete16 = 0x03;
static constexpr uint8_t kAdShortName = 0x08;
static constexpr uint8_t kAdCompleteName = 0x09;
static constexpr uint8_t kAdTxPower = 0x0A;
static constexpr uint8_t kAdServiceData16 = 0x16;
static constexpr uint8_t kAdManufacturer = 0xFF;

void parse_advertisement(const uint8_t* data, size_t len, Advertisement& out) {
    if (data == nullptr) return;
    size_t pos = 0;
    while (pos < len) {
        uint8_t field_len = data[pos];
        if (field_len == 0) break;                 // padding to the end
        if (pos + 1 + field_len > len) break;      // runs past the end
        uint8_t type = data[pos + 1];
        const uint8_t* value = &data[pos + 2];
        size_t value_len = field_len - 1;

        switch (type) {
            case kAdIncomplete16:
            case kAdComplete16:
                for (size_t i = 0; i + 1 < value_len; i += 2) {
                    out.add_uuid((uint16_t)(value[i] | (value[i + 1] << 8)));
                }
                break;
            case kAdServiceData16:
                if (value_len >= 2) out.add_uuid((uint16_t)(value[0] | (value[1] << 8)));
                break;
            case kAdShortName:
            case kAdCompleteName:
                // A complete name replaces a shortened one; a shortened one only fills a blank
                if (type == kAdCompleteName || out.name[0] == '\0') {
                    size_t n = std::min(value_len, sizeof(out.name) - 1);
                    memcpy(out.name, value, n);
                    out.name[n] = '\0';
                    // Keep JSON and logs clean: names are text, cut at the first control byte
                    for (size_t i = 0; i < n; ++i) {
                        if ((uint8_t)out.name[i] < 0x20) {
                            out.name[i] = '\0';
                            break;
                        }
                    }
                }
                break;
            case kAdTxPower:
                if (value_len >= 1) {
                    out.has_tx_power = true;
                    out.tx_power = (int8_t)value[0];
                }
                break;
            case kAdManufacturer:
                if (value_len >= 2) {
                    out.has_manufacturer = true;
                    out.manufacturer_id = (uint16_t)(value[0] | (value[1] << 8));
                }
                break;
            default:
                break;
        }
        pos += 1 + field_len;
    }
}

bool is_tile(const Advertisement& adv) {
    for (uint8_t i = 0; i < adv.uuid_count; ++i) {
        if (adv.uuids16[i] == kTileUuidFeed || adv.uuids16[i] == kTileUuidFeec) return true;
    }
    return false;
}

void RssiStats::add(int8_t rssi) {
    if (count == 0) {
        min = rssi;
        max = rssi;
    } else {
        if (rssi < min) min = rssi;
        if (rssi > max) max = rssi;
    }
    last = rssi;
    sum += rssi;
    ++count;
}

void TrackedWindows::set_devices(const std::vector<TrackedDevice>& devices) {
    devices_ = devices;
    stats_.assign(devices_.size(), RssiStats{});
    ads_.assign(devices_.size(), Advertisement{});
}

int TrackedWindows::find(const Address& addr) const {
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (devices_[i].addr == addr) return (int)i;
    }
    return -1;
}

bool TrackedWindows::record(const Address& addr, int8_t rssi, const Advertisement& adv) {
    int i = find(addr);
    if (i < 0) return false;
    if (rssi == kRssiUnavailable) return true;
    stats_[i].add(rssi);
    ads_[i].merge(adv);
    return true;
}

std::vector<TrackedWindows::Report> TrackedWindows::take() {
    std::vector<Report> reports;
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (stats_[i].count == 0) continue;
        reports.push_back({devices_[i], stats_[i], ads_[i]});
        stats_[i] = RssiStats{};
        ads_[i] = Advertisement{};
    }
    return reports;
}

} // namespace ble
