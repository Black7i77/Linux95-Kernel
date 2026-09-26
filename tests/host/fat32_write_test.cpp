#include "filesystem/fat32.hpp"
#include "filesystem/fat32_write.hpp"
#include "filesystem/fat32_helpers.hpp"
#include "storage/disk.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace {

uint8_t boot[512], fat[2][512], other_fat_sector[512], data_sectors[64][512];
uint8_t (&root)[512] = data_sectors[0];
uint8_t (&second_root)[512] = data_sectors[1];
uint8_t (&file_a)[512] = data_sectors[2];
uint8_t (&file_b)[512] = data_sectors[3];
uint8_t (&third_root)[512] = data_sectors[4];
uint32_t writes = 0;
uint32_t write_attempts = 0;
uint32_t fail_lba = 0xffffffffu;
uint32_t fail_read_lba = 0xffffffffu;
struct WriteFailure { uint32_t ordinal; uint32_t lba; };
WriteFailure write_failures[2] = {};
uint32_t write_failure_count = 0;
linux95::storage::ata::DeviceInfo device = {true, true, 131072, {}};

void fail_read_on(uint32_t lba) { fail_read_lba = lba; }
void fail_write_on(uint32_t ordinal, uint32_t lba) {
    assert(write_failure_count < 2);
    write_failures[write_failure_count++] = {ordinal, lba};
}
uint32_t write_attempt_count() { return write_attempts; }

void put16(uint8_t* p, uint32_t n, uint16_t v) {
    p[n] = static_cast<uint8_t>(v);
    p[n + 1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t n, uint32_t v) {
    for (uint32_t i = 0; i < 4; ++i) p[n + i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t get32(const uint8_t* p, uint32_t n) {
    return linux95::filesystem::fat32::helpers::le32(p + n);
}
void reset(uint16_t flags = 0) {
    device.lba28_sector_count = 131072;
    memset(boot, 0, sizeof boot);
    memset(fat, 0, sizeof fat);
    memset(other_fat_sector, 0xff, sizeof other_fat_sector);
    memset(data_sectors, 0, sizeof data_sectors);
    memset(root, 0xe5, sizeof root);
    memset(file_a, 'A', sizeof file_a);
    memset(file_b, 'B', sizeof file_b);
    put16(boot, 11, 512); boot[13] = 1; put16(boot, 14, 32);
    boot[16] = 2; put32(boot, 32, 131072); put32(boot, 36, 1010);
    put16(boot, 40, flags); put32(boot, 44, 2);
    boot[510] = 0x55; boot[511] = 0xaa;
    for (int i = 0; i < 2; ++i) {
        put32(fat[i], 8, 0xafffffff);
        put32(fat[i], 12, 0xbfffffff);
        put32(fat[i], 16, 0xcfffffff);
        put32(fat[i], 20, 0xdfffffff);
    }
    const uint8_t name[11] = {'D','O','C','S',' ',' ',' ',' ',' ',' ',' '};
    memcpy(root, name, 11); root[11] = 0x10; root[20] = 0; root[21] = 0; root[26] = 3; root[27] = 0;
    const uint8_t file[11] = {'F','I','L','E',' ',' ',' ',' ','T','X','T'};
    memcpy(second_root, file, 11); second_root[11] = 0x20;
    second_root[26] = 4; put32(second_root, 28, 1024);
    second_root[32] = 0;
    writes = 0; write_attempts = 0;
    fail_lba = 0xffffffffu; fail_read_lba = 0xffffffffu;
    write_failure_count = 0;
}
bool visit(const linux95::filesystem::Entry& e, void* p) {
    *static_cast<bool*>(p) = strcmp(e.name, "FILE.TXT") == 0;
    return true;
}
void mounted() {
    linux95::filesystem::VolumeInfo v = {};
    assert(linux95::filesystem::fat32::mount(v));
}
void fill_root() {
    for (uint32_t i = 0; i < 16; ++i) {
        memset(root + i * 32, 0, 32);
        memset(root + i * 32, ' ', 11);
        root[i * 32] = 'A' + static_cast<uint8_t>(i);
        root[i * 32 + 11] = 0x20;
    }
}
void check_invalid_path(const char* path) {
    linux95::filesystem::fat32::write::ResolvedPath resolved = {};
    const uint32_t before = write_attempts;
    assert(linux95::filesystem::fat32::write::resolve_path(path, resolved) ==
           linux95::filesystem::Status::InvalidName);
    assert(write_attempts == before);
}
void seed_file_chain() {
    put32(fat[0], 16, 5); put32(fat[1], 16, 5);
}
void check_file(const char* path, const uint8_t* expected, size_t length) {
    uint8_t actual[2048] = {};
    size_t read = 0;
    uint32_t size = 0;
    assert(length <= sizeof actual);
    assert(linux95::filesystem::fat32::read_file(path, 0, actual,
        sizeof actual, read, size) == linux95::filesystem::Status::Ok);
    assert(size == length && read == length);
    assert(memcmp(actual, expected, length) == 0);
}
void check_old_file() {
    uint8_t expected[1024];
    memset(expected, 'A', 512); memset(expected + 512, 'B', 512);
    check_file("DOCS/FILE.TXT", expected, sizeof expected);
}
} // namespace

namespace linux95::storage {
bool read_sector(DiskId disk, uint32_t lba, uint8_t* out) {
    if (disk != DiskId::Test || out == nullptr || lba == fail_read_lba) return false;
    const uint8_t* src = nullptr;
    if (lba == 0) src = boot;
    if (lba == 32) src = fat[0];
    if (lba == 1042) src = fat[1];
    if ((lba > 32 && lba < 1042) || (lba > 1042 && lba < 2052)) src = other_fat_sector;
    if (lba >= 2052 && lba < 2052 + 64) src = data_sectors[lba - 2052];
    if (!src) return false;
    memcpy(out, src, 512); return true;
}
bool write_sector(DiskId disk, uint32_t lba, const uint8_t* in) {
    if (disk != DiskId::Test || in == nullptr) return false;
    ++write_attempts;
    if (lba == fail_lba) return false;
    for (uint32_t i = 0; i < write_failure_count; ++i)
        if (write_attempts == write_failures[i].ordinal && lba == write_failures[i].lba)
            return false;
    uint8_t* dst = lba == 32 ? fat[0] : lba == 1042 ? fat[1] :
        lba >= 2052 && lba < 2052 + 64 ? data_sectors[lba - 2052] : nullptr;
    if (!dst) return false;
    memcpy(dst, in, 512); ++writes; return true;
}
const ata::DeviceInfo& info(DiskId) { return device; }
} // namespace linux95::storage

int main() {
    using namespace linux95;
    using namespace filesystem;
    using namespace filesystem::fat32;

    reset();
    put32(boot, 32, 0x10000000u);
    put32(boot, 36, 2100000u);
    helpers::BpbGeometry oversized = {};
    assert(!helpers::parse_bpb(boot, 0x0fffffffu, oversized));

    reset(); mounted();
    helpers::BpbGeometry g = {};
    assert(mounted_geometry(g) && g.fat_count == 2 && g.sectors_per_fat == 1010);
    assert(g.cluster_count == 129020 && g.first_data_lba == 2052);
    assert(g.mirroring_enabled && g.active_fat == 0);
    uint32_t value = 0;
    assert(write::read_fat_entry(2, value) == Status::Ok && value == 0x0fffffffu);
    put32(fat[0], 16, 5);
    put32(fat[1], 16, 0x0ffffff7u);
    uint8_t mirrored_data[1024] = {};
    size_t mirrored_read = 0; uint32_t mirrored_size = 0;
    assert(fat32::read_file("DOCS/FILE.TXT", 0, mirrored_data,
        sizeof mirrored_data, mirrored_read, mirrored_size) == Status::Ok);
    assert(mirrored_read == 1024 && mirrored_data[512] == 'B');
    put32(fat[0], 16, 0xcfffffffu);
    put32(fat[1], 16, 0xdfffffffu);
    assert(write::write_fat_entry(4, 5) == Status::Ok);
    assert(get32(fat[0], 16) == 0xc0000005u && get32(fat[1], 16) == 0xd0000005u);
    assert(writes == 2);
    assert(write::write_fat_entry(4, 0x0fffffffu) == Status::Ok);
    assert(get32(fat[0], 16) == 0xcfffffffu && get32(fat[1], 16) == 0xdfffffffu);

    reset(0x0081); mounted();
    assert(mounted_geometry(g) && !g.mirroring_enabled && g.active_fat == 1);
    put32(fat[0], 8, 0x0ffffff7u);
    put32(fat[0], 12, 0x0ffffff7u);
    put32(fat[0], 16, 0x0ffffff7u);
    put32(fat[1], 8, 3); // root continues in cluster 3
    put32(fat[1], 12, 6);
    put32(fat[1], 24, 0x0fffffffu);
    put32(fat[1], 16, 5); // file cluster 4 continues in cluster 5
    memset(second_root, 0xe5, sizeof second_root);
    memcpy(third_root, "FILE    TXT", 11);
    third_root[11] = 0x20;
    third_root[26] = 4;
    put32(third_root, 28, 1024);
    third_root[32] = 0;
    bool found = false;
    assert(fat32::list_directory("/DOCS", visit, &found) == Status::Ok && found);
    uint8_t data[1024] = {};
    size_t read = 0; uint32_t size = 0;
    assert(fat32::read_file("DOCS/FILE.TXT", 0, data, sizeof data, read, size) == Status::Ok);
    assert(read == 1024 && data[0] == 'A' && data[512] == 'B');
    assert(write::write_fat_entry(4, 0) == Status::Unsupported && writes == 0);
    assert(write::allocate_chain(1, value) == Status::Unsupported && writes == 0);
    assert(write::free_chain(4) == Status::Unsupported && writes == 0);

    reset(0x0082); VolumeInfo invalid = {};
    assert(!mount(invalid) && !invalid.mounted);
    assert(!mounted_geometry(g));
    reset(); mounted();
    uint32_t count = 99;
    assert(write::file_cluster_count(0, 512, count) == Status::Ok && count == 0);
    assert(write::file_cluster_count(513, 512, count) == Status::Ok && count == 2);
    assert(write::file_cluster_count(1, 0, count) == Status::Corrupt);
    assert(write::file_cluster_count(0x100000000ull, 512, count) == Status::Unsupported);
    assert(write::allocate_chain(2, value) == Status::Ok && value == 6);
    assert(get32(fat[0], 24) == 7 && get32(fat[1], 24) == 7);
    assert((get32(fat[0], 28) & 0x0fffffffu) == 0x0fffffffu);
    assert(write::free_chain(6) == Status::Ok);
    assert((get32(fat[0], 24) & 0x0fffffffu) == 0);
    assert((get32(fat[1], 28) & 0x0fffffffu) == 0);
    assert(write::read_fat_entry(129022, value) == Status::Corrupt);
    assert(write::write_fat_entry(129022, 0) == Status::Corrupt);
    put32(fat[0], 24, 0x0ffffff7u);
    assert(write::free_chain(6) == Status::Corrupt);
    put32(fat[0], 24, 129022);
    assert(write::free_chain(6) == Status::Corrupt);
    put32(fat[0], 24, 7); put32(fat[0], 28, 6);
    assert(write::free_chain(6) == Status::Corrupt);
    assert(get32(fat[0], 24) == 7 && get32(fat[0], 28) == 6);
    put32(fat[0], 24, 0); put32(fat[0], 28, 0);
    fail_lba = 1042; writes = 0;
    value = 123;
    assert(write::allocate_chain(1, value) == Status::IoError && value == 0);
    assert((get32(fat[0], 24) & 0x0fffffffu) == 0 && writes == 2);

    reset(); mounted();
    put32(fat[0], 24, 0x10000000u); put32(fat[1], 24, 0x20000000u);
    put32(fat[0], 28, 0x30000000u); put32(fat[1], 28, 0x40000000u);
    fail_write_on(6, 1042); // second copy of the later 6 -> 7 link
    value = 123;
    assert(write::allocate_chain(2, value) == Status::IoError);
    assert(value == 0);
    assert(get32(fat[0], 24) == 0x10000000u && get32(fat[1], 24) == 0x20000000u);
    assert(get32(fat[0], 28) == 0x30000000u && get32(fat[1], 28) == 0x40000000u);
    assert(write_attempt_count() == 11);

    reset(); mounted();
    put32(fat[0], 16, 0xafffffffu); put32(fat[1], 16, 0xbfffffffu);
    fail_write_on(2, 1042); // second-copy write
    fail_write_on(3, 32);   // rollback of the earlier successful write
    assert(write::write_fat_entry(4, 5) == Status::IoError);
    assert(get32(fat[0], 16) == 0xa0000005u && get32(fat[1], 16) == 0xbfffffffu);
    assert(write_attempt_count() == 3 && writes == 1);

    reset(); mounted();
    put32(fat[0], 16, 0xafffffffu); put32(fat[1], 16, 0xbfffffffu);
    fail_read_on(1042);
    assert(write::write_fat_entry(4, 5) == Status::IoError);
    assert(get32(fat[0], 16) == 0xafffffffu && get32(fat[1], 16) == 0xbfffffffu);
    assert(write_attempt_count() == 0 && writes == 0);

    // A wrong punctuation whitelist, case fold, or padding breaks these literals.
    uint8_t short_name[11] = {};
    assert(helpers::encode_short_name("mIx.Ed", 6, short_name));
    assert(memcmp(short_name, "MIX     ED ", 11) == 0);
    assert(helpers::encode_short_name("AbC12345.xYz", 12, short_name));
    assert(memcmp(short_name, "ABC12345XYZ", 11) == 0);
    assert(helpers::encode_short_name("$%'_-@~!.`()", 12, short_name));
    assert(memcmp(short_name, "$%'_-@~!`()", 11) == 0);
    assert(helpers::encode_short_name("{}^#&", 5, short_name));
    assert(memcmp(short_name, "{}^#&      ", 11) == 0);
    const char* invalid_names[] = {
        "", ".X", "X.", "X..Y", "ABCDEFGHI", "A.ABCD", ".", "..",
        "A B", "A\tB", "A\x7f", "A\x80", "A\x01",
        "A\"B", "A*B", "A+B", "A,B", "A/B", "A:B", "A;B", "A<B",
        "A=B", "A>B", "A?B", "A[B", "A\\B", "A]B", "A|B", "A\x7e\x7f"
    };
    for (const char* name : invalid_names) {
        assert(!helpers::encode_short_name(name, strlen(name), short_name));
    }
    assert(helpers::valid_path_component("A+B", 3)); // legacy read parser stays compatible
    assert(helpers::encode_short_name("A~B", 3, short_name));
    assert(!helpers::encode_short_name("A\\B", 3, short_name));
    assert(!helpers::encode_short_name("A B", 3, short_name));

    reset(); mounted();
    write::ResolvedPath path = {};
    assert(write::resolve_path("/dOcS/fIlE.TxT", path) == Status::Ok);
    assert(path.exists && path.parent_cluster == 3);
    assert(memcmp(path.name, "FILE    TXT", 11) == 0);
    assert(write::resolve_path("DOCS/new.txt", path) == Status::Ok);
    assert(!path.exists && path.parent_cluster == 3);
    assert(write::resolve_path("/missing/new.txt", path) == Status::NotFound);
    assert(write::resolve_path("DOCS/FILE.TXT/CHILD", path) == Status::NotDirectory);
    fail_read_on(2052);
    assert(write::resolve_path("DOCS/FILE.TXT", path) == Status::IoError);
    fail_read_on(0xffffffffu);
    root[26] = 0; root[27] = 0;
    assert(write::resolve_path("DOCS/FILE.TXT", path) == Status::Corrupt);
    root[26] = 3;
    check_invalid_path(""); check_invalid_path("/");
    check_invalid_path("/DOCS//X"); check_invalid_path("DOCS/");
    check_invalid_path("//DOCS/X"); check_invalid_path("DOCS/./X");
    check_invalid_path("DOCS/../X"); check_invalid_path("A?B");
    check_invalid_path("MISSING/A?B");
    char long_path[130];
    for (uint32_t i = 0; i < 126; i += 2) { long_path[i] = 'A'; long_path[i + 1] = '/'; }
    long_path[126] = 'A'; long_path[127] = 'A'; long_path[128] = 0;
    check_invalid_path(long_path);
    long_path[127] = 0;
    assert(write::resolve_path(long_path, path) == Status::NotFound);
    assert(writes == 0);

    uint8_t prototype[32] = {};
    prototype[11] = 0x20;
    prototype[12] = 0x5a; // unrelated metadata must survive updates
    prototype[21] = 0xa0; // reserved high cluster bits are not owned by the updater
    prototype[24] = 0x77;
    write::DirectorySlot slot = {};
    assert(write::create_directory_entry(3, "BAD+NAME", prototype, slot) ==
           Status::InvalidName && write_attempt_count() == 0);
    assert(write::create_directory_entry(3, "mIx.Ed", prototype, slot) == Status::Ok);
    uint8_t raw[32] = {};
    assert(write::find_directory_entry(3, "MIX.ED", slot, raw) == Status::Ok);
    assert(memcmp(raw, "MIX     ED ", 11) == 0 && raw[12] == 0x5a);
    uint8_t reread[32] = {};
    assert(write::read_directory_entry(slot, reread) == Status::Ok);
    assert(memcmp(raw, reread, 32) == 0);
    uint8_t new_name[11] = {};
    assert(helpers::encode_short_name("Renamed.Txt", 11, new_name));
    assert(write::update_directory_entry(slot, new_name, 7, 1234) == Status::Ok);
    assert(write::find_directory_entry(3, "renamed.txt", slot, raw) == Status::Ok);
    assert(raw[11] == 0x20 && raw[12] == 0x5a && raw[21] == 0xa0 &&
           raw[24] == 0x77 && raw[26] == 7 && get32(raw, 28) == 1234);
    assert(write::delete_directory_entry(slot) == Status::Ok);
    assert(write::read_directory_entry(slot, raw) == Status::Ok);
    assert(raw[0] == 0xe5 && raw[12] == 0x5a && raw[24] == 0x77);
    assert(write::find_directory_entry(3, "RENAMED.TXT", slot, raw) == Status::NotFound);

    reset(); mounted(); fill_root();
    memcpy(third_root, "STALE   TXT", 11); third_root[11] = 0x20;
    assert(write::create_directory_entry(2, "NEW.TXT", prototype, slot) == Status::Ok);
    assert((get32(fat[0], 8) & 0x0fffffffu) == 6);
    assert((get32(fat[1], 8) & 0x0fffffffu) == 6);
    assert((get32(fat[0], 24) & 0x0fffffffu) == 0x0fffffffu);
    assert(memcmp(third_root, "NEW     TXT", 11) == 0);
    assert(third_root[32] == 0 && third_root[512 - 1] == 0);
    assert(write::find_directory_entry(2, "STALE.TXT", slot, raw) == Status::NotFound);
    assert(write::find_directory_entry(2, "new.txt", slot, raw) == Status::Ok);
    assert(write::create_directory_entry(2, "SECOND.TXT", prototype, slot) == Status::Ok);
    assert(write::find_directory_entry(2, "second.txt", slot, raw) == Status::Ok);
    assert((get32(fat[0], 8) & 0x0fffffffu) == 6);

    // A two-sector cluster must be fully cleared before its link is exposed.
    reset(); boot[13] = 2; put32(boot, 32, 140000);
    device.lba28_sector_count = 140000; mounted(); fill_root();
    for (uint32_t i = 0; i < 16; ++i) {
        memset(second_root + i * 32, ' ', 11);
        second_root[i * 32] = 'Q';
        second_root[i * 32 + 1] = 'A' + static_cast<uint8_t>(i);
        second_root[i * 32 + 11] = 0x20;
    }
    memset(data_sectors[8], 0xa5, 512);
    memset(data_sectors[9], 0xa5, 512);
    assert(write::create_directory_entry(2, "WIDE.TXT", prototype, slot) == Status::Ok);
    assert((get32(fat[0], 8) & 0x0fffffffu) == 6);
    assert(memcmp(data_sectors[8], "WIDE    TXT", 11) == 0);
    assert(data_sectors[8][32] == 0 && data_sectors[9][0] == 0 &&
           data_sectors[9][511] == 0);
    assert(write::find_directory_entry(2, "WIDE.TXT", slot, raw) == Status::Ok);

    reset(); boot[13] = 2; put32(boot, 32, 140000);
    device.lba28_sector_count = 140000; mounted(); fill_root();
    for (uint32_t i = 0; i < 16; ++i) {
        memset(second_root + i * 32, ' ', 11);
        second_root[i * 32] = 'Q';
        second_root[i * 32 + 1] = 'A' + static_cast<uint8_t>(i);
        second_root[i * 32 + 11] = 0x20;
    }
    fail_write_on(4, 2061); // second sector clear; link must not happen
    assert(write::create_directory_entry(2, "WIDE.TXT", prototype, slot) == Status::IoError);
    assert((get32(fat[0], 8) & 0x0fffffffu) == 0x0fffffffu);
    assert((get32(fat[0], 24) & 0x0fffffffu) == 0);

    // Failure at each publication stage leaves no stale or early entry.
    for (uint32_t stage = 0; stage < 4; ++stage) {
        reset(); mounted(); fill_root();
        memset(third_root, 0xa5, sizeof third_root);
        const uint32_t ordinal[] = {3, 4, 6, 8};
        const uint32_t lba[] = {2056, 32, 32, 2056};
        fail_write_on(ordinal[stage], lba[stage]);
        assert(write::create_directory_entry(2, "NEW.TXT", prototype, slot) == Status::IoError);
        assert(write::find_directory_entry(2, "NEW.TXT", slot, raw) == Status::NotFound);
        assert((get32(fat[0], 8) & 0x0fffffffu) == 0x0fffffffu);
        assert((get32(fat[0], 24) & 0x0fffffffu) == 0);
        assert((get32(fat[1], 24) & 0x0fffffffu) == 0);
        assert(write_attempt_count() < 20);
    }
    reset(); mounted(); fill_root();
    fail_write_on(8, 2056); fail_write_on(9, 32); // publication, then unlink rollback
    assert(write::create_directory_entry(2, "NEW.TXT", prototype, slot) == Status::IoError);
    assert(write_attempt_count() < 20);
    assert((get32(fat[0], 8) & 0x0fffffffu) == 6); // keep linked cluster when unlink fails

    reset(); mounted(); fill_root();
    for (uint32_t cluster = 6; cluster < 128; ++cluster) {
        put32(fat[0], cluster * 4, 0x0fffffffu);
        put32(fat[1], cluster * 4, 0x0fffffffu);
    }
    assert(write::create_directory_entry(2, "NEW.TXT", prototype, slot) == Status::NoSpace);
    assert(write_attempt_count() == 0);
    assert((get32(fat[0], 8) & 0x0fffffffu) == 0x0fffffffu);

    reset(); mounted(); fill_root();
    put32(fat[0], 8, 129022); put32(fat[1], 8, 129022);
    assert(write::find_directory_entry(2, "LOST.TXT", slot, raw) == Status::Corrupt);
    assert(write_attempt_count() == 0);
    reset(); mounted(); fill_root();
    put32(fat[0], 8, 2); put32(fat[1], 8, 2);
    assert(write::find_directory_entry(2, "LOST.TXT", slot, raw) == Status::Corrupt);
    assert(write_attempt_count() == 0);

    // Replacing an end marker must leave the following stale short entry hidden.
    reset(); mounted();
    root[0] = 0;
    memcpy(root + 32, "STALE   TXT", 11); root[32 + 11] = 0x20;
    assert(write::find_directory_entry(2, "STALE.TXT", slot, raw) == Status::NotFound);
    assert(write::create_directory_entry(2, "NEW.TXT", prototype, slot) == Status::Ok);
    assert(root[32] == 0);
    assert(write::find_directory_entry(2, "STALE.TXT", slot, raw) == Status::NotFound);
    assert(write::find_directory_entry(2, "NEW.TXT", slot, raw) == Status::Ok);

    // The replacement terminator can be in the next sector of one cluster.
    reset(); boot[13] = 2; put32(boot, 32, 140000);
    device.lba28_sector_count = 140000; mounted(); fill_root();
    root[15 * 32] = 0;
    memcpy(second_root, "STALE   TXT", 11); second_root[11] = 0x20;
    assert(write::create_directory_entry(2, "NEW.TXT", prototype, slot) == Status::Ok);
    assert(second_root[0] == 0);
    assert(write::find_directory_entry(2, "STALE.TXT", slot, raw) == Status::NotFound);

    // The next cluster may already be linked, or need to be grown first.
    reset(); mounted(); fill_root(); root[15 * 32] = 0;
    put32(fat[0], 8, 6); put32(fat[1], 8, 6);
    put32(fat[0], 24, 0x0fffffffu); put32(fat[1], 24, 0x0fffffffu);
    memcpy(third_root, "STALE   TXT", 11); third_root[11] = 0x20;
    assert(write::create_directory_entry(2, "NEW.TXT", prototype, slot) == Status::Ok);
    assert(third_root[0] == 0);
    assert(write::find_directory_entry(2, "STALE.TXT", slot, raw) == Status::NotFound);

    reset(); mounted(); fill_root(); root[15 * 32] = 0;
    memset(third_root, 0xa5, sizeof third_root);
    assert(write::create_directory_entry(2, "NEW.TXT", prototype, slot) == Status::Ok);
    assert((get32(fat[0], 8) & 0x0fffffffu) == 6);
    assert((get32(fat[0], 24) & 0x0fffffffu) == 0x0fffffffu);
    assert(third_root[0] == 0);
    assert(write::find_directory_entry(2, "NEW.TXT", slot, raw) == Status::Ok);

    // An in-range but unallocated directory cluster is still corrupt.
    const uint32_t invalid_directory_fat[] = {0u, 0x0ffffff7u};
    for (uint32_t invalid : invalid_directory_fat) {
        reset(); mounted();
        put32(fat[0], 8, invalid); put32(fat[1], 8, invalid);
        assert(write::find_directory_entry(2, "DOCS", slot, raw) == Status::Corrupt);
        assert(write::resolve_path("DOCS/FILE.TXT", path) == Status::Corrupt);
        assert(write::create_directory_entry(2, "NEW.TXT", prototype, slot) == Status::Corrupt);
        assert(write_attempt_count() == 0);

        reset(); mounted();
        put32(fat[0], 12, invalid); put32(fat[1], 12, invalid);
        assert(write::resolve_path("DOCS/FILE.TXT", path) == Status::Corrupt);
        assert(write::create_directory_entry(3, "NEW.TXT", prototype, slot) == Status::Corrupt);
        assert(write_attempt_count() == 0);
    }

    // Touch creates an empty file and does not change an existing file at all.
    reset(); mounted(); seed_file_chain();
    second_root[12] = 0x5a; second_root[24] = 0x77;
    uint8_t before_entry[32]; memcpy(before_entry, second_root, 32);
    assert(write::touch("/docs/new.txt") == Status::Ok);
    assert(memcmp(second_root + 32, "NEW     TXT", 11) == 0);
    assert(second_root[32 + 11] == 0x20 && get32(second_root + 32, 28) == 0);
    assert(second_root[32 + 20] == 0 && second_root[32 + 26] == 0);
    check_file("DOCS/NEW.TXT", nullptr, 0);
    const uint32_t after_create = write_attempt_count();
    assert(write::touch("docs/file.txt") == Status::Ok);
    assert(write_attempt_count() == after_create);
    assert(memcmp(before_entry, second_root, 32) == 0);
    check_old_file();
    assert(write::touch("DOCS") == Status::IsDirectory);
    assert(write::write_file("DOCS", nullptr, 0) == Status::IsDirectory);
    assert(write::touch("MISSING/X.TXT") == Status::NotFound);
    assert(write::touch("DOCS/FILE.TXT/X") == Status::NotDirectory);

    // The byte API accepts NUL/non-text bytes and streams across clusters.
    reset(); mounted(); seed_file_chain();
    uint8_t bytes[1537];
    for (size_t i = 0; i < sizeof bytes; ++i)
        bytes[i] = static_cast<uint8_t>((i * 37u) & 0xffu);
    assert(write::write_file("DOCS/FILE.TXT", bytes, sizeof bytes) == Status::Ok);
    check_file("DOCS/FILE.TXT", bytes, sizeof bytes);
    assert(get32(second_root, 28) == sizeof bytes && second_root[26] == 6);
    assert((get32(fat[0], 24) & 0x0fffffffu) == 7);
    assert((get32(fat[1], 36) & 0x0fffffffu) == 0x0fffffffu);
    assert((get32(fat[0], 16) & 0x0fffffffu) == 0);
    assert((get32(fat[1], 20) & 0x0fffffffu) == 0);
    assert(write::write_file("DOCS/FILE.TXT", bytes, 17) == Status::Ok);
    check_file("DOCS/FILE.TXT", bytes, 17);
    assert(write::write_file("DOCS/FILE.TXT", nullptr, 0) == Status::Ok);
    check_file("DOCS/FILE.TXT", nullptr, 0);
    assert(second_root[26] == 0 && get32(second_root, 28) == 0);
    assert(write::write_file("DOCS/FILE.TXT", bytes, 1025) == Status::Ok);
    check_file("DOCS/FILE.TXT", bytes, 1025);

    const size_t boundary_lengths[] = {511, 512, 513};
    for (size_t length : boundary_lengths) {
        reset(); mounted();
        assert(write::write_file("DOCS/NEW.TXT", bytes, length) == Status::Ok);
        check_file("DOCS/NEW.TXT", bytes, length);
        assert(get32(second_root + 32, 28) == length);
        assert((get32(fat[0], 24) & 0x0fffffffu) ==
            (length == 513 ? 7u : 0x0fffffffu));
    }

    // Reject impossible sizes before even looking at a sentinel data pointer.
    reset(); mounted(); seed_file_chain();
    const uint8_t* unreadable = reinterpret_cast<const uint8_t*>(uintptr_t(1));
    assert(write::write_file("DOCS/FILE.TXT", unreadable,
        size_t(UINT32_MAX) + 1u) == Status::Unsupported);
    assert(write_attempt_count() == 0);
    check_old_file();
    assert(write::write_file("DOCS/FILE.TXT", nullptr, 1) != Status::Ok);
    assert(write_attempt_count() == 0);
    assert(write::write_file("DOCS/BAD?.TXT", bytes, 1) == Status::InvalidName);
    assert(write_attempt_count() == 0);
    reset(0x0081); mounted();
    assert(write::touch("DOCS/NEW.TXT") == Status::Unsupported);
    assert(write::write_file("DOCS/NEW.TXT", bytes, 1) == Status::Unsupported);
    assert(write_attempt_count() == 0);

    // Every prepublication failure retains the original entry and bytes.
    reset(); mounted(); seed_file_chain();
    for (uint32_t cluster = 6; cluster < 128; ++cluster) {
        put32(fat[0], cluster * 4, 0x0fffffffu);
        put32(fat[1], cluster * 4, 0x0fffffffu);
    }
    assert(write::write_file("DOCS/FILE.TXT", bytes, 513) == Status::NoSpace);
    assert(write_attempt_count() == 0);
    check_old_file();

    reset(); mounted(); seed_file_chain();
    fail_write_on(2, 1042); // secondary FAT copy during allocation
    assert(write::write_file("DOCS/FILE.TXT", bytes, 17) == Status::IoError);
    assert(second_root[26] == 4 && get32(second_root, 28) == 1024);
    check_old_file();

    reset(); mounted(); seed_file_chain();
    fail_write_on(6, 1042); // secondary copy while linking a two-cluster chain
    assert(write::write_file("DOCS/FILE.TXT", bytes, 513) == Status::IoError);
    assert(second_root[26] == 4 && get32(second_root, 28) == 1024);
    check_old_file();

    reset(); mounted(); seed_file_chain();
    fail_lba = 2056; // new data sector
    assert(write::write_file("DOCS/FILE.TXT", bytes, 17) == Status::IoError);
    assert(second_root[26] == 4 && get32(second_root, 28) == 1024);
    check_old_file();
    assert((get32(fat[0], 24) & 0x0fffffffu) == 0);

    reset(); mounted(); seed_file_chain();
    fail_write_on(4, 2053); // directory publication
    assert(write::write_file("DOCS/FILE.TXT", bytes, 17) == Status::IoError);
    assert(second_root[26] == 4 && get32(second_root, 28) == 1024);
    check_old_file();
    assert((get32(fat[0], 24) & 0x0fffffffu) == 0);

    reset(); mounted(); seed_file_chain();
    fail_write_on(3, 2056); // data write, then new-chain cleanup fails
    fail_write_on(4, 32);
    assert(write::write_file("DOCS/FILE.TXT", bytes, 17) == Status::IoError);
    assert(second_root[26] == 4 && get32(second_root, 28) == 1024);
    check_old_file();

    reset(); mounted(); seed_file_chain();
    fail_write_on(6, 1042); // old-chain free after new entry is published
    assert(write::write_file("DOCS/FILE.TXT", bytes, 17) == Status::IoError);
    check_file("DOCS/FILE.TXT", bytes, 17);
    assert(second_root[26] == 6 && get32(second_root, 28) == 17);
    assert((get32(fat[0], 24) & 0x0fffffffu) == 0x0fffffffu);

    puts("fat32 write tests: PASS");
}
