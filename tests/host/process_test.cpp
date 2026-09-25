#include <cassert>
#include <cstddef>
#include "process/process.hpp"
#include "arch/interrupts.hpp"

namespace {

struct CleanupRecord {
    struct Mapping {
        uint64_t virtual_address;
        uint64_t physical_address;
    } mappings[12];
    size_t mapping_count;
    uint64_t released[12];
    size_t released_count;
    uint64_t destroyed_root;
    size_t destroy_count;
};

bool lookup_page(void* opaque,
                 uint64_t,
                 uint64_t virtual_address,
                 uint64_t& physical_address)
{
    auto& record = *static_cast<CleanupRecord*>(opaque);
    for (size_t index = 0; index < record.mapping_count; ++index) {
        if (record.mappings[index].virtual_address == virtual_address) {
            physical_address = record.mappings[index].physical_address;
            return true;
        }
    }
    return false;
}

void release_page(void* opaque, uint64_t physical_address)
{
    auto& record = *static_cast<CleanupRecord*>(opaque);
    record.released[record.released_count++] = physical_address;
}

void destroy_address_space(void* opaque, uint64_t root_physical)
{
    auto& record = *static_cast<CleanupRecord*>(opaque);
    record.destroyed_root = root_physical;
    ++record.destroy_count;
}

uint64_t kernel_stack_physical(void*, uint64_t kernel_stack_base)
{
    return kernel_stack_base - 0xFFFF800000000000ULL;
}

size_t release_count(const CleanupRecord& record, uint64_t physical_address)
{
    size_t count = 0;
    for (size_t index = 0; index < record.released_count; ++index) {
        if (record.released[index] == physical_address) ++count;
    }
    return count;
}

} // namespace

int main() {
    using namespace linux95::process;

    initialize();
    assert(capacity() == 16);

    Process* first = allocate();
    Process* second = allocate();

    assert(first != nullptr);
    assert(second != nullptr);
    assert(first->pid == 1);
    assert(second->pid == 2);
    assert(first->state == State::Created);
    assert(second->state == State::Created);

    for (int i = 0; i < 14; ++i) {
        assert(allocate() != nullptr);
    }
    assert(allocate() == nullptr);

    const uint32_t old_pid = first->pid;
    release(*first);
    Process* replacement = allocate();
    assert(replacement != nullptr);
    assert(replacement->pid > old_pid);

    Process lifecycle{};
    lifecycle.pid = 42;
    lifecycle.state = State::Running;

    mark_exited(lifecycle, 7);

    assert(lifecycle.state == State::Exited);
    assert(lifecycle.exit_code == 7);

    reap_one_for_test(lifecycle);

    assert(lifecycle.state == State::Unused);
    assert(lifecycle.pid == 0);

    constexpr uint64_t kImageBase = 0x0000400000000000ULL;
    constexpr uint64_t kStackTop = 0x00007FFFFFF00000ULL;
    constexpr uint64_t kPageSize = 4096;
    constexpr uint64_t kSharedKernelPage = 0x90000;
    CleanupRecord cleanup{};
    cleanup.mappings[cleanup.mapping_count++] = {kImageBase, 0x20000};
    cleanup.mappings[cleanup.mapping_count++] = {kImageBase + kPageSize,
                                                  0x21000};
    for (size_t index = 0; index < 8; ++index) {
        cleanup.mappings[cleanup.mapping_count++] = {
            kStackTop - (8 - index) * kPageSize,
            0x30000 + index * kPageSize,
        };
    }
    cleanup.mappings[cleanup.mapping_count++] = {
        0xFFFF800000001000ULL,
        kSharedKernelPage,
    };

    Process owned{};
    owned.pid = 43;
    owned.state = State::Exited;
    owned.page_table_physical = 0x10000;
    owned.kernel_stack_base = 0xFFFF800000050000ULL;
    owned.kernel_stack_top = owned.kernel_stack_base + kPageSize;

    const ReapOperations operations{
        &cleanup,
        lookup_page,
        release_page,
        destroy_address_space,
        kernel_stack_physical,
    };
    reap_one_for_test(owned, operations);

    assert(release_count(cleanup, 0x20000) == 1);
    assert(release_count(cleanup, 0x21000) == 1);
    for (size_t index = 0; index < 8; ++index) {
        assert(release_count(cleanup, 0x30000 + index * kPageSize) == 1);
    }
    assert(release_count(cleanup, 0x50000) == 1);
    assert(release_count(cleanup, kSharedKernelPage) == 0);
    assert(cleanup.destroy_count == 1);
    assert(cleanup.destroyed_root == 0x10000);
    assert(owned.state == State::Unused);
    assert(owned.pid == 0);
    assert(owned.page_table_physical == 0);
    assert(owned.kernel_stack_base == 0);
    assert(owned.kernel_stack_top == 0);

    reap_one_for_test(owned, operations);
    assert(cleanup.released_count == 11);
    assert(cleanup.destroy_count == 1);

    linux95::interrupts::InterruptFrame frame{};
    frame.r15 = 0x15;
    frame.r14 = 0x14;
    frame.r13 = 0x13;
    frame.r12 = 0x12;
    frame.r11 = 0x11;
    frame.r10 = 0x10;
    frame.r9 = 0x09;
    frame.r8 = 0x08;
    frame.rbp = 0x70;
    frame.rdi = 0x71;
    frame.rsi = 0x72;
    frame.rdx = 0x73;
    frame.rcx = 0x74;
    frame.rbx = 0x75;
    frame.rax = 0x76;
    frame.rip = 0x0000400000001234ULL;
    frame.rsp = 0x00007FFFFFEFF000ULL;
    frame.rflags = 0x202;
    frame.cs = 0x23;
    frame.ss = 0x1B;

    Process captured{};
    capture_interrupt_context(captured, frame);

    assert(captured.context.r15 == frame.r15);
    assert(captured.context.r14 == frame.r14);
    assert(captured.context.r13 == frame.r13);
    assert(captured.context.r12 == frame.r12);
    assert(captured.context.r11 == frame.r11);
    assert(captured.context.r10 == frame.r10);
    assert(captured.context.r9 == frame.r9);
    assert(captured.context.r8 == frame.r8);
    assert(captured.context.rbp == frame.rbp);
    assert(captured.context.rdi == frame.rdi);
    assert(captured.context.rsi == frame.rsi);
    assert(captured.context.rdx == frame.rdx);
    assert(captured.context.rcx == frame.rcx);
    assert(captured.context.rbx == frame.rbx);
    assert(captured.context.rax == frame.rax);
    assert(captured.context.rip == frame.rip);
    assert(captured.context.rsp == frame.rsp);
    assert(captured.context.rflags == frame.rflags);
    assert(captured.context.cs == frame.cs);
    assert(captured.context.ss == frame.ss);
    assert(captured.context.return_kind == ReturnKind::Iret);

    return 0;
}
