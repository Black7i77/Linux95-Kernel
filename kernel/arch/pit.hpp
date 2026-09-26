#pragma once

#include <stdint.h>

namespace linux95::pit {

void initialize(uint32_t hz);
void on_irq();
uint64_t ticks();
uint32_t ticks_per_second();
uint64_t uptime_seconds();

} // namespace linux95::pit
