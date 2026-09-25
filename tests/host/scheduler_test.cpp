#include <cassert>
#include "process/scheduler.hpp"

int main() {
    using namespace linux95::process;

    Process table[4]{};

    table[0].state = State::Ready;
    table[1].state = State::Blocked;
    table[2].state = State::Ready;
    table[3].state = State::Exited;

    assert(linux95::scheduler::choose_next(table, 4, -1) == 0);
    assert(linux95::scheduler::choose_next(table, 4, 0) == 2);
    assert(linux95::scheduler::choose_next(table, 4, 2) == 0);

    table[0].state = State::Blocked;
    table[2].state = State::Exited;
    assert(linux95::scheduler::choose_next(table, 4, 2) == -1);

    return 0;
}
