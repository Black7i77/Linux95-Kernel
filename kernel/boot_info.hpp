#pragma once

#include <stdint.h>

namespace linux95 {

constexpr uint32_t kBootInfoMagic = 0x4C393542u;
constexpr uint32_t kMaxE820Entries = 128u;

struct __attribute__((packed)) E820Entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
};

struct __attribute__((packed)) BootInfo {
    uint32_t magic;
    uint32_t e820_count;
    uint64_t e820_address;
    uint8_t boot_drive;
};

static_assert(sizeof(E820Entry) == 24, "Linux95 E820 ABI changed");
static_assert(sizeof(BootInfo) == 17, "Linux95 BootInfo ABI changed");

inline bool valid_boot_info(const BootInfo* info)
{
    return info != nullptr &&
           info->magic == kBootInfoMagic &&
           info->e820_count > 0 &&
           info->e820_count <= kMaxE820Entries &&
           info->e820_address != 0;
}

} // namespace linux95
