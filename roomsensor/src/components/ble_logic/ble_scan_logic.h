#pragma once

// BLE scanning logic with no ESP-IDF dependencies, so it can be unit tested on the host
// (see host_test/). The ble config module validates track lists with it, and
// components/ble/BLEScanner.cpp feeds it advertisements from NimBLE and publishes what it
// collects.

#include <algorithm>
#include <array>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace ble {

// A device address, most significant byte first, as it is written: aa:bb:cc:dd:ee:ff.
// (NimBLE stores addresses least significant byte first; see address_from_le.)
using Address = std::array<uint8_t, 6>;

// Accepts aa:bb:cc:dd:ee:ff, aa-bb-cc-dd-ee-ff or aabbccddeeff, in either case, with
// surrounding whitespace.
bool parse_address(const char* text, Address& out);

// Lowercase with colons: aa:bb:cc:dd:ee:ff
std::string format_address(const Address& addr);

// From the 6 bytes NimBLE reports (least significant first).
Address address_from_le(const uint8_t* le);

// NimBLE/HCI address type: 0 public, 1 random, 2 public identity, 3 random identity.
// Random addresses are split by their top two bits, which say whether the address stays put:
// "public", "random_static" (fixed until the device is reset), "random_private" (resolvable,
// rotates every few minutes, as phones do) or "random_nonresolvable" (rotates too).
const char* describe_address_type(uint8_t addr_type, const Address& addr);

// Tracked devices, configured as ble.track: "aa:bb:cc:dd:ee:ff=Mittens,112233445566=Tom".
// The name is optional and defaults to the address.
struct TrackedDevice {
    Address addr{};
    std::string name;
};

constexpr size_t kMaxTracked = 32;
constexpr size_t kMaxNameLen = 31;

// Parses a track list. Entries are separated by commas, semicolons or newlines; blank entries
// are skipped. On any invalid entry it returns false, leaves out unchanged and describes the
// problem in error (when given). A repeated address keeps the last name given for it.
bool parse_track_list(const char* text, std::vector<TrackedDevice>& out, std::string* error = nullptr);

// Canonical form of a track list: lowercase addresses with colons, comma separated.
std::string format_track_list(const std::vector<TrackedDevice>& devices);

// What an advertisement (or scan response) says about its device. Fixed size, so recording
// one never allocates in the BLE host task.
struct Advertisement {
    static constexpr size_t kMaxUuids = 8;

    char name[kMaxNameLen + 1] = {0};
    uint16_t uuids16[kMaxUuids] = {0};
    uint8_t uuid_count = 0;
    bool has_manufacturer = false;
    uint16_t manufacturer_id = 0;
    bool has_tx_power = false;
    int8_t tx_power = 0;

    // Adds a 16-bit service UUID unless it is already listed or the list is full.
    void add_uuid(uint16_t uuid);
    // Fills in what other has and this lacks (a scan response adds to its advertisement).
    void merge(const Advertisement& other);
};

// Parses advertising data structures (length, type, data...) into out, adding to what out
// already holds. Stops at the first malformed structure.
void parse_advertisement(const uint8_t* data, size_t len, Advertisement& out);

// Tile, Inc.'s 16-bit service UUIDs, which Tile trackers advertise.
constexpr uint16_t kTileUuidFeed = 0xFEED;
constexpr uint16_t kTileUuidFeec = 0xFEEC;
bool is_tile(const Advertisement& adv);

// Signal strength over a reporting window.
struct RssiStats {
    uint32_t count = 0;
    int32_t sum = 0;
    int8_t min = 0;
    int8_t max = 0;
    int8_t last = 0;

    void add(int8_t rssi);
    float mean() const { return count ? (float)sum / (float)count : 0.0f; }
};

// HCI reports 127 when the controller has no RSSI for a packet.
constexpr int8_t kRssiUnavailable = 127;

// The tracked devices and how each was heard during the current window.
class TrackedWindows {
public:
    struct Report {
        TrackedDevice device;
        RssiStats rssi;
        Advertisement adv;
    };

    // Replaces the tracked devices and starts a fresh window.
    void set_devices(const std::vector<TrackedDevice>& devices);
    const std::vector<TrackedDevice>& devices() const { return devices_; }

    // Index of addr among the tracked devices, or -1.
    int find(const Address& addr) const;

    // Records one advertisement from a tracked device; false if addr isn't tracked.
    bool record(const Address& addr, int8_t rssi, const Advertisement& adv);

    // The devices heard since the last call, in configured order, and starts a fresh window.
    std::vector<Report> take();

private:
    std::vector<TrackedDevice> devices_;
    std::vector<RssiStats> stats_;
    std::vector<Advertisement> ads_;
};

// One device heard during a discovery window.
struct SeenDevice {
    Address addr{};
    uint8_t addr_type = 0;
    RssiStats rssi;
    Advertisement adv;
};

// Every device heard during the current window, for discovery reports. Alloc places the
// table's storage: the scanner keeps it in PSRAM, since a full table is about 10 KB.
template <class Alloc = std::allocator<SeenDevice>>
class DeviceTable {
public:
    using Entry = SeenDevice;
    using Entries = std::vector<Entry, Alloc>;

    explicit DeviceTable(size_t capacity) : capacity_(capacity) {}

    // Records one advertisement. A new device is dropped (and counted) once the table is full.
    bool record(const Address& addr, uint8_t addr_type, int8_t rssi, const Advertisement& adv) {
        if (rssi == kRssiUnavailable) return false;
        for (Entry& e : entries_) {
            if (e.addr == addr) {
                e.addr_type = addr_type;
                e.rssi.add(rssi);
                e.adv.merge(adv);
                return true;
            }
        }
        if (entries_.size() >= capacity_) {
            ++dropped_;
            return false;
        }
        Entry e;
        e.addr = addr;
        e.addr_type = addr_type;
        e.rssi.add(rssi);
        e.adv = adv;
        entries_.push_back(e);
        return true;
    }

    // The window's devices, strongest (by max RSSI) first, and how many new devices were
    // dropped because the table was full. Clears the table for the next window, with room for
    // capacity devices so record() doesn't allocate in the BLE host task.
    Entries take(size_t* dropped = nullptr) {
        Entries out;
        out.swap(entries_);
        entries_.reserve(capacity_);
        if (dropped) *dropped = dropped_;
        dropped_ = 0;
        std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
            if (a.rssi.max != b.rssi.max) return a.rssi.max > b.rssi.max;
            return a.addr < b.addr;
        });
        return out;
    }

    // Empties the table and frees its memory, for while discovery is off.
    void release() {
        Entries().swap(entries_);
        dropped_ = 0;
    }

    size_t size() const { return entries_.size(); }

private:
    size_t capacity_;
    size_t dropped_ = 0;
    Entries entries_;
};

} // namespace ble
