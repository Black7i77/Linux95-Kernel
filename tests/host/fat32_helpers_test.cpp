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

    return 0;
}
