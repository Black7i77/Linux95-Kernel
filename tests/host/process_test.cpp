#include <cassert>
#include "process/process.hpp"

int main() {
    using namespace linux95::process;

    initialize();
    assert(capacity() == 16);

    Process* first = allocate();
    Process* second = allocate();

    assert(first != nullptr);
    assert(second != nullptr);
    assert(first->pid == 1);
    assert(second->pid == 2);
    assert(first->state == State::Created);
    assert(second->state == State::Created);

    for (int i = 0; i < 14; ++i) {
        assert(allocate() != nullptr);
    }
    assert(allocate() == nullptr);

    const uint32_t old_pid = first->pid;
    release(*first);
    Process* replacement = allocate();
    assert(replacement != nullptr);
    assert(replacement->pid > old_pid);

    return 0;
}
