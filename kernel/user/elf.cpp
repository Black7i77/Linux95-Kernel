#include "user/elf.hpp"

#include "memory/address.hpp"
#include "memory/heap.hpp"
#include "memory/physical.hpp"
#include "memory/user_space.hpp"
#include "filesystem/vfs.hpp"
#include "process/process.hpp"
#include "arch/x86_64/segments.hpp"

namespace linux95::user {
namespace {

constexpr size_t kElfHeaderSize = 64;
constexpr size_t kProgramHeaderSize = 56;
constexpr size_t kMaxSegments = 16;
constexpr size_t kKernelStackPages = 1;

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

void clear_process(linux95::process::Process& process)
{
    auto* bytes = reinterpret_cast<uint8_t*>(&process);
    for (size_t index = 0; index < sizeof(process); ++index) bytes[index] = 0;
}

void clear_context(linux95::process::UserContext& context)
{
    auto* bytes = reinterpret_cast<uint8_t*>(&context);
    for (size_t index = 0; index < sizeof(context); ++index) bytes[index] = 0;
}

void zero_bytes(uint8_t* data, size_t size)
{
    for (size_t index = 0; index < size; ++index) data[index] = 0;
}

void copy_bytes(uint8_t* destination, const uint8_t* source, size_t size)
{
    for (size_t index = 0; index < size; ++index) destination[index] = source[index];
}

bool checked_add_size(uint64_t left, uint64_t right, uint64_t& result)
{
    if (right > UINT64_MAX - left) return false;
    result = left + right;
    return true;
}

void release_pages(uint64_t* pages, size_t count)
{
    if (pages == nullptr) return;
    for (size_t index = 0; index < count; ++index) {
        memory::physical::free_page(pages[index]);
    }
}

ElfStatus failure_status(ElfStatus preferred)
{
    return preferred == ElfStatus::Ok ? ElfStatus::InvalidSegment : preferred;
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

ElfStatus build_load_plan(const uint8_t* elf,
                          size_t size,
                          SegmentPagePlan* out,
                          size_t out_capacity,
                          size_t& out_count)
{
    out_count = 0;
    ElfImageInfo image;
    const ElfStatus status = inspect_elf64(elf, size, image);
    if (status != ElfStatus::Ok) return status;
    if (image.segment_count > out_capacity ||
        (image.segment_count != 0 && out == nullptr)) {
        return ElfStatus::InvalidSegment;
    }

    for (size_t index = 0; index < image.segment_count; ++index) {
        const LoadSegment& segment = image.segments[index];
        uint64_t end = 0;
        if (!checked_add(segment.virtual_address, segment.memory_size, end)) {
            return ElfStatus::RangeOverflow;
        }
        const uint64_t first =
            memory::align_down(segment.virtual_address, memory::kPageSize);
        const uint64_t last = memory::align_up(end, memory::kPageSize);
        if (last == UINT64_MAX || last <= first) {
            return ElfStatus::RangeOverflow;
        }
        bool writable = false;
        bool executable = false;
        segment_permissions(segment.flags, writable, executable);
        out[index] = {
            first,
            (last - first) / memory::kPageSize,
            writable,
            executable,
            segment.file_offset,
            segment.file_size,
            segment.memory_size,
        };
    }
    out_count = image.segment_count;
    return ElfStatus::Ok;
}

ElfStatus load_process_image(const char* path,
                             linux95::process::Process& process)
{
    const auto fail = [&](ElfStatus result) {
        clear_process(process);
        process.state = linux95::process::State::Unused;
        return result;
    };
    process.state = linux95::process::State::Created;
    process.page_table_physical = 0;
    process.kernel_stack_base = 0;
    process.kernel_stack_top = 0;

    filesystem::Status file_status = filesystem::Status::Ok;
    const int fd = filesystem::vfs::open(path, file_status);
    if (fd < 0 || file_status != filesystem::Status::Ok) {
        return fail(ElfStatus::InvalidSegment);
    }

    filesystem::vfs::FileStat stat;
    if (filesystem::vfs::fstat(fd, stat) != filesystem::Status::Ok ||
        stat.is_directory || stat.size == 0) {
        filesystem::vfs::close(fd);
        return fail(ElfStatus::InvalidSegment);
    }
    auto* elf = static_cast<uint8_t*>(heap::allocate(stat.size));
    if (elf == nullptr) {
        filesystem::vfs::close(fd);
        return fail(ElfStatus::InvalidSegment);
    }
    size_t total_read = 0;
    while (total_read < stat.size) {
        size_t bytes_read = 0;
        const filesystem::Status status = filesystem::vfs::read(
            fd, elf + total_read, stat.size - total_read, bytes_read);
        if (status != filesystem::Status::Ok || bytes_read == 0) {
            filesystem::vfs::close(fd);
            return fail(ElfStatus::InvalidSegment);
        }
        total_read += bytes_read;
    }
    filesystem::vfs::close(fd);

    SegmentPagePlan plans[kMaxSegments];
    size_t plan_count = 0;
    ElfStatus status = build_load_plan(
        elf, stat.size, plans, kMaxSegments, plan_count);
    if (status != ElfStatus::Ok) return fail(status);
    ElfImageInfo image;
    status = inspect_elf64(elf, stat.size, image);
    if (status != ElfStatus::Ok) return fail(status);

    uint64_t user_page_count = memory::kUserStackPages;
    for (size_t index = 0; index < plan_count; ++index) {
        if (plans[index].page_count > UINT64_MAX - user_page_count) {
            return fail(ElfStatus::RangeOverflow);
        }
        user_page_count += plans[index].page_count;
    }
    if (user_page_count > static_cast<uint64_t>(SIZE_MAX / sizeof(uint64_t))) {
        return fail(ElfStatus::RangeOverflow);
    }
    auto* user_pages = static_cast<uint64_t*>(
        heap::allocate(static_cast<size_t>(user_page_count) * sizeof(uint64_t)));
    if (user_pages == nullptr) return fail(ElfStatus::InvalidSegment);
    size_t allocated_user_pages = 0;
    uint64_t kernel_stack_page = memory::physical::kInvalidPhysicalAddress;
    memory::UserAddressSpace address_space{memory::paging::kInvalidAddress};

    if (!memory::create_user_address_space(address_space)) {
        return fail(ElfStatus::InvalidSegment);
    }

    for (size_t segment_index = 0; segment_index < plan_count; ++segment_index) {
        const LoadSegment& segment = image.segments[segment_index];
        const SegmentPagePlan& plan = plans[segment_index];
        uint64_t segment_end = 0;
        if (!checked_add_size(segment.virtual_address, segment.memory_size,
                              segment_end)) {
            status = ElfStatus::RangeOverflow;
            goto cleanup;
        }
        uint64_t file_end = 0;
        if (!checked_add_size(segment.virtual_address, segment.file_size,
                              file_end)) {
            status = ElfStatus::RangeOverflow;
            goto cleanup;
        }
        for (uint64_t page_index = 0; page_index < plan.page_count; ++page_index) {
            const uint64_t virtual_page =
                plan.first_page + page_index * memory::kPageSize;
            const uint64_t physical_page = memory::physical::allocate_page();
            if (physical_page == memory::physical::kInvalidPhysicalAddress) {
                status = ElfStatus::InvalidSegment;
                goto cleanup;
            }
            user_pages[allocated_user_pages++] = physical_page;
            auto* page = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
                memory::physical_to_hhdm(physical_page)));
            zero_bytes(page, memory::kPageSize);

            const uint64_t page_end = virtual_page + memory::kPageSize;
            const uint64_t copy_start =
                virtual_page > segment.virtual_address ? virtual_page : segment.virtual_address;
            const uint64_t copy_end =
                page_end < file_end ? page_end : file_end;
            if (copy_end > copy_start) {
                const uint64_t source_offset = segment.file_offset +
                    (copy_start - segment.virtual_address);
                const uint64_t copy_size = copy_end - copy_start;
                if (source_offset > stat.size || copy_size > stat.size - source_offset) {
                    status = ElfStatus::RangeOverflow;
                    goto cleanup;
                }
                copy_bytes(page + (copy_start - virtual_page),
                           elf + source_offset,
                           static_cast<size_t>(copy_size));
            }
            if (!memory::map_user_page(address_space, virtual_page, physical_page,
                                       plan.writable, plan.executable)) {
                status = ElfStatus::InvalidSegment;
                goto cleanup;
            }
        }
    }

    for (size_t stack_index = 0; stack_index < memory::kUserStackPages; ++stack_index) {
        const uint64_t virtual_page =
            memory::kUserStackTop - (memory::kUserStackPages - stack_index) * memory::kPageSize;
        const uint64_t physical_page = memory::physical::allocate_page();
        if (physical_page == memory::physical::kInvalidPhysicalAddress) {
            status = ElfStatus::InvalidSegment;
            goto cleanup;
        }
        user_pages[allocated_user_pages++] = physical_page;
        zero_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
                       memory::physical_to_hhdm(physical_page))),
                   memory::kPageSize);
        if (!memory::map_user_page(address_space, virtual_page, physical_page,
                                   true, false)) {
            status = ElfStatus::InvalidSegment;
            goto cleanup;
        }
    }

    kernel_stack_page = memory::physical::allocate_page();
    if (kernel_stack_page == memory::physical::kInvalidPhysicalAddress) {
        status = ElfStatus::InvalidSegment;
        goto cleanup;
    }
    zero_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
                   memory::physical_to_hhdm(kernel_stack_page))),
               memory::kPageSize * kKernelStackPages);

    process.page_table_physical = address_space.root_physical;
    process.user_entry = image.entry;
    process.user_stack_top = memory::kUserStackTop;
    process.kernel_stack_base = memory::physical_to_hhdm(kernel_stack_page);
    process.kernel_stack_top = process.kernel_stack_base + memory::kPageSize;
    clear_context(process.context);
    process.context.rip = image.entry;
    process.context.rsp = memory::kUserStackTop;
    process.context.rflags = 0x202;
    process.context.cs = arch::x86_64::kUserCodeSelector;
    process.context.ss = arch::x86_64::kUserDataSelector;
    process.context.return_kind = linux95::process::ReturnKind::Iret;
    process.state = linux95::process::State::Ready;
    return ElfStatus::Ok;

cleanup:
    release_pages(user_pages, allocated_user_pages);
    if (kernel_stack_page != memory::physical::kInvalidPhysicalAddress) {
        memory::physical::free_page(kernel_stack_page);
    }
    memory::destroy_user_address_space(address_space);
    clear_process(process);
    process.state = linux95::process::State::Unused;
    return failure_status(status);
}

ElfStatus inspect_elf64(const uint8_t* data,
                        size_t size,
                        ElfImageInfo& out)
{
    clear_image_info(out);
    if (data == nullptr || size < kElfHeaderSize) {
        return ElfStatus::Truncated;
    }

    Elf64Header header;
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

    uint64_t expanded_starts[kMaxSegments];
    uint64_t expanded_ends[kMaxSegments];
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

        Elf64ProgramHeader program_header;
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
