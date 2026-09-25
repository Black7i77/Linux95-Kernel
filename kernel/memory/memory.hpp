#pragma once

#include "boot_info.hpp"

#include <stdint.h>

namespace linux95::memory {

bool initialize(const BootInfo& boot_info);
uint64_t total_bytes();
uint64_t usable_bytes();
uint32_t usable_regions();
uint64_t maximum_physical_address();
uint64_t maximum_physical_address(const BootInfo& boot_info);
uint64_t maximum_usable_physical_address(const BootInfo& boot_info);
uintptr_t heap_start();
uintptr_t heap_end();
bool kernel_virtual_to_physical(
    const void* address,
    uint64_t& physical);

} // namespace linux95::memory
