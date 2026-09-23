#pragma once

#include <stdint.h>

#include "boot_info.hpp"

namespace linux95::memory::virtual_memory {

bool prepare_bootstrap(const linux95::BootInfo& boot_info);
uint64_t bootstrap_cr3();
uint64_t higher_half_alias(uint64_t low_address);
bool hhdm_contains(uint64_t physical_address);
uint64_t hhdm_limit();

} // namespace linux95::memory::virtual_memory
