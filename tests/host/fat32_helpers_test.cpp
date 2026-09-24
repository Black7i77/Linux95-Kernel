#include "filesystem/fat32_helpers.hpp"

#include <assert.h>
#include <stdint.h>
#include <string.h>

using namespace linux95::filesystem::fat32::helpers;

int main()
{
    const uint8_t le[] = {0x34, 0x12, 0x78, 0x56};

    assert(le16(le) == 0x1234u);
    assert(le32(le) == 0x56781234u);

    assert(is_power_of_two(1));
    assert(is_power_of_two(8));
    assert(!is_power_of_two(0));
    assert(!is_power_of_two(3));

    assert(fat28(0xF1234567u) == 0x01234567u);
    assert(is_eoc(0x0FFFFFF8u));
    assert(is_bad_cluster(0x0FFFFFF7u));
    assert(is_free_cluster(0));
    assert(is_reserved_cluster(0x0FFFFFF0u));
    assert(!is_reserved_cluster(0x0FFFFFEFu));

    uint32_t out = 0;

    assert(checked_add_u32(1, 2, out) && out == 3);
    assert(!checked_add_u32(0xFFFFFFFFu, 1, out));

    assert(checked_mul_u32(4, 8, out) && out == 32);
    assert(!checked_mul_u32(0xFFFFFFFFu, 2, out));

    assert(cluster_to_lba(100, 4, 3, out) && out == 104);
    assert(!cluster_to_lba(100, 1, 1, out));

    const uint8_t raw[11] =
        {'R','E','A','D','M','E',' ',' ','T','X','T'};

    char name[13] = {};

    assert(format_short_name(raw, name));
    assert(strcmp(name, "README.TXT") == 0);

    assert(ascii_iequals("readme.txt", "README.TXT"));

    assert(valid_path_component("README.TXT", 10));
    assert(!valid_path_component("ABCDEFGHI.TXT", 13));

    uint8_t bpb[512] = {};

    auto put16 = [&](size_t offset, uint16_t value) {
        bpb[offset] = static_cast<uint8_t>(value & 0xFFu);
        bpb[offset + 1] =
            static_cast<uint8_t>((value >> 8) & 0xFFu);
    };

    auto put32 = [&](size_t offset, uint32_t value) {
        bpb[offset] =
            static_cast<uint8_t>(value & 0xFFu);
        bpb[offset + 1] =
            static_cast<uint8_t>((value >> 8) & 0xFFu);
        bpb[offset + 2] =
            static_cast<uint8_t>((value >> 16) & 0xFFu);
        bpb[offset + 3] =
            static_cast<uint8_t>((value >> 24) & 0xFFu);
    };

    put16(11, 512);
    bpb[13] = 1;
    put16(14, 32);
    bpb[16] = 2;
    put16(17, 0);
    put16(19, 0);
    put16(22, 0);
    put32(32, 131072);
    put32(36, 1010);
    put16(42, 0);
    put32(44, 2);
    bpb[510] = 0x55;
    bpb[511] = 0xAA;

    BpbGeometry geometry = {};

    assert(parse_bpb(bpb, 131072, geometry));
    assert(geometry.bytes_per_sector == 512);
    assert(geometry.sectors_per_cluster == 1);
    assert(geometry.reserved_sectors == 32);
    assert(geometry.fat_count == 2);
    assert(geometry.total_sectors == 131072);
    assert(geometry.sectors_per_fat == 1010);
    assert(geometry.root_cluster == 2);
    assert(geometry.fat_begin_lba == 32);
    assert(geometry.first_data_lba == 2052);
    assert(geometry.cluster_count == 129020);

    bpb[510] = 0;
    assert(!parse_bpb(bpb, 131072, geometry));
    bpb[510] = 0x55;

    put16(11, 1024);
    assert(!parse_bpb(bpb, 131072, geometry));
    put16(11, 512);

    bpb[13] = 0;
    assert(!parse_bpb(bpb, 131072, geometry));
    bpb[13] = 1;

    bpb[13] = 3;
    assert(!parse_bpb(bpb, 131072, geometry));
    bpb[13] = 1;

    put16(14, 0);
    assert(!parse_bpb(bpb, 131072, geometry));
    put16(14, 32);

    bpb[16] = 0;
    assert(!parse_bpb(bpb, 131072, geometry));
    bpb[16] = 2;

    put16(42, 1);
    assert(!parse_bpb(bpb, 131072, geometry));
    put16(42, 0);

    put32(44, 1);
    assert(!parse_bpb(bpb, 131072, geometry));
    put32(44, 2);

    assert(!parse_bpb(bpb, 100000, geometry));

    put32(36, 0xFFFFFFFFu);
    assert(!parse_bpb(bpb, 131072, geometry));
    put32(36, 1010);

    return 0;
}
