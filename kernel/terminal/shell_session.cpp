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

void write_dns_status(Output& output, net::dns::Status status)
{
    switch (status) {
    case net::dns::Status::InvalidName:
        write(output, "dns: invalid hostname\n");
        break;
    case net::dns::Status::NotFound:
        write(output, "dns: name not found\n");
        break;
    case net::dns::Status::ServerFailure:
        write(output, "dns: server failure\n");
        break;
    case net::dns::Status::MalformedResponse:
        write(output, "dns: malformed response\n");
        break;
    case net::dns::Status::TruncatedResponse:
        write(output, "dns: truncated response (TCP unsupported)\n");
        break;
    case net::dns::Status::CnameLoopOrLimit:
        write(output, "dns: CNAME loop or limit\n");
        break;
    case net::dns::Status::TimedOut:
        write(output, "dns: timed out\n");
        break;
    case net::dns::Status::NetworkUnavailable:
        write(output, "dns: network unavailable\n");
        break;
    case net::dns::Status::Busy:
        write(output, "dns: lookup already in progress\n");
        break;
    case net::dns::Status::Idle:
        write(output, "dns: no result\n");
        break;
    case net::dns::Status::Success:
    case net::dns::Status::Pending:
        break;
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
    const NetworkCallbacks* network_callbacks,
    const DnsCallbacks* dns_callbacks)
    : output_(output),
      execute_context_(context),
      execute_(execute),
      network_callbacks_(network_callbacks),
      dns_callbacks_(dns_callbacks),
      dns_pending_(false),
      length_(0)
{
    command_[0] = '\0';
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

bool ShellSession::execute_dns_command()
{
    const char* command = command_;
    while (*command == ' ') {
        ++command;
    }

    if (word_equals(command, "dnsserver")) {
        const char* argument = command_argument(command);
        if (dns_callbacks_ == nullptr || dns_callbacks_->server == nullptr) {
            write(output_, "dnsserver: unavailable\n");
            return true;
        }
        if (argument != nullptr) {
            net::Ipv4Address address{};
            if (!parse_ipv4(argument, address)) {
                write(output_, "usage: dnsserver [IPv4 address]\n");
            } else if (dns_callbacks_->set_server == nullptr) {
                write(output_, "dnsserver: unavailable\n");
            } else {
                const net::dns::Status status = dns_callbacks_->set_server(
                    dns_callbacks_->context, address);
                if (status == net::dns::Status::Busy) {
                    write(output_, "dnsserver: lookup in progress\n");
                }
            }
        }
        write(output_, "dns server: ");
        write_ipv4(output_, dns_callbacks_->server(dns_callbacks_->context));
        write(output_, "\n");
        return true;
    }

    if (!word_equals(command, "dns")) {
        return false;
    }

    const char* argument = command_argument(command);
    if (argument == nullptr) {
        write(output_, "usage: dns <hostname>\n");
        return true;
    }
    for (const char* cursor = argument; *cursor != '\0'; ++cursor) {
        if (*cursor == ' ') {
            write(output_, "usage: dns <hostname>\n");
            return true;
        }
    }
    if (dns_callbacks_ == nullptr || dns_callbacks_->begin_lookup == nullptr) {
        write_dns_status(output_, net::dns::Status::NetworkUnavailable);
        return true;
    }

    const net::dns::Status status = dns_callbacks_->begin_lookup(
        dns_callbacks_->context, argument);
    if (status == net::dns::Status::Pending) {
        dns_pending_ = true;
    } else {
        write_dns_status(output_, status);
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
    dns_pending_ = false;
    length_ = 0;
    command_[0] = '\0';

    prompt();
}

void ShellSession::on_char(char c)
{
    if (dns_pending_) {
        return;
    }

    if (c == '\n') {
        if (output_.put_char != nullptr) {
            output_.put_char(
                output_.context,
                '\n');
        }

        command_[length_] = '\0';

        if (!execute_network_command() &&
            !execute_dns_command() && execute_ != nullptr) {
            execute_(
                execute_context_,
                output_,
                command_);
        }

        length_ = 0;
        command_[0] = '\0';

        if (!dns_pending_) {
            prompt();
        }
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
    bool changed = false;
    if (network_callbacks_ != nullptr &&
        network_callbacks_->ping_result != nullptr) {
        const net::icmp::PingResult result =
            network_callbacks_->ping_result(network_callbacks_->context);
        if (result.state == net::icmp::PingState::ReplyReceived ||
            result.state == net::icmp::PingState::HostUnreachable ||
            result.state == net::icmp::PingState::TimedOut) {
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
            } else {
                write(output_, "Request timeout for ");
                write_ipv4(output_, result.address);
                write(output_, "\n");
            }
            if (network_callbacks_->clear_ping_result != nullptr) {
                network_callbacks_->clear_ping_result(network_callbacks_->context);
            }
            if (!dns_pending_) {
                prompt();
            }
            changed = true;
        }
    }

    if (dns_pending_ && dns_callbacks_ != nullptr &&
        dns_callbacks_->lookup_status != nullptr) {
        const net::dns::Status status = dns_callbacks_->lookup_status(
            dns_callbacks_->context);
        if (status != net::dns::Status::Pending) {
            if (status == net::dns::Status::Success) {
                const size_t count = dns_callbacks_->result_count != nullptr
                    ? dns_callbacks_->result_count(dns_callbacks_->context) : 0;
                for (size_t index = 0; index < count && index < 8; ++index) {
                    net::Ipv4Address address{};
                    if (dns_callbacks_->result_address != nullptr &&
                        dns_callbacks_->result_address(
                            dns_callbacks_->context, index, address)) {
                        write(output_, "dns: ");
                        write_ipv4(output_, address);
                        write(output_, "\n");
                    }
                }
            } else {
                write_dns_status(output_, status);
            }
            dns_pending_ = false;
            prompt();
            changed = true;
        }
    }
    return changed;
}

} // namespace linux95::terminal
