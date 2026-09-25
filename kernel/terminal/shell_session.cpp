#include "terminal/shell_session.hpp"

namespace linux95::terminal {
namespace {

bool word_equals(const char* text, const char* word)
{
    while (*text != '\0' && *word != '\0' && *text == *word) {
        ++text;
        ++word;
    }
    return *word == '\0' && (*text == '\0' || *text == ' ');
}

const char* command_argument(const char* command)
{
    while (*command != '\0' && *command != ' ') {
        ++command;
    }
    while (*command == ' ') {
        ++command;
    }
    return *command == '\0' ? nullptr : command;
}

void write_ipv4(Output& output, const net::Ipv4Address& address)
{
    for (uint8_t i = 0; i < 4; ++i) {
        if (i != 0 && output.put_char != nullptr) {
            output.put_char(output.context, '.');
        }
        write_uint(output, address.bytes[i]);
    }
}

void write_mac(Output& output, const net::MacAddress& address)
{
    constexpr char kHex[] = "0123456789abcdef";
    if (output.put_char == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < 6; ++i) {
        if (i != 0) {
            output.put_char(output.context, ':');
        }
        output.put_char(output.context, kHex[address.bytes[i] >> 4]);
        output.put_char(output.context, kHex[address.bytes[i] & 0x0Fu]);
    }
}

} // namespace

bool parse_ipv4(
    const char* text,
    net::Ipv4Address& out)
{
    if (text == nullptr || *text == '\0') {
        return false;
    }

    net::Ipv4Address parsed{};
    for (uint8_t component = 0; component < 4; ++component) {
        if (*text < '0' || *text > '9') {
            return false;
        }

        uint16_t value = 0;
        while (*text >= '0' && *text <= '9') {
            const uint8_t digit = static_cast<uint8_t>(*text - '0');
            if (value > 25 || (value == 25 && digit > 5)) {
                return false;
            }
            value = static_cast<uint16_t>(
                value * 10u + digit);
            ++text;
        }
        parsed.bytes[component] = static_cast<uint8_t>(value);

        if (component < 3) {
            if (*text != '.') {
                return false;
            }
            ++text;
        } else if (*text != '\0') {
            return false;
        }
    }

    out = parsed;
    return true;
}

ShellSession::ShellSession(
    Output& output,
    void* context,
    ExecuteCallback execute,
    const NetworkCallbacks* network_callbacks)
    : output_(output),
      execute_context_(context),
      execute_(execute),
      network_callbacks_(network_callbacks),
      command_{},
      length_(0)
{
}

bool ShellSession::execute_network_command()
{
    const char* command = command_;
    while (*command == ' ') {
        ++command;
    }

    if (word_equals(command, "ip")) {
        if (network_callbacks_ == nullptr ||
            network_callbacks_->status == nullptr) {
            write(output_, "interface: offline\n");
            return true;
        }

        const network::Status status =
            network_callbacks_->status(network_callbacks_->context);
        if (!status.online) {
            write(output_, "interface: offline\n");
            return true;
        }

        write(output_, "interface: rtl8139\nmac: ");
        write_mac(output_, status.mac);
        write(output_, "\nip: ");
        write_ipv4(output_, status.ip);
        write(output_, "\nnetmask: ");
        write_ipv4(output_, status.netmask);
        write(output_, "\ngateway: ");
        write_ipv4(output_, status.gateway);
        write(output_, "\nlink: up\n");
        return true;
    }

    if (!word_equals(command, "ping")) {
        return false;
    }

    const char* argument = command_argument(command);
    net::Ipv4Address destination{};
    if (argument == nullptr || !parse_ipv4(argument, destination)) {
        write(output_, "usage: ping <IPv4 address>\n");
        return true;
    }

    if (network_callbacks_ == nullptr ||
        network_callbacks_->status == nullptr ||
        !network_callbacks_->status(network_callbacks_->context).online) {
        write(output_, "ping: network unavailable\n");
        return true;
    }

    if (network_callbacks_->start_ping != nullptr &&
        network_callbacks_->start_ping(
            network_callbacks_->context,
            destination)) {
        write(output_, "PING ");
        write_ipv4(output_, destination);
        write(output_, "\n");
    }
    return true;
}

void ShellSession::prompt()
{
    write(
        output_,
        "linux95> ");
}

void ShellSession::begin()
{
    length_ = 0;
    command_[0] = '\0';

    prompt();
}

void ShellSession::on_char(char c)
{
    if (c == '\n') {
        if (output_.put_char != nullptr) {
            output_.put_char(
                output_.context,
                '\n');
        }

        command_[length_] = '\0';

        if (!execute_network_command() && execute_ != nullptr) {
            execute_(
                execute_context_,
                output_,
                command_);
        }

        length_ = 0;
        command_[0] = '\0';

        prompt();
        return;
    }

    if (c == '\b') {
        if (length_ == 0) {
            return;
        }

        --length_;
        command_[length_] = '\0';

        if (output_.put_char != nullptr) {
            output_.put_char(
                output_.context,
                '\b');
        }

        return;
    }

    if (c < 32 ||
        c > 126) {
        return;
    }

    if (length_ + 1 >= kCommandCapacity) {
        return;
    }

    command_[length_] = c;
    ++length_;
    command_[length_] = '\0';

    if (output_.put_char != nullptr) {
        output_.put_char(
            output_.context,
            c);
    }
}

bool ShellSession::poll()
{
    if (network_callbacks_ == nullptr ||
        network_callbacks_->ping_result == nullptr) {
        return false;
    }

    const net::icmp::PingResult result =
        network_callbacks_->ping_result(network_callbacks_->context);
    if (result.state != net::icmp::PingState::ReplyReceived &&
        result.state != net::icmp::PingState::HostUnreachable &&
        result.state != net::icmp::PingState::TimedOut) {
        return false;
    }

    write(output_, "\n");
    if (result.state == net::icmp::PingState::ReplyReceived) {
        write_uint(output_, result.payload_bytes);
        write(output_, " bytes from ");
        write_ipv4(output_, result.address);
        write(output_, ": icmp_seq=");
        write_uint(output_, result.sequence);
        write(output_, "\n");
    } else if (result.state == net::icmp::PingState::HostUnreachable) {
        write(output_, "ping: host unreachable\n");
    } else if (result.state == net::icmp::PingState::TimedOut) {
        write(output_, "Request timeout for ");
        write_ipv4(output_, result.address);
        write(output_, "\n");
    }

    if (network_callbacks_->clear_ping_result != nullptr) {
        network_callbacks_->clear_ping_result(network_callbacks_->context);
    }
    prompt();
    return true;
}

} // namespace linux95::terminal
