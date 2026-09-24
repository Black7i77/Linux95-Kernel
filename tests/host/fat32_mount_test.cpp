#include "filesystem/filesystem.hpp"
#include "filesystem/fat32.hpp"
#include "storage/disk.hpp"

#include <assert.h>
#include <stdint.h>
#include <string.h>

namespace {

uint8_t fake_sector[512] = {};

linux95::storage::ata::DeviceInfo fake_device = {
    true,
    true,
    131072,
    {}
};

void put16(size_t offset, uint16_t value)
{
    fake_sector[offset] = static_cast<uint8_t>(value & 0xFFu);
    fake_sector[offset + 1] =
        static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void put32(size_t offset, uint32_t value)
{
    fake_sector[offset] =
        static_cast<uint8_t>(value & 0xFFu);
    fake_sector[offset + 1] =
        static_cast<uint8_t>((value >> 8) & 0xFFu);
    fake_sector[offset + 2] =
        static_cast<uint8_t>((value >> 16) & 0xFFu);
    fake_sector[offset + 3] =
        static_cast<uint8_t>((value >> 24) & 0xFFu);
}

void make_valid_bpb()
{
    memset(fake_sector, 0, sizeof(fake_sector));

    put16(11, 512);
    fake_sector[13] = 1;
    put16(14, 32);
    fake_sector[16] = 2;

    put16(17, 0);
    put16(19, 0);
    put16(22, 0);

    put32(32, 131072);
    put32(36, 1010);

    put16(42, 0);
    put32(44, 2);

    fake_sector[510] = 0x55;
    fake_sector[511] = 0xAA;
}

} // namespace

namespace linux95::storage {

bool read_sector(
    DiskId disk,
    uint32_t lba,
    uint8_t* buffer)
{
    if (disk != DiskId::Test ||
        lba != 0 ||
        buffer == nullptr) {
        return false;
    }

    memcpy(buffer, fake_sector, sizeof(fake_sector));
    return true;
}

const ata::DeviceInfo& info(DiskId)
{
    return fake_device;
}

} // namespace linux95::storage

int main()
{
    using namespace linux95;

    make_valid_bpb();

    assert(filesystem::initialize());

    const filesystem::VolumeInfo& volume =
        filesystem::volume_info();

    assert(volume.mounted);
    assert(volume.bytes_per_sector == 512);
    assert(volume.sectors_per_cluster == 1);
    assert(volume.fat_count == 2);
    assert(volume.sectors_per_fat == 1010);
    assert(volume.total_sectors == 131072);
    assert(volume.root_cluster == 2);

    fake_sector[510] = 0;

    assert(!filesystem::initialize());
    assert(!filesystem::volume_info().mounted);

    make_valid_bpb();
    fake_device.present = false;

    assert(!filesystem::initialize());
    assert(!filesystem::volume_info().mounted);

    fake_device.present = true;
    fake_device.ata_device = false;

    assert(!filesystem::initialize());
    assert(!filesystem::volume_info().mounted);

    fake_device.ata_device = true;

    return 0;
}
