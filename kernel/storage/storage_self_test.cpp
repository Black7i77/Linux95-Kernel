#include "storage/storage_self_test.hpp"

#include "arch/debug.hpp"
#include "storage/ata_helpers.hpp"
#include "storage/disk.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::storage::self_test {
namespace {

constexpr uint32_t kTestLba = 64;
constexpr uint32_t kMinimumTestSectors = 128;

alignas(2) uint8_t g_original[512];
alignas(2) uint8_t g_pattern[512];
alignas(2) uint8_t g_verify[512];

bool equal_sector(const uint8_t* a, const uint8_t* b)
{
    for (size_t i = 0; i < 512; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

bool restore_original()
{
    if (!storage::write_sector(DiskId::Test, kTestLba, g_original)) {
        debug::write("[FAIL] ata_restore_write\n");
        return false;
    }

    if (!storage::read_sector(DiskId::Test, kTestLba, g_verify)) {
        debug::write("[FAIL] ata_restore_read\n");
        return false;
    }

    if (!equal_sector(g_original, g_verify)) {
        debug::write("[FAIL] ata_restore_compare\n");
        return false;
    }

    debug::write("[PASS] ata_restore\n");
    return true;
}

} // namespace

bool run()
{
    if (!storage::initialize()) {
        debug::write("[FAIL] ata_master_identify\n");
        return false;
    }

    const ata::DeviceInfo& boot = storage::info(DiskId::Boot);
    if (!boot.present || !boot.ata_device) {
        debug::write("[FAIL] ata_master_identify\n");
        return false;
    }
    debug::write("[PASS] ata_master_identify\n");

    const ata::DeviceInfo& test = storage::info(DiskId::Test);
    if (!test.present || !test.ata_device ||
        test.lba28_sector_count <= kMinimumTestSectors) {
        debug::write("[FAIL] ata_slave_identify\n");
        return false;
    }
    debug::write("[PASS] ata_slave_identify\n");

    if (!storage::read_sector(DiskId::Test, kTestLba, g_original)) {
        debug::write("[FAIL] ata_read\n");
        return false;
    }
    debug::write("[PASS] ata_read\n");

    bool destructive_write_happened = false;
    bool test_ok = true;

    ata::helpers::fill_test_pattern(g_pattern, 0x31U);
    if (!storage::write_sector(DiskId::Test, kTestLba, g_pattern)) {
        debug::write("[FAIL] ata_write_pattern_a\n");
        return false;
    }
    destructive_write_happened = true;

    if (!storage::read_sector(DiskId::Test, kTestLba, g_verify) ||
        !equal_sector(g_pattern, g_verify)) {
        debug::write("[FAIL] ata_verify_pattern_a\n");
        test_ok = false;
    }

    if (test_ok) {
        debug::write("[PASS] ata_write\n");
        ata::helpers::fill_test_pattern(g_pattern, 0x72U);
        if (!storage::write_sector(DiskId::Test, kTestLba, g_pattern)) {
            debug::write("[FAIL] ata_write_pattern_b\n");
            test_ok = false;
        }
    }

    if (test_ok &&
        (!storage::read_sector(DiskId::Test, kTestLba, g_verify) ||
         !equal_sector(g_pattern, g_verify))) {
        debug::write("[FAIL] ata_verify_pattern_b\n");
        test_ok = false;
    }

    if (destructive_write_happened && !restore_original()) {
        return false;
    }

    if (!test_ok) return false;

    ata::helpers::fill_test_pattern(g_pattern, 0xA5U);
    if (storage::write_sector(DiskId::Boot, kTestLba, g_pattern)) {
        debug::write("[FAIL] master_write_guard\n");
        return false;
    }
    debug::write("[PASS] master_write_guard\n");

    debug::write("[PASS] storage_self_test\n");
    return true;
}

} // namespace linux95::storage::self_test
