#pragma once

#include <cstddef>

#include "process/process.hpp"

namespace linux95::scheduler {

int choose_next(const linux95::process::Process* table,
                size_t count,
                int previous_slot);

}
