#include "net/dns.hpp"

namespace linux95::net::dns {

namespace {

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

} // namespace

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

} // namespace linux95::net::dns
