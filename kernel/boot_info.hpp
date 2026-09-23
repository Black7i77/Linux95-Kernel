#pragma once

#include <stdint.h>

namespace linux95 {

constexpr uint32_t kBootInfoMagic = 0x4C393542u;

struct __attribute__((packed)) BootInfo {
    uint32_t magic;
    uint32_t e820_count;
    uint64_t e820_address;
    uint8_t boot_drive;
};

static_assert(sizeof(BootInfo) == 17,
              "Linux95 BootInfo ABI changed");

}
