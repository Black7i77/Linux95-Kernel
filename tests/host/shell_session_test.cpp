#include "terminal/output.hpp"
#include "terminal/shell_session.hpp"
#include "net/net_types.hpp"
#include "net/network.hpp"

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

struct FakeNetwork {
    linux95::network::Status status;
    linux95::net::icmp::PingResult result;
    linux95::net::Ipv4Address started_address;
    bool start_result;
    size_t start_calls;
    size_t clear_calls;
};

linux95::network::Status fake_network_status(void* context)
{
    return static_cast<FakeNetwork*>(context)->status;
}

bool fake_start_ping(
    void* context,
    linux95::net::Ipv4Address address)
{
    auto* network = static_cast<FakeNetwork*>(context);
    ++network->start_calls;
    network->started_address = address;
    return network->start_result;
}

linux95::net::icmp::PingResult fake_ping_result(void* context)
{
    return static_cast<FakeNetwork*>(context)->result;
}

void fake_clear_ping_result(void* context)
{
    auto* network = static_cast<FakeNetwork*>(context);
    ++network->clear_calls;
    network->result.state = linux95::net::icmp::PingState::Idle;
}

NetworkCallbacks make_network_callbacks(FakeNetwork& network)
{
    return NetworkCallbacks{
        &network,
        fake_network_status,
        fake_start_ping,
        fake_ping_result,
        fake_clear_ping_result,
    };
}

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

void enter_command(ShellSession& session, const char* command)
{
    while (*command != '\0') {
        session.on_char(*command++);
    }
    session.on_char('\n');
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

void test_parse_ipv4()
{
    linux95::net::Ipv4Address address{};

    assert(parse_ipv4("10.0.2.2", address));
    assert(address.bytes[0] == 10);
    assert(address.bytes[1] == 0);
    assert(address.bytes[2] == 2);
    assert(address.bytes[3] == 2);

    assert(parse_ipv4("0.0.0.0", address));
    assert(address.bytes[0] == 0);
    assert(address.bytes[1] == 0);
    assert(address.bytes[2] == 0);
    assert(address.bytes[3] == 0);

    assert(parse_ipv4("255.255.255.255", address));
    assert(address.bytes[0] == 255);
    assert(address.bytes[1] == 255);
    assert(address.bytes[2] == 255);
    assert(address.bytes[3] == 255);

    assert(!parse_ipv4("256.0.0.1", address));
    assert(!parse_ipv4("10.0.2", address));
    assert(!parse_ipv4("10.0.2.2x", address));
    assert(!parse_ipv4("", address));
}

void test_ip_offline_output()
{
    FakeOutput fake{};
    FakeExecutor executor{};
    FakeNetwork network{};
    NetworkCallbacks callbacks = make_network_callbacks(network);
    Output output = make_fake_output(fake);
    ShellSession session(output, &executor, fake_execute, &callbacks);

    session.begin();
    enter_command(session, "ip");

    assert(strcmp(
        fake.text,
        "linux95> ip\ninterface: offline\nlinux95> ") == 0);
    assert(executor.calls == 0);
}

void test_ip_online_output_includes_configuration_and_mac()
{
    FakeOutput fake{};
    FakeExecutor executor{};
    FakeNetwork network{};
    network.status = linux95::network::Status{
        true,
        {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}},
        {{10, 0, 2, 15}},
        {{255, 255, 255, 0}},
        {{10, 0, 2, 2}},
    };
    NetworkCallbacks callbacks = make_network_callbacks(network);
    Output output = make_fake_output(fake);
    ShellSession session(output, &executor, fake_execute, &callbacks);

    session.begin();
    enter_command(session, "ip");

    assert(strstr(fake.text, "interface: rtl8139\n") != nullptr);
    assert(strstr(fake.text, "mac: 52:54:00:12:34:56\n") != nullptr);
    assert(strstr(fake.text, "ip: 10.0.2.15\n") != nullptr);
    assert(strstr(fake.text, "netmask: 255.255.255.0\n") != nullptr);
    assert(strstr(fake.text, "gateway: 10.0.2.2\n") != nullptr);
    assert(strstr(fake.text, "link: up\n") != nullptr);
    assert(executor.calls == 0);
}

void test_ping_invalid_and_offline_output()
{
    FakeOutput fake{};
    FakeExecutor executor{};
    FakeNetwork network{};
    network.status.online = true;
    NetworkCallbacks callbacks = make_network_callbacks(network);
    Output output = make_fake_output(fake);
    ShellSession session(output, &executor, fake_execute, &callbacks);

    session.begin();
    enter_command(session, "ping 10.0.2");
    assert(strstr(fake.text, "usage: ping <IPv4 address>\n") != nullptr);
    assert(network.start_calls == 0);

    reset_output(fake);
    network.status.online = false;
    enter_command(session, "ping 10.0.2.2");
    assert(strcmp(
        fake.text,
        "ping 10.0.2.2\nping: network unavailable\nlinux95> ") == 0);
    assert(network.start_calls == 0);
}

void test_ping_starts_and_completion_prints_once()
{
    FakeOutput fake{};
    FakeExecutor executor{};
    FakeNetwork network{};
    network.status.online = true;
    network.start_result = true;
    NetworkCallbacks callbacks = make_network_callbacks(network);
    Output output = make_fake_output(fake);
    ShellSession session(output, &executor, fake_execute, &callbacks);

    session.begin();
    enter_command(session, "ping 10.0.2.2");
    assert(strstr(fake.text, "PING 10.0.2.2\n") != nullptr);
    assert(network.start_calls == 1);
    assert(network.started_address.bytes[0] == 10);
    assert(network.started_address.bytes[1] == 0);
    assert(network.started_address.bytes[2] == 2);
    assert(network.started_address.bytes[3] == 2);

    network.result = linux95::net::icmp::PingResult{
        linux95::net::icmp::PingState::ReplyReceived,
        {{10, 0, 2, 2}},
        7,
        32,
    };
    assert(session.poll());
    assert(strcmp(
        fake.text,
        "linux95> ping 10.0.2.2\n"
        "PING 10.0.2.2\n"
        "linux95> \n"
        "32 bytes from 10.0.2.2: icmp_seq=7\n"
        "linux95> ") == 0);
    assert(network.clear_calls == 1);
    assert(!session.poll());
    assert(strcmp(
        fake.text,
        "linux95> ping 10.0.2.2\n"
        "PING 10.0.2.2\n"
        "linux95> \n"
        "32 bytes from 10.0.2.2: icmp_seq=7\n"
        "linux95> ") == 0);
    assert(network.clear_calls == 1);
}

void test_ping_failure_outputs()
{
    FakeOutput fake{};
    FakeExecutor executor{};
    FakeNetwork network{};
    NetworkCallbacks callbacks = make_network_callbacks(network);
    Output output = make_fake_output(fake);
    ShellSession session(output, &executor, fake_execute, &callbacks);

    network.result = linux95::net::icmp::PingResult{
        linux95::net::icmp::PingState::HostUnreachable,
        {{10, 0, 2, 2}},
        1,
        0,
    };
    assert(session.poll());
    assert(strcmp(fake.text, "\nping: host unreachable\nlinux95> ") == 0);

    reset_output(fake);
    network.result = linux95::net::icmp::PingResult{
        linux95::net::icmp::PingState::TimedOut,
        {{10, 0, 2, 2}},
        2,
        0,
    };
    assert(session.poll());
    assert(strcmp(
        fake.text,
        "\nRequest timeout for 10.0.2.2\nlinux95> ") == 0);
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
    test_parse_ipv4();
    test_ip_offline_output();
    test_ip_online_output_includes_configuration_and_mac();
    test_ping_invalid_and_offline_output();
    test_ping_starts_and_completion_prints_once();
    test_ping_failure_outputs();

    return 0;
}
