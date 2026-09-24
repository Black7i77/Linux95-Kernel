#include "filesystem/fat32.hpp"

#include "filesystem/fat32_helpers.hpp"
#include "storage/disk.hpp"

namespace linux95::filesystem::fat32 {

bool mount(VolumeInfo& volume)
{
    volume.mounted = false;
    volume.bytes_per_sector = 0;
    volume.sectors_per_cluster = 0;
    volume.fat_count = 0;
    volume.sectors_per_fat = 0;
    volume.total_sectors = 0;
    volume.root_cluster = 0;

    const storage::ata::DeviceInfo& device =
        storage::info(storage::DiskId::Test);

    if (!device.present || !device.ata_device) {
        return false;
    }

    uint8_t boot_sector[512];

    if (!storage::read_sector(
            storage::DiskId::Test,
            0,
            boot_sector)) {
        return false;
    }

    helpers::BpbGeometry geometry = {};

    if (!helpers::parse_bpb(
            boot_sector,
            device.lba28_sector_count,
            geometry)) {
        return false;
    }

    volume.mounted = true;
    volume.bytes_per_sector =
        geometry.bytes_per_sector;
    volume.sectors_per_cluster =
        geometry.sectors_per_cluster;
    volume.fat_count =
        geometry.fat_count;
    volume.sectors_per_fat =
        geometry.sectors_per_fat;
    volume.total_sectors =
        geometry.total_sectors;
    volume.root_cluster =
        geometry.root_cluster;

    return true;
}

} // namespace linux95::filesystem::fat32
