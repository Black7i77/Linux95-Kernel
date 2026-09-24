#include "terminal/output.hpp"
#include "terminal/shell_session.hpp"

#include <assert.h>
#include <stddef.h>
#include <string.h>

using namespace linux95::terminal;

namespace {

struct FakeOutput {
    char text[1024];
    size_t length;
    size_t clears;
};

struct FakeExecutor {
    char command[128];
    size_t calls;
};

void fake_put_char(void* context, char c)
{
    auto* fake =
        static_cast<FakeOutput*>(context);

    if (fake->length + 1 >= sizeof(fake->text)) {
        return;
    }

    fake->text[fake->length++] = c;
    fake->text[fake->length] = '\0';
}

void fake_clear(void* context)
{
    auto* fake =
        static_cast<FakeOutput*>(context);

    ++fake->clears;
    fake->length = 0;
    fake->text[0] = '\0';
}

void fake_set_color(
    void*,
    uint8_t,
    uint8_t)
{
}

void fake_execute(
    void* context,
    Output&,
    char* command)
{
    auto* executor =
        static_cast<FakeExecutor*>(context);

    ++executor->calls;

    size_t i = 0;

    while (
        command[i] != '\0' &&
        i + 1 < sizeof(executor->command)) {
        executor->command[i] = command[i];
        ++i;
    }

    executor->command[i] = '\0';
}

Output make_fake_output(FakeOutput& fake)
{
    return Output{
        &fake,
        fake_put_char,
        fake_clear,
        fake_set_color,
    };
}

void test_begin_emits_prompt()
{
    FakeOutput fake{};
    FakeExecutor executor{};

    Output output =
        make_fake_output(fake);

    ShellSession session(
        output,
        &executor,
        fake_execute);

    session.begin();

    assert(
        strcmp(
            fake.text,
            "linux95> ") == 0);
}

void test_printable_characters_echo()
{
    FakeOutput fake{};
    FakeExecutor executor{};

    Output output =
        make_fake_output(fake);

    ShellSession session(
        output,
        &executor,
        fake_execute);

    session.begin();

    session.on_char('a');
    session.on_char('b');
    session.on_char('c');

    assert(
        strcmp(
            fake.text,
            "linux95> abc") == 0);

    assert(executor.calls == 0);
}

void test_backspace_edits_without_underflow()
{
    FakeOutput fake{};
    FakeExecutor executor{};

    Output output =
        make_fake_output(fake);

    ShellSession session(
        output,
        &executor,
        fake_execute);

    session.begin();

    // Empty command: must not underflow.
    session.on_char('\b');

    session.on_char('a');
    session.on_char('b');
    session.on_char('\b');
    session.on_char('c');
    session.on_char('\n');

    assert(executor.calls == 1);

    assert(
        strcmp(
            executor.command,
            "ac") == 0);
}

void test_enter_submits_and_prompts_again()
{
    FakeOutput fake{};
    FakeExecutor executor{};

    Output output =
        make_fake_output(fake);

    ShellSession session(
        output,
        &executor,
        fake_execute);

    session.begin();

    session.on_char('h');
    session.on_char('i');
    session.on_char('\n');

    assert(executor.calls == 1);

    assert(
        strcmp(
            executor.command,
            "hi") == 0);

    const char* expected_suffix =
        "linux95> ";

    const size_t suffix_length =
        strlen(expected_suffix);

    assert(fake.length >= suffix_length);

    assert(
        strcmp(
            fake.text +
                fake.length -
                suffix_length,
            expected_suffix) == 0);
}

void test_command_capacity_is_64_including_nul()
{
    FakeOutput fake{};
    FakeExecutor executor{};

    Output output =
        make_fake_output(fake);

    ShellSession session(
        output,
        &executor,
        fake_execute);

    session.begin();

    // Only 63 printable bytes may enter a
    // 64-byte NUL-terminated command buffer.
    for (size_t i = 0; i < 80; ++i) {
        session.on_char('x');
    }

    session.on_char('\n');

    assert(executor.calls == 1);
    assert(strlen(executor.command) == 63);

    for (size_t i = 0; i < 63; ++i) {
        assert(executor.command[i] == 'x');
    }

    assert(executor.command[63] == '\0');
}

void reset_output(FakeOutput& fake)
{
    fake.length = 0;
    fake.text[0] = '\0';
}

void test_output_number_helpers()
{
    FakeOutput fake{};
    Output output =
        make_fake_output(fake);

    write_uint(output, 0);

    assert(
        strcmp(
            fake.text,
            "0") == 0);

    reset_output(fake);

    write_uint(
        output,
        18446744073709551615ULL);

    assert(
        strcmp(
            fake.text,
            "18446744073709551615") == 0);

    reset_output(fake);

    write_hex(output, 0);

    assert(
        strcmp(
            fake.text,
            "0x0") == 0);

    reset_output(fake);

    write_hex(
        output,
        0x1234ABCDEFULL);

    assert(
        strcmp(
            fake.text,
            "0x1234ABCDEF") == 0);
}

void test_second_command_starts_empty()
{
    FakeOutput fake{};
    FakeExecutor executor{};

    Output output =
        make_fake_output(fake);

    ShellSession session(
        output,
        &executor,
        fake_execute);

    session.begin();

    session.on_char('a');
    session.on_char('\n');

    assert(executor.calls == 1);
    assert(strcmp(executor.command, "a") == 0);

    session.on_char('b');
    session.on_char('\n');

    assert(executor.calls == 2);
    assert(strcmp(executor.command, "b") == 0);
}

} // namespace

int main()
{
    test_output_number_helpers();
    test_begin_emits_prompt();
    test_printable_characters_echo();
    test_backspace_edits_without_underflow();
    test_enter_submits_and_prompts_again();
    test_command_capacity_is_64_including_nul();
    test_second_command_starts_empty();

    return 0;
}
