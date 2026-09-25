#include <cassert>

#include "memory/user_space.hpp"
#include "arch/x86_64/msr.hpp"
#include "arch/x86_64/segments.hpp"
#include "syscall/syscall.hpp"

int main() {
    using namespace linux95;
    using syscall::valid_sysret_target;

    constexpr uint64_t expected_star =
        (0x13ULL << 48) | (0x08ULL << 32) | (0x10ULL << 16);
    static_assert(syscall::encode_star_for_test(
                      arch::x86_64::kKernelCodeSelector,
                      arch::x86_64::kKernelDataSelector,
                      arch::x86_64::kUserCodeSelector,
                      arch::x86_64::kUserDataSelector) == expected_star);
    static_assert(arch::x86_64::encode_star(
                      arch::x86_64::kKernelCodeSelector,
                      arch::x86_64::kKernelDataSelector,
                      arch::x86_64::kUserCodeSelector,
                      arch::x86_64::kUserDataSelector) == expected_star);

    process::Process p{};
    p.pid = 1;
    p.state = process::State::Running;

    syscall::Frame unknown{};
    unknown.rax = 999;
    const auto unknown_result = syscall::dispatch_for_test(p, unknown);
    assert(unknown_result.value < 0);
    assert(unknown_result.action == syscall::Action::ReturnToUser);

    syscall::Frame yield{};
    yield.rax = 1;
    const auto yield_result = syscall::dispatch_for_test(p, yield);
    assert(yield_result.action == syscall::Action::YieldToHost);

    syscall::Frame exit{};
    exit.rax = 2;
    exit.rdi = 7;
    const auto exit_result = syscall::dispatch_for_test(p, exit);
    assert(exit_result.action == syscall::Action::ExitToHost);
    assert(p.exit_code == 7);

    const memory::PageInfo pages[2] = {
        {true, true, false, true, 0x1000},
        {true, false, false, false, 0x2000},
    };
    assert(!memory::validate_page_sequence(
        pages, 2, memory::UserAccess::Read));

    assert(valid_sysret_target(
        0x0000400000001000ULL,
        0x00007FFFFFEFF000ULL));

    assert(!valid_sysret_target(
        0xFFFF800000001000ULL,
        0x00007FFFFFEFF000ULL));

    assert(!valid_sysret_target(
        0x0000400000001000ULL,
        0xFFFF800000001000ULL));

    assert(!valid_sysret_target(
        0x0000800000000000ULL,
        0x00007FFFFFEFF000ULL));

    return 0;
}
