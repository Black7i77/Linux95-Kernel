#include "terminal/shell.hpp"

#include "arch/io.hpp"
#include "arch/keyboard.hpp"
#include "arch/pit.hpp"
#include "arch/x86_64/control_regs.hpp"
#include "memory/address.hpp"
#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "memory/physical.hpp"
#include "filesystem/filesystem.hpp"
#include "storage/disk.hpp"
#include "terminal/vga.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::shell {
namespace {

constexpr size_t kCommandCapacity = 64;

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

void prompt()
{
    vga::set_color(10, 0);
    vga::write("linux95");
    vga::set_color(7, 0);
    vga::write("> ");
}

void print_version()
{
    vga::write("Linux95 Kernel v1.0 Storage Foundation\n");
    vga::write("Architecture: x86_64 higher-half\n");
    vga::write("Kernel: freestanding C++17\n");
}

void print_help()
{
    vga::write("Commands:\n");
    vga::write("  help     Show this command list\n");
    vga::write("  clear    Clear the screen\n");
    vga::write("  version  Show kernel version\n");
    vga::write("  mem      Show memory statistics\n");
    vga::write("  diskinfo Show ATA disk information\n");
    vga::write("  fsinfo   Show FAT32 filesystem information\n");
    vga::write("  ls [path] List FAT32 directory\n");
    vga::write("  cat <path> Read FAT32 file\n");
    vga::write("  uptime   Show uptime in seconds\n");
    vga::write("  reboot   Reboot the machine\n");
}

void print_memory()
{
    vga::write("Total RAM: ");
    vga::write_uint(memory::total_bytes() / (1024u * 1024u));
    vga::write(" MiB\n");

    vga::write("Usable RAM: ");
    vga::write_uint(memory::usable_bytes() / (1024u * 1024u));
    vga::write(" MiB\n");

    vga::write("Usable regions: ");
    vga::write_uint(memory::usable_regions());
    vga::put_char('\n');

    vga::write("Physical pages: total=");
    vga::write_uint(memory::physical::total_pages());
    vga::write(" used=");
    vga::write_uint(memory::physical::used_pages());
    vga::write(" free=");
    vga::write_uint(memory::physical::free_pages());
    vga::put_char('\n');

    vga::write("Page size: ");
    vga::write_uint(memory::kPageSize);
    vga::write(" bytes\n");

    vga::write("HHDM base: ");
    vga::write_hex(memory::kHhdmBase);
    vga::put_char('\n');

    vga::write("CR3: ");
    vga::write_hex(arch::x86_64::read_cr3());
    vga::put_char('\n');

    vga::write("Heap used: ");
    vga::write_uint(heap::used_bytes() / 1024u);
    vga::write(" KiB / ");
    vga::write_uint(heap::capacity_bytes() / 1024u);
    vga::write(" KiB\n");
}


void print_disk(const char* label, storage::DiskId id, bool writable)
{
    const storage::ata::DeviceInfo& device = storage::info(id);

    vga::write(label);
    vga::write(":\n");
    vga::write("  Present: ");
    vga::write(device.present ? "yes\n" : "no\n");
    vga::write("  Interface: ATA PIO\n");
    vga::write("  Mode: LBA28\n");
    vga::write("  Model: ");
    vga::write(device.present && device.model[0] != '\0' ? device.model : "(none)");
    vga::put_char('\n');
    vga::write("  Sectors: ");
    vga::write_uint(device.lba28_sector_count);
    vga::put_char('\n');
    vga::write("  Writable: ");
    vga::write(writable ? "yes\n" : "no\n");
}

void print_diskinfo()
{
    print_disk("Boot disk", storage::DiskId::Boot, false);
    vga::put_char('\n');
    print_disk("Test disk", storage::DiskId::Test, true);
}

void print_fsinfo()
{
    const filesystem::VolumeInfo& info =
        filesystem::volume_info();

    vga::write("FAT32 filesystem:\n");

    vga::write("  Mounted: ");
    vga::write(info.mounted ? "yes\n" : "no\n");

    vga::write("  Device: ATA test/slave disk\n");

    vga::write("  Bytes/sector: ");
    vga::write_uint(info.bytes_per_sector);
    vga::put_char('\n');

    vga::write("  Sectors/cluster: ");
    vga::write_uint(info.sectors_per_cluster);
    vga::put_char('\n');

    vga::write("  FAT count: ");
    vga::write_uint(info.fat_count);
    vga::put_char('\n');

    vga::write("  Sectors/FAT: ");
    vga::write_uint(info.sectors_per_fat);
    vga::put_char('\n');

    vga::write("  Total sectors: ");
    vga::write_uint(info.total_sectors);
    vga::put_char('\n');

    vga::write("  Root cluster: ");
    vga::write_uint(info.root_cluster);
    vga::put_char('\n');

    vga::write("  Mode: read-only\n");
}

bool print_entry(
    const filesystem::Entry& entry,
    void*)
{
    vga::write(entry.name);

    if (entry.is_directory) {
        vga::put_char('/');
    }

    vga::put_char('\n');
    return true;
}

void print_ls(const char* path)
{
    const char* const target =
        path != nullptr ? path : "/";

    const filesystem::Status status =
        filesystem::list_directory(
            target,
            print_entry,
            nullptr);

    if (status == filesystem::Status::Ok) {
        return;
    }

    vga::write("ls: ");

    if (status == filesystem::Status::NotFound) {
        vga::write("not found");
    } else if (status == filesystem::Status::NotDirectory) {
        vga::write("not a directory");
    } else if (status == filesystem::Status::NotMounted) {
        vga::write("filesystem not mounted");
    } else if (status == filesystem::Status::Corrupt) {
        vga::write("filesystem corrupt");
    } else if (status == filesystem::Status::IoError) {
        vga::write("I/O error");
    } else {
        vga::write("unable to list directory");
    }

    vga::put_char('\n');
}

void print_cat(const char* path)
{
    if (path == nullptr ||
        path[0] == '\0') {
        vga::write("Usage: cat <path>\n");
        return;
    }

    uint8_t buffer[128];
    uint32_t offset = 0;

    for (;;) {
        size_t got = 0;
        uint32_t size = 0;

        const filesystem::Status status =
            filesystem::read_file(
                path,
                offset,
                buffer,
                sizeof(buffer),
                got,
                size);

        if (status != filesystem::Status::Ok) {
            vga::write("cat: ");

            if (status == filesystem::Status::NotFound) {
                vga::write("not found");
            } else if (status == filesystem::Status::IsDirectory) {
                vga::write("is a directory");
            } else if (status == filesystem::Status::NotDirectory) {
                vga::write("path component is not a directory");
            } else if (status == filesystem::Status::NotMounted) {
                vga::write("filesystem not mounted");
            } else if (status == filesystem::Status::Corrupt) {
                vga::write("filesystem corrupt");
            } else if (status == filesystem::Status::IoError) {
                vga::write("I/O error");
            } else {
                vga::write("unable to read file");
            }

            vga::put_char('\n');
            return;
        }

        for (size_t i = 0;
             i < got;
             ++i) {
            vga::put_char(
                static_cast<char>(
                    buffer[i]));
        }

        offset +=
            static_cast<uint32_t>(got);

        if (got == 0 ||
            offset >= size) {
            break;
        }
    }
}

void reboot()
{
    vga::write("Rebooting...\n");
    constexpr uint32_t kWaitLimit = 1000000u;
    for (uint32_t i = 0; i < kWaitLimit; ++i) {
        if ((io::inb(0x64) & 0x02u) == 0) {
            io::outb(0x64, 0xFE);
            for (uint32_t spin = 0; spin < kWaitLimit; ++spin) io::pause();
            vga::write("Reboot request did not reset the system.\n");
            return;
        }
        io::pause();
    }
    vga::write("Keyboard controller stayed busy; reboot cancelled.\n");
}

void execute(char* command)
{
    ParsedCommand parsed =
        parse_command(command);

    if (parsed.name[0] == '\0') {
        return;
    }

    if (equals(parsed.name, "help")) { print_help(); return; }
    if (equals(parsed.name, "clear")) { vga::clear(); return; }
    if (equals(parsed.name, "version")) { print_version(); return; }
    if (equals(parsed.name, "mem")) { print_memory(); return; }
    if (equals(parsed.name, "diskinfo")) { print_diskinfo(); return; }
    if (equals(parsed.name, "fsinfo")) { print_fsinfo(); return; }
    if (equals(parsed.name, "ls")) {
        print_ls(parsed.argument);
        return;
    }

    if (equals(parsed.name, "cat")) {
        print_cat(parsed.argument);
        return;
    }

    if (equals(parsed.name, "uptime")) {
        vga::write("Uptime: ");
        vga::write_uint(pit::uptime_seconds());
        vga::write(" seconds\n");
        return;
    }
    if (equals(parsed.name, "reboot")) { reboot(); return; }

    vga::write("Unknown command: ");
    vga::write(parsed.name);
    vga::write("\nType 'help' for commands.\n");
}

} // namespace

[[noreturn]] void run()
{
    char command[kCommandCapacity];
    size_t length = 0;
    prompt();

    for (;;) {
        if (!keyboard::has_char()) {
            io::halt();
            continue;
        }

        const char c = keyboard::read_char();
        if (c == '\n') {
            vga::put_char('\n');
            command[length] = '\0';
            execute(command);
            length = 0;
            prompt();
            continue;
        }
        if (c == '\b') {
            if (length > 0) {
                --length;
                vga::put_char('\b');
            }
            continue;
        }
        if (c >= 32 && c <= 126 && length + 1 < kCommandCapacity) {
            command[length++] = c;
            vga::put_char(c);
        }
    }
}

} // namespace linux95::shell
