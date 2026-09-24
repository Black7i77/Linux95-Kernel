#include "filesystem/filesystem.hpp"
#include "filesystem/fat32.hpp"
#include "storage/disk.hpp"

#include <assert.h>
#include <stdint.h>
#include <string.h>

namespace {

uint8_t fake_sector[512] = {};
uint8_t fake_fat_sector[512] = {};
uint8_t fake_root_sector[512] = {};
uint8_t fake_docs_sector[512] = {};
uint8_t fake_kernel_sector[512] = {};
uint8_t fake_readme_sector[512] = {};
uint8_t fake_chain_sector_0[512] = {};
uint8_t fake_chain_sector_1[512] = {};
uint8_t fake_chain_sector_2[512] = {};

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

void make_root_directory()
{
    memset(fake_fat_sector, 0, sizeof(fake_fat_sector));
    memset(fake_root_sector, 0, sizeof(fake_root_sector));

    // Root cluster 2 -> EOC.
    fake_fat_sector[8]  = 0xFF;
    fake_fat_sector[9]  = 0xFF;
    fake_fat_sector[10] = 0xFF;
    fake_fat_sector[11] = 0x0F;

    // DOCS cluster 3 -> EOC.
    fake_fat_sector[12] = 0xFF;
    fake_fat_sector[13] = 0xFF;
    fake_fat_sector[14] = 0xFF;
    fake_fat_sector[15] = 0x0F;

    // KERNEL.TXT cluster 4 -> EOC.
    fake_fat_sector[16] = 0xFF;
    fake_fat_sector[17] = 0xFF;
    fake_fat_sector[18] = 0xFF;
    fake_fat_sector[19] = 0x0F;

    // README.TXT cluster 5 -> EOC.
    fake_fat_sector[20] = 0xFF;
    fake_fat_sector[21] = 0xFF;
    fake_fat_sector[22] = 0xFF;
    fake_fat_sector[23] = 0x0F;

    // CHAIN.TXT cluster 6 -> 7 -> 8 -> EOC.
    fake_fat_sector[24] = 7;
    fake_fat_sector[28] = 8;

    fake_fat_sector[32] = 0xFF;
    fake_fat_sector[33] = 0xFF;
    fake_fat_sector[34] = 0xFF;
    fake_fat_sector[35] = 0x0F;

    // Fill every directory slot as deleted so traversal must
    // reach the FAT entry after scanning the whole cluster.
    for (size_t offset = 0;
         offset < sizeof(fake_root_sector);
         offset += 32) {
        fake_root_sector[offset] = 0xE5;
    }

    const uint8_t name[11] =
        {'R','E','A','D','M','E',' ',' ','T','X','T'};

    memcpy(fake_root_sector, name, sizeof(name));

    fake_root_sector[11] = 0x20;

    // First cluster = 5.
    fake_root_sector[26] = 5;
    fake_root_sector[27] = 0;

    // File size = 34.
    fake_root_sector[28] = 34;

    memset(fake_readme_sector, 0, sizeof(fake_readme_sector));

    const char readme_text[] =
        "Linux95 FAT32 filesystem online.\r\n";

    memcpy(
        fake_readme_sector,
        readme_text,
        sizeof(readme_text) - 1);

    const uint8_t docs_name[11] =
        {'D','O','C','S',' ',' ',' ',' ',' ',' ',' '};

    memcpy(fake_root_sector + 32, docs_name, sizeof(docs_name));
    fake_root_sector[32 + 11] = 0x10;
    fake_root_sector[32 + 26] = 3;

    const uint8_t chain_name[11] =
        {'C','H','A','I','N',' ',' ',' ','T','X','T'};

    memcpy(
        fake_root_sector + 64,
        chain_name,
        sizeof(chain_name));

    fake_root_sector[64 + 11] = 0x20;
    fake_root_sector[64 + 26] = 6;

    // 1536 bytes = 0x00000600.
    fake_root_sector[64 + 28] = 0x00;
    fake_root_sector[64 + 29] = 0x06;

    for (size_t i = 0; i < 512; ++i) {
        fake_chain_sector_0[i] =
            static_cast<uint8_t>('A' + (i % 26));

        fake_chain_sector_1[i] =
            static_cast<uint8_t>(
                'A' + ((512 + i) % 26));

        fake_chain_sector_2[i] =
            static_cast<uint8_t>(
                'A' + ((1024 + i) % 26));
    }

    memset(fake_docs_sector, 0, sizeof(fake_docs_sector));

    for (size_t offset = 0;
         offset < sizeof(fake_docs_sector);
         offset += 32) {
        fake_docs_sector[offset] = 0xE5;
    }

    const uint8_t kernel_name[11] =
        {'K','E','R','N','E','L',' ',' ','T','X','T'};

    memcpy(fake_docs_sector, kernel_name, sizeof(kernel_name));
    fake_docs_sector[11] = 0x20;
    fake_docs_sector[26] = 4;
    fake_docs_sector[28] = 37;

    memset(fake_kernel_sector, 0, sizeof(fake_kernel_sector));

    const char kernel_text[] =
        "Linux95 kernel filesystem test file.\r\n";

    memcpy(
        fake_kernel_sector,
        kernel_text,
        sizeof(kernel_text) - 1);
}

bool capture_docs_entry(
    const linux95::filesystem::Entry& entry,
    void* context)
{
    int* count = static_cast<int*>(context);

    assert(strcmp(entry.name, "KERNEL.TXT") == 0);
    assert(!entry.is_directory);
    assert(entry.size == 37u);

    ++(*count);
    return true;
}

bool capture_root_entry(
    const linux95::filesystem::Entry& entry,
    void* context)
{
    int* count = static_cast<int*>(context);

    if (*count == 0) {
        assert(strcmp(entry.name, "README.TXT") == 0);
        assert(!entry.is_directory);
        assert(entry.size == 34u);
    } else if (*count == 1) {
        assert(strcmp(entry.name, "DOCS") == 0);
        assert(entry.is_directory);
        assert(entry.size == 0u);
    } else if (*count == 2) {
        assert(strcmp(entry.name, "CHAIN.TXT") == 0);
        assert(!entry.is_directory);
        assert(entry.size == 1536u);
    } else {
        assert(false);
    }

    ++(*count);
    return true;
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
    if (disk != DiskId::Test || buffer == nullptr) {
        return false;
    }

    if (lba == 0) {
        memcpy(buffer, fake_sector, sizeof(fake_sector));
        return true;
    }

    // FAT begins at LBA 32.
    if (lba == 32) {
        memcpy(
            buffer,
            fake_fat_sector,
            sizeof(fake_fat_sector));
        return true;
    }

    // Cluster 2 begins at first_data_lba 2052.
    if (lba == 2052) {
        memcpy(
            buffer,
            fake_root_sector,
            sizeof(fake_root_sector));
        return true;
    }

    // Cluster 3 begins at LBA 2053.
    if (lba == 2053) {
        memcpy(
            buffer,
            fake_docs_sector,
            sizeof(fake_docs_sector));
        return true;
    }

    // Cluster 4 begins at LBA 2054.
    if (lba == 2054) {
        memcpy(
            buffer,
            fake_kernel_sector,
            sizeof(fake_kernel_sector));
        return true;
    }

    // Cluster 5 begins at LBA 2055.
    if (lba == 2055) {
        memcpy(
            buffer,
            fake_readme_sector,
            sizeof(fake_readme_sector));
        return true;
    }

    if (lba == 2056) {
        memcpy(buffer, fake_chain_sector_0, 512);
        return true;
    }

    if (lba == 2057) {
        memcpy(buffer, fake_chain_sector_1, 512);
        return true;
    }

    if (lba == 2058) {
        memcpy(buffer, fake_chain_sector_2, 512);
        return true;
    }

    return false;
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

    make_root_directory();

    int root_entries = 0;

    assert(filesystem::list_directory(
        "/",
        capture_root_entry,
        &root_entries) == filesystem::Status::Ok);

    assert(root_entries == 3);

    int docs_entries = 0;

    assert(filesystem::list_directory(
        "/dOcS",
        capture_docs_entry,
        &docs_entries) == filesystem::Status::Ok);

    assert(docs_entries == 1);

    int nested_entries = 0;

    assert(filesystem::list_directory(
        "/DoCs/KeRnEl.TxT",
        capture_docs_entry,
        &nested_entries) ==
        filesystem::Status::NotDirectory);

    assert(nested_entries == 0);

    uint8_t read_buffer[64] = {};
    size_t bytes_read = 999;
    uint32_t file_size = 999;

    assert(filesystem::read_file(
        "/README.TXT",
        0,
        read_buffer,
        sizeof(read_buffer),
        bytes_read,
        file_size) == filesystem::Status::Ok);

    assert(bytes_read == 34u);
    assert(file_size == 34u);

    const char expected_readme[] =
        "Linux95 FAT32 filesystem online.\r\n";

    assert(memcmp(
        read_buffer,
        expected_readme,
        34) == 0);

    uint8_t kernel_buffer[64] = {};
    bytes_read = 999;
    file_size = 999;

    assert(filesystem::read_file(
        "/DoCs/KeRnEl.TxT",
        0,
        kernel_buffer,
        sizeof(kernel_buffer),
        bytes_read,
        file_size) == filesystem::Status::Ok);

    assert(bytes_read == 37u);
    assert(file_size == 37u);

    const char expected_kernel[] =
        "Linux95 kernel filesystem test file.\r\n";

    assert(memcmp(
        kernel_buffer,
        expected_kernel,
        37) == 0);

    uint8_t chain_buffer[1536] = {};
    bytes_read = 999;
    file_size = 999;

    assert(filesystem::read_file(
        "/CHAIN.TXT",
        0,
        chain_buffer,
        sizeof(chain_buffer),
        bytes_read,
        file_size) == filesystem::Status::Ok);

    assert(bytes_read == 1536u);
    assert(file_size == 1536u);

    for (size_t i = 0; i < sizeof(chain_buffer); ++i) {
        assert(chain_buffer[i] ==
            static_cast<uint8_t>(
                'A' + (i % 26)));
    }

    uint8_t offset_buffer[100] = {};
    bytes_read = 999;
    file_size = 999;

    assert(filesystem::read_file(
        "/CHAIN.TXT",
        600,
        offset_buffer,
        sizeof(offset_buffer),
        bytes_read,
        file_size) == filesystem::Status::Ok);

    assert(bytes_read == 100u);
    assert(file_size == 1536u);

    for (size_t i = 0; i < sizeof(offset_buffer); ++i) {
        assert(offset_buffer[i] ==
            static_cast<uint8_t>(
                'A' + ((600 + i) % 26)));
    }

    // Read ending exactly at EOF.
    uint8_t tail_buffer[100] = {};
    bytes_read = 999;
    file_size = 999;

    assert(filesystem::read_file(
        "/CHAIN.TXT",
        1500,
        tail_buffer,
        sizeof(tail_buffer),
        bytes_read,
        file_size) == filesystem::Status::Ok);

    assert(bytes_read == 36u);
    assert(file_size == 1536u);

    for (size_t i = 0; i < bytes_read; ++i) {
        assert(tail_buffer[i] ==
            static_cast<uint8_t>(
                'A' + ((1500 + i) % 26)));
    }

    // Offset exactly at EOF.
    bytes_read = 999;
    file_size = 999;

    assert(filesystem::read_file(
        "/CHAIN.TXT",
        1536,
        tail_buffer,
        sizeof(tail_buffer),
        bytes_read,
        file_size) == filesystem::Status::Ok);

    assert(bytes_read == 0u);
    assert(file_size == 1536u);

    // Offset beyond EOF.
    bytes_read = 999;
    file_size = 999;

    assert(filesystem::read_file(
        "/CHAIN.TXT",
        2000,
        tail_buffer,
        sizeof(tail_buffer),
        bytes_read,
        file_size) == filesystem::Status::Ok);

    assert(bytes_read == 0u);
    assert(file_size == 1536u);

    // Zero-capacity read.
    bytes_read = 999;
    file_size = 999;

    assert(filesystem::read_file(
        "/CHAIN.TXT",
        100,
        tail_buffer,
        0,
        bytes_read,
        file_size) == filesystem::Status::Ok);

    assert(bytes_read == 0u);
    assert(file_size == 1536u);

    // Corruption test: chain ends before declared file size.
    // Temporarily make cluster 7 -> EOC.
    fake_fat_sector[28] = 0xFF;
    fake_fat_sector[29] = 0xFF;
    fake_fat_sector[30] = 0xFF;
    fake_fat_sector[31] = 0x0F;

    bytes_read = 999;
    file_size = 999;

    assert(filesystem::read_file(
        "/CHAIN.TXT",
        0,
        chain_buffer,
        sizeof(chain_buffer),
        bytes_read,
        file_size) == filesystem::Status::Corrupt);

    // Restore cluster 7 -> 8.
    fake_fat_sector[28] = 8;
    fake_fat_sector[29] = 0;
    fake_fat_sector[30] = 0;
    fake_fat_sector[31] = 0;

    // Corruption test: make cluster 8 point back to cluster 6.
    // The chain becomes 6 -> 7 -> 8 -> 6.
    fake_fat_sector[32] = 6;
    fake_fat_sector[33] = 0;
    fake_fat_sector[34] = 0;
    fake_fat_sector[35] = 0;

    bytes_read = 999;
    file_size = 999;

    assert(filesystem::read_file(
        "/CHAIN.TXT",
        0,
        chain_buffer,
        sizeof(chain_buffer),
        bytes_read,
        file_size) == filesystem::Status::Corrupt);

    // Restore cluster 8 -> EOC.
    fake_fat_sector[32] = 0xFF;
    fake_fat_sector[33] = 0xFF;
    fake_fat_sector[34] = 0xFF;
    fake_fat_sector[35] = 0x0F;

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
