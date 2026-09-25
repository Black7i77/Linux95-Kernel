#pragma once

#include <stddef.h>
#include <stdint.h>

namespace linux95::heap {

struct Checkpoint {
    uintptr_t current;
};

bool initialize(uintptr_t start, uintptr_t end);
void* allocate(size_t bytes);
Checkpoint checkpoint();
bool rewind(Checkpoint checkpoint);
uint64_t used_bytes();
uint64_t capacity_bytes();

} // namespace linux95::heap
