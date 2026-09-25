#include "pci/pci.hpp"

#include <assert.h>
#include <stdint.h>

int main()
{
    using linux95::pci::Address;

    assert(
        linux95::pci::make_config_address(
            Address{2, 5, 1},
            0x10) ==
        0x80022910u);

    assert(
        linux95::pci::io_bar_base(0x0000C001u) ==
        0x0000C000u);

    assert(linux95::pci::is_io_bar(0x0000C001u));
    assert(!linux95::pci::is_io_bar(0xFEBF0000u));

    return 0;
}
