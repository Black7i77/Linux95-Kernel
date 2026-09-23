#include "storage/disk.hpp"

namespace linux95::storage {
namespace {

ata::DeviceInfo g_boot_info{};
ata::DeviceInfo g_test_info{};
bool g_initialized = false;

ata::DeviceInfo& mutable_info(DiskId disk)
{
    return disk == DiskId::Boot ? g_boot_info : g_test_info;
}

} // namespace

bool initialize()
{
    g_boot_info = {};
    g_test_info = {};

    const bool boot_ok = ata::identify(ata::Drive::Master, g_boot_info);
    (void)ata::identify(ata::Drive::Slave, g_test_info);
    g_initialized = true;
    return boot_ok;
}

bool read_sector(DiskId disk, uint32_t lba, uint8_t* buffer)
{
    if (!g_initialized) return false;
    const ata::DeviceInfo& device = mutable_info(disk);
    if (!device.present || !device.ata_device) return false;
    if (device.lba28_sector_count != 0 && lba >= device.lba28_sector_count) {
        return false;
    }
    return ata::read_sector(drive_for(disk), lba, buffer);
}

bool write_sector(DiskId disk, uint32_t lba, const uint8_t* buffer)
{
    if (!g_initialized || !disk_write_allowed(disk)) return false;
    const ata::DeviceInfo& device = mutable_info(disk);
    if (!device.present || !device.ata_device) return false;
    if (device.lba28_sector_count != 0 && lba >= device.lba28_sector_count) {
        return false;
    }
    return ata::write_sector(drive_for(disk), lba, buffer);
}

const ata::DeviceInfo& info(DiskId disk)
{
    return mutable_info(disk);
}

} // namespace linux95::storage
