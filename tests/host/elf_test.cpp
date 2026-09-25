#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "user/elf.hpp"

namespace {

constexpr uint64_t kImageBase = 0x0000400000000000ULL;
constexpr size_t kHeaderSize = 64;
constexpr size_t kProgramHeaderSize = 56;
constexpr size_t kProgramHeaderOffset = 64;

void put16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

void put32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

void put64(std::vector<uint8_t>& data, size_t offset, uint64_t value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::vector<uint8_t> fixture(size_t program_headers = 1) {
    std::vector<uint8_t> data(0x400, 0);
    data[0] = 0x7F;
    data[1] = 'E';
    data[2] = 'L';
    data[3] = 'F';
    data[4] = 2;
    data[5] = 1;
    put16(data, 16, 2);
    put16(data, 18, 62);
    put32(data, 20, 1);
    put64(data, 24, kImageBase);
    put64(data, 32, kProgramHeaderOffset);
    put16(data, 52, kHeaderSize);
    put16(data, 54, kProgramHeaderSize);
    put16(data, 56, static_cast<uint16_t>(program_headers));

    for (size_t i = 0; i < program_headers; ++i) {
        const size_t offset = kProgramHeaderOffset + i * kProgramHeaderSize;
        put32(data, offset, 1);
        put32(data, offset + 4, 5);
        put64(data, offset + 8, 0x200 + i * 0x20);
        put64(data, offset + 16, kImageBase + i * 0x800);
        put64(data, offset + 32, 0x100);
        put64(data, offset + 40, 0x1000);
        put64(data, offset + 48, 0x1000);
    }
    return data;
}

void set_first_phdr_sizes(std::vector<uint8_t>& data,
                          uint64_t filesz,
                          uint64_t memsz) {
    put64(data, kProgramHeaderOffset + 32, filesz);
    put64(data, kProgramHeaderOffset + 40, memsz);
}

void set_first_phdr_file_range(std::vector<uint8_t>& data,
                               uint64_t offset,
                               uint64_t size) {
    put64(data, kProgramHeaderOffset + 8, offset);
    put64(data, kProgramHeaderOffset + 32, size);
}

} // namespace

int main() {
    using namespace linux95::user;

    const auto valid = fixture();
    ElfImageInfo info{};
    assert(inspect_elf64(valid.data(), valid.size(), info) == ElfStatus::Ok);
    assert(info.entry == kImageBase);

    auto bad_magic = valid;
    bad_magic[0] = 0;
    assert(inspect_elf64(bad_magic.data(), bad_magic.size(), info) ==
           ElfStatus::BadMagic);

    auto truncated = valid;
    truncated.resize(kHeaderSize - 1);
    assert(inspect_elf64(truncated.data(), truncated.size(), info) ==
           ElfStatus::Truncated);

    auto filesz_gt_memsz = valid;
    set_first_phdr_sizes(filesz_gt_memsz, 0x2000, 0x1000);
    assert(inspect_elf64(filesz_gt_memsz.data(), filesz_gt_memsz.size(), info) ==
           ElfStatus::InvalidSegment);

    auto file_overflow = valid;
    set_first_phdr_file_range(file_overflow, UINT64_MAX - 8, 32);
    assert(inspect_elf64(file_overflow.data(), file_overflow.size(), info) ==
           ElfStatus::RangeOverflow);

    auto virtual_overflow = valid;
    put64(virtual_overflow, kProgramHeaderOffset + 16, UINT64_MAX - 8);
    put64(virtual_overflow, kProgramHeaderOffset + 32, 0);
    put64(virtual_overflow, kProgramHeaderOffset + 40, 32);
    assert(inspect_elf64(virtual_overflow.data(), virtual_overflow.size(), info) ==
           ElfStatus::RangeOverflow);

    auto overlapping = fixture(2);
    assert(inspect_elf64(overlapping.data(), overlapping.size(), info) ==
           ElfStatus::OverlappingSegments);

    auto wrong_class = valid;
    wrong_class[4] = 1;
    assert(inspect_elf64(wrong_class.data(), wrong_class.size(), info) ==
           ElfStatus::Unsupported);

    auto wrong_endian = valid;
    wrong_endian[5] = 2;
    assert(inspect_elf64(wrong_endian.data(), wrong_endian.size(), info) ==
           ElfStatus::Unsupported);

    auto wrong_machine = valid;
    put16(wrong_machine, 18, 3);
    assert(inspect_elf64(wrong_machine.data(), wrong_machine.size(), info) ==
           ElfStatus::Unsupported);

    auto dynamic = valid;
    put16(dynamic, 16, 3);
    assert(inspect_elf64(dynamic.data(), dynamic.size(), info) ==
           ElfStatus::Unsupported);

    auto interp = valid;
    put32(interp, kProgramHeaderOffset, 3);
    assert(inspect_elf64(interp.data(), interp.size(), info) ==
           ElfStatus::InterpreterNotSupported);

    auto dynamic_segment = fixture(2);
    put32(dynamic_segment, kProgramHeaderOffset + kProgramHeaderSize, 2);
    assert(inspect_elf64(dynamic_segment.data(), dynamic_segment.size(), info) ==
           ElfStatus::Unsupported);

    auto outside = valid;
    put64(outside, kProgramHeaderOffset + 16, kImageBase - 0x1000);
    assert(inspect_elf64(outside.data(), outside.size(), info) ==
           ElfStatus::AddressOutOfRange);

    auto bad_entry = valid;
    put64(bad_entry, 24, kImageBase + 0x2000);
    assert(inspect_elf64(bad_entry.data(), bad_entry.size(), info) ==
           ElfStatus::InvalidEntry);

    bool writable = true;
    bool executable = false;
    assert(segment_permissions(4, writable, executable));
    assert(!writable && !executable);
    assert(segment_permissions(5, writable, executable));
    assert(!writable && executable);
    assert(segment_permissions(6, writable, executable));
    assert(writable && !executable);
    assert(segment_permissions(7, writable, executable));
    assert(writable && executable);

    return 0;
}
