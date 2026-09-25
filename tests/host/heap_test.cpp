#include <cassert>
#include <cstdint>

#include "memory/heap.hpp"

int main()
{
    alignas(16) uint8_t arena[256]{};
    const uintptr_t start = reinterpret_cast<uintptr_t>(arena);
    assert(linux95::heap::initialize(start, start + sizeof(arena)));

    void* first = linux95::heap::allocate(16);
    assert(first == arena);
    const auto checkpoint = linux95::heap::checkpoint();
    const uint64_t used_at_checkpoint = linux95::heap::used_bytes();

    void* temporary = linux95::heap::allocate(31);
    assert(temporary != nullptr);
    assert(linux95::heap::used_bytes() > used_at_checkpoint);
    assert(linux95::heap::rewind(checkpoint));
    assert(linux95::heap::used_bytes() == used_at_checkpoint);
    assert(linux95::heap::allocate(31) == temporary);
    return 0;
}
