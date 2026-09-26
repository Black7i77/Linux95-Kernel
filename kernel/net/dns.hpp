#pragma once

#include <stddef.h>
#include <stdint.h>

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

bool encode_hostname(const char* hostname, Name& out);
bool names_equal(const Name& left, const Name& right);
bool decode_name(const uint8_t* message, size_t length,
                 size_t& cursor, Name& out);
bool build_query(uint16_t transaction_id, const Name& question,
                 uint8_t* output, size_t capacity, size_t& output_length);

} // namespace linux95::net::dns
