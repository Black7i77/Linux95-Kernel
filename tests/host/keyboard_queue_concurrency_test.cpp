#include "arch/keyboard_helpers.hpp"

#include <assert.h>
#include <atomic>
#include <stddef.h>
#include <thread>

using namespace linux95::keyboard;

int main()
{
    constexpr size_t kEvents = 500000;
    EventQueue queue;
    std::atomic<bool> producer_done{false};
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        for (size_t i = 0; i < kEvents; ++i) {
            const KeyEvent event{
                KeyCode::Character,
                static_cast<char>('a' + (i % 26)),
                (i & 1U) != 0,
                (i & 2U) != 0,
                true};
            size_t retries = 0;
            while (!queue.push(event)) {
                if (++retries > 100000) {
                    failed.store(true, std::memory_order_relaxed);
                    producer_done.store(true, std::memory_order_release);
                    return;
                }
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    size_t consumed = 0;
    while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
        if (queue.empty()) {
            std::this_thread::yield();
            continue;
        }
        const KeyEvent event = queue.pop();
        if (consumed == kEvents) {
            failed.store(true, std::memory_order_relaxed);
            break;
        }
        if (event.key != KeyCode::Character ||
            event.character != static_cast<char>('a' + (consumed % 26)) ||
            event.ctrl != ((consumed & 1U) != 0) ||
            event.shift != ((consumed & 2U) != 0)) {
            failed.store(true, std::memory_order_relaxed);
            break;
        }
        ++consumed;
    }
    producer.join();

    assert(!failed.load(std::memory_order_relaxed));
    assert(consumed == kEvents);
    assert(queue.empty());
    return 0;
}
