#pragma once

#include <stddef.h>
#include <stdint.h>

namespace linux95::filesystem::fat32::helpers {

inline uint16_t le16(const uint8_t* p)
{
    return static_cast<uint16_t>(
        static_cast<uint16_t>(p[0]) |
        (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline bool is_power_of_two(uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

inline uint32_t fat28(uint32_t value)
{
    return value & 0x0FFFFFFFu;
}

inline bool is_eoc(uint32_t value)
{
    const uint32_t v = fat28(value);
    return v >= 0x0FFFFFF8u && v <= 0x0FFFFFFFu;
}

inline bool is_bad_cluster(uint32_t value)
{
    return fat28(value) == 0x0FFFFFF7u;
}

inline bool is_free_cluster(uint32_t value)
{
    return fat28(value) == 0;
}

inline bool is_reserved_cluster(uint32_t value)
{
    const uint32_t v = fat28(value);
    return v >= 0x0FFFFFF0u && v <= 0x0FFFFFF6u;
}

inline bool checked_add_u32(uint32_t a, uint32_t b, uint32_t& out)
{
    if (b > 0xFFFFFFFFu - a) {
        return false;
    }

    out = a + b;
    return true;
}

inline bool checked_mul_u32(uint32_t a, uint32_t b, uint32_t& out)
{
    if (a != 0 && b > 0xFFFFFFFFu / a) {
        return false;
    }

    out = a * b;
    return true;
}

inline bool cluster_to_lba(
    uint32_t first_data_lba,
    uint32_t sectors_per_cluster,
    uint32_t cluster,
    uint32_t& out_lba)
{
    if (cluster < 2 || sectors_per_cluster == 0) {
        return false;
    }

    uint32_t cluster_offset = 0;

    if (!checked_mul_u32(
            cluster - 2,
            sectors_per_cluster,
            cluster_offset)) {
        return false;
    }

    return checked_add_u32(
        first_data_lba,
        cluster_offset,
        out_lba);
}

inline char ascii_upper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return static_cast<char>(c - ('a' - 'A'));
    }

    return c;
}

inline bool ascii_iequals(const char* a, const char* b)
{
    if (a == nullptr || b == nullptr) {
        return false;
    }

    while (*a != '\0' && *b != '\0') {
        if (ascii_upper(*a) != ascii_upper(*b)) {
            return false;
        }

        ++a;
        ++b;
    }

    return *a == '\0' && *b == '\0';
}

inline bool format_short_name(
    const uint8_t raw_name[11],
    char out[13])
{
    if (raw_name == nullptr || out == nullptr) {
        return false;
    }

    size_t pos = 0;

    for (size_t i = 0; i < 8; ++i) {
        uint8_t value = raw_name[i];

        if (i == 0 && value == 0x05) {
            value = 0xE5;
        }

        if (value == ' ') {
            break;
        }

        if (pos >= 12) {
            return false;
        }

        out[pos++] = static_cast<char>(value);
    }

    bool has_extension = false;

    for (size_t i = 8; i < 11; ++i) {
        if (raw_name[i] != ' ') {
            has_extension = true;
            break;
        }
    }

    if (has_extension) {
        if (pos == 0 || pos >= 12) {
            return false;
        }

        out[pos++] = '.';

        for (size_t i = 8; i < 11; ++i) {
            if (raw_name[i] == ' ') {
                break;
            }

            if (pos >= 12) {
                return false;
            }

            out[pos++] = static_cast<char>(raw_name[i]);
        }
    }

    out[pos] = '\0';
    return pos != 0;
}

inline bool valid_path_component(
    const char* text,
    size_t length)
{
    if (text == nullptr || length == 0) {
        return false;
    }

    if ((length == 1 && text[0] == '.') ||
        (length == 2 &&
         text[0] == '.' &&
         text[1] == '.')) {
        return false;
    }

    size_t base_length = 0;
    size_t extension_length = 0;
    bool seen_dot = false;

    for (size_t i = 0; i < length; ++i) {
        const char c = text[i];

        if (c == '\0' ||
            c == '/' ||
            c == '\\' ||
            c == ' ' ||
            static_cast<unsigned char>(c) < 0x21 ||
            static_cast<unsigned char>(c) > 0x7E) {
            return false;
        }

        if (c == '.') {
            if (seen_dot || base_length == 0) {
                return false;
            }

            seen_dot = true;
            continue;
        }

        if (!seen_dot) {
            ++base_length;

            if (base_length > 8) {
                return false;
            }
        } else {
            ++extension_length;

            if (extension_length > 3) {
                return false;
            }
        }
    }

    if (base_length == 0) {
        return false;
    }

    if (seen_dot && extension_length == 0) {
        return false;
    }

    return true;
}


struct BpbGeometry {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t fat_begin_lba;
    uint32_t first_data_lba;
    uint32_t cluster_count;
};

inline bool parse_bpb(
    const uint8_t sector[512],
    uint32_t disk_sectors,
    BpbGeometry& out)
{
    if (sector == nullptr || disk_sectors == 0) {
        return false;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return false;
    }

    const uint16_t bytes_per_sector = le16(sector + 11);
    const uint8_t sectors_per_cluster = sector[13];
    const uint16_t reserved_sectors = le16(sector + 14);
    const uint8_t fat_count = sector[16];

    const uint16_t root_entry_count = le16(sector + 17);
    const uint16_t total_sectors_16 = le16(sector + 19);
    const uint16_t sectors_per_fat_16 = le16(sector + 22);

    const uint32_t total_sectors_32 = le32(sector + 32);
    const uint32_t sectors_per_fat = le32(sector + 36);

    const uint16_t filesystem_version = le16(sector + 42);
    const uint32_t root_cluster = le32(sector + 44);

    if (bytes_per_sector != 512) {
        return false;
    }

    if (!is_power_of_two(sectors_per_cluster) ||
        sectors_per_cluster > 128) {
        return false;
    }

    if (reserved_sectors == 0 || fat_count == 0) {
        return false;
    }

    if (root_entry_count != 0 ||
        sectors_per_fat_16 != 0 ||
        sectors_per_fat == 0) {
        return false;
    }

    if (filesystem_version != 0 || root_cluster < 2) {
        return false;
    }

    const uint32_t total_sectors =
        total_sectors_16 != 0
            ? static_cast<uint32_t>(total_sectors_16)
            : total_sectors_32;

    if (total_sectors == 0 || total_sectors > disk_sectors) {
        return false;
    }

    uint32_t fat_sectors = 0;

    if (!checked_mul_u32(
            static_cast<uint32_t>(fat_count),
            sectors_per_fat,
            fat_sectors)) {
        return false;
    }

    uint32_t first_data_lba = 0;

    if (!checked_add_u32(
            static_cast<uint32_t>(reserved_sectors),
            fat_sectors,
            first_data_lba)) {
        return false;
    }

    if (first_data_lba >= total_sectors) {
        return false;
    }

    const uint32_t data_sectors =
        total_sectors - first_data_lba;

    const uint32_t cluster_count =
        data_sectors /
        static_cast<uint32_t>(sectors_per_cluster);

    // Microsoft FAT classification:
    // FAT32 requires at least 65525 data clusters.
    if (cluster_count < 65525) {
        return false;
    }

    uint32_t max_cluster = 0;

    if (!checked_add_u32(cluster_count, 1, max_cluster)) {
        return false;
    }

    if (root_cluster > max_cluster) {
        return false;
    }

    out.bytes_per_sector = bytes_per_sector;
    out.sectors_per_cluster = sectors_per_cluster;
    out.reserved_sectors = reserved_sectors;
    out.fat_count = fat_count;
    out.total_sectors = total_sectors;
    out.sectors_per_fat = sectors_per_fat;
    out.root_cluster = root_cluster;
    out.fat_begin_lba =
        static_cast<uint32_t>(reserved_sectors);
    out.first_data_lba = first_data_lba;
    out.cluster_count = cluster_count;

    return true;
}

} // namespace linux95::filesystem::fat32::helpers
