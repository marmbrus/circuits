#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>
#include "esp_heap_caps.h"

// Simple allocator that places allocations in SPIRAM using ESP-IDF heap capabilities.
// Intended for large frame/pixel buffers that don't need to be in internal DRAM.
template <class T>
class PsramAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <class U>
    struct rebind { using other = PsramAllocator<U>; };

    PsramAllocator() noexcept {}
    template <class U>
    PsramAllocator(const PsramAllocator<U>&) noexcept {}

    [[nodiscard]] pointer allocate(size_type n) {
        if (n > max_size()) return nullptr;
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
        return static_cast<pointer>(p);
    }

    void deallocate(pointer p, size_type) noexcept {
        heap_caps_free(p);
    }

    constexpr size_type max_size() const noexcept {
        return std::numeric_limits<size_type>::max() / sizeof(T);
    }

    using is_always_equal = std::true_type;
};

template <class T, class U>
constexpr bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept { return true; }
template <class T, class U>
constexpr bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept { return false; }


