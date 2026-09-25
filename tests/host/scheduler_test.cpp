#include <cassert>
#include <cstdint>
#include "process/scheduler.hpp"

static_assert(
    linux95::scheduler::kUserQuantumTicks == 5,
    "user scheduling quantum must remain five PIT ticks");

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

    using linux95::scheduler::on_timer_tick;
    using linux95::scheduler::reset_user_quantum;

    reset_user_quantum();
    for (uint32_t tick = 0; tick < 4; ++tick) {
        assert(!on_timer_tick(true));
    }
    assert(on_timer_tick(true));
    assert(!on_timer_tick(true));

    reset_user_quantum();
    assert(!on_timer_tick(true));
    assert(!on_timer_tick(false));
    assert(!on_timer_tick(false));
    assert(!on_timer_tick(true));
    assert(!on_timer_tick(true));
    assert(!on_timer_tick(true));
    assert(on_timer_tick(true));

    reset_user_quantum();
    for (uint32_t tick = 0; tick < 4; ++tick) {
        assert(!on_timer_tick(true));
    }
    reset_user_quantum();
    for (uint32_t tick = 0; tick < 4; ++tick) {
        assert(!on_timer_tick(true));
    }
    assert(on_timer_tick(true));

    return 0;
}
