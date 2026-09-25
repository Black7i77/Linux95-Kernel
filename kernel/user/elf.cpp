#include "user/elf.hpp"

#include "memory/address.hpp"

namespace linux95::user {
namespace {

constexpr size_t kElfHeaderSize = 64;
constexpr size_t kProgramHeaderSize = 56;
constexpr size_t kMaxSegments = 16;

struct [[gnu::packed]] Elf64Header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
};

struct [[gnu::packed]] Elf64ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
};

bool checked_add(uint64_t left, uint64_t right, uint64_t& result)
{
    if (right > UINT64_MAX - left) return false;
    result = left + right;
    return true;
}

bool checked_mul(uint64_t left, uint64_t right, uint64_t& result)
{
    if (left != 0 && right > UINT64_MAX / left) return false;
    result = left * right;
    return true;
}

bool read_bytes(const uint8_t* data,
                size_t size,
                uint64_t offset,
                void* destination,
                size_t length)
{
    if (offset > size || length > size - static_cast<size_t>(offset)) {
        return false;
    }
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = data + static_cast<size_t>(offset);
    for (size_t index = 0; index < length; ++index) {
        output[index] = input[index];
    }
    return true;
}

void clear_image_info(ElfImageInfo& out)
{
    auto* bytes = reinterpret_cast<uint8_t*>(&out);
    for (size_t index = 0; index < sizeof(out); ++index) {
        bytes[index] = 0;
    }
}

} // namespace

bool segment_permissions(uint32_t elf_flags,
                         bool& writable,
                         bool& executable)
{
    writable = (elf_flags & PF_W) != 0;
    executable = (elf_flags & PF_X) != 0;
    return true;
}

ElfStatus inspect_elf64(const uint8_t* data,
                        size_t size,
                        ElfImageInfo& out)
{
    clear_image_info(out);
    if (data == nullptr || size < kElfHeaderSize) {
        return ElfStatus::Truncated;
    }

    Elf64Header header{};
    if (!read_bytes(data, size, 0, &header, sizeof(header))) {
        return ElfStatus::Truncated;
    }
    if (header.ident[0] != 0x7F || header.ident[1] != 'E' ||
        header.ident[2] != 'L' || header.ident[3] != 'F') {
        return ElfStatus::BadMagic;
    }
    if (header.ident[4] != ELFCLASS64 || header.ident[5] != ELFDATA2LSB ||
        header.type != ET_EXEC || header.machine != EM_X86_64 ||
        header.header_size < kElfHeaderSize) {
        return ElfStatus::Unsupported;
    }
    if (header.program_header_size < kProgramHeaderSize) {
        return ElfStatus::Truncated;
    }

    uint64_t program_headers_bytes = 0;
    if (!checked_mul(header.program_header_count,
                     header.program_header_size,
                     program_headers_bytes)) {
        return ElfStatus::RangeOverflow;
    }
    uint64_t program_headers_end = 0;
    if (!checked_add(header.program_header_offset,
                     program_headers_bytes,
                     program_headers_end)) {
        return ElfStatus::RangeOverflow;
    }
    if (program_headers_end > size) {
        return ElfStatus::Truncated;
    }

    uint64_t expanded_starts[kMaxSegments]{};
    uint64_t expanded_ends[kMaxSegments]{};
    size_t load_count = 0;
    bool entry_in_executable_segment = false;

    for (uint16_t index = 0; index < header.program_header_count; ++index) {
        uint64_t relative_offset = 0;
        if (!checked_mul(index, header.program_header_size, relative_offset)) {
            return ElfStatus::RangeOverflow;
        }
        uint64_t offset = 0;
        if (!checked_add(header.program_header_offset, relative_offset, offset)) {
            return ElfStatus::RangeOverflow;
        }

        Elf64ProgramHeader program_header{};
        if (!read_bytes(data, size, offset, &program_header,
                        sizeof(program_header))) {
            return ElfStatus::Truncated;
        }
        if (program_header.type == PT_INTERP) {
            return ElfStatus::InterpreterNotSupported;
        }
        if (program_header.type != PT_LOAD) continue;
        if (load_count == kMaxSegments) return ElfStatus::InvalidSegment;
        if (program_header.file_size > program_header.memory_size) {
            return ElfStatus::InvalidSegment;
        }

        uint64_t file_end = 0;
        if (!checked_add(program_header.offset,
                         program_header.file_size,
                         file_end)) {
            return ElfStatus::RangeOverflow;
        }
        if (file_end > size) return ElfStatus::Truncated;

        uint64_t virtual_end = 0;
        if (!checked_add(program_header.virtual_address,
                         program_header.memory_size,
                         virtual_end)) {
            return ElfStatus::RangeOverflow;
        }
        if (program_header.memory_size == 0) {
            return ElfStatus::InvalidSegment;
        }

        const uint64_t expanded_start =
            memory::align_down(program_header.virtual_address, memory::kPageSize);
        uint64_t expanded_end = memory::align_up(virtual_end, memory::kPageSize);
        if (expanded_end == UINT64_MAX ||
            expanded_start < kUserImageBase ||
            expanded_end > kUserImageLimit ||
            expanded_start >= expanded_end) {
            return ElfStatus::AddressOutOfRange;
        }

        for (size_t previous = 0; previous < load_count; ++previous) {
            if (expanded_start < expanded_ends[previous] &&
                expanded_end > expanded_starts[previous]) {
                return ElfStatus::OverlappingSegments;
            }
        }

        expanded_starts[load_count] = expanded_start;
        expanded_ends[load_count] = expanded_end;
        out.segments[load_count] = {
            program_header.virtual_address,
            program_header.memory_size,
            program_header.offset,
            program_header.file_size,
            program_header.flags,
        };
        if ((program_header.flags & PF_X) != 0 &&
            header.entry >= program_header.virtual_address &&
            header.entry < virtual_end) {
            entry_in_executable_segment = true;
        }
        ++load_count;
    }

    if (!entry_in_executable_segment) return ElfStatus::InvalidEntry;
    out.entry = header.entry;
    out.segment_count = load_count;
    return load_count == 0 ? ElfStatus::InvalidSegment : ElfStatus::Ok;
}

} // namespace linux95::user
