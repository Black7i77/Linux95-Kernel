#include "process/process.hpp"

namespace linux95::process {

namespace {

constexpr size_t kProcessCapacity = 16;
Process g_processes[kProcessCapacity]{};
uint32_t g_next_pid = 1;

}

void initialize() {
    for (Process& process : g_processes) {
        process = {};
        process.state = State::Unused;
    }
    g_next_pid = 1;
}

Process* allocate() {
    for (Process& process : g_processes) {
        if (process.state == State::Unused) {
            process = {};
            process.pid = g_next_pid++;
            process.state = State::Created;
            return &process;
        }
    }
    return nullptr;
}

Process* find(uint32_t pid) {
    for (Process& process : g_processes) {
        if (process.state != State::Unused && process.pid == pid) {
            return &process;
        }
    }
    return nullptr;
}

void release(Process& process) {
    process = {};
    process.state = State::Unused;
}

size_t capacity() {
    return kProcessCapacity;
}

}
