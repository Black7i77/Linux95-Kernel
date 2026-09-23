#include "storage/ata.hpp"

#include "arch/io.hpp"
#include "storage/ata_helpers.hpp"

#include <stddef.h>
#include <stdint.h>

namespace linux95::storage::ata {
namespace {

constexpr uint16_t kData = 0x1F0;
constexpr uint16_t kError = 0x1F1;
constexpr uint16_t kSectorCount = 0x1F2;
constexpr uint16_t kLbaLow = 0x1F3;
constexpr uint16_t kLbaMid = 0x1F4;
constexpr uint16_t kLbaHigh = 0x1F5;
constexpr uint16_t kDriveHead = 0x1F6;
constexpr uint16_t kCommandStatus = 0x1F7;
constexpr uint16_t kAltStatus = 0x3F6;

constexpr uint8_t kStatusErr = 0x01;
constexpr uint8_t kStatusDrq = 0x08;
constexpr uint8_t kStatusDf = 0x20;
constexpr uint8_t kStatusBsy = 0x80;

constexpr uint8_t kCmdIdentify = 0xEC;
constexpr uint8_t kCmdRead = 0x20;
constexpr uint8_t kCmdWrite = 0x30;
constexpr uint8_t kCmdFlush = 0xE7;

constexpr uint32_t kPollBudget = 1000000U;

void clear_info(DeviceInfo& info)
{
    info.present = false;
    info.ata_device = false;
    info.lba28_sector_count = 0;
    info.model[0] = '\0';
}

uint8_t drive_bit(Drive drive)
{
    return drive == Drive::Slave ? 0x10U : 0x00U;
}

void delay_400ns()
{
    (void)io::inb(kAltStatus);
    (void)io::inb(kAltStatus);
    (void)io::inb(kAltStatus);
    (void)io::inb(kAltStatus);
}

bool wait_can_accept_command()
{
    for (uint32_t i = 0; i < kPollBudget; ++i) {
        const uint8_t status = io::inb(kCommandStatus);
        if (helpers::status_is_absent(status)) return false;
        if (helpers::status_can_accept_command(status)) return true;
        io::pause();
    }
    return false;
}

bool wait_drq()
{
    for (uint32_t i = 0; i < kPollBudget; ++i) {
        const uint8_t status = io::inb(kCommandStatus);
        if (helpers::status_is_absent(status)) return false;
        if ((status & kStatusBsy) != 0) {
            io::pause();
            continue;
        }
        if (helpers::status_has_error(status)) return false;
        if (helpers::status_data_ready(status)) return true;
        io::pause();
    }
    return false;
}

bool wait_command_complete()
{
    for (uint32_t i = 0; i < kPollBudget; ++i) {
        const uint8_t status = io::inb(kCommandStatus);
        if (helpers::status_is_absent(status)) return false;
        if ((status & kStatusBsy) != 0) {
            io::pause();
            continue;
        }
        if (helpers::status_has_error(status)) return false;
        return true;
    }
    return false;
}

bool wait_write_complete()
{
    for (uint32_t i = 0; i < kPollBudget; ++i) {
        const uint8_t status = io::inb(kCommandStatus);
        if (helpers::status_is_absent(status)) return false;
        if ((status & kStatusBsy) != 0) {
            io::pause();
            continue;
        }
        if (helpers::status_has_error(status)) return false;
        if (helpers::status_write_complete(status)) return true;
        io::pause();
    }
    return false;
}

void select_identify_drive(Drive drive)
{
    io::outb(kDriveHead, static_cast<uint8_t>(0xA0U | drive_bit(drive)));
    delay_400ns();
}

void select_lba28_drive(Drive drive, uint32_t lba)
{
    io::outb(
        kDriveHead,
        static_cast<uint8_t>(
            0xE0U | drive_bit(drive) | ((lba >> 24U) & 0x0FU)));
    delay_400ns();
}

bool program_lba28(Drive drive, uint32_t lba)
{
    select_lba28_drive(drive, lba);
    if (!wait_can_accept_command()) return false;
    io::outb(kSectorCount, 1);
    io::outb(kLbaLow, static_cast<uint8_t>(lba & 0xFFU));
    io::outb(kLbaMid, static_cast<uint8_t>((lba >> 8U) & 0xFFU));
    io::outb(kLbaHigh, static_cast<uint8_t>((lba >> 16U) & 0xFFU));
    return true;
}

} // namespace

bool identify(Drive drive, DeviceInfo& info)
{
    clear_info(info);

    select_identify_drive(drive);
    if (!wait_can_accept_command()) return false;
    io::outb(kSectorCount, 0);
    io::outb(kLbaLow, 0);
    io::outb(kLbaMid, 0);
    io::outb(kLbaHigh, 0);
    io::outb(kCommandStatus, kCmdIdentify);

    uint8_t status = io::inb(kCommandStatus);
    if (helpers::status_is_absent(status)) return false;

    if (!wait_command_complete()) return false;

    const uint8_t signature_mid = io::inb(kLbaMid);
    const uint8_t signature_high = io::inb(kLbaHigh);
    if (signature_mid != 0 || signature_high != 0) return false;

    if (!wait_drq()) return false;

    uint16_t words[256];
    for (size_t i = 0; i < 256; ++i) {
        words[i] = io::inw(kData);
    }

    helpers::decode_model(words, info.model);
    info.lba28_sector_count =
        static_cast<uint32_t>(words[60]) |
        (static_cast<uint32_t>(words[61]) << 16U);
    info.present = true;
    info.ata_device = true;
    return true;
}

bool read_sector(Drive drive, uint32_t lba, uint8_t* buffer)
{
    if (buffer == nullptr || !helpers::valid_lba28(lba)) return false;

    if (!program_lba28(drive, lba)) return false;
    io::outb(kCommandStatus, kCmdRead);

    if (!wait_drq()) return false;

    for (size_t i = 0; i < 256; ++i) {
        const uint16_t word = io::inw(kData);
        buffer[i * 2] = static_cast<uint8_t>(word & 0xFFU);
        buffer[i * 2 + 1] = static_cast<uint8_t>((word >> 8U) & 0xFFU);
    }

    delay_400ns();
    return true;
}

bool flush_cache(Drive drive)
{
    if (!helpers::write_allowed(drive)) return false;

    select_lba28_drive(drive, 0);
    if (!wait_can_accept_command()) return false;
    io::outb(kCommandStatus, kCmdFlush);
    delay_400ns();
    return wait_command_complete();
}

bool write_sector(Drive drive, uint32_t lba, const uint8_t* buffer)
{
    if (buffer == nullptr ||
        !helpers::valid_lba28(lba) ||
        !helpers::write_allowed(drive)) {
        return false;
    }

    if (!program_lba28(drive, lba)) return false;
    io::outb(kCommandStatus, kCmdWrite);

    if (!wait_drq()) return false;

    for (size_t i = 0; i < 256; ++i) {
        const uint16_t word =
            static_cast<uint16_t>(buffer[i * 2]) |
            (static_cast<uint16_t>(buffer[i * 2 + 1]) << 8U);
        io::outw(kData, word);
    }

    delay_400ns();
    if (!wait_write_complete()) return false;
    return flush_cache(drive);
}

} // namespace linux95::storage::ata
