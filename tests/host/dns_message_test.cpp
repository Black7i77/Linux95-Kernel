#include "net/dns.hpp"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

namespace dns = linux95::net::dns;

void passed(const char* name)
{
    printf("[PASS] %s\n", name);
}

void test_hostname_encoding()
{
    dns::Name name{};
    assert(dns::encode_hostname("ExAmPlE.CoM", name));
    const uint8_t expected[] = {7, 'E', 'x', 'A', 'm', 'P', 'l', 'E',
                                3, 'C', 'o', 'M', 0};
    assert(name.length == sizeof(expected));
    assert(memcmp(name.wire, expected, sizeof(expected)) == 0);

    dns::Name trailing{};
    assert(dns::encode_hostname("ExAmPlE.CoM.", trailing));
    assert(trailing.length == name.length);
    assert(memcmp(trailing.wire, expected, sizeof(expected)) == 0);

    dns::Name different_case{};
    assert(dns::encode_hostname("example.com", different_case));
    assert(dns::names_equal(name, different_case));
    assert(!dns::names_equal(name, dns::Name{}));
    assert(dns::encode_hostname("example.net", different_case));
    assert(!dns::names_equal(name, different_case));
    passed("dns_hostname_encoding_and_equality");
}

void test_hostname_rejections_and_limits()
{
    dns::Name name{};
    const char* invalid[] = {nullptr, "", ".", "a..b", ".a", "a..",
                             "-a.com", "a-.com", "a_b", "a/b", "a b",
                             "a\xC3\xA9", "a.é"};
    for (const char* value : invalid) {
        assert(!dns::encode_hostname(value, name));
    }

    char label63[64];
    memset(label63, 'a', 63);
    label63[63] = 0;
    assert(dns::encode_hostname(label63, name));
    assert(name.length == 65);
    char label64[65];
    memset(label64, 'a', 64);
    label64[64] = 0;
    assert(!dns::encode_hostname(label64, name));

    char maximum[255];
    size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        memset(maximum + pos, 'a' + i, 63);
        pos += 63;
        maximum[pos++] = '.';
    }
    memset(maximum + pos, 'z', 61);
    pos += 61;
    maximum[pos] = 0;
    assert(pos == 253);
    assert(dns::encode_hostname(maximum, name));
    assert(name.length == 255);
    maximum[pos] = 'z';
    maximum[pos + 1] = 0;
    assert(!dns::encode_hostname(maximum, name));
    passed("dns_hostname_rejections_and_limits");
}

void test_query()
{
    dns::Name name{};
    assert(dns::encode_hostname("Ab.c", name));
    const uint8_t expected[] = {
        0xBE, 0xEF, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0,
        2, 'A', 'b', 1, 'c', 0, 0, 1, 0, 1};
    uint8_t output[sizeof(expected) + 1];
    memset(output, 0xA5, sizeof(output));
    size_t length = 99;
    assert(dns::build_query(0xBEEF, name, output, sizeof(expected), length));
    assert(length == sizeof(expected));
    assert(memcmp(output, expected, sizeof(expected)) == 0);
    assert(output[sizeof(expected)] == 0xA5);
    assert(!dns::build_query(0xBEEF, name, output, sizeof(expected) - 1, length));
    assert(!dns::build_query(0xBEEF, name, nullptr, sizeof(output), length));
    assert(!dns::build_query(0xBEEF, dns::Name{}, output, sizeof(output), length));
    passed("dns_query_header_question_and_bounds");
}

void test_decode_uncompressed_and_pointer_cursor()
{
    const uint8_t root[] = {0};
    dns::Name root_name{};
    size_t root_cursor = 0;
    assert(dns::decode_name(root, sizeof(root), root_cursor, root_name));
    assert(root_cursor == 1 && root_name.length == 1);
    assert(dns::names_equal(root_name, root_name));

    const uint8_t packet[] = {3, 'W', 'w', 'W', 0, 0xC0, 0x00, 0xAA};
    dns::Name plain{};
    size_t cursor = 0;
    assert(dns::decode_name(packet, sizeof(packet), cursor, plain));
    assert(cursor == 5);
    assert(plain.length == 5);
    dns::Name compressed{};
    cursor = 5;
    assert(dns::decode_name(packet, sizeof(packet), cursor, compressed));
    assert(cursor == 7);
    assert(dns::names_equal(plain, compressed));
    assert(memcmp(compressed.wire, packet, 5) == 0);

    const uint8_t prefix[] = {1, 'x', 0xC0, 0x06, 0xAA, 0xAA,
                              1, 'Y', 0};
    cursor = 0;
    assert(dns::decode_name(prefix, sizeof(prefix), cursor, compressed));
    const uint8_t expected[] = {1, 'x', 1, 'Y', 0};
    assert(cursor == 4);
    assert(compressed.length == sizeof(expected));
    assert(memcmp(compressed.wire, expected, sizeof(expected)) == 0);
    passed("dns_decode_uncompressed_and_compressed_cursor");
}

void test_decode_malformed_pointers()
{
    dns::Name name{};
    size_t cursor = 0;
    const uint8_t at_end[] = {0xC0, 0x02};
    assert(!dns::decode_name(at_end, sizeof(at_end), cursor, name));
    const uint8_t beyond[] = {0xC0, 0x03};
    cursor = 0;
    assert(!dns::decode_name(beyond, sizeof(beyond), cursor, name));
    const uint8_t self[] = {0xC0, 0x00};
    cursor = 0;
    assert(!dns::decode_name(self, sizeof(self), cursor, name));
    const uint8_t loop[] = {0xC0, 0x02, 0xC0, 0x00};
    cursor = 0;
    assert(!dns::decode_name(loop, sizeof(loop), cursor, name));
    const uint8_t truncated_pointer[] = {0xC0};
    cursor = 0;
    assert(!dns::decode_name(truncated_pointer, sizeof(truncated_pointer), cursor, name));

    uint8_t chain[36]{};
    for (size_t i = 0; i < 17; ++i) {
        chain[2 * i] = 0xC0;
        chain[2 * i + 1] = static_cast<uint8_t>(2 * i + 2);
    }
    chain[34] = 0;
    cursor = 0;
    assert(!dns::decode_name(chain, 35, cursor, name));
    chain[32] = 0;
    cursor = 0;
    assert(dns::decode_name(chain, 35, cursor, name));
    assert(cursor == 2 && name.length == 1);
    passed("dns_decode_pointer_bounds_loops_and_jump_limit");
}

void test_decode_malformed_labels_and_length()
{
    dns::Name name{};
    size_t cursor = 0;
    const uint8_t reserved01[] = {0x40, 0};
    assert(!dns::decode_name(reserved01, sizeof(reserved01), cursor, name));
    const uint8_t reserved10[] = {0x80, 0};
    cursor = 0;
    assert(!dns::decode_name(reserved10, sizeof(reserved10), cursor, name));
    const uint8_t truncated[] = {3, 'a', 'b'};
    cursor = 0;
    assert(!dns::decode_name(truncated, sizeof(truncated), cursor, name));
    cursor = sizeof(truncated);
    assert(!dns::decode_name(truncated, sizeof(truncated), cursor, name));
    assert(!dns::decode_name(nullptr, sizeof(truncated), cursor, name));

    uint8_t maximum[255]{};
    size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        maximum[pos++] = 63;
        memset(maximum + pos, 'a', 63);
        pos += 63;
    }
    maximum[pos++] = 61;
    memset(maximum + pos, 'z', 61);
    pos += 61;
    maximum[pos++] = 0;
    assert(pos == 255);
    cursor = 0;
    assert(dns::decode_name(maximum, sizeof(maximum), cursor, name));
    assert(cursor == 255 && name.length == 255);
    uint8_t too_long[257]{};
    memcpy(too_long, maximum, 254);
    too_long[192] = 63;
    memset(too_long + 193, 'z', 63);
    too_long[256] = 0;
    cursor = 0;
    assert(!dns::decode_name(too_long, sizeof(too_long), cursor, name));
    passed("dns_decode_label_and_wire_length_bounds");
}

} // namespace

int main()
{
    test_hostname_encoding();
    test_hostname_rejections_and_limits();
    test_query();
    test_decode_uncompressed_and_pointer_cursor();
    test_decode_malformed_pointers();
    test_decode_malformed_labels_and_length();
    return 0;
}
