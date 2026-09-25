#include "process/scheduler.hpp"

namespace linux95::scheduler {

int choose_next(const linux95::process::Process* table,
                size_t count,
                int previous_slot) {
    if (table == nullptr || count == 0) {
        return -1;
    }

    const size_t start = static_cast<size_t>(previous_slot + 1) % count;
    for (size_t offset = 0; offset < count; ++offset) {
        const size_t slot = (start + offset) % count;
        if (table[slot].state == linux95::process::State::Ready) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

}
