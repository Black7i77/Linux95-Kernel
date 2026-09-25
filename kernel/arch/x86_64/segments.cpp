#include "arch/x86_64/segments.hpp"

#include "arch/x86_64/tss.hpp"

namespace linux95::arch::x86_64 {

extern TaskStateSegment g_tss;

namespace {

struct [[gnu::packed]] DescriptorTablePointer {
    uint16_t limit;
    uint64_t base;
};

struct [[gnu::packed]] GlobalDescriptorTable {
    uint64_t null_descriptor;
    uint64_t kernel_code;
    uint64_t kernel_data;
    uint64_t user_data;
    uint64_t user_code;
    TssDescriptor tss;
};

static_assert(sizeof(GlobalDescriptorTable) == 56);

GlobalDescriptorTable g_gdt{};

extern "C" void linux95_load_gdt(
    const DescriptorTablePointer* pointer,
    uint16_t code_selector,
    uint16_t data_selector);

}

void initialize_segments()
{
    g_gdt.null_descriptor = 0;
    g_gdt.kernel_code = make_code_data_descriptor(
        0, 0xFFFFF, DescriptorPrivilege::Ring0, SegmentKind::Code);
    g_gdt.kernel_data = make_code_data_descriptor(
        0, 0xFFFFF, DescriptorPrivilege::Ring0, SegmentKind::Data);
    g_gdt.user_data = make_code_data_descriptor(
        0, 0xFFFFF, DescriptorPrivilege::Ring3, SegmentKind::Data);
    g_gdt.user_code = make_code_data_descriptor(
        0, 0xFFFFF, DescriptorPrivilege::Ring3, SegmentKind::Code);
    g_gdt.tss = make_tss_descriptor(
        reinterpret_cast<uint64_t>(&g_tss),
        sizeof(TaskStateSegment) - 1);

    const DescriptorTablePointer pointer{
        sizeof(GlobalDescriptorTable) - 1,
        reinterpret_cast<uint64_t>(&g_gdt),
    };
    linux95_load_gdt(&pointer, kKernelCodeSelector, kKernelDataSelector);
}

}
