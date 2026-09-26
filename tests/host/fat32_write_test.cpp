#include "filesystem/fat32.hpp"
#include "filesystem/fat32_write.hpp"
#include "filesystem/fat32_helpers.hpp"
#include "storage/disk.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace {

uint8_t boot[512], fat[2][512], root[512], second_root[512], third_root[512], file_a[512], file_b[512];
uint32_t writes = 0;
uint32_t fail_lba = 0xffffffffu;
linux95::storage::ata::DeviceInfo device = {true, true, 131072, {}};

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
    memset(boot, 0, sizeof boot);
    memset(fat, 0, sizeof fat);
    memset(root, 0xe5, sizeof root);
    memset(second_root, 0, sizeof second_root);
    memset(third_root, 0, sizeof third_root);
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
    writes = 0; fail_lba = 0xffffffffu;
}
bool visit(const linux95::filesystem::Entry& e, void* p) {
    *static_cast<bool*>(p) = strcmp(e.name, "FILE.TXT") == 0;
    return true;
}
void mounted() {
    linux95::filesystem::VolumeInfo v = {};
    assert(linux95::filesystem::fat32::mount(v));
}
} // namespace

namespace linux95::storage {
bool read_sector(DiskId disk, uint32_t lba, uint8_t* out) {
    if (disk != DiskId::Test || out == nullptr) return false;
    const uint8_t* src = nullptr;
    if (lba == 0) src = boot;
    if (lba == 32) src = fat[0];
    if (lba == 1042) src = fat[1];
    if (lba == 2052) src = root;
    if (lba == 2053) src = second_root;
    if (lba == 2056) src = third_root;
    if (lba == 2054) src = file_a;
    if (lba == 2055) src = file_b;
    if (!src) return false;
    memcpy(out, src, 512); return true;
}
bool write_sector(DiskId disk, uint32_t lba, const uint8_t* in) {
    if (disk != DiskId::Test || in == nullptr || lba == fail_lba) return false;
    uint8_t* dst = lba == 32 ? fat[0] : lba == 1042 ? fat[1] : nullptr;
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
    put32(fat[1], 16, 0xcfffffffu);
    assert(write::write_fat_entry(4, 5) == Status::Ok);
    assert(get32(fat[0], 16) == 0xc0000005u && get32(fat[1], 16) == 0xc0000005u);
    assert(writes == 2);
    assert(write::write_fat_entry(4, 0x0fffffffu) == Status::Ok);

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
    puts("fat32 write tests: PASS");
}
