#pragma once

#include <stdint.h>

#include "boot_info.hpp"

namespace linux95::memory::physical {

constexpr uint64_t kInvalidPhysicalAddress = UINT64_MAX;

bool initialize(const linux95::BootInfo& boot_info);
uint64_t allocate_page();
bool free_page(uint64_t physical_address);
void reserve_range(uint64_t start, uint64_t length);
uint64_t total_pages();
uint64_t free_pages();
uint64_t used_pages();
uint64_t bitmap_physical_address();
uint64_t bitmap_bytes();

} // namespace linux95::memory::physical
