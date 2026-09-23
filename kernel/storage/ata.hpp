#pragma once

#include <stdint.h>

namespace linux95::storage::ata {

enum class Drive : uint8_t {
    Master = 0,
    Slave = 1,
};

struct DeviceInfo {
    bool present;
    bool ata_device;
    uint32_t lba28_sector_count;
    char model[41];
};

bool identify(Drive drive, DeviceInfo& info);
bool read_sector(Drive drive, uint32_t lba, uint8_t* buffer);
bool write_sector(Drive drive, uint32_t lba, const uint8_t* buffer);
bool flush_cache(Drive drive);

} // namespace linux95::storage::ata
