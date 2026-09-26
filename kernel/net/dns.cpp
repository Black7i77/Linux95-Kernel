#include "net/dns.hpp"

#include "arch/pit.hpp"
#include "net/network.hpp"

namespace linux95::net::dns {

namespace {

constexpr uint16_t kClientPort = 53000;
constexpr uint16_t kServerPort = 53;

struct Resolver {
    net::Ipv4Address server{{10, 0, 2, 3}};
    Status status = Status::Idle;
    bool bound = false;
    bool awaiting = false;
    bool queued_followup = false;
    Name question{};
    Name visited_names[5]{};
    uint8_t visited_count = 0;
    net::Ipv4Address addresses[8]{};
    uint8_t address_count = 0;
    uint16_t next_id = 0;
    uint16_t active_id = 0;
    uint8_t attempts = 0;
    uint64_t sent_at = 0;
    uint8_t query_packet[512]{};
};

Resolver g_resolver{};

bool addresses_equal(const net::Ipv4Address& left,
                     const net::Ipv4Address& right)
{
    for (size_t i = 0; i < 4; ++i) {
        if (left.bytes[i] != right.bytes[i]) {
            return false;
        }
    }
    return true;
}

void fail_unavailable()
{
    g_resolver.status = Status::NetworkUnavailable;
    g_resolver.awaiting = false;
    g_resolver.queued_followup = false;
}

void send_attempt()
{
    if (!network::status().online) {
        fail_unavailable();
        return;
    }
    const uint16_t id = static_cast<uint16_t>(g_resolver.next_id + 1);
    size_t packet_length = 0;
    if (!build_query(id, g_resolver.question, g_resolver.query_packet,
                     sizeof(g_resolver.query_packet), packet_length) ||
        !network::send_udp(g_resolver.server, kClientPort, kServerPort,
                           g_resolver.query_packet,
                           static_cast<uint16_t>(packet_length))) {
        fail_unavailable();
        return;
    }
    g_resolver.next_id = id;
    g_resolver.active_id = id;
    ++g_resolver.attempts;
    g_resolver.sent_at = pit::ticks();
    g_resolver.awaiting = true;
}

void on_udp(const net::Ipv4Address& source, uint16_t source_port,
            uint16_t destination_port, const uint8_t* payload,
            uint16_t payload_length, void*)
{
    if (g_resolver.status != Status::Pending || !g_resolver.awaiting ||
        !addresses_equal(source, g_resolver.server) ||
        source_port != kServerPort || destination_port != kClientPort) {
        return;
    }
    const ParsedResponse parsed = parse_response(
        payload, payload_length, g_resolver.active_id,
        g_resolver.question, g_resolver.visited_names,
        g_resolver.visited_count);
    if (parsed.disposition == ParseDisposition::Ignored) {
        return;
    }
    g_resolver.awaiting = false;
    if (parsed.disposition == ParseDisposition::Followup) {
        g_resolver.question = parsed.followup_name;
        g_resolver.visited_count = parsed.visited_count;
        for (size_t i = 0; i < parsed.visited_count; ++i) {
            g_resolver.visited_names[i] = parsed.visited_names[i];
        }
        g_resolver.attempts = 0;
        g_resolver.queued_followup = true;
        return;
    }
    g_resolver.status = parsed.status;
    g_resolver.address_count = parsed.status == Status::Success
        ? parsed.address_count : 0;
    for (size_t i = 0; i < g_resolver.address_count; ++i) {
        g_resolver.addresses[i] = parsed.addresses[i];
    }
}

bool ascii_alnum(uint8_t value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9');
}

uint8_t ascii_lower(uint8_t value)
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<uint8_t>(value + ('a' - 'A')) : value;
}

bool valid_wire_name(const Name& name)
{
    if (name.length < 1 || name.length > sizeof(name.wire)) {
        return false;
    }
    size_t offset = 0;
    while (offset < name.length) {
        const uint8_t label_length = name.wire[offset++];
        if (label_length == 0) {
            return offset == name.length;
        }
        if (label_length > 63 || label_length > name.length - offset) {
            return false;
        }
        offset += label_length;
    }
    return false;
}

uint16_t read_be16(const uint8_t* bytes, size_t offset)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
                                 bytes[offset + 1]);
}

struct ResponseRecord {
    uint16_t owner_offset;
    uint16_t rdata_offset;
    uint16_t rdata_length;
    uint16_t type;
    uint16_t klass;
};

bool record_owner(const uint8_t* message, size_t length,
                  const ResponseRecord& record, Name& out)
{
    size_t cursor = record.owner_offset;
    return decode_name(message, length, cursor, out);
}

bool cname_target(const uint8_t* message, size_t length,
                  const ResponseRecord& record, Name& out)
{
    size_t cursor = record.rdata_offset;
    return decode_name(message, length, cursor, out) &&
           cursor == static_cast<size_t>(record.rdata_offset) + record.rdata_length;
}

bool read_record(const uint8_t* message, size_t length,
                 size_t& cursor, ResponseRecord& record)
{
    if (cursor > length || cursor > 512) {
        return false;
    }
    record.owner_offset = static_cast<uint16_t>(cursor);
    Name owner{};
    if (!decode_name(message, length, cursor, owner) ||
        cursor > length || length - cursor < 10) {
        return false;
    }
    record.type = read_be16(message, cursor);
    record.klass = read_be16(message, cursor + 2);
    record.rdata_length = read_be16(message, cursor + 8);
    cursor += 10;
    if (record.rdata_length > length - cursor) {
        return false;
    }
    record.rdata_offset = static_cast<uint16_t>(cursor);
    if (record.type == 1 && record.rdata_length != 4) {
        return false;
    }
    if (record.type == 5) {
        Name target{};
        if (!cname_target(message, length, record, target)) {
            return false;
        }
    }
    cursor += record.rdata_length;
    return true;
}

} // namespace

bool initialize()
{
    if (g_resolver.bound) {
        return true;
    }
    g_resolver.bound = network::bind_udp_port(kClientPort, on_udp, nullptr);
    return g_resolver.bound;
}

Status begin_lookup(const char* hostname)
{
    if (g_resolver.status == Status::Pending) {
        return Status::Busy;
    }
    Name question{};
    if (!encode_hostname(hostname, question)) {
        g_resolver.status = Status::InvalidName;
        return g_resolver.status;
    }
    g_resolver.address_count = 0;
    g_resolver.awaiting = false;
    g_resolver.queued_followup = false;
    if (!g_resolver.bound || !network::status().online) {
        fail_unavailable();
        return g_resolver.status;
    }
    g_resolver.question = question;
    g_resolver.visited_names[0] = question;
    g_resolver.visited_count = 1;
    g_resolver.attempts = 0;
    g_resolver.status = Status::Pending;
    send_attempt();
    return g_resolver.status;
}

void poll()
{
    if (g_resolver.status != Status::Pending) {
        return;
    }
    if (g_resolver.queued_followup) {
        g_resolver.queued_followup = false;
        send_attempt();
        return;
    }
    if (!g_resolver.awaiting ||
        pit::ticks() - g_resolver.sent_at <
            static_cast<uint64_t>(pit::ticks_per_second()) * 2) {
        return;
    }
    g_resolver.awaiting = false;
    if (g_resolver.attempts == 1) {
        send_attempt();
    } else {
        g_resolver.status = Status::TimedOut;
    }
}

Status lookup_status() { return g_resolver.status; }

size_t result_count() { return g_resolver.address_count; }

bool result_address(size_t index, net::Ipv4Address& out)
{
    if (index >= g_resolver.address_count) {
        return false;
    }
    out = g_resolver.addresses[index];
    return true;
}

net::Ipv4Address server() { return g_resolver.server; }

Status set_server(const net::Ipv4Address& address)
{
    if (g_resolver.status == Status::Pending) {
        return Status::Busy;
    }
    g_resolver.server = address;
    return Status::Success;
}

bool encode_hostname(const char* hostname, Name& out)
{
    if (!hostname || !*hostname) {
        return false;
    }
    Name encoded{};
    size_t input = 0;
    while (hostname[input] != 0) {
        if (encoded.length >= sizeof(encoded.wire)) {
            return false;
        }
        const size_t label_position = encoded.length++;
        uint8_t label_length = 0;
        while (hostname[input] != 0 && hostname[input] != '.') {
            const uint8_t character = static_cast<uint8_t>(hostname[input]);
            if ((!ascii_alnum(character) && character != '-') ||
                (label_length == 0 && character == '-') ||
                label_length == 63 || encoded.length >= sizeof(encoded.wire)) {
                return false;
            }
            encoded.wire[encoded.length++] = character;
            ++label_length;
            ++input;
        }
        if (label_length == 0 || encoded.wire[encoded.length - 1] == '-') {
            return false;
        }
        encoded.wire[label_position] = label_length;
        if (hostname[input] == '.') {
            ++input;
            if (hostname[input] == '.') {
                return false;
            }
        }
    }
    if (encoded.length >= sizeof(encoded.wire)) {
        return false;
    }
    encoded.wire[encoded.length++] = 0;
    out = encoded;
    return true;
}

bool names_equal(const Name& left, const Name& right)
{
    if (!valid_wire_name(left) || !valid_wire_name(right) ||
        left.length != right.length) {
        return false;
    }
    for (size_t i = 0; i < left.length; ++i) {
        if (ascii_lower(left.wire[i]) != ascii_lower(right.wire[i])) {
            return false;
        }
    }
    return true;
}

bool decode_name(const uint8_t* message, size_t length,
                 size_t& cursor, Name& out)
{
    if (!message || cursor >= length) {
        return false;
    }
    Name decoded{};
    size_t position = cursor;
    size_t next_cursor = cursor;
    bool jumped = false;
    uint16_t visited[16]{};
    size_t jump_count = 0;
    while (position < length) {
        const uint8_t label_length = message[position];
        if ((label_length & 0xC0) == 0xC0) {
            if (position + 1 >= length || jump_count == 16) {
                return false;
            }
            const uint16_t target = static_cast<uint16_t>(
                (static_cast<uint16_t>(label_length & 0x3F) << 8) |
                message[position + 1]);
            if (target >= length) {
                return false;
            }
            for (size_t i = 0; i < jump_count; ++i) {
                if (visited[i] == target) {
                    return false;
                }
            }
            visited[jump_count++] = target;
            if (!jumped) {
                next_cursor = position + 2;
                jumped = true;
            }
            position = target;
            continue;
        }
        if ((label_length & 0xC0) != 0) {
            return false;
        }
        ++position;
        if (label_length == 0) {
            if (decoded.length >= sizeof(decoded.wire)) {
                return false;
            }
            decoded.wire[decoded.length++] = 0;
            out = decoded;
            cursor = jumped ? next_cursor : position;
            return true;
        }
        if (label_length > length - position ||
            static_cast<size_t>(decoded.length) + label_length + 2 >
                sizeof(decoded.wire)) {
            return false;
        }
        decoded.wire[decoded.length++] = label_length;
        for (size_t i = 0; i < label_length; ++i) {
            decoded.wire[decoded.length++] = message[position++];
        }
    }
    return false;
}

bool build_query(uint16_t transaction_id, const Name& question,
                 uint8_t* output, size_t capacity, size_t& output_length)
{
    if (!output || !valid_wire_name(question) ||
        capacity < static_cast<size_t>(question.length) + 16) {
        return false;
    }
    output[0] = static_cast<uint8_t>(transaction_id >> 8);
    output[1] = static_cast<uint8_t>(transaction_id);
    output[2] = 0x01;
    output[3] = 0;
    output[4] = 0;
    output[5] = 1;
    for (size_t i = 6; i < 12; ++i) {
        output[i] = 0;
    }
    for (size_t i = 0; i < question.length; ++i) {
        output[12 + i] = question.wire[i];
    }
    const size_t tail = 12 + question.length;
    output[tail] = 0;
    output[tail + 1] = 1;
    output[tail + 2] = 0;
    output[tail + 3] = 1;
    output_length = tail + 4;
    return true;
}

ParsedResponse parse_response(const uint8_t* message, size_t length,
                              uint16_t expected_id,
                              const Name& expected_question,
                              const Name* visited_names,
                              size_t visited_count)
{
    ParsedResponse result{};
    result.disposition = ParseDisposition::Complete;
    result.status = Status::MalformedResponse;

    if (!message || length < 2) {
        return result;
    }
    if (read_be16(message, 0) != expected_id) {
        result.disposition = ParseDisposition::Ignored;
        result.status = Status::Idle;
        return result;
    }
    if (length < 12 || length > 512 || !valid_wire_name(expected_question) ||
        visited_count > 5 || (visited_count != 0 && !visited_names)) {
        return result;
    }
    if (visited_count == 0) {
        result.visited_names[0] = expected_question;
        result.visited_count = 1;
    } else {
        for (size_t i = 0; i < visited_count; ++i) {
            if (!valid_wire_name(visited_names[i])) {
                return result;
            }
            result.visited_names[i] = visited_names[i];
        }
        result.visited_count = static_cast<uint8_t>(visited_count);
        if (!names_equal(result.visited_names[visited_count - 1], expected_question)) {
            return result;
        }
    }
    result.cname_hops = static_cast<uint8_t>(result.visited_count - 1);

    const uint16_t flags = read_be16(message, 2);
    if ((flags & 0x8000) == 0 || (flags & 0x7800) != 0) {
        return result;
    }
    if ((flags & 0x0200) != 0) {
        result.status = Status::TruncatedResponse;
        return result;
    }
    if (read_be16(message, 4) != 1) {
        return result;
    }
    size_t cursor = 12;
    Name question{};
    if (!decode_name(message, length, cursor, question) ||
        cursor > length || length - cursor < 4 ||
        read_be16(message, cursor) != 1 ||
        read_be16(message, cursor + 2) != 1 ||
        !names_equal(question, expected_question)) {
        return result;
    }
    cursor += 4;

    // A 512-byte message fits fewer than 48 minimum-size resource records.
    ResponseRecord answers[48]{};
    const uint16_t answer_count = read_be16(message, 6);
    const uint16_t authority_count = read_be16(message, 8);
    const uint16_t additional_count = read_be16(message, 10);
    if (answer_count > 48) {
        return result;
    }
    for (size_t i = 0; i < answer_count; ++i) {
        if (!read_record(message, length, cursor, answers[i])) {
            return result;
        }
    }
    ResponseRecord ignored{};
    for (size_t i = 0; i < authority_count; ++i) {
        if (!read_record(message, length, cursor, ignored)) {
            return result;
        }
    }
    for (size_t i = 0; i < additional_count; ++i) {
        if (!read_record(message, length, cursor, ignored)) {
            return result;
        }
    }
    if (cursor != length) {
        return result;
    }

    const uint16_t rcode = flags & 0x000F;
    if (rcode != 0) {
        result.status = rcode == 3 ? Status::NotFound : Status::ServerFailure;
        return result;
    }

    // Reject ambiguous answer data even when it is unrelated to this query.
    for (size_t i = 0; i < answer_count; ++i) {
        const ResponseRecord& alias = answers[i];
        if (alias.type != 5 || alias.klass != 1) {
            continue;
        }
        Name alias_owner{};
        Name alias_target{};
        if (!record_owner(message, length, alias, alias_owner) ||
            !cname_target(message, length, alias, alias_target)) {
            return result;
        }
        for (size_t j = 0; j < answer_count; ++j) {
            if (j == i || answers[j].klass != 1 ||
                (answers[j].type != 1 && answers[j].type != 5)) {
                continue;
            }
            Name other_owner{};
            if (!record_owner(message, length, answers[j], other_owner)) {
                return result;
            }
            if (!names_equal(alias_owner, other_owner)) {
                continue;
            }
            if (answers[j].type == 1) {
                return result;
            }
            Name other_target{};
            if (!cname_target(message, length, answers[j], other_target) ||
                !names_equal(alias_target, other_target)) {
                return result;
            }
        }
    }

    Name terminal = expected_question;
    bool followed_alias = false;
    for (;;) {
        const ResponseRecord* link = nullptr;
        for (size_t i = 0; i < answer_count; ++i) {
            if (answers[i].type != 5 || answers[i].klass != 1) {
                continue;
            }
            Name owner{};
            if (!record_owner(message, length, answers[i], owner)) {
                return result;
            }
            if (names_equal(owner, terminal)) {
                link = &answers[i];
                break;
            }
        }
        if (!link) {
            break;
        }
        Name target{};
        if (!cname_target(message, length, *link, target)) {
            return result;
        }
        for (size_t i = 0; i < result.visited_count; ++i) {
            if (names_equal(result.visited_names[i], target)) {
                result.status = Status::CnameLoopOrLimit;
                return result;
            }
        }
        if (result.visited_count == 5) {
            result.status = Status::CnameLoopOrLimit;
            return result;
        }
        result.visited_names[result.visited_count++] = target;
        result.cname_hops = static_cast<uint8_t>(result.visited_count - 1);
        terminal = target;
        followed_alias = true;
    }

    for (size_t i = 0; i < answer_count; ++i) {
        const ResponseRecord& record = answers[i];
        if (record.type != 1 || record.klass != 1) {
            continue;
        }
        Name owner{};
        if (!record_owner(message, length, record, owner)) {
            return result;
        }
        if (!names_equal(owner, terminal)) {
            continue;
        }
        net::Ipv4Address address{};
        for (size_t byte = 0; byte < 4; ++byte) {
            address.bytes[byte] = message[record.rdata_offset + byte];
        }
        bool duplicate = false;
        for (size_t j = 0; j < result.address_count; ++j) {
            bool equal = true;
            for (size_t byte = 0; byte < 4; ++byte) {
                if (result.addresses[j].bytes[byte] != address.bytes[byte]) {
                    equal = false;
                    break;
                }
            }
            if (equal) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && result.address_count < 8) {
            result.addresses[result.address_count++] = address;
        }
    }
    if (result.address_count != 0) {
        result.status = Status::Success;
    } else if (followed_alias) {
        result.disposition = ParseDisposition::Followup;
        result.status = Status::Pending;
        result.followup_name = terminal;
    } else {
        result.status = Status::NotFound;
    }
    return result;
}

} // namespace linux95::net::dns
