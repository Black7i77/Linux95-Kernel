#include "terminal/shell.hpp"

#include "arch/io.hpp"
#include "arch/keyboard.hpp"
#include "arch/pit.hpp"
#include "arch/x86_64/control_regs.hpp"
#include "memory/address.hpp"
#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "memory/physical.hpp"
#include "net/network.hpp"
#include "filesystem/filesystem.hpp"
#include "filesystem/vfs.hpp"
#include "storage/disk.hpp"
#include "terminal/output.hpp"
#include "terminal/shell_session.hpp"
#include "terminal/vga_output.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::shell {
namespace {

bool equals(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

struct ParsedCommand {
    char* name;
    char* argument;
};

ParsedCommand parse_command(char* input)
{
    while (*input == ' ') {
        ++input;
    }

    char* const name = input;

    while (*input != '\0' &&
           *input != ' ') {
        ++input;
    }

    if (*input == '\0') {
        return {name, nullptr};
    }

    *input = '\0';
    ++input;

    while (*input == ' ') {
        ++input;
    }

    return {
        name,
        *input != '\0' ? input : nullptr,
    };
}

void print_version(terminal::Output& output)
{
    terminal::write(output, "Linux95 Kernel v1.0 Storage Foundation\n");
    terminal::write(output, "Architecture: x86_64 higher-half\n");
    terminal::write(output, "Kernel: freestanding C++17\n");
}

void print_help(terminal::Output& output)
{
    terminal::write(output, "Commands:\n");
    terminal::write(output, "  help     Show this command list\n");
    terminal::write(output, "  clear    Clear the screen\n");
    terminal::write(output, "  version  Show kernel version\n");
    terminal::write(output, "  mem      Show memory statistics\n");
    terminal::write(output, "  diskinfo Show ATA disk information\n");
    terminal::write(output, "  fsinfo   Show FAT32 filesystem information\n");
    terminal::write(output, "  ls [path] List FAT32 directory\n");
    terminal::write(output, "  cat <path> Read FAT32 file\n");
    terminal::write(output, "  uptime   Show uptime in seconds\n");
    terminal::write(output, "  ip       Show network configuration\n");
    terminal::write(output, "  ping <IPv4 address> Send ICMP Echo Request\n");
    terminal::write(output, "  reboot   Reboot the machine\n");
}

void print_memory(terminal::Output& output)
{
    terminal::write(output, "Total RAM: ");
    terminal::write_uint(output, memory::total_bytes() / (1024u * 1024u));
    terminal::write(output, " MiB\n");

    terminal::write(output, "Usable RAM: ");
    terminal::write_uint(output, memory::usable_bytes() / (1024u * 1024u));
    terminal::write(output, " MiB\n");

    terminal::write(output, "Usable regions: ");
    terminal::write_uint(output, memory::usable_regions());
    output.put_char(output.context, '\n');

    terminal::write(output, "Physical pages: total=");
    terminal::write_uint(output, memory::physical::total_pages());
    terminal::write(output, " used=");
    terminal::write_uint(output, memory::physical::used_pages());
    terminal::write(output, " free=");
    terminal::write_uint(output, memory::physical::free_pages());
    output.put_char(output.context, '\n');

    terminal::write(output, "Page size: ");
    terminal::write_uint(output, memory::kPageSize);
    terminal::write(output, " bytes\n");

    terminal::write(output, "HHDM base: ");
    terminal::write_hex(output, memory::kHhdmBase);
    output.put_char(output.context, '\n');

    terminal::write(output, "CR3: ");
    terminal::write_hex(output, arch::x86_64::read_cr3());
    output.put_char(output.context, '\n');

    terminal::write(output, "Heap used: ");
    terminal::write_uint(output, heap::used_bytes() / 1024u);
    terminal::write(output, " KiB / ");
    terminal::write_uint(output, heap::capacity_bytes() / 1024u);
    terminal::write(output, " KiB\n");
}


void print_disk(terminal::Output& output, const char* label, storage::DiskId id, bool writable)
{
    const storage::ata::DeviceInfo& device = storage::info(id);

    terminal::write(output, label);
    terminal::write(output, ":\n");
    terminal::write(output, "  Present: ");
    terminal::write(output, device.present ? "yes\n" : "no\n");
    terminal::write(output, "  Interface: ATA PIO\n");
    terminal::write(output, "  Mode: LBA28\n");
    terminal::write(output, "  Model: ");
    terminal::write(output, device.present && device.model[0] != '\0' ? device.model : "(none)");
    output.put_char(output.context, '\n');
    terminal::write(output, "  Sectors: ");
    terminal::write_uint(output, device.lba28_sector_count);
    output.put_char(output.context, '\n');
    terminal::write(output, "  Writable: ");
    terminal::write(output, writable ? "yes\n" : "no\n");
}

void print_diskinfo(terminal::Output& output)
{
    print_disk(output, "Boot disk", storage::DiskId::Boot, false);
    output.put_char(output.context, '\n');
    print_disk(output, "Test disk", storage::DiskId::Test, true);
}

void print_fsinfo(terminal::Output& output)
{
    const filesystem::VolumeInfo& info =
        filesystem::volume_info();

    terminal::write(output, "FAT32 filesystem:\n");

    terminal::write(output, "  Mounted: ");
    terminal::write(output, info.mounted ? "yes\n" : "no\n");

    terminal::write(output, "  Device: ATA test/slave disk\n");

    terminal::write(output, "  Bytes/sector: ");
    terminal::write_uint(output, info.bytes_per_sector);
    output.put_char(output.context, '\n');

    terminal::write(output, "  Sectors/cluster: ");
    terminal::write_uint(output, info.sectors_per_cluster);
    output.put_char(output.context, '\n');

    terminal::write(output, "  FAT count: ");
    terminal::write_uint(output, info.fat_count);
    output.put_char(output.context, '\n');

    terminal::write(output, "  Sectors/FAT: ");
    terminal::write_uint(output, info.sectors_per_fat);
    output.put_char(output.context, '\n');

    terminal::write(output, "  Total sectors: ");
    terminal::write_uint(output, info.total_sectors);
    output.put_char(output.context, '\n');

    terminal::write(output, "  Root cluster: ");
    terminal::write_uint(output, info.root_cluster);
    output.put_char(output.context, '\n');

    terminal::write(output, "  Mode: read-only\n");
}

void print_ls_error(
    terminal::Output& output,
    filesystem::Status status)
{
    terminal::write(output, "ls: ");

    if (status == filesystem::Status::NotFound) {
        terminal::write(output, "not found");
    } else if (
        status ==
        filesystem::Status::NotDirectory) {
        terminal::write(output, "not a directory");
    } else if (
        status ==
        filesystem::Status::NotMounted) {
        terminal::write(output, "filesystem not mounted");
    } else if (
        status ==
        filesystem::Status::Corrupt) {
        terminal::write(output, "filesystem corrupt");
    } else if (
        status ==
        filesystem::Status::IoError) {
        terminal::write(output, "I/O error");
    } else if (
        status ==
        filesystem::Status::Unsupported) {
        terminal::write(output, "unsupported path");
    } else if (
        status ==
        filesystem::Status::TooManyOpenDirectories) {
        terminal::write(output, "too many open directories");
    } else if (
        status ==
        filesystem::Status::InvalidHandle) {
        terminal::write(output, "invalid directory handle");
    } else {
        terminal::write(output, "unable to list directory");
    }

    output.put_char(output.context, '\n');
}

void print_ls(
    terminal::Output& output,
    const char* path)
{
    const char* const target =
        path != nullptr ? path : "/";

    filesystem::Status status =
        filesystem::Status::Corrupt;

    const int handle =
        filesystem::vfs::opendir(
            target,
            status);

    if (handle < 0) {
        print_ls_error(output, status);
        return;
    }

    for (;;) {
        filesystem::vfs::DirectoryEntry entry = {};
        bool end = false;

        status =
            filesystem::vfs::readdir(
                handle,
                entry,
                end);

        if (status != filesystem::Status::Ok) {
            filesystem::vfs::closedir(handle);
            print_ls_error(output, status);
            return;
        }

        if (end) {
            break;
        }

        terminal::write(output, entry.name);

        if (entry.is_directory) {
            output.put_char(output.context, '/');
        }

        output.put_char(output.context, '\n');
    }

    status =
        filesystem::vfs::closedir(handle);

    if (status != filesystem::Status::Ok) {
        print_ls_error(output, status);
    }
}

void print_cat_error(
    terminal::Output& output,
    filesystem::Status status)
{
    terminal::write(output, "cat: ");

    if (status == filesystem::Status::NotFound) {
        terminal::write(output, "not found");
    } else if (
        status ==
        filesystem::Status::IsDirectory) {
        terminal::write(output, "is a directory");
    } else if (
        status ==
        filesystem::Status::NotDirectory) {
        terminal::write(output,
            "path component is not a directory");
    } else if (
        status ==
        filesystem::Status::NotMounted) {
        terminal::write(output, "filesystem not mounted");
    } else if (
        status ==
        filesystem::Status::Corrupt) {
        terminal::write(output, "filesystem corrupt");
    } else if (
        status ==
        filesystem::Status::IoError) {
        terminal::write(output, "I/O error");
    } else if (
        status ==
        filesystem::Status::Unsupported) {
        terminal::write(output, "unsupported path");
    } else if (
        status ==
        filesystem::Status::TooManyOpenFiles) {
        terminal::write(output, "too many open files");
    } else if (
        status ==
        filesystem::Status::InvalidDescriptor) {
        terminal::write(output, "invalid file descriptor");
    } else {
        terminal::write(output, "unable to read file");
    }

    output.put_char(output.context, '\n');
}

void print_cat(
    terminal::Output& output,
    const char* path)
{
    if (path == nullptr ||
        path[0] == '\0') {
        terminal::write(output, "Usage: cat <path>\n");
        return;
    }

    filesystem::Status status =
        filesystem::Status::Corrupt;

    const int fd =
        filesystem::vfs::open(
            path,
            status);

    if (fd < 0) {
        print_cat_error(output, status);
        return;
    }

    uint8_t buffer[128];

    for (;;) {
        size_t got = 0;

        status =
            filesystem::vfs::read(
                fd,
                buffer,
                sizeof(buffer),
                got);

        if (status != filesystem::Status::Ok) {
            filesystem::vfs::close(fd);
            print_cat_error(output, status);
            return;
        }

        for (size_t i = 0;
             i < got;
             ++i) {
            output.put_char(output.context,
                static_cast<char>(
                    buffer[i]));
        }

        if (got == 0) {
            break;
        }
    }

    status =
        filesystem::vfs::close(fd);

    if (status != filesystem::Status::Ok) {
        print_cat_error(output, status);
    }
}

void reboot(terminal::Output& output)
{
    terminal::write(output, "Rebooting...\n");
    constexpr uint32_t kWaitLimit = 1000000u;
    for (uint32_t i = 0; i < kWaitLimit; ++i) {
        if ((io::inb(0x64) & 0x02u) == 0) {
            io::outb(0x64, 0xFE);
            for (uint32_t spin = 0; spin < kWaitLimit; ++spin) io::pause();
            terminal::write(output, "Reboot request did not reset the system.\n");
            return;
        }
        io::pause();
    }
    terminal::write(output, "Keyboard controller stayed busy; reboot cancelled.\n");
}

} // namespace

void execute_command(
    terminal::Output& output,
    char* command)
{
    ParsedCommand parsed =
        parse_command(command);

    if (parsed.name[0] == '\0') {
        return;
    }

    if (equals(parsed.name, "help")) { print_help(output); return; }
    if (equals(parsed.name, "clear")) {
        output.clear(output.context);
        return;
    }
    if (equals(parsed.name, "version")) { print_version(output); return; }
    if (equals(parsed.name, "mem")) { print_memory(output); return; }
    if (equals(parsed.name, "diskinfo")) { print_diskinfo(output); return; }
    if (equals(parsed.name, "fsinfo")) { print_fsinfo(output); return; }
    if (equals(parsed.name, "ls")) {
        print_ls(output, parsed.argument);
        return;
    }

    if (equals(parsed.name, "cat")) {
        print_cat(output, parsed.argument);
        return;
    }

    if (equals(parsed.name, "uptime")) {
        terminal::write(output, "Uptime: ");
        terminal::write_uint(output, pit::uptime_seconds());
        terminal::write(output, " seconds\n");
        return;
    }
    if (equals(parsed.name, "reboot")) { reboot(output); return; }

    terminal::write(output, "Unknown command: ");
    terminal::write(output, parsed.name);
    terminal::write(output, "\nType 'help' for commands.\n");
}


namespace {

network::Status network_status(void*)
{
    return network::status();
}

bool network_start_ping(void*, net::Ipv4Address destination)
{
    return network::start_ping(destination);
}

net::icmp::PingResult network_ping_result(void*)
{
    return network::ping_result();
}

void network_clear_ping_result(void*)
{
    network::clear_ping_result();
}

void execute_session_command(
    void*,
    terminal::Output& output,
    char* command)
{
    execute_command(
        output,
        command);
}

} // namespace

[[noreturn]] void run_vga()
{
    terminal::Output output =
        terminal::make_vga_output();

    terminal::NetworkCallbacks network_callbacks{
        nullptr,
        network_status,
        network_start_ping,
        network_ping_result,
        network_clear_ping_result,
    };

    terminal::ShellSession session(
        output,
        nullptr,
        execute_session_command,
        &network_callbacks);

    session.begin();

    for (;;) {
        network::poll();
        (void)session.poll();

        if (!keyboard::has_char()) {
            io::halt();
            continue;
        }

        session.on_char(
            keyboard::read_char());
    }
}

[[noreturn]] void run()
{
    run_vga();
}

} // namespace linux95::shell
