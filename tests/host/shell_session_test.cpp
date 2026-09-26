#include "terminal/output.hpp"
#include "terminal/shell_session.hpp"
#include "net/net_types.hpp"
#include "net/network.hpp"
#include "net/dns.hpp"

#include <assert.h>
#include <stddef.h>
#include <string.h>

using namespace linux95::terminal;

namespace {

struct FakeOutput {
    char text[4096];
    size_t length;
    size_t clears;
};

struct FakeExecutor {
    char command[320];
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

struct FakeDns {
    linux95::net::Ipv4Address server{{10, 0, 2, 3}};
    linux95::net::Ipv4Address addresses[8]{};
    linux95::net::dns::Status begin_result = linux95::net::dns::Status::Pending;
    linux95::net::dns::Status status = linux95::net::dns::Status::Idle;
    linux95::net::dns::Status set_result = linux95::net::dns::Status::Success;
    size_t count = 0;
    size_t begin_calls = 0;
    size_t set_calls = 0;
    char hostname[260]{};
};

linux95::net::dns::Status fake_dns_begin(void* context, const char* hostname)
{
    auto* dns = static_cast<FakeDns*>(context);
    ++dns->begin_calls;
    size_t i = 0;
    for (; hostname[i] != '\0' && i + 1 < sizeof(dns->hostname); ++i) {
        dns->hostname[i] = hostname[i];
    }
    dns->hostname[i] = '\0';
    if (dns->begin_result == linux95::net::dns::Status::Pending) {
        dns->status = linux95::net::dns::Status::Pending;
    }
    return dns->begin_result;
}

linux95::net::dns::Status fake_dns_status(void* context)
{
    return static_cast<FakeDns*>(context)->status;
}

size_t fake_dns_count(void* context)
{
    return static_cast<FakeDns*>(context)->count;
}

bool fake_dns_address(void* context, size_t index, linux95::net::Ipv4Address& out)
{
    auto* dns = static_cast<FakeDns*>(context);
    if (index >= dns->count || index >= 8) {
        return false;
    }
    out = dns->addresses[index];
    return true;
}

linux95::net::Ipv4Address fake_dns_server(void* context)
{
    return static_cast<FakeDns*>(context)->server;
}

linux95::net::dns::Status fake_dns_set_server(
    void* context, const linux95::net::Ipv4Address& address)
{
    auto* dns = static_cast<FakeDns*>(context);
    ++dns->set_calls;
    if (dns->set_result != linux95::net::dns::Status::Busy) {
        dns->server = address;
    }
    return dns->set_result;
}

DnsCallbacks make_dns_callbacks(FakeDns& dns)
{
    return DnsCallbacks{
        &dns,
        fake_dns_begin,
        fake_dns_status,
        fake_dns_count,
        fake_dns_address,
        fake_dns_server,
        fake_dns_set_server,
    };
}

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

void test_command_capacity_accepts_259_printable_bytes()
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

    for (size_t i = 0; i < 300; ++i) {
        session.on_char('x');
    }

    session.on_char('\n');

    assert(executor.calls == 1);
    assert(strlen(executor.command) == 259);

    for (size_t i = 0; i < 259; ++i) {
        assert(executor.command[i] == 'x');
    }

    assert(executor.command[259] == '\0');
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

void test_dns_command_usage_and_recognition()
{
    FakeOutput fake{};
    FakeExecutor executor{};
    FakeDns dns{};
    DnsCallbacks callbacks = make_dns_callbacks(dns);
    Output output = make_fake_output(fake);
    ShellSession session(output, &executor, fake_execute, nullptr, &callbacks);
    session.begin();

    enter_command(session, "dns");
    assert(strcmp(fake.text, "linux95> dns\nusage: dns <hostname>\nlinux95> ") == 0);
    assert(dns.begin_calls == 0);

    reset_output(fake);
    enter_command(session, "dns example.com extra");
    assert(strcmp(fake.text, "dns example.com extra\nusage: dns <hostname>\nlinux95> ") == 0);
    assert(dns.begin_calls == 0);

    reset_output(fake);
    enter_command(session, "dnsx");
    assert(executor.calls == 1);
    assert(strcmp(executor.command, "dnsx") == 0);
    assert(dns.begin_calls == 0);
}

void test_dnsserver_display_update_invalid_busy_and_offline()
{
    FakeOutput fake{};
    FakeExecutor executor{};
    FakeDns dns{};
    DnsCallbacks callbacks = make_dns_callbacks(dns);
    Output output = make_fake_output(fake);
    ShellSession session(output, &executor, fake_execute, nullptr, &callbacks);
    session.begin();

    // No network callback is supplied: stored configuration must remain usable.
    enter_command(session, "dnsserver");
    assert(strcmp(fake.text, "linux95> dnsserver\ndns server: 10.0.2.3\nlinux95> ") == 0);

    reset_output(fake);
    enter_command(session, "dnsserver 1.2.3.4");
    assert(strcmp(fake.text, "dnsserver 1.2.3.4\ndns server: 1.2.3.4\nlinux95> ") == 0);
    assert(dns.set_calls == 1);
    assert(dns.server.bytes[0] == 1 && dns.server.bytes[3] == 4);

    const char* invalid[] = {"dnsserver 256.2.3.4", "dnsserver 1.2.3", "dnsserver 1.2.3.4 extra"};
    for (const char* command : invalid) {
        reset_output(fake);
        enter_command(session, command);
        assert(strstr(fake.text, "usage: dnsserver [IPv4 address]\nlinux95> ") != nullptr);
        assert(dns.set_calls == 1);
        assert(dns.server.bytes[0] == 1 && dns.server.bytes[3] == 4);
    }

    dns.set_result = linux95::net::dns::Status::Busy;
    reset_output(fake);
    enter_command(session, "dnsserver 8.8.8.8");
    assert(strcmp(fake.text, "dnsserver 8.8.8.8\ndnsserver: lookup in progress\nlinux95> ") == 0);
    assert(dns.set_calls == 2);
    assert(dns.server.bytes[0] == 1 && dns.server.bytes[3] == 4);

    reset_output(fake);
    enter_command(session, "dnsserver");
    assert(strcmp(fake.text, "dnsserver\ndns server: 1.2.3.4\nlinux95> ") == 0);
}

void test_dns_begin_failures_return_prompt()
{
    struct Case {
        linux95::net::dns::Status result;
        const char* message;
    };
    const Case cases[] = {
        {linux95::net::dns::Status::Busy, "dns: lookup already in progress\n"},
        {linux95::net::dns::Status::InvalidName, "dns: invalid hostname\n"},
        {linux95::net::dns::Status::NetworkUnavailable, "dns: network unavailable\n"},
    };
    for (const Case& test_case : cases) {
        FakeOutput fake{};
        FakeExecutor executor{};
        FakeDns dns{};
        dns.begin_result = test_case.result;
        DnsCallbacks callbacks = make_dns_callbacks(dns);
        Output output = make_fake_output(fake);
        ShellSession session(output, &executor, fake_execute, nullptr, &callbacks);
        session.begin();
        enter_command(session, "dns example.com");
        assert(dns.begin_calls == 1);
        assert(strcmp(dns.hostname, "example.com") == 0);
        assert(strstr(fake.text, test_case.message) != nullptr);
        assert(strcmp(fake.text + fake.length - 9, "linux95> ") == 0);
        assert(!session.poll());
    }
}

void test_dns_pending_completion_and_input_isolation()
{
    FakeOutput fake{};
    FakeExecutor executor{};
    FakeDns dns{};
    DnsCallbacks callbacks = make_dns_callbacks(dns);
    Output output = make_fake_output(fake);
    ShellSession session(output, &executor, fake_execute, nullptr, &callbacks);
    session.begin();
    enter_command(session, "dns example.com");
    assert(strcmp(fake.text, "linux95> dns example.com\n") == 0);
    assert(dns.status == linux95::net::dns::Status::Pending);
    assert(!session.poll());

    // Input arriving during the lookup must neither echo nor run another command.
    enter_command(session, "help");
    assert(strcmp(fake.text, "linux95> dns example.com\n") == 0);
    assert(executor.calls == 0);
    assert(dns.begin_calls == 1);

    dns.addresses[0] = {{1, 2, 3, 4}};
    dns.count = 1;
    dns.status = linux95::net::dns::Status::Success;
    assert(session.poll());
    assert(strcmp(fake.text, "linux95> dns example.com\ndns: 1.2.3.4\nlinux95> ") == 0);
    assert(!session.poll());
    assert(strcmp(fake.text, "linux95> dns example.com\ndns: 1.2.3.4\nlinux95> ") == 0);

    enter_command(session, "ok");
    assert(executor.calls == 1);
    assert(strcmp(executor.command, "ok") == 0);
}

void test_dns_multiple_addresses()
{
    FakeOutput fake{};
    FakeExecutor executor{};
    FakeDns dns{};
    DnsCallbacks callbacks = make_dns_callbacks(dns);
    Output output = make_fake_output(fake);
    ShellSession session(output, &executor, fake_execute, nullptr, &callbacks);
    session.begin();
    enter_command(session, "dns example.com");
    dns.addresses[0] = {{10, 0, 2, 2}};
    dns.addresses[1] = {{93, 184, 215, 14}};
    dns.count = 2;
    dns.status = linux95::net::dns::Status::Success;
    assert(session.poll());
    assert(strcmp(fake.text, "linux95> dns example.com\ndns: 10.0.2.2\ndns: 93.184.215.14\nlinux95> ") == 0);
}

void test_dns_terminal_status_messages()
{
    struct Case {
        linux95::net::dns::Status status;
        const char* message;
    };
    const Case cases[] = {
        {linux95::net::dns::Status::NotFound, "dns: name not found\n"},
        {linux95::net::dns::Status::ServerFailure, "dns: server failure\n"},
        {linux95::net::dns::Status::MalformedResponse, "dns: malformed response\n"},
        {linux95::net::dns::Status::TruncatedResponse, "dns: truncated response (TCP unsupported)\n"},
        {linux95::net::dns::Status::CnameLoopOrLimit, "dns: CNAME loop or limit\n"},
        {linux95::net::dns::Status::TimedOut, "dns: timed out\n"},
        {linux95::net::dns::Status::NetworkUnavailable, "dns: network unavailable\n"},
        {linux95::net::dns::Status::InvalidName, "dns: invalid hostname\n"},
        {linux95::net::dns::Status::Busy, "dns: lookup already in progress\n"},
        {linux95::net::dns::Status::Idle, "dns: no result\n"},
    };
    for (const Case& test_case : cases) {
        FakeOutput fake{};
        FakeExecutor executor{};
        FakeDns dns{};
        DnsCallbacks callbacks = make_dns_callbacks(dns);
        Output output = make_fake_output(fake);
        ShellSession session(output, &executor, fake_execute, nullptr, &callbacks);
        session.begin();
        enter_command(session, "dns example.com");
        dns.status = test_case.status;
        assert(session.poll());
        char expected[160] = "linux95> dns example.com\n";
        strcat(expected, test_case.message);
        strcat(expected, "linux95> ");
        assert(strcmp(fake.text, expected) == 0);
        assert(!session.poll());
        assert(strcmp(fake.text, expected) == 0);
    }
}

void test_dns_maximum_presentation_name_reaches_resolver()
{
    // Wire length: (63+1) + (63+1) + (63+1) + (61+1) + root = 255.
    char name[255]{};
    size_t cursor = 0;
    const size_t labels[] = {63, 63, 63, 61};
    for (size_t label = 0; label < 4; ++label) {
        for (size_t i = 0; i < labels[label]; ++i) name[cursor++] = 'a';
        if (label != 3) name[cursor++] = '.';
    }
    assert(cursor == 253);
    name[cursor++] = '.'; // A final dot is valid presentation syntax.
    name[cursor] = '\0';
    assert(cursor == 254);

    FakeOutput fake{};
    FakeExecutor executor{};
    FakeDns dns{};
    DnsCallbacks callbacks = make_dns_callbacks(dns);
    Output output = make_fake_output(fake);
    ShellSession session(output, &executor, fake_execute, nullptr, &callbacks);
    session.begin();
    char command[260] = "dns ";
    strcat(command, name);
    enter_command(session, command);
    assert(dns.begin_calls == 1);
    assert(strcmp(dns.hostname, name) == 0);
    assert(dns.status == linux95::net::dns::Status::Pending);
}

} // namespace

int main()
{
    test_output_number_helpers();
    test_begin_emits_prompt();
    test_printable_characters_echo();
    test_backspace_edits_without_underflow();
    test_enter_submits_and_prompts_again();
    test_command_capacity_accepts_259_printable_bytes();
    test_second_command_starts_empty();
    test_parse_ipv4();
    test_ip_offline_output();
    test_ip_online_output_includes_configuration_and_mac();
    test_ping_invalid_and_offline_output();
    test_ping_starts_and_completion_prints_once();
    test_ping_failure_outputs();
    test_dns_command_usage_and_recognition();
    test_dnsserver_display_update_invalid_busy_and_offline();
    test_dns_begin_failures_return_prompt();
    test_dns_pending_completion_and_input_isolation();
    test_dns_multiple_addresses();
    test_dns_terminal_status_messages();
    test_dns_maximum_presentation_name_reaches_resolver();

    return 0;
}
