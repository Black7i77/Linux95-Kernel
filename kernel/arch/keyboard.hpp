#pragma once

namespace linux95::keyboard {

void initialize();
void on_irq();
bool has_char();
char read_char();

} // namespace linux95::keyboard
