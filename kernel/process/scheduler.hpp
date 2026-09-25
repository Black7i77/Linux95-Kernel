#pragma once

#include <cstddef>

#include "process/process.hpp"
#include "process/context.hpp"

namespace linux95::scheduler {

int choose_next(const linux95::process::Process* table,
                size_t count,
                int previous_slot);

enum class HostReason {
    Yield,
    Exit,
    Fault,
};

bool run_once();
[[noreturn]] void return_to_host(
    linux95::process::Process& process,
    linux95::process::UserContext& context,
    HostReason reason);

}
