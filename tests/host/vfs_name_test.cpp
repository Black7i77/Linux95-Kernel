#include "filesystem/filesystem.hpp"
#include "filesystem/vfs.hpp"
#include "storage/ata.hpp"

#include <assert.h>

namespace linux95::storage::ata {

bool identify(Drive, DeviceInfo& info)
{
    info = DeviceInfo{};
    return false;
}

bool read_sector(Drive, uint32_t, uint8_t*)
{
    return false;
}

bool write_sector(Drive, uint32_t, const uint8_t*)
{
    return false;
}

bool flush_cache(Drive)
{
    return false;
}

} // namespace linux95::storage::ata

namespace {

void expect_valid(const char* name)
{
    assert(linux95::filesystem::validate_name(name) ==
           linux95::filesystem::Status::Ok);
    assert(linux95::filesystem::vfs::validate_name(name) ==
           linux95::filesystem::Status::Ok);
}

void expect_invalid(const char* name)
{
    assert(linux95::filesystem::validate_name(name) ==
           linux95::filesystem::Status::InvalidName);
    assert(linux95::filesystem::vfs::validate_name(name) ==
           linux95::filesystem::Status::InvalidName);
}

} // namespace

int main()
{
    // Syntax validation is independent of mounting and disk mutation.
    assert(!linux95::filesystem::volume_info().mounted);
    expect_valid("README.TXT");
    expect_valid("readme.txt");
    expect_valid("ReAdMe.TxT");
    expect_valid("ABCDEFGH.XYZ");
    expect_valid("ABCDEFGH");

    expect_invalid(nullptr);
    expect_invalid("");
    expect_invalid(".");
    expect_invalid("..");
    expect_invalid(".TXT");
    expect_invalid("NAME.");
    expect_invalid("A..B");
    expect_invalid("ABCDEFGHI.TXT");
    expect_invalid("FILE.TXTX");
    expect_invalid("BAD NAME.TXT");
    expect_invalid("BAD+NAME.TXT");
    expect_invalid("A/B");
    expect_invalid("A\\B");

    return 0;
}
