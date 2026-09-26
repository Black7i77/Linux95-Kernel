#pragma once

#include <stddef.h>
#include <stdint.h>
#include "net/net_types.hpp"

namespace linux95::net::dns {

enum class Status {
    Idle,
    Pending,
    Success,
    InvalidName,
    NotFound,
    ServerFailure,
    MalformedResponse,
    TruncatedResponse,
    CnameLoopOrLimit,
    TimedOut,
    NetworkUnavailable,
    Busy,
};

struct Name {
    uint8_t wire[255];
    uint16_t length;
};

enum class ParseDisposition {
    Ignored,
    Complete,
    Followup,
};

struct ParsedResponse {
    ParseDisposition disposition;
    Status status;
    Name followup_name;
    net::Ipv4Address addresses[8];
    uint8_t address_count;
    Name visited_names[5];
    uint8_t visited_count;
    uint8_t cname_hops;
};

bool encode_hostname(const char* hostname, Name& out);
bool names_equal(const Name& left, const Name& right);
bool decode_name(const uint8_t* message, size_t length,
                 size_t& cursor, Name& out);
bool build_query(uint16_t transaction_id, const Name& question,
                 uint8_t* output, size_t capacity, size_t& output_length);
ParsedResponse parse_response(const uint8_t* message, size_t length,
                              uint16_t expected_id,
                              const Name& expected_question,
                              const Name* visited_names,
                              size_t visited_count);

bool initialize();
Status begin_lookup(const char* hostname);
void poll();
Status lookup_status();
size_t result_count();
bool result_address(size_t index, net::Ipv4Address& out);
net::Ipv4Address server();
Status set_server(const net::Ipv4Address& address);

} // namespace linux95::net::dns
