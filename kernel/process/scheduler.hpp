#pragma once

#include <cstddef>
#include <cstdint>

#include "process/process.hpp"
#include "process/context.hpp"

namespace linux95::scheduler {

constexpr uint32_t kUserQuantumTicks = 5;

int choose_next(const linux95::process::Process* table,
                size_t count,
                int previous_slot);

void reset_user_quantum();
bool on_timer_tick(bool from_user);

enum class HostReason {
    Yield,
    Exit,
    Fault,
    Preempt,
};

bool run_once();
[[noreturn]] void return_to_host(
    linux95::process::Process& process,
    linux95::process::UserContext& context,
    HostReason reason);

}
