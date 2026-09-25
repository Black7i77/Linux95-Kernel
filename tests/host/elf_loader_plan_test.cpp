#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "user/elf.hpp"

namespace {

constexpr uint64_t kBase = 0x0000400000000000ULL;
constexpr size_t kPhoff = 64;
constexpr size_t kPhentsize = 56;

void put16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}
void put32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}
void put64(std::vector<uint8_t>& data, size_t offset, uint64_t value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::vector<uint8_t> fixture(size_t count = 1) {
    std::vector<uint8_t> data(0x500, 0);
    data[0] = 0x7f; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
    data[4] = 2; data[5] = 1;
    put16(data, 16, 2); put16(data, 18, 62); put32(data, 20, 1);
    put64(data, 24, kBase + 0x123);
    put64(data, 32, kPhoff);
    put16(data, 52, 64); put16(data, 54, kPhentsize);
    put16(data, 56, static_cast<uint16_t>(count));
    for (size_t i = 0; i < count; ++i) {
        const size_t ph = kPhoff + i * kPhentsize;
        put32(data, ph, 1); put32(data, ph + 4, 5);
        put64(data, ph + 8, 0x200 + i * 0x100);
        put64(data, ph + 16, kBase + 0x123 + i * 0x2000);
        put64(data, ph + 32, 0x100);
        put64(data, ph + 40, i == 0 ? 0x2100 : 0x1000);
        put64(data, ph + 48, 0x1000);
    }
    return data;
}

} // namespace

int main() {
    using namespace linux95::user;

    auto unaligned = fixture();
    SegmentPagePlan plans[2]{};
    size_t count = 0;
    assert(build_load_plan(unaligned.data(), unaligned.size(), plans, 2,
                           count) == ElfStatus::Ok);
    assert(count == 1);
    assert(plans[0].first_page == kBase);
    assert(plans[0].page_count == 3);
    assert(plans[0].file_bytes == 0x100);
    assert(plans[0].memory_bytes == 0x2100);
    assert(plans[0].memory_bytes > plans[0].file_bytes);
    assert(plans[0].executable);
    assert(!plans[0].writable);

    auto too_many = fixture(2);
    put64(too_many, kPhoff + kPhentsize + 16, kBase + 0x4000);
    assert(build_load_plan(too_many.data(), too_many.size(), plans, 1, count) ==
           ElfStatus::InvalidSegment);

    auto overlap = fixture(2);
    put64(overlap, kPhoff + kPhentsize + 16, kBase + 0x800);
    assert(build_load_plan(overlap.data(), overlap.size(), plans, 2, count) ==
           ElfStatus::OverlappingSegments);

    return 0;
}
