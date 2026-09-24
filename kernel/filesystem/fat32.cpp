#include "filesystem/fat32.hpp"

#include "filesystem/fat32_helpers.hpp"
#include "storage/disk.hpp"

namespace linux95::filesystem::fat32 {

namespace {

bool mounted = false;
uint32_t fat_begin_lba = 0;
uint32_t first_data_lba = 0;
uint32_t cluster_count = 0;
uint32_t root_cluster = 0;
uint8_t sectors_per_cluster = 0;

bool valid_data_cluster(uint32_t cluster)
{
    return cluster >= 2 &&
           (cluster - 2u) < cluster_count;
}

} // namespace

bool mount(VolumeInfo& volume)
{
    mounted = false;

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

    fat_begin_lba = geometry.fat_begin_lba;
    first_data_lba = geometry.first_data_lba;
    cluster_count = geometry.cluster_count;
    root_cluster = geometry.root_cluster;
    sectors_per_cluster = geometry.sectors_per_cluster;
    mounted = true;

    return true;
}

namespace {

Status next_cluster(
    uint32_t cluster,
    uint32_t& next,
    bool& end_of_chain)
{
    uint32_t fat_sector_lba = 0;
    uint16_t inside = 0;

    if (!helpers::fat_entry_location(
            cluster,
            fat_begin_lba,
            fat_sector_lba,
            inside)) {
        return Status::Corrupt;
    }

    uint8_t fat_sector[512];

    if (!storage::read_sector(
            storage::DiskId::Test,
            fat_sector_lba,
            fat_sector)) {
        return Status::IoError;
    }

    const uint32_t value =
        helpers::fat_entry_value(
            helpers::le32(fat_sector + inside));

    const helpers::FatEntryKind kind =
        helpers::classify_fat_entry(value);

    if (kind == helpers::FatEntryKind::EndOfChain) {
        end_of_chain = true;
        next = 0;
        return Status::Ok;
    }

    if (kind != helpers::FatEntryKind::Next ||
        !valid_data_cluster(value)) {
        return Status::Corrupt;
    }

    end_of_chain = false;
    next = value;
    return Status::Ok;
}

Status emit_directory(
    uint32_t first_cluster,
    EntryVisitor visitor,
    void* context)
{
    uint32_t cluster = first_cluster;

    for (uint32_t walked = 0;
         walked < cluster_count;
         ++walked) {

        if (!valid_data_cluster(cluster)) {
            return Status::Corrupt;
        }

        uint32_t cluster_lba = 0;

        if (!helpers::cluster_to_lba(
                first_data_lba,
                sectors_per_cluster,
                cluster,
                cluster_lba)) {
            return Status::Corrupt;
        }

        for (uint32_t sector_index = 0;
             sector_index < sectors_per_cluster;
             ++sector_index) {

            uint32_t lba = 0;

            if (!helpers::checked_add_u32(
                    cluster_lba,
                    sector_index,
                    lba)) {
                return Status::Corrupt;
            }

            uint8_t sector[512];

            if (!storage::read_sector(
                    storage::DiskId::Test,
                    lba,
                    sector)) {
                return Status::IoError;
            }

            for (uint32_t offset = 0;
                 offset < 512;
                 offset += 32) {

                helpers::DirectoryEntry decoded;

                const helpers::DirectoryEntryKind kind =
                    helpers::decode_directory_entry(
                        sector + offset,
                        decoded);

                if (kind ==
                    helpers::DirectoryEntryKind::End) {
                    return Status::Ok;
                }

                if (kind ==
                    helpers::DirectoryEntryKind::Skip) {
                    continue;
                }

                Entry entry;

                for (uint32_t i = 0; i < 13; ++i) {
                    entry.name[i] = decoded.name[i];
                }

                entry.is_directory =
                    decoded.is_directory;
                entry.size =
                    decoded.size;

                if (!visitor(entry, context)) {
                    return Status::Ok;
                }
            }
        }

        uint32_t next = 0;
        bool eoc = false;

        const Status status =
            next_cluster(cluster, next, eoc);

        if (status != Status::Ok) {
            return status;
        }

        if (eoc) {
            return Status::Ok;
        }

        cluster = next;
    }

    return Status::Corrupt;
}

Status find_entry(
    uint32_t first_cluster,
    const char* wanted,
    helpers::DirectoryEntry& found)
{
    uint32_t cluster = first_cluster;

    for (uint32_t walked = 0;
         walked < cluster_count;
         ++walked) {

        if (!valid_data_cluster(cluster)) {
            return Status::Corrupt;
        }

        uint32_t cluster_lba = 0;

        if (!helpers::cluster_to_lba(
                first_data_lba,
                sectors_per_cluster,
                cluster,
                cluster_lba)) {
            return Status::Corrupt;
        }

        for (uint32_t sector_index = 0;
             sector_index < sectors_per_cluster;
             ++sector_index) {

            uint32_t lba = 0;

            if (!helpers::checked_add_u32(
                    cluster_lba,
                    sector_index,
                    lba)) {
                return Status::Corrupt;
            }

            uint8_t sector[512];

            if (!storage::read_sector(
                    storage::DiskId::Test,
                    lba,
                    sector)) {
                return Status::IoError;
            }

            for (uint32_t offset = 0;
                 offset < 512;
                 offset += 32) {

                helpers::DirectoryEntry decoded;

                const helpers::DirectoryEntryKind kind =
                    helpers::decode_directory_entry(
                        sector + offset,
                        decoded);

                if (kind ==
                    helpers::DirectoryEntryKind::End) {
                    return Status::NotFound;
                }

                if (kind ==
                    helpers::DirectoryEntryKind::Skip) {
                    continue;
                }

                if (!helpers::ascii_iequals(
                        decoded.name,
                        wanted)) {
                    continue;
                }

                for (uint32_t i = 0; i < 13; ++i) {
                    found.name[i] = decoded.name[i];
                }

                found.is_directory =
                    decoded.is_directory;
                found.first_cluster =
                    decoded.first_cluster;
                found.size =
                    decoded.size;

                return Status::Ok;
            }
        }

        uint32_t next = 0;
        bool eoc = false;

        const Status status =
            next_cluster(cluster, next, eoc);

        if (status != Status::Ok) {
            return status;
        }

        if (eoc) {
            return Status::NotFound;
        }

        cluster = next;
    }

    return Status::Corrupt;
}

} // namespace

Status list_directory(
    const char* path,
    EntryVisitor visitor,
    void* context)
{
    if (!mounted) {
        return Status::NotMounted;
    }

    if (visitor == nullptr || path == nullptr) {
        return Status::Unsupported;
    }

    if (path[0] == '/' && path[1] == '\0') {
        return emit_directory(
            root_cluster,
            visitor,
            context);
    }

    const char* cursor = path;

    if (*cursor == '/') {
        ++cursor;
    }

    if (*cursor == '\0' ||
        !helpers::valid_path(cursor)) {
        return Status::Unsupported;
    }

    uint32_t directory_cluster = root_cluster;

    while (*cursor != '\0') {
        char component[13];
        uint32_t length = 0;

        while (cursor[length] != '\0' &&
               cursor[length] != '/') {

            if (length >= 12) {
                return Status::Unsupported;
            }

            component[length] = cursor[length];
            ++length;
        }

        component[length] = '\0';

        helpers::DirectoryEntry entry;

        const Status status =
            find_entry(
                directory_cluster,
                component,
                entry);

        if (status != Status::Ok) {
            return status;
        }

        cursor += length;

        const bool final_component =
            (*cursor == '\0');

        if (!entry.is_directory) {
            return Status::NotDirectory;
        }

        if (!valid_data_cluster(
                entry.first_cluster)) {
            return Status::Corrupt;
        }

        directory_cluster =
            entry.first_cluster;

        if (final_component) {
            return emit_directory(
                directory_cluster,
                visitor,
                context);
        }

        // Skip the slash separating components.
        ++cursor;
    }

    return Status::Unsupported;
}


Status read_file(
    const char* path,
    uint32_t offset,
    uint8_t* buffer,
    size_t buffer_size,
    size_t& bytes_read,
    uint32_t& file_size)
{
    bytes_read = 0;
    file_size = 0;

    if (!mounted) {
        return Status::NotMounted;
    }

    if (path == nullptr ||
        buffer == nullptr) {
        return Status::Unsupported;
    }

    const char* cursor = path;

    if (*cursor == '/') {
        ++cursor;
    }

    if (*cursor == '\0' ||
        !helpers::valid_path(cursor)) {
        return Status::Unsupported;
    }

    uint32_t directory_cluster =
        root_cluster;

    helpers::DirectoryEntry entry;

    while (*cursor != '\0') {
        char component[13];
        uint32_t length = 0;

        while (cursor[length] != '\0' &&
               cursor[length] != '/') {

            if (length >= 12) {
                return Status::Unsupported;
            }

            component[length] =
                cursor[length];

            ++length;
        }

        component[length] = '\0';

        const Status lookup =
            find_entry(
                directory_cluster,
                component,
                entry);

        if (lookup != Status::Ok) {
            return lookup;
        }

        cursor += length;

        const bool final_component =
            (*cursor == '\0');

        if (final_component) {
            break;
        }

        if (!entry.is_directory) {
            return Status::NotDirectory;
        }

        if (!valid_data_cluster(
                entry.first_cluster)) {
            return Status::Corrupt;
        }

        directory_cluster =
            entry.first_cluster;

        ++cursor;
    }

    if (entry.is_directory) {
        return Status::IsDirectory;
    }

    file_size = entry.size;

    if (offset >= file_size ||
        buffer_size == 0) {
        return Status::Ok;
    }

    if (!valid_data_cluster(
            entry.first_cluster)) {
        return Status::Corrupt;
    }

    uint32_t cluster_bytes = 0;

    if (!helpers::checked_mul_u32(
            static_cast<uint32_t>(
                sectors_per_cluster),
            512u,
            cluster_bytes)) {
        return Status::Corrupt;
    }

    const uint32_t bytes_until_eof =
        file_size - offset;

    uint32_t remaining =
        bytes_until_eof;

    if (remaining > buffer_size) {
        remaining =
            static_cast<uint32_t>(
                buffer_size);
    }

    const bool reaches_eof =
        remaining == bytes_until_eof;

    if (remaining == 0) {
        return Status::Ok;
    }

    uint32_t cluster =
        entry.first_cluster;

    uint32_t cluster_offset =
        offset % cluster_bytes;

    uint32_t clusters_to_skip =
        offset / cluster_bytes;

    uint32_t transitions = 0;

    while (clusters_to_skip != 0) {
        if (transitions >= cluster_count ||
            !valid_data_cluster(cluster)) {
            return Status::Corrupt;
        }

        uint32_t next = 0;
        bool eoc = false;

        const Status status =
            next_cluster(
                cluster,
                next,
                eoc);

        if (status != Status::Ok) {
            return status;
        }

        if (eoc) {
            return Status::Corrupt;
        }

        cluster = next;
        --clusters_to_skip;
        ++transitions;
    }

    for (uint32_t walked = transitions;
         walked < cluster_count;
         ++walked) {

        if (!valid_data_cluster(cluster)) {
            return Status::Corrupt;
        }

        uint32_t cluster_lba = 0;

        if (!helpers::cluster_to_lba(
                first_data_lba,
                sectors_per_cluster,
                cluster,
                cluster_lba)) {
            return Status::Corrupt;
        }

        uint32_t sector_index =
            cluster_offset / 512u;

        uint32_t inside_sector =
            cluster_offset % 512u;

        while (sector_index <
                   sectors_per_cluster &&
               remaining != 0) {

            uint32_t lba = 0;

            if (!helpers::checked_add_u32(
                    cluster_lba,
                    sector_index,
                    lba)) {
                return Status::Corrupt;
            }

            uint8_t sector[512];

            if (!storage::read_sector(
                    storage::DiskId::Test,
                    lba,
                    sector)) {
                return Status::IoError;
            }

            uint32_t amount =
                512u - inside_sector;

            if (amount > remaining) {
                amount = remaining;
            }

            for (uint32_t i = 0;
                 i < amount;
                 ++i) {
                buffer[bytes_read + i] =
                    sector[inside_sector + i];
            }

            bytes_read += amount;
            remaining -= amount;

            ++sector_index;
            inside_sector = 0;
        }

        if (remaining == 0) {
            if (!reaches_eof) {
                return Status::Ok;
            }

            uint32_t tail_next = 0;
            bool tail_eoc = false;

            const Status tail_status =
                next_cluster(
                    cluster,
                    tail_next,
                    tail_eoc);

            if (tail_status != Status::Ok) {
                return tail_status;
            }

            if (!tail_eoc) {
                return Status::Corrupt;
            }

            return Status::Ok;
        }

        uint32_t next = 0;
        bool eoc = false;

        const Status status =
            next_cluster(
                cluster,
                next,
                eoc);

        if (status != Status::Ok) {
            return status;
        }

        // The file claims more bytes than the FAT chain contains.
        if (eoc) {
            return Status::Corrupt;
        }

        cluster = next;
        cluster_offset = 0;
    }

    return Status::Corrupt;
}

} // namespace linux95::filesystem::fat32
