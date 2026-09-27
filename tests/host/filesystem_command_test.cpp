#include "filesystem/vfs.hpp"
#include "terminal/output.hpp"
#include "terminal/shell_session.hpp"

#include <assert.h>
#include <initializer_list>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace linux95::terminal {
bool execute_filesystem_command(Output& output, char* command);
}

namespace {

using linux95::filesystem::Status;

struct Call {
    const char* operation = nullptr;
    char first[300]{};
    char second[300]{};
    uint8_t data[300]{};
    size_t size = 0;
    size_t count = 0;
    Status result = Status::Ok;
} call;

struct Display {
    char text[1024]{};
    size_t length = 0;
} display;

void put_char(void* context, char c)
{
    auto& out = *static_cast<Display*>(context);
    assert(out.length + 1 < sizeof(out.text));
    out.text[out.length++] = c;
    out.text[out.length] = '\0';
}

linux95::terminal::Output output{&display, put_char, nullptr, nullptr};

void reset(Status result = Status::Ok)
{
    call = {};
    call.result = result;
    display = {};
}

Status record(const char* operation, const char* first, const char* second = nullptr)
{
    call.operation = operation;
    ++call.count;
    strcpy(call.first, first);
    if (second != nullptr) strcpy(call.second, second);
    return call.result;
}

bool run(const char* text)
{
    char command[300];
    strcpy(command, text);
    return linux95::terminal::execute_filesystem_command(output, command);
}

void expect_call(const char* operation, const char* first, const char* second,
                 const char* message)
{
    assert(call.count == 1);
    assert(strcmp(call.operation, operation) == 0);
    assert(strcmp(call.first, first) == 0);
    assert(strcmp(call.second, second) == 0);
    assert(strcmp(display.text, message) == 0);
}

void dispatch(void*, linux95::terminal::Output& out, char* command)
{
    assert(linux95::terminal::execute_filesystem_command(out, command));
}

void test_recognition_and_usage()
{
    for (const char* text : {"", "help", "touchdown a", "cat a", "writefile a b", "ls"}) {
        reset();
        assert(!run(text));
        assert(call.count == 0 && display.length == 0);
    }

    struct Case { const char* command; const char* usage; };
    const Case invalid[] = {
        {"touch", "Usage: touch <path>\n"},
        {"touch a b", "Usage: touch <path>\n"},
        {"mkdir", "Usage: mkdir <path>\n"},
        {"mkdir a b", "Usage: mkdir <path>\n"},
        {"rm", "Usage: rm <path>\n"},
        {"rm a b", "Usage: rm <path>\n"},
        {"cp", "Usage: cp <source> <destination>\n"},
        {"cp a", "Usage: cp <source> <destination>\n"},
        {"cp a b c", "Usage: cp <source> <destination>\n"},
        {"mv", "Usage: mv <source> <destination>\n"},
        {"mv a", "Usage: mv <source> <destination>\n"},
        {"mv a b c", "Usage: mv <source> <destination>\n"},
        {"write", "Usage: write <path> <data>\n"},
    };
    for (const auto& item : invalid) {
        reset();
        assert(run(item.command));
        assert(call.count == 0);
        assert(strcmp(display.text, item.usage) == 0);
    }
}

void test_dispatch_and_payload()
{
    reset(); assert(run("  touch   /A.TXT  "));
    expect_call("touch", "/A.TXT", "", "touch: file ready\n");
    reset(); assert(run("mkdir /DIR"));
    expect_call("mkdir", "/DIR", "", "mkdir: directory created\n");
    reset(); assert(run("rm /A.TXT"));
    expect_call("rm", "/A.TXT", "", "rm: removed\n");
    reset(); assert(run("cp /A.TXT /B.TXT"));
    expect_call("cp", "/A.TXT", "/B.TXT", "cp: copied\n");
    reset(); assert(run("mv /A.TXT /B.TXT"));
    expect_call("mv", "/A.TXT", "/B.TXT", "mv: moved\n");

    reset(); assert(run("write /A.TXT hello,  world! $%"));
    expect_call("write_file", "/A.TXT", "", "write: written 17 bytes\n");
    assert(call.size == 17);
    assert(memcmp(call.data, "hello,  world! $%", 17) == 0);

    reset(); assert(run("write /A.TXT  leading  and trailing  "));
    expect_call("write_file", "/A.TXT", "", "write: written 24 bytes\n");
    assert(call.size == 24);
    assert(memcmp(call.data, " leading  and trailing  ", 24) == 0);

    reset(); assert(run("write /A.TXT"));
    expect_call("write_file", "/A.TXT", "", "write: written 0 bytes\n");
    assert(call.size == 0);
    reset(); assert(run("write /A.TXT   "));
    expect_call("write_file", "/A.TXT", "", "write: written 2 bytes\n");
    assert(call.size == 2 && memcmp(call.data, "  ", 2) == 0);

    reset(); assert(run("write /A.TXT \"quoted\" \\raw"));
    expect_call("write_file", "/A.TXT", "", "write: written 13 bytes\n");
    assert(memcmp(call.data, "\"quoted\" \\raw", 13) == 0);
}

void test_status_messages()
{
    struct Case { Status status; const char* reason; };
    const Case cases[] = {
        {Status::NotMounted, "filesystem not mounted"},
        {Status::IoError, "I/O error"},
        {Status::InvalidFilesystem, "invalid filesystem"},
        {Status::NotFound, "not found"},
        {Status::NotDirectory, "not a directory"},
        {Status::IsDirectory, "is a directory"},
        {Status::Corrupt, "filesystem corrupt"},
        {Status::Unsupported, "unsupported operation"},
        {Status::InvalidDescriptor, "invalid file descriptor"},
        {Status::InvalidHandle, "invalid directory handle"},
        {Status::TooManyOpenFiles, "too many open files"},
        {Status::TooManyOpenDirectories, "too many open directories"},
        {Status::AlreadyExists, "already exists"},
        {Status::InvalidName, "invalid 8.3 name or path"},
        {Status::NoSpace, "no space"},
        {Status::DirectoryNotEmpty, "directory not empty"},
        {Status::ReadOnly, "read-only volume"},
    };
    for (const auto& item : cases) {
        reset(item.status);
        assert(run("touch /A.TXT"));
        assert(call.count == 1);
        char expected[100];
        snprintf(expected, sizeof(expected), "touch: %s\n", item.reason);
        assert(strcmp(display.text, expected) == 0);
    }
}

void test_fixed_shell_capacity()
{
    reset();
    linux95::terminal::ShellSession session(output, nullptr, dispatch);
    session.begin();
    const char* prefix = "write /A.TXT ";
    for (const char* p = prefix; *p; ++p) session.on_char(*p);
    for (size_t i = strlen(prefix); i < 259; ++i) session.on_char('x');
    session.on_char('!'); // The 260th byte is rejected by ShellSession.
    session.on_char('\n');
    assert(call.count == 1 && call.size == 246);
    for (size_t i = 0; i < call.size; ++i) assert(call.data[i] == 'x');
}

} // namespace

namespace linux95::filesystem::vfs {
Status touch(const char* path) { return record("touch", path); }
Status mkdir(const char* path) { return record("mkdir", path); }
Status remove(const char* path) { return record("rm", path); }
Status copy_file(const char* source, const char* destination)
{ return record("cp", source, destination); }
Status move(const char* source, const char* destination)
{ return record("mv", source, destination); }
Status write_file(const char* path, const uint8_t* data, size_t size)
{
    const Status result = record("write_file", path);
    assert(size <= sizeof(call.data));
    call.size = size;
    if (size != 0) memcpy(call.data, data, size);
    return result;
}
} // namespace linux95::filesystem::vfs

int main()
{
    test_recognition_and_usage();
    test_dispatch_and_payload();
    test_status_messages();
    test_fixed_shell_capacity();
    puts("filesystem command tests: PASS");
}
