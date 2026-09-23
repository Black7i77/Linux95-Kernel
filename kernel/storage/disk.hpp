#pragma once

#include <stdint.h>

#include "storage/ata.hpp"

namespace linux95::storage {

enum class DiskId : uint8_t {
    Boot = 0,
    Test = 1,
};

constexpr ata::Drive drive_for(DiskId disk)
{
    return disk == DiskId::Boot ? ata::Drive::Master : ata::Drive::Slave;
}

constexpr bool disk_write_allowed(DiskId disk)
{
    return disk == DiskId::Test;
}

bool initialize();
bool read_sector(DiskId disk, uint32_t lba, uint8_t* buffer);
bool write_sector(DiskId disk, uint32_t lba, const uint8_t* buffer);
const ata::DeviceInfo& info(DiskId disk);

} // namespace linux95::storage
