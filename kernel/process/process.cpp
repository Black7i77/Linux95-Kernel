#include "process/process.hpp"

#if !__STDC_HOSTED__
#include "memory/address.hpp"
#include "memory/physical.hpp"
#include "memory/user_space.hpp"
#endif

namespace linux95::process {

namespace {

constexpr size_t kProcessCapacity = 16;
Process g_processes[kProcessCapacity]{};
uint32_t g_next_pid = 1;

constexpr uint64_t kPageSize = 4096;
constexpr uint64_t kUserImageBase = 0x0000400000000000ULL;
constexpr uint64_t kUserImageLimit = 0x0000400100000000ULL;
constexpr uint64_t kUserStackTop = 0x00007FFFFFF00000ULL;
constexpr size_t kUserStackPages = 8;

#if !__STDC_HOSTED__
bool lookup_user_page(void*,
                      uint64_t root_physical,
                      uint64_t virtual_address,
                      uint64_t& physical_address)
{
    memory::PageInfo info{};
    if (!memory::query_page(root_physical, virtual_address, info) ||
        !info.present || !info.user) {
        return false;
    }
    physical_address = info.physical;
    return true;
}

void release_physical_page(void*, uint64_t physical_address)
{
    memory::physical::free_page(physical_address);
}

void destroy_address_space(void*, uint64_t root_physical)
{
    memory::UserAddressSpace address_space{root_physical};
    memory::destroy_user_address_space(address_space);
}

uint64_t kernel_stack_physical(void*, uint64_t kernel_stack_base)
{
    return memory::hhdm_to_physical(kernel_stack_base);
}

#endif

void release_mapped_range(Process& process,
                          const ReapOperations& operations,
                          uint64_t first,
                          uint64_t limit)
{
    if (operations.lookup_user_page == nullptr ||
        operations.release_physical_page == nullptr ||
        process.page_table_physical == 0) {
        return;
    }
    for (uint64_t address = first; address < limit; address += kPageSize) {
        uint64_t physical = 0;
        if (operations.lookup_user_page(
                operations.context,
                process.page_table_physical,
                address,
                physical)) {
            operations.release_physical_page(operations.context, physical);
        }
    }
}

void reap_one(Process& process, const ReapOperations& operations)
{
    if (process.state != State::Exited) {
        return;
    }

    release_mapped_range(
        process,
        operations,
        kUserImageBase,
        kUserImageLimit);
    release_mapped_range(
        process,
        operations,
        kUserStackTop - kUserStackPages * kPageSize,
        kUserStackTop);

    if (process.page_table_physical != 0 &&
        operations.destroy_address_space != nullptr) {
        operations.destroy_address_space(
            operations.context,
            process.page_table_physical);
    }
    if (process.kernel_stack_base != 0 &&
        operations.kernel_stack_physical != nullptr &&
        operations.release_physical_page != nullptr) {
        operations.release_physical_page(
            operations.context,
            operations.kernel_stack_physical(
                operations.context,
                process.kernel_stack_base));
    }
    release(process);
}

}

void initialize() {
    for (Process& process : g_processes) {
        process = {};
        process.state = State::Unused;
    }
    g_next_pid = 1;
}

Process* allocate() {
    for (Process& process : g_processes) {
        if (process.state == State::Unused) {
            process = {};
            process.pid = g_next_pid++;
            process.state = State::Created;
            return &process;
        }
    }
    return nullptr;
}

Process* find(uint32_t pid) {
    for (Process& process : g_processes) {
        if (process.state != State::Unused && process.pid == pid) {
            return &process;
        }
    }
    return nullptr;
}

void release(Process& process) {
    process = {};
    process.state = State::Unused;
}

size_t capacity() {
    return kProcessCapacity;
}

Process* table() {
    return g_processes;
}

void mark_exited(Process& process, int64_t code) {
    process.exit_code = code;
    process.state = State::Exited;
}

void reap_exited() {
#if !__STDC_HOSTED__
    const ReapOperations operations{
        nullptr,
        lookup_user_page,
        release_physical_page,
        destroy_address_space,
        kernel_stack_physical,
    };
#else
    const ReapOperations operations{};
#endif
    for (Process& process : g_processes) {
        reap_one(process, operations);
    }
}

void reap_one_for_test(Process& process) {
    const ReapOperations operations{};
    reap_one(process, operations);
}

void reap_one_for_test(Process& process,
                       const ReapOperations& operations) {
    reap_one(process, operations);
}

}
