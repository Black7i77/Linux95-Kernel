#pragma once

#include <stdint.h>

namespace linux95::ps2 {

bool wait_input_clear(uint32_t limit);
bool wait_output_full(uint32_t limit);

} // namespace linux95::ps2
