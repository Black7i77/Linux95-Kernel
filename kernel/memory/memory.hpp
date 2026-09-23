#pragma once

#include "boot_info.hpp"

#include <stdint.h>

namespace linux95::memory {

bool initialize(const BootInfo& boot_info);
uint64_t total_bytes();
uint64_t usable_bytes();
uint32_t usable_regions();
uintptr_t heap_start();
uintptr_t heap_end();

} // namespace linux95::memory
