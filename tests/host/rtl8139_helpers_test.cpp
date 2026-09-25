#include "drivers/rtl8139_helpers.hpp"

#include <assert.h>

int main()
{
    using namespace linux95::rtl8139;

    assert(valid_rx_length(64));
    assert(valid_rx_length(1518));
    assert(!valid_rx_length(0));
    assert(!valid_rx_length(3));
    assert(!valid_rx_length(2048));

    assert(next_tx_slot(0) == 1);
    assert(next_tx_slot(3) == 0);

    assert(
        advance_rx_offset(0, 64) ==
        68);

    assert(
        advance_rx_offset(8180, 64) <
        8192);

    return 0;
}
