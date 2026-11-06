// Host unit tests for ble_scan_logic (no ESP-IDF needed). Run ./run.sh from this directory.

#include "ble_scan_logic.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

using namespace ble;

static void test_parse_address() {
    Address a{};
    CHECK(parse_address("E1:2B:03:4c:5D:6f", a));
    CHECK(format_address(a) == "e1:2b:03:4c:5d:6f");
    CHECK(parse_address("  e12b034c5d6f ", a));
    CHECK(format_address(a) == "e1:2b:03:4c:5d:6f");
    CHECK(parse_address("e1-2b-03-4c-5d-6f", a));
    CHECK(format_address(a) == "e1:2b:03:4c:5d:6f");

    Address untouched{};
    CHECK(!parse_address("e1:2b:03:4c:5d", untouched));
    CHECK(!parse_address("e1:2b-03:4c:5d:6f", untouched));
    CHECK(!parse_address("g12b034c5d6f", untouched));
    CHECK(!parse_address("", untouched));
    CHECK(!parse_address(nullptr, untouched));
    CHECK(untouched == Address{});
}

static void test_address_from_le_and_type() {
    const uint8_t le[6] = {0x6f, 0x5d, 0x4c, 0x03, 0x2b, 0xe1};
    Address a = address_from_le(le);
    CHECK(format_address(a) == "e1:2b:03:4c:5d:6f");

    CHECK(strcmp(describe_address_type(0, a), "public") == 0);
    CHECK(strcmp(describe_address_type(1, a), "random_static") == 0);   // 0xe1 = 11xxxxxx
    Address rpa{0x4a, 1, 2, 3, 4, 5};
    CHECK(strcmp(describe_address_type(1, rpa), "random_private") == 0);
    Address nrpa{0x0a, 1, 2, 3, 4, 5};
    CHECK(strcmp(describe_address_type(1, nrpa), "random_nonresolvable") == 0);
    CHECK(strcmp(describe_address_type(9, a), "unknown") == 0);
}

static void test_parse_track_list() {
    std::vector<TrackedDevice> devices;
    std::string error;
    CHECK(parse_track_list("E1:2B:03:4C:5D:6F=Mittens, 112233445566 = Tom ;\naabbccddeeff", devices, &error));
    CHECK(devices.size() == 3);
    CHECK(devices[0].name == "Mittens");
    CHECK(format_address(devices[1].addr) == "11:22:33:44:55:66");
    CHECK(devices[1].name == "Tom");
    CHECK(devices[2].name == "aa:bb:cc:dd:ee:ff");
    CHECK(format_track_list(devices) ==
          "e1:2b:03:4c:5d:6f=Mittens,11:22:33:44:55:66=Tom,aa:bb:cc:dd:ee:ff=aa:bb:cc:dd:ee:ff");

    // A repeated address keeps the last name
    CHECK(parse_track_list("112233445566=Tom,11:22:33:44:55:66=Thomas", devices, &error));
    CHECK(devices.size() == 1);
    CHECK(devices[0].name == "Thomas");

    // Blank clears
    CHECK(parse_track_list("", devices, &error));
    CHECK(devices.empty());
    CHECK(parse_track_list(" , ;", devices, &error));
    CHECK(devices.empty());
    CHECK(parse_track_list(nullptr, devices, &error));
    CHECK(devices.empty());

    // Invalid entries leave the list alone
    CHECK(parse_track_list("112233445566=Tom", devices, &error));
    CHECK(!parse_track_list("112233445566=Tom,nonsense=Bad", devices, &error));
    CHECK(error.find("nonsense") != std::string::npos);
    CHECK(!parse_track_list("112233445566=a=b", devices, &error));
    CHECK(!parse_track_list("112233445566=this name is far too long for a tag", devices, &error));
    CHECK(devices.size() == 1 && devices[0].name == "Tom");

    std::string many;
    for (int i = 0; i <= (int)kMaxTracked; ++i) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%012x,", i);
        many += buf;
    }
    CHECK(!parse_track_list(many.c_str(), devices, &error));
    CHECK(error.find("more than") != std::string::npos);
}

static void test_parse_advertisement() {
    // A Tile-like advertisement: flags, complete 16-bit UUID list (FEED), service data (FEED),
    // then a scan response with a name and TX power.
    const uint8_t adv[] = {
        0x02, 0x01, 0x06,                     // flags
        0x03, 0x03, 0xED, 0xFE,               // complete list: 0xFEED
        0x05, 0x16, 0xED, 0xFE, 0x01, 0x02,   // service data for 0xFEED
        0x05, 0xFF, 0x4C, 0x00, 0x10, 0x05,   // manufacturer 0x004C
    };
    Advertisement a;
    parse_advertisement(adv, sizeof(adv), a);
    CHECK(a.uuid_count == 1);
    CHECK(a.uuids16[0] == 0xFEED);
    CHECK(is_tile(a));
    CHECK(a.has_manufacturer && a.manufacturer_id == 0x004C);
    CHECK(a.name[0] == '\0');

    const uint8_t rsp[] = {
        0x05, 0x08, 'T', 'i', 'l', 'e',       // shortened name
        0x02, 0x0A, 0xF4,                     // TX power -12
        0x00, 0x00,                           // padding
    };
    Advertisement r;
    parse_advertisement(rsp, sizeof(rsp), r);
    CHECK(strcmp(r.name, "Tile") == 0);
    CHECK(r.has_tx_power && r.tx_power == -12);
    a.merge(r);
    CHECK(strcmp(a.name, "Tile") == 0);
    CHECK(a.has_tx_power && a.tx_power == -12);
    CHECK(a.uuid_count == 1);

    // A complete name replaces a shortened one, a shortened one doesn't replace a complete one
    const uint8_t names[] = {0x03, 0x08, 'A', 'B', 0x04, 0x09, 'A', 'B', 'C', 0x02, 0x08, 'Z'};
    Advertisement n;
    parse_advertisement(names, sizeof(names), n);
    CHECK(strcmp(n.name, "ABC") == 0);

    // Long names are cut to fit, control bytes end the name
    uint8_t long_name[2 + 40];
    long_name[0] = 41;
    long_name[1] = 0x09;
    memset(long_name + 2, 'x', 40);
    Advertisement l;
    parse_advertisement(long_name, sizeof(long_name), l);
    CHECK(strlen(l.name) == kMaxNameLen);
    const uint8_t ctrl[] = {0x04, 0x09, 'h', 0x01, 'i'};
    Advertisement c;
    parse_advertisement(ctrl, sizeof(ctrl), c);
    CHECK(strcmp(c.name, "h") == 0);

    // Malformed lengths stop parsing without reading past the end
    const uint8_t bad[] = {0x03, 0x03, 0xEC, 0xFE, 0x09, 0x09, 'x'};
    Advertisement b;
    parse_advertisement(bad, sizeof(bad), b);
    CHECK(b.uuid_count == 1 && b.uuids16[0] == 0xFEEC);
    CHECK(is_tile(b));
    CHECK(b.name[0] == '\0');

    parse_advertisement(nullptr, 10, b);
    Advertisement none;
    CHECK(!is_tile(none));

    // UUID list is capped and deduplicated
    Advertisement u;
    for (int i = 0; i < 20; ++i) u.add_uuid((uint16_t)(i % 10));
    CHECK(u.uuid_count == Advertisement::kMaxUuids);
}

static void test_rssi_stats() {
    RssiStats s;
    CHECK(s.mean() == 0.0f);
    s.add(-70);
    s.add(-60);
    s.add(-80);
    CHECK(s.count == 3);
    CHECK(s.min == -80 && s.max == -60 && s.last == -80);
    CHECK(s.mean() == -70.0f);
}

static void test_tracked_windows() {
    std::vector<TrackedDevice> devices;
    CHECK(parse_track_list("112233445566=Tom,aabbccddeeff=Mittens", devices));
    TrackedWindows w;
    w.set_devices(devices);

    Address tom = devices[0].addr;
    Address mittens = devices[1].addr;
    Address stranger{1, 2, 3, 4, 5, 6};
    Advertisement adv;
    adv.has_tx_power = true;
    adv.tx_power = -12;

    CHECK(w.find(mittens) == 1);
    CHECK(w.find(stranger) == -1);
    CHECK(!w.record(stranger, -50, adv));
    CHECK(w.record(mittens, -71, adv));
    CHECK(w.record(mittens, -65, Advertisement{}));
    CHECK(w.record(mittens, kRssiUnavailable, Advertisement{}));

    auto reports = w.take();
    CHECK(reports.size() == 1);
    CHECK(reports[0].device.name == "Mittens");
    CHECK(reports[0].rssi.count == 2);
    CHECK(reports[0].rssi.max == -65);
    CHECK(reports[0].adv.has_tx_power && reports[0].adv.tx_power == -12);

    // The next window starts empty
    CHECK(w.take().empty());
    CHECK(w.record(tom, -90, Advertisement{}));
    reports = w.take();
    CHECK(reports.size() == 1 && reports[0].device.name == "Tom");

    // Replacing the devices drops the old window
    CHECK(w.record(tom, -90, Advertisement{}));
    w.set_devices({});
    CHECK(w.take().empty());
    CHECK(!w.record(tom, -90, Advertisement{}));
}

static void test_device_table() {
    DeviceTable<> t(2);
    Address a{1, 1, 1, 1, 1, 1};
    Address b{2, 2, 2, 2, 2, 2};
    Address c{3, 3, 3, 3, 3, 3};

    Advertisement named;
    strcpy(named.name, "Tile");
    CHECK(t.record(a, 1, -80, Advertisement{}));
    CHECK(t.record(b, 0, -60, Advertisement{}));
    CHECK(t.record(a, 1, -70, named));
    CHECK(!t.record(c, 0, -40, Advertisement{}));          // full
    CHECK(!t.record(a, 1, kRssiUnavailable, Advertisement{}));

    size_t dropped = 0;
    auto entries = t.take(&dropped);
    CHECK(dropped == 1);
    CHECK(entries.size() == 2);
    CHECK(entries[0].addr == b);                            // strongest first
    CHECK(entries[1].addr == a);
    CHECK(entries[1].rssi.count == 2);
    CHECK(strcmp(entries[1].adv.name, "Tile") == 0);

    CHECK(t.size() == 0);
    CHECK(t.record(c, 0, -40, Advertisement{}));
    entries = t.take(&dropped);
    CHECK(dropped == 0);
    CHECK(entries.size() == 1);

    CHECK(t.record(c, 0, -40, Advertisement{}));
    CHECK(t.record(a, 0, -40, Advertisement{}));
    t.release();
    CHECK(t.size() == 0);
    CHECK(t.take(&dropped).empty() && dropped == 0);
}

// Counts what passes through it, to check where the table's storage comes from
static size_t g_live_bytes = 0;
static size_t g_allocations = 0;

template <class T>
struct CountingAllocator {
    using value_type = T;
    CountingAllocator() = default;
    template <class U>
    CountingAllocator(const CountingAllocator<U>&) {}
    T* allocate(size_t n) {
        g_live_bytes += n * sizeof(T);
        ++g_allocations;
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* p, size_t n) {
        g_live_bytes -= n * sizeof(T);
        ::operator delete(p);
    }
};
template <class T, class U>
bool operator==(const CountingAllocator<T>&, const CountingAllocator<U>&) { return true; }
template <class T, class U>
bool operator!=(const CountingAllocator<T>&, const CountingAllocator<U>&) { return false; }

static void test_device_table_allocator() {
    {
        DeviceTable<CountingAllocator<SeenDevice>> t(128);
        CHECK(g_live_bytes == 0);                       // nothing until discovery starts

        t.take();                                       // discovery on: room for a full window
        CHECK(g_live_bytes == 128 * sizeof(SeenDevice));

        size_t allocations = g_allocations;
        for (int i = 0; i < 100; ++i) {
            Address a{0xc0, 0, 0, 0, 0, (uint8_t)i};
            CHECK(t.record(a, 1, -60, Advertisement{}));
        }
        CHECK(g_allocations == allocations);            // recording never allocates

        {
            auto entries = t.take();                    // the window's entries, plus a fresh table
            CHECK(entries.size() == 100);
            CHECK(g_live_bytes == 2 * 128 * sizeof(SeenDevice));
        }
        CHECK(g_live_bytes == 128 * sizeof(SeenDevice));

        t.release();                                    // discovery off: all of it goes back
        CHECK(g_live_bytes == 0);
    }
    CHECK(g_live_bytes == 0);
}

int main() {
    test_parse_address();
    test_address_from_le_and_type();
    test_parse_track_list();
    test_parse_advertisement();
    test_rssi_stats();
    test_tracked_windows();
    test_device_table();
    test_device_table_allocator();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ble_scan_logic: all tests passed\n");
    return 0;
}
