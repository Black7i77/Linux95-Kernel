#include "terminal/filesystem_commands.hpp"

#include "filesystem/vfs.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::terminal {
namespace {

bool space(char c)
{
    return c == ' ' || c == '\t';
}

bool equals(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

char* skip_space(char* text)
{
    while (space(*text)) ++text;
    return text;
}

// Splits one path token, returning the text following its first separator.
char* split_token(char* token)
{
    while (*token != '\0' && !space(*token)) ++token;
    if (*token == '\0') return token;
    *token = '\0';
    return token + 1;
}

const char* reason(filesystem::Status status)
{
    switch (status) {
    case filesystem::Status::NotMounted: return "filesystem not mounted";
    case filesystem::Status::IoError: return "I/O error";
    case filesystem::Status::InvalidFilesystem: return "invalid filesystem";
    case filesystem::Status::NotFound: return "not found";
    case filesystem::Status::NotDirectory: return "not a directory";
    case filesystem::Status::IsDirectory: return "is a directory";
    case filesystem::Status::Corrupt: return "filesystem corrupt";
    case filesystem::Status::Unsupported: return "unsupported operation";
    case filesystem::Status::InvalidDescriptor: return "invalid file descriptor";
    case filesystem::Status::InvalidHandle: return "invalid directory handle";
    case filesystem::Status::TooManyOpenFiles: return "too many open files";
    case filesystem::Status::TooManyOpenDirectories: return "too many open directories";
    case filesystem::Status::AlreadyExists: return "already exists";
    case filesystem::Status::InvalidName: return "invalid 8.3 name or path";
    case filesystem::Status::NoSpace: return "no space";
    case filesystem::Status::DirectoryNotEmpty: return "directory not empty";
    case filesystem::Status::ReadOnly: return "read-only volume";
    case filesystem::Status::Ok: return "";
    }
    return "operation failed";
}

void report(Output& output, const char* name, filesystem::Status status,
            const char* success, size_t bytes = 0)
{
    write(output, name);
    write(output, ": ");
    if (status != filesystem::Status::Ok) {
        write(output, reason(status));
    } else if (bytes != 0 || equals(name, "write")) {
        write(output, "written ");
        write_uint(output, bytes);
        write(output, " bytes");
    } else {
        write(output, success);
    }
    write(output, "\n");
}

void usage(Output& output, const char* form)
{
    write(output, "Usage: ");
    write(output, form);
    write(output, "\n");
}

} // namespace

bool execute_filesystem_command(Output& output, char* command)
{
    if (command == nullptr) return false;

    char* name = skip_space(command);
    char* name_end = name;
    while (*name_end != '\0' && !space(*name_end)) ++name_end;
    const char saved = *name_end;
    *name_end = '\0';
    const bool is_touch = equals(name, "touch");
    const bool is_mkdir = equals(name, "mkdir");
    const bool is_write = equals(name, "write");
    const bool is_rm = equals(name, "rm");
    const bool is_cp = equals(name, "cp");
    const bool is_mv = equals(name, "mv");
    *name_end = saved;
    if (!(is_touch || is_mkdir || is_write || is_rm || is_cp || is_mv)) return false;

    const char* label = is_touch ? "touch" : is_mkdir ? "mkdir" :
                        is_write ? "write" : is_rm ? "rm" : is_cp ? "cp" : "mv";
    const char* form = is_touch ? "touch <path>" : is_mkdir ? "mkdir <path>" :
                       is_write ? "write <path> <data>" : is_rm ? "rm <path>" :
                       is_cp ? "cp <source> <destination>" : "mv <source> <destination>";

    char* first = skip_space(name_end);
    if (*first == '\0') {
        usage(output, form);
        return true;
    }

    char* rest = split_token(first);
    if (is_write) {
        const size_t bytes = [&] {
            size_t length = 0;
            while (rest[length] != '\0') ++length;
            return length;
        }();
        const filesystem::Status status = filesystem::vfs::write_file(
            first, reinterpret_cast<const uint8_t*>(rest), bytes);
        report(output, label, status, "", bytes);
        return true;
    }

    rest = skip_space(rest);
    if (is_cp || is_mv) {
        if (*rest == '\0') {
            usage(output, form);
            return true;
        }
        char* const second = rest;
        rest = skip_space(split_token(second));
        if (*rest != '\0') {
            usage(output, form);
            return true;
        }
        const filesystem::Status status = is_cp ?
            filesystem::vfs::copy_file(first, second) :
            filesystem::vfs::move(first, second);
        report(output, label, status, is_cp ? "copied" : "moved");
        return true;
    }

    if (*rest != '\0') {
        usage(output, form);
        return true;
    }
    const filesystem::Status status = is_touch ? filesystem::vfs::touch(first) :
                                      is_mkdir ? filesystem::vfs::mkdir(first) :
                                                 filesystem::vfs::remove(first);
    report(output, label, status, is_touch ? "file ready" :
           is_mkdir ? "directory created" : "removed");
    return true;
}

} // namespace linux95::terminal
