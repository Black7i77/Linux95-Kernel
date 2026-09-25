#include "memory/heap.hpp"

#include <stdint.h>

namespace linux95::heap {
namespace {

uintptr_t g_start = 0;
uintptr_t g_current = 0;
uintptr_t g_end = 0;

uintptr_t align_up_16(uintptr_t value)
{
    if (value > UINTPTR_MAX - 15u) {
        return 0;
    }

    return (value + 15u) & ~static_cast<uintptr_t>(15u);
}

} // namespace

bool initialize(uintptr_t start, uintptr_t end)
{
    const uintptr_t aligned_start = align_up_16(start);
    const uintptr_t aligned_end = end & ~static_cast<uintptr_t>(15u);

    if (aligned_start == 0 || aligned_end <= aligned_start) {
        g_start = 0;
        g_current = 0;
        g_end = 0;
        return false;
    }

    g_start = aligned_start;
    g_current = aligned_start;
    g_end = aligned_end;
    return true;
}

void* allocate(size_t bytes)
{
    if (bytes == 0 || g_current == 0) {
        return nullptr;
    }

    if (bytes > UINTPTR_MAX - 15u) {
        return nullptr;
    }

    const uintptr_t aligned_size =
        (static_cast<uintptr_t>(bytes) + 15u) &
        ~static_cast<uintptr_t>(15u);

    if (aligned_size > g_end - g_current) {
        return nullptr;
    }

    const uintptr_t result = g_current;
    g_current += aligned_size;
    return reinterpret_cast<void*>(result);
}

Checkpoint checkpoint()
{
    return {g_current};
}

bool rewind(Checkpoint checkpoint_value)
{
    if (g_current == 0 || checkpoint_value.current < g_start ||
        checkpoint_value.current > g_current ||
        (checkpoint_value.current & 0xFULL) != 0) {
        return false;
    }
    g_current = checkpoint_value.current;
    return true;
}

uint64_t used_bytes()
{
    return g_current >= g_start ? g_current - g_start : 0;
}

uint64_t capacity_bytes()
{
    return g_end >= g_start ? g_end - g_start : 0;
}

} // namespace linux95::heap
