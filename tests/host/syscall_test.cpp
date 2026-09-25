#include <cassert>

#include "memory/user_space.hpp"
#include "syscall/syscall.hpp"

int main() {
    using namespace linux95;

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

    return 0;
}
