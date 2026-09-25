#pragma once

#include "arch/mouse_helpers.hpp"

namespace linux95::mouse {

bool initialize();
void on_irq();

bool has_event();
MouseEvent read_event();

} // namespace linux95::mouse
