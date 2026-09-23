#pragma once

#include <stdint.h>

namespace linux95::panic {

[[noreturn]] void halt(const char* message);
[[noreturn]] void exception(uint64_t vector, uint64_t error_code);

} // namespace linux95::panic
