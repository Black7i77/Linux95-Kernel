#pragma once

#include <stdint.h>

namespace linux95::arch::x86_64 {

constexpr uint16_t kKernelCodeSelector = 0x08;
constexpr uint16_t kKernelDataSelector = 0x10;
constexpr uint16_t kUserDataSelector = 0x1B;
constexpr uint16_t kUserCodeSelector = 0x23;
constexpr uint16_t kTssSelector = 0x28;

enum class DescriptorPrivilege : uint8_t {
    Ring0 = 0,
    Ring3 = 3,
};

enum class SegmentKind : uint8_t {
    Code,
    Data,
};

struct TssDescriptor {
    uint64_t low;
    uint64_t high;
};

constexpr uint64_t make_code_data_descriptor(
    uint32_t base,
    uint32_t limit,
    DescriptorPrivilege privilege,
    SegmentKind kind)
{
    const uint64_t type = kind == SegmentKind::Code ? 0xAULL : 0x2ULL;
    const uint64_t long_mode = kind == SegmentKind::Code ? (1ULL << 53) : 0;
    const uint64_t default_size = kind == SegmentKind::Data ? (1ULL << 54) : 0;

    return (static_cast<uint64_t>(limit) & 0xFFFFULL)
        | ((static_cast<uint64_t>(base) & 0xFFFFFFULL) << 16)
        | (type << 40)
        | (1ULL << 44)
        | (static_cast<uint64_t>(privilege) << 45)
        | (1ULL << 47)
        | ((static_cast<uint64_t>(limit) & 0xF0000ULL) << 32)
        | long_mode
        | default_size
        | (1ULL << 55)
        | ((static_cast<uint64_t>(base) & 0xFF000000ULL) << 32);
}

constexpr TssDescriptor make_tss_descriptor(uint64_t base, uint32_t limit)
{
    return {
        (static_cast<uint64_t>(limit) & 0xFFFFULL)
            | ((base & 0xFFFFFFULL) << 16)
            | (0x9ULL << 40)
            | (1ULL << 47)
            | ((static_cast<uint64_t>(limit) & 0xF0000ULL) << 32)
            | ((base & 0xFF000000ULL) << 32),
        base >> 32,
    };
}

void initialize_segments();

}
