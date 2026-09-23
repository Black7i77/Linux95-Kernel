#pragma once

#include <stddef.h>
#include <stdint.h>

#include "storage/ata.hpp"

namespace linux95::storage::ata::helpers {

constexpr uint32_t kMaxLba28 = 0x0FFFFFFFU;

constexpr bool valid_lba28(uint32_t lba)
{
    return lba <= kMaxLba28;
}

constexpr bool write_allowed(Drive drive)
{
    return drive == Drive::Slave;
}

constexpr bool status_is_absent(uint8_t status)
{
    return status == 0x00U || status == 0xFFU;
}

constexpr bool status_has_error(uint8_t status)
{
    return (status & (0x01U | 0x20U)) != 0;
}

constexpr bool status_can_accept_command(uint8_t status)
{
    if (status_is_absent(status)) return false;
    return (status & 0x80U) == 0;
}

constexpr bool status_data_ready(uint8_t status)
{
    if (status_is_absent(status) || status_has_error(status)) return false;
    return (status & 0x80U) == 0 && (status & 0x08U) != 0;
}

constexpr bool status_write_complete(uint8_t status)
{
    if (status_is_absent(status) || status_has_error(status)) return false;
    return (status & (0x80U | 0x08U)) == 0;
}

inline void decode_model(const uint16_t* words, char out[41])
{
    if (words == nullptr || out == nullptr) return;

    for (size_t i = 0; i < 20; ++i) {
        const uint16_t word = words[27 + i];
        out[i * 2] = static_cast<char>((word >> 8) & 0xFFU);
        out[i * 2 + 1] = static_cast<char>(word & 0xFFU);
    }

    out[40] = '\0';

    size_t end = 40;
    while (end > 0 && out[end - 1] == ' ') {
        --end;
    }
    out[end] = '\0';
}

inline void fill_test_pattern(uint8_t* out, uint8_t seed)
{
    if (out == nullptr) return;

    for (size_t i = 0; i < 512; ++i) {
        out[i] = static_cast<uint8_t>(
            seed ^ static_cast<uint8_t>(i & 0xFFU));
    }
}

} // namespace linux95::storage::ata::helpers
