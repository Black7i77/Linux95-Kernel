#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "storage/ata.hpp"
#include "storage/ata_helpers.hpp"
#include "storage/disk.hpp"

int main()
{
    using linux95::storage::ata::Drive;
    using namespace linux95::storage::ata::helpers;

    static_assert(valid_lba28(0x00000000U));
    static_assert(valid_lba28(0x0FFFFFFFU));
    static_assert(!valid_lba28(0x10000000U));

    static_assert(!write_allowed(Drive::Master));
    static_assert(write_allowed(Drive::Slave));

    using linux95::storage::DiskId;
    static_assert(!linux95::storage::disk_write_allowed(DiskId::Boot));
    static_assert(linux95::storage::disk_write_allowed(DiskId::Test));
    static_assert(linux95::storage::drive_for(DiskId::Boot) == Drive::Master);
    static_assert(linux95::storage::drive_for(DiskId::Test) == Drive::Slave);

    static_assert(status_is_absent(0x00));
    static_assert(status_is_absent(0xFF));
    static_assert(!status_is_absent(0x40));
    static_assert(status_has_error(0x01));
    static_assert(status_has_error(0x20));
    static_assert(!status_has_error(0x08));


    static_assert(!status_can_accept_command(0x80));
    static_assert(!status_can_accept_command(0xFF));
    static_assert(status_can_accept_command(0x01));
    static_assert(status_can_accept_command(0x40));

    static_assert(status_data_ready(0x08));
    static_assert(!status_data_ready(0x09));
    static_assert(!status_data_ready(0x28));
    static_assert(!status_data_ready(0x88));

    static_assert(status_write_complete(0x40));
    static_assert(!status_write_complete(0x48));
    static_assert(!status_write_complete(0x80));
    static_assert(!status_write_complete(0x01));

    uint16_t identify_words[256] = {};
    const char encoded[] = "QEMU HARDDISK        ";

    for (unsigned i = 0; i < 20; ++i) {
        const uint8_t a = i * 2 < sizeof(encoded) - 1
            ? static_cast<uint8_t>(encoded[i * 2])
            : static_cast<uint8_t>(' ');
        const uint8_t b = i * 2 + 1 < sizeof(encoded) - 1
            ? static_cast<uint8_t>(encoded[i * 2 + 1])
            : static_cast<uint8_t>(' ');
        identify_words[27 + i] =
            static_cast<uint16_t>((a << 8) | b);
    }

    char model[41] = {};
    decode_model(identify_words, model);
    assert(strcmp(model, "QEMU HARDDISK") == 0);

    uint8_t a[512] = {};
    uint8_t b[512] = {};
    fill_test_pattern(a, 0x31);
    fill_test_pattern(b, 0x72);

    assert(memcmp(a, b, 512) != 0);
    for (unsigned i = 0; i < 512; ++i) {
        assert(a[i] == static_cast<uint8_t>(0x31U ^ (i & 0xFFU)));
        assert(b[i] == static_cast<uint8_t>(0x72U ^ (i & 0xFFU)));
    }

    return 0;
}
