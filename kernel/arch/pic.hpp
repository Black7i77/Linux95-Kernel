#pragma once

#include <stdint.h>

namespace linux95::pic {

void initialize();
void mask_all();
void unmask_irq(uint8_t irq);
void send_eoi(uint8_t irq);

} // namespace linux95::pic
