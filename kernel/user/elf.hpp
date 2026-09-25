#pragma once

#include <stddef.h>
#include <stdint.h>

namespace linux95::user {

constexpr uint8_t ELFCLASS64 = 2;
constexpr uint8_t ELFDATA2LSB = 1;
constexpr uint16_t ET_EXEC = 2;
constexpr uint16_t EM_X86_64 = 62;
constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t PT_INTERP = 3;
constexpr uint32_t PF_X = 1;
constexpr uint32_t PF_W = 2;
constexpr uint32_t PF_R = 4;

constexpr uint64_t kUserImageBase = 0x0000400000000000ULL;
constexpr uint64_t kUserImageLimit = 0x0000400100000000ULL;

enum class ElfStatus {
    Ok,
    BadMagic,
    Truncated,
    Unsupported,
    InvalidSegment,
    RangeOverflow,
    OverlappingSegments,
    InterpreterNotSupported,
    AddressOutOfRange,
    InvalidEntry,
};

struct LoadSegment {
    uint64_t virtual_address;
    uint64_t memory_size;
    uint64_t file_offset;
    uint64_t file_size;
    uint32_t flags;
};

struct ElfImageInfo {
    uint64_t entry;
    size_t segment_count;
    LoadSegment segments[16];
};

ElfStatus inspect_elf64(const uint8_t* data,
                        size_t size,
                        ElfImageInfo& out);

bool segment_permissions(uint32_t elf_flags,
                          bool& writable,
                          bool& executable);

} // namespace linux95::user
