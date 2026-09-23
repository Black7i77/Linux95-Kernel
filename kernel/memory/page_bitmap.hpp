#pragma once

#include <stdint.h>

namespace linux95::memory {

class PageBitmap {
public:
    static constexpr uint64_t kInvalid = UINT64_MAX;

    void initialize(uint8_t* storage, uint64_t page_count)
    {
        storage_ = storage;
        page_count_ = page_count;
        free_count_ = 0;
        next_hint_ = 0;

        if (storage_ == nullptr || page_count_ == 0) {
            page_count_ = 0;
            return;
        }

        const uint64_t bytes = (page_count_ + 7ULL) / 8ULL;
        for (uint64_t i = 0; i < bytes; ++i) {
            storage_[i] = 0xFFu;
        }
    }

    uint64_t capacity() const { return page_count_; }
    uint64_t free_count() const { return free_count_; }

    bool is_used(uint64_t index) const
    {
        if (storage_ == nullptr || index >= page_count_) {
            return true;
        }
        const uint8_t mask = static_cast<uint8_t>(1u << (index & 7ULL));
        return (storage_[index >> 3] & mask) != 0;
    }

    bool mark_free(uint64_t index)
    {
        if (storage_ == nullptr || index >= page_count_ || !is_used(index)) {
            return false;
        }

        const uint8_t mask = static_cast<uint8_t>(1u << (index & 7ULL));
        storage_[index >> 3] &= static_cast<uint8_t>(~mask);
        ++free_count_;
        if (index < next_hint_) {
            next_hint_ = index;
        }
        return true;
    }

    bool mark_used(uint64_t index)
    {
        if (storage_ == nullptr || index >= page_count_ || is_used(index)) {
            return false;
        }

        const uint8_t mask = static_cast<uint8_t>(1u << (index & 7ULL));
        storage_[index >> 3] |= mask;
        --free_count_;
        if (index == next_hint_) {
            ++next_hint_;
        }
        return true;
    }

    uint64_t allocate()
    {
        if (storage_ == nullptr || free_count_ == 0) {
            return kInvalid;
        }

        for (uint64_t pass = 0; pass < 2; ++pass) {
            const uint64_t begin = pass == 0 ? next_hint_ : 0;
            const uint64_t end = pass == 0 ? page_count_ : next_hint_;
            for (uint64_t index = begin; index < end; ++index) {
                if (!is_used(index)) {
                    mark_used(index);
                    next_hint_ = index + 1;
                    if (next_hint_ >= page_count_) {
                        next_hint_ = 0;
                    }
                    return index;
                }
            }
        }

        return kInvalid;
    }

private:
    uint8_t* storage_ = nullptr;
    uint64_t page_count_ = 0;
    uint64_t free_count_ = 0;
    uint64_t next_hint_ = 0;
};

} // namespace linux95::memory
