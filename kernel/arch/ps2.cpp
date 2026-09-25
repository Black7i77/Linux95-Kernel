#include "arch/ps2.hpp"

#include "arch/io.hpp"

#include <stdint.h>

namespace linux95::ps2 {

namespace {

constexpr uint16_t kStatusPort = 0x64;

constexpr uint8_t kStatusOutputFull = 1u << 0;
constexpr uint8_t kStatusInputFull = 1u << 1;

} // namespace

bool wait_input_clear(uint32_t limit)
{
    while (limit != 0) {
        if ((io::inb(kStatusPort) & kStatusInputFull) == 0) {
            return true;
        }

        --limit;
        io::pause();
    }

    return false;
}

bool wait_output_full(uint32_t limit)
{
    while (limit != 0) {
        if ((io::inb(kStatusPort) & kStatusOutputFull) != 0) {
            return true;
        }

        --limit;
        io::pause();
    }

    return false;
}

} // namespace linux95::ps2
