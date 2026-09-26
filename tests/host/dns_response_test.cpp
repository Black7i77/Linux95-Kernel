#include "net/dns.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace {
namespace dns = linux95::net::dns;

constexpr uint16_t kId = 0xBEEF;

dns::Name name(const char* text)
{
    dns::Name result{};
    assert(dns::encode_hostname(text, result));
    return result;
}

struct Packet {
    uint8_t bytes[600]{};
    size_t length = 12;

    Packet(uint16_t answers = 0, uint16_t authority = 0,
           uint16_t additional = 0, const char* question = "a.test")
    {
        set16(0, kId);
        set16(2, 0x8180);
        set16(4, 1);
        set16(6, answers);
        set16(8, authority);
        set16(10, additional);
        add_name(question);
        add16(1);
        add16(1);
    }

    void set16(size_t at, uint16_t value)
    {
        bytes[at] = static_cast<uint8_t>(value >> 8);
        bytes[at + 1] = static_cast<uint8_t>(value);
    }

    void add8(uint8_t value) { assert(length < sizeof(bytes)); bytes[length++] = value; }
    void add16(uint16_t value) { add8(static_cast<uint8_t>(value >> 8)); add8(static_cast<uint8_t>(value)); }
    void add_name(const char* text)
    {
        const dns::Name encoded = name(text);
        for (size_t i = 0; i < encoded.length; ++i) add8(encoded.wire[i]);
    }
    void pointer(uint16_t offset)
    {
        add8(static_cast<uint8_t>(0xC0 | (offset >> 8)));
        add8(static_cast<uint8_t>(offset));
    }
    size_t rr(const char* owner, uint16_t type, uint16_t klass, uint16_t rdata_length)
    {
        if (owner) add_name(owner); else pointer(12);
        add16(type);
        add16(klass);
        add16(0);
        add16(0);
        add16(rdata_length);
        return length;
    }
    void a(const char* owner, uint8_t last, uint16_t klass = 1,
           uint16_t data_length = 4)
    {
        rr(owner, 1, klass, data_length);
        add8(10); add8(20); add8(30); add8(last);
    }
    void cname(const char* owner, const char* target)
    {
        const dns::Name encoded = name(target);
        rr(owner, 5, 1, encoded.length);
        add_name(target);
    }
    dns::ParsedResponse parse(const dns::Name* prior = nullptr,
                              size_t prior_count = 0,
                              const char* question = "a.test") const
    {
        const dns::Name expected = name(question);
        return dns::parse_response(bytes, length, kId, expected, prior, prior_count);
    }
};

void complete(const dns::ParsedResponse& result, dns::Status status)
{
    assert(result.disposition == dns::ParseDisposition::Complete);
    assert(result.status == status);
}

void address(const dns::ParsedResponse& result, size_t index, uint8_t last)
{
    assert(index < result.address_count);
    const uint8_t expected[4] = {10, 20, 30, last};
    assert(memcmp(result.addresses[index].bytes, expected, 4) == 0);
}

void test_a_records()
{
    Packet one(1);
    one.a(nullptr, 7);
    auto result = one.parse();
    complete(result, dns::Status::Success);
    assert(result.address_count == 1);
    address(result, 0, 7);

    Packet many(4);
    many.a("other.test", 90);
    many.a("A.TEST", 1);
    many.a(nullptr, 2);
    many.a("a.test", 1);
    result = many.parse();
    complete(result, dns::Status::Success);
    assert(result.address_count == 2);
    address(result, 0, 1);
    address(result, 1, 2);

    Packet capped(11);
    for (uint8_t i = 1; i <= 10; ++i) capped.a(nullptr, i);
    capped.a(nullptr, 1);
    result = capped.parse();
    complete(result, dns::Status::Success);
    assert(result.address_count == 8);
    for (uint8_t i = 1; i <= 8; ++i) address(result, i - 1, i);
    capped.length -= 1; // Even a record after the eighth result must be validated.
    complete(capped.parse(), dns::Status::MalformedResponse);
    puts("[PASS] a_owner_dedup_first_eight_and_late_bounds");
}

void test_cname_chains()
{
    Packet ordered(4);
    ordered.a("c.test", 9);
    ordered.cname("b.test", "c.test");
    ordered.a("a.test", 8); // CNAME+A at a.test is prohibited.
    ordered.cname("a.test", "b.test");
    complete(ordered.parse(), dns::Status::MalformedResponse);

    Packet shuffled(3);
    shuffled.a("c.test", 9);
    shuffled.cname("b.test", "c.test");
    shuffled.cname("a.test", "b.test");
    auto result = shuffled.parse();
    complete(result, dns::Status::Success);
    assert(result.address_count == 1);
    address(result, 0, 9);
    assert(result.visited_count == 3 && result.cname_hops == 2);
    assert(dns::names_equal(result.visited_names[2], name("c.test")));

    Packet follow(1);
    follow.cname(nullptr, "b.test");
    result = follow.parse();
    assert(result.disposition == dns::ParseDisposition::Followup);
    assert(result.status == dns::Status::Pending);
    assert(dns::names_equal(result.followup_name, name("b.test")));
    assert(result.visited_count == 2 && result.cname_hops == 1);
    assert(dns::names_equal(result.visited_names[0], name("a.test")));

    dns::Name prior[2] = {name("a.test"), name("b.test")};
    Packet second(1, 0, 0, "b.test");
    second.cname(nullptr, "a.test");
    complete(second.parse(prior, 2, "b.test"), dns::Status::CnameLoopOrLimit);

    Packet loop(2);
    loop.cname("b.test", "a.test");
    loop.cname("a.test", "b.test");
    complete(loop.parse(), dns::Status::CnameLoopOrLimit);

    Packet four(5);
    four.cname("d.test", "e.test");
    four.cname("b.test", "c.test");
    four.a("e.test", 4);
    four.cname("a.test", "b.test");
    four.cname("c.test", "d.test");
    result = four.parse();
    complete(result, dns::Status::Success);
    assert(result.cname_hops == 4 && result.visited_count == 5);
    address(result, 0, 4);

    Packet five(6);
    five.cname("e.test", "f.test");
    five.cname("a.test", "b.test");
    five.cname("c.test", "d.test");
    five.cname("b.test", "c.test");
    five.cname("d.test", "e.test");
    five.a("f.test", 6);
    complete(five.parse(), dns::Status::CnameLoopOrLimit);
    puts("[PASS] cname_order_followup_prior_loop_and_four_link_limit");
}

void test_conflicts_and_cname_rdata()
{
    Packet conflict(2);
    conflict.cname(nullptr, "b.test");
    conflict.cname("A.TEST", "c.test");
    complete(conflict.parse(), dns::Status::MalformedResponse);

    Packet unrelated(2);
    unrelated.cname("x.test", "y.test");
    unrelated.a("x.test", 3);
    complete(unrelated.parse(), dns::Status::MalformedResponse);

    Packet short_data(1);
    short_data.rr(nullptr, 5, 1, 1);
    short_data.pointer(12);
    complete(short_data.parse(), dns::Status::MalformedResponse);

    Packet extra_data(1);
    extra_data.rr(nullptr, 5, 1, 3);
    extra_data.pointer(12);
    extra_data.add8(0);
    complete(extra_data.parse(), dns::Status::MalformedResponse);

    Packet compressed(2);
    compressed.rr(nullptr, 5, 1, 2);
    compressed.pointer(12); // Self-alias through compressed RDATA.
    compressed.a("other.test", 8);
    complete(compressed.parse(), dns::Status::CnameLoopOrLimit);

    Packet valid_compressed(2);
    const size_t target = valid_compressed.rr(nullptr, 5, 1, 2);
    valid_compressed.pointer(12);
    const size_t answer_owner = valid_compressed.length;
    valid_compressed.a("b.test", 5);
    valid_compressed.set16(target, static_cast<uint16_t>(0xC000 | answer_owner));
    auto result = valid_compressed.parse();
    complete(result, dns::Status::Success);
    address(result, 0, 5);
    puts("[PASS] cname_conflicts_and_rdata_boundaries");
}

void test_header_question_and_rcode()
{
    Packet wrong_name(0, 0, 0, "x.test");
    complete(wrong_name.parse(), dns::Status::MalformedResponse);
    Packet wrong_type;
    wrong_type.set16(wrong_type.length - 4, 28);
    complete(wrong_type.parse(), dns::Status::MalformedResponse);
    Packet wrong_class;
    wrong_class.set16(wrong_class.length - 2, 3);
    complete(wrong_class.parse(), dns::Status::MalformedResponse);
    Packet wrong_qr;
    wrong_qr.set16(2, 0x0180);
    complete(wrong_qr.parse(), dns::Status::MalformedResponse);
    Packet opcode;
    opcode.set16(2, 0x8980);
    complete(opcode.parse(), dns::Status::MalformedResponse);
    Packet nxdomain;
    nxdomain.set16(2, 0x8183);
    complete(nxdomain.parse(), dns::Status::NotFound);
    Packet servfail;
    servfail.set16(2, 0x8182);
    complete(servfail.parse(), dns::Status::ServerFailure);
    Packet refused;
    refused.set16(2, 0x8185);
    complete(refused.parse(), dns::Status::ServerFailure);
    Packet nodata;
    complete(nodata.parse(), dns::Status::NotFound);
    Packet tc(1);
    tc.set16(2, 0x8380);
    complete(tc.parse(), dns::Status::TruncatedResponse);
    Packet stale;
    stale.set16(0, 0xCAFE);
    const auto ignored = stale.parse();
    assert(ignored.disposition == dns::ParseDisposition::Ignored);
    assert(ignored.status == dns::Status::Idle);
    puts("[PASS] header_question_rcode_truncation_and_stale_id");
}

void test_section_structure()
{
    Packet counts;
    counts.set16(4, 2);
    complete(counts.parse(), dns::Status::MalformedResponse);
    Packet missing_answer(1);
    complete(missing_answer.parse(), dns::Status::MalformedResponse);
    Packet fixed_fields(1);
    fixed_fields.pointer(12);
    fixed_fields.add16(1);
    complete(fixed_fields.parse(), dns::Status::MalformedResponse);
    Packet short_a(1);
    short_a.a(nullptr, 1, 1, 3);
    complete(short_a.parse(), dns::Status::MalformedResponse);
    Packet authority(0, 1);
    authority.rr(nullptr, 2, 1, 2);
    authority.add8(0xC0);
    complete(authority.parse(), dns::Status::MalformedResponse);
    Packet bad_authority_name(0, 1);
    bad_authority_name.add8(0xC0);
    bad_authority_name.add8(0xFF);
    bad_authority_name.add16(2); bad_authority_name.add16(1);
    bad_authority_name.add16(0); bad_authority_name.add16(0);
    bad_authority_name.add16(0);
    complete(bad_authority_name.parse(), dns::Status::MalformedResponse);
    Packet bad_additional_cname(0, 0, 1);
    bad_additional_cname.rr(nullptr, 5, 1, 2);
    bad_additional_cname.pointer(0x3FFF);
    complete(bad_additional_cname.parse(), dns::Status::MalformedResponse);
    Packet bad_other_class_cname(0, 0, 1);
    bad_other_class_cname.rr(nullptr, 5, 3, 2);
    bad_other_class_cname.pointer(0x3FFF);
    complete(bad_other_class_cname.parse(), dns::Status::MalformedResponse);
    Packet bad_other_class_a(0, 0, 1);
    bad_other_class_a.rr(nullptr, 1, 3, 3);
    bad_other_class_a.add8(1); bad_other_class_a.add8(2); bad_other_class_a.add8(3);
    complete(bad_other_class_a.parse(), dns::Status::MalformedResponse);
    Packet additional(0, 0, 1);
    additional.rr(nullptr, 1, 1, 4);
    additional.add8(1); additional.add8(2); additional.add8(3); additional.add8(4);
    complete(additional.parse(), dns::Status::NotFound);
    additional.length--;
    complete(additional.parse(), dns::Status::MalformedResponse);
    Packet bad_owner(1);
    bad_owner.add8(0xC0); bad_owner.add8(0xFF);
    complete(bad_owner.parse(), dns::Status::MalformedResponse);
    Packet compressed_question;
    compressed_question.bytes[12] = 0xC0;
    compressed_question.bytes[13] = 0x20;
    complete(compressed_question.parse(), dns::Status::MalformedResponse);
    Packet valid_compressed_question(1);
    valid_compressed_question.length = 12;
    valid_compressed_question.pointer(18);
    valid_compressed_question.add16(1);
    valid_compressed_question.add16(1);
    valid_compressed_question.a("a.test", 6);
    auto compressed_result = valid_compressed_question.parse();
    complete(compressed_result, dns::Status::Success);
    address(compressed_result, 0, 6);
    Packet oversized;
    oversized.length = 513;
    complete(oversized.parse(), dns::Status::MalformedResponse);
    complete(dns::parse_response(nullptr, 0, kId, name("a.test"), nullptr, 0),
             dns::Status::MalformedResponse);
    puts("[PASS] all_section_bounds_and_packet_limit");
}

void test_trailing_undeclared_byte()
{
    Packet answer(1);
    answer.a(nullptr, 7);
    complete(answer.parse(), dns::Status::Success);
    answer.add8(0xAA);
    assert(answer.parse().status == dns::Status::MalformedResponse);
    puts("[PASS] trailing_undeclared_byte_rejected");
}
} // namespace

int main()
{
    test_a_records();
    test_cname_chains();
    test_conflicts_and_cname_rdata();
    test_header_question_and_rcode();
    test_section_structure();
    test_trailing_undeclared_byte();
}
