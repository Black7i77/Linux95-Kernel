#pragma once

#include <stddef.h>
#include <stdint.h>

namespace linux95::heap {

bool initialize(uintptr_t start, uintptr_t end);
void* allocate(size_t bytes);
uint64_t used_bytes();
uint64_t capacity_bytes();

} // namespace linux95::heap
