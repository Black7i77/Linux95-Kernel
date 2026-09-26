#include "filesystem/fat32_write.hpp"

#include "filesystem/fat32.hpp"
#include "filesystem/fat32_helpers.hpp"
#include "filesystem/vfs.hpp"
#include "storage/disk.hpp"

namespace linux95::filesystem::fat32::write {
namespace {

bool valid_cluster(const helpers::BpbGeometry& g, uint32_t cluster)
{
    return cluster >= 2 && cluster - 2u < g.cluster_count;
}

Status geometry_for_write(helpers::BpbGeometry& g)
{
    if (!mounted_geometry(g)) return Status::NotMounted;
    if (!g.mirroring_enabled) return Status::Unsupported;
    return Status::Ok;
}

bool entry_location(const helpers::BpbGeometry& g, uint32_t cluster,
                    uint32_t copy, uint32_t& lba, uint16_t& inside)
{
    if (!valid_cluster(g, cluster) || copy >= g.fat_count) return false;
    const uint32_t sector_offset = cluster / 128u;
    if (sector_offset >= g.sectors_per_fat) return false;
    inside = static_cast<uint16_t>((cluster % 128u) * 4u);
    uint32_t copy_offset = 0;
    uint32_t fat_start = 0;
    return helpers::checked_mul_u32(copy, g.sectors_per_fat, copy_offset) &&
           helpers::checked_add_u32(g.fat_begin_lba, copy_offset, fat_start) &&
           helpers::checked_add_u32(fat_start, sector_offset, lba) &&
           lba < g.first_data_lba && lba < g.total_sectors;
}

void put32(uint8_t* p, uint16_t offset, uint32_t value)
{
    for (uint32_t i = 0; i < 4; ++i)
        p[offset + i] = static_cast<uint8_t>(value >> (8u * i));
}

Status read_entry(const helpers::BpbGeometry& g, uint32_t cluster,
                  uint32_t copy, uint32_t& raw)
{
    uint32_t lba = 0;
    uint16_t inside = 0;
    if (!entry_location(g, cluster, copy, lba, inside)) return Status::Corrupt;
    uint8_t sector[512];
    if (!storage::read_sector(storage::DiskId::Test, lba, sector))
        return Status::IoError;
    raw = helpers::le32(sector + inside);
    return Status::Ok;
}

Status write_raw(const helpers::BpbGeometry& g, uint32_t cluster,
                 uint32_t copy, uint32_t raw)
{
    uint32_t lba = 0;
    uint16_t inside = 0;
    if (!entry_location(g, cluster, copy, lba, inside)) return Status::Corrupt;
    uint8_t sector[512];
    if (!storage::read_sector(storage::DiskId::Test, lba, sector))
        return Status::IoError;
    put32(sector, inside, raw);
    return storage::write_sector(storage::DiskId::Test, lba, sector)
        ? Status::Ok : Status::IoError;
}

Status chain_next(const helpers::BpbGeometry& g, uint32_t cluster,
                  uint32_t& next, bool& end, bool check_copies = false)
{
    uint32_t raw = 0;
    const Status status = read_entry(g, cluster, 0, raw);
    if (status != Status::Ok) return status;
    const uint32_t value = helpers::fat28(raw);
    if (check_copies) {
        for (uint32_t copy = 1; copy < g.fat_count; ++copy) {
            uint32_t mirrored = 0;
            const Status read = read_entry(g, cluster, copy, mirrored);
            if (read != Status::Ok) return read;
            if (helpers::fat28(mirrored) != value) return Status::Corrupt;
        }
    }
    const auto kind = helpers::classify_fat_entry(value);
    if (kind == helpers::FatEntryKind::EndOfChain) {
        end = true; next = 0; return Status::Ok;
    }
    if (kind != helpers::FatEntryKind::Next || !valid_cluster(g, value))
        return Status::Corrupt;
    end = false; next = value; return Status::Ok;
}

bool directory_lba(const helpers::BpbGeometry& g, uint32_t cluster,
                   uint32_t sector_index, uint32_t& lba)
{
    uint32_t first = 0;
    return valid_cluster(g, cluster) && sector_index < g.sectors_per_cluster &&
        helpers::cluster_to_lba(g.first_data_lba, g.sectors_per_cluster,
                                cluster, first) &&
        helpers::checked_add_u32(first, sector_index, lba) &&
        lba < g.total_sectors;
}

bool valid_slot(const helpers::BpbGeometry& g, const DirectorySlot& slot)
{
    return slot.lba >= g.first_data_lba && slot.lba < g.total_sectors &&
        slot.offset < 512 && slot.offset % 32 == 0;
}

bool live_short_entry(const uint8_t* entry)
{
    return entry[0] != 0 && entry[0] != 0xe5 &&
        entry[11] != 0x0f && (entry[11] & 0x08) == 0;
}

Status find_in_directory(const helpers::BpbGeometry& g, uint32_t first_cluster,
                         const uint8_t name[11], DirectorySlot& found_slot,
                         uint8_t found_entry[32], DirectorySlot* free_slot,
                         uint32_t* tail, bool* free_is_end = nullptr)
{
    if (!valid_cluster(g, first_cluster)) return Status::Corrupt;
    bool have_free = false;
    uint32_t cluster = first_cluster;
    for (uint32_t walked = 0; walked < g.cluster_count; ++walked) {
        uint32_t next = 0;
        bool end = false;
        const Status step = chain_next(g, cluster, next, end);
        if (step != Status::Ok) return step;
        for (uint32_t sector_index = 0; sector_index < g.sectors_per_cluster;
             ++sector_index) {
            uint32_t lba = 0;
            if (!directory_lba(g, cluster, sector_index, lba)) return Status::Corrupt;
            uint8_t sector[512];
            if (!storage::read_sector(storage::DiskId::Test, lba, sector))
                return Status::IoError;
            for (uint16_t offset = 0; offset < 512; offset += 32) {
                const uint8_t* entry = sector + offset;
                if ((entry[0] == 0 || entry[0] == 0xe5) && !have_free) {
                    if (free_slot != nullptr) *free_slot = {lba, offset};
                    if (free_is_end != nullptr) *free_is_end = entry[0] == 0;
                    have_free = true;
                }
                if (entry[0] == 0) {
                    if (tail != nullptr) *tail = cluster;
                    return Status::NotFound;
                }
                if (live_short_entry(entry)) {
                    bool equal = true;
                    for (uint32_t i = 0; i < 11; ++i)
                        if (static_cast<uint8_t>(helpers::ascii_upper(
                                static_cast<char>(entry[i]))) != name[i]) equal = false;
                    if (equal) {
                        found_slot = {lba, offset};
                        for (uint32_t i = 0; i < 32; ++i) found_entry[i] = entry[i];
                        return Status::Ok;
                    }
                }
            }
        }
        if (end) {
            if (tail != nullptr) *tail = cluster;
            return Status::NotFound;
        }
        cluster = next;
    }
    return Status::Corrupt;
}

bool encode_component(const char* name, uint8_t encoded[11])
{
    if (name == nullptr) return false;
    size_t length = 0;
    while (length < 13 && name[length] != 0) ++length;
    return length < 13 && helpers::encode_short_name(name, length, encoded);
}

void short_component(const uint8_t encoded[11], char name[13])
{
    uint32_t length = 0;
    for (uint32_t i = 0; i < 8 && encoded[i] != ' '; ++i)
        name[length++] = static_cast<char>(encoded[i]);
    if (encoded[8] != ' ') {
        name[length++] = '.';
        for (uint32_t i = 8; i < 11 && encoded[i] != ' '; ++i)
            name[length++] = static_cast<char>(encoded[i]);
    }
    name[length] = 0;
}

Status put_directory_entry(const helpers::BpbGeometry& g,
                           const DirectorySlot& slot, const uint8_t entry[32],
                           bool* unchanged)
{
    if (unchanged != nullptr) *unchanged = false;
    if (!valid_slot(g, slot) || entry == nullptr) return Status::Corrupt;
    uint8_t sector[512];
    if (!storage::read_sector(storage::DiskId::Test, slot.lba, sector))
        return Status::IoError;
    uint8_t previous[32];
    for (uint32_t i = 0; i < 32; ++i) {
        previous[i] = sector[slot.offset + i];
        sector[slot.offset + i] = entry[i];
    }
    if (storage::write_sector(storage::DiskId::Test, slot.lba, sector)) return Status::Ok;
    if (unchanged != nullptr) {
        uint8_t observed[512];
        if (storage::read_sector(storage::DiskId::Test, slot.lba, observed)) {
            *unchanged = true;
            for (uint32_t i = 0; i < 32; ++i)
                if (observed[slot.offset + i] != previous[i]) *unchanged = false;
        }
    }
    return Status::IoError;
}

Status clear_end_marker(const helpers::BpbGeometry& g, const DirectorySlot& slot)
{
    if (!valid_slot(g, slot)) return Status::Corrupt;
    uint8_t sector[512];
    if (!storage::read_sector(storage::DiskId::Test, slot.lba, sector))
        return Status::IoError;
    if (sector[slot.offset] == 0) return Status::Ok;
    sector[slot.offset] = 0;
    return storage::write_sector(storage::DiskId::Test, slot.lba, sector)
        ? Status::Ok : Status::IoError;
}

void reclaim_if_unlinked(const helpers::BpbGeometry& g, uint32_t tail,
                         uint32_t fresh)
{
    for (uint32_t copy = 0; copy < g.fat_count; ++copy) {
        uint32_t raw = 0;
        if (read_entry(g, tail, copy, raw) != Status::Ok ||
            !helpers::is_eoc(raw)) return;
    }
    (void)free_chain(fresh);
}

uint32_t first_cluster(const uint8_t entry[32])
{
    return (static_cast<uint32_t>(helpers::le16(entry + 20) & 0x0fffu) << 16) |
           helpers::le16(entry + 26);
}

void set_first_cluster(uint8_t entry[32], uint32_t cluster)
{
    entry[20] = static_cast<uint8_t>(cluster >> 16);
    entry[21] = static_cast<uint8_t>(cluster >> 24);
    entry[26] = static_cast<uint8_t>(cluster);
    entry[27] = static_cast<uint8_t>(cluster >> 8);
}

bool is_dot_entry(const uint8_t entry[32])
{
    if ((entry[11] & 0x10u) == 0) return false;
    const bool one_dot = entry[0] == '.' && entry[1] == ' ';
    const bool two_dots = entry[0] == '.' && entry[1] == '.';
    if (!one_dot && !two_dots) return false;
    for (uint32_t i = 2; i < 11; ++i)
        if (entry[i] != ' ') return false;
    return true;
}

Status directory_is_empty(const helpers::BpbGeometry& g, uint32_t first)
{
    if (!valid_cluster(g, first)) return Status::Corrupt;
    uint32_t cluster = first;
    bool past_end_marker = false;
    for (uint32_t walked = 0; walked < g.cluster_count; ++walked) {
        uint32_t next = 0;
        bool end = false;
        const Status step = chain_next(g, cluster, next, end, true);
        if (step != Status::Ok) return step;
        if (!past_end_marker) {
            for (uint32_t sector_index = 0; sector_index < g.sectors_per_cluster;
                 ++sector_index) {
                uint32_t lba = 0;
                if (!directory_lba(g, cluster, sector_index, lba)) return Status::Corrupt;
                uint8_t sector[512];
                if (!storage::read_sector(storage::DiskId::Test, lba, sector))
                    return Status::IoError;
                for (uint32_t offset = 0; offset < 512; offset += 32) {
                    const uint8_t* entry = sector + offset;
                    if (entry[0] == 0) { past_end_marker = true; break; }
                    if (entry[0] != 0xe5 && !is_dot_entry(entry))
                        return Status::DirectoryNotEmpty;
                }
                if (past_end_marker) break;
            }
        }
        if (end) return Status::Ok;
        cluster = next;
    }
    return Status::Corrupt;
}

Status validate_file_chain(const helpers::BpbGeometry& g,
                           const uint8_t entry[32], uint32_t first)
{
    uint32_t cluster_bytes = 0;
    if (!helpers::checked_mul_u32(g.sectors_per_cluster, 512u, cluster_bytes))
        return Status::Corrupt;
    uint32_t count = 0;
    const Status size = file_cluster_count(helpers::le32(entry + 28),
                                           cluster_bytes, count);
    if (size != Status::Ok) return size;
    if ((count == 0) != (first == 0) || count > g.cluster_count ||
        (first != 0 && !valid_cluster(g, first))) return Status::Corrupt;
    uint32_t cluster = first;
    for (uint32_t walked = 0; walked < count; ++walked) {
        uint32_t next = 0;
        bool end = false;
        const Status step = chain_next(g, cluster, next, end, true);
        if (step != Status::Ok) return step;
        if (end != (walked + 1 == count)) return Status::Corrupt;
        cluster = next;
    }
    return Status::Ok;
}

} // namespace

Status touch(const char* path)
{
    ResolvedPath resolved = {};
    const Status status = resolve_path(path, resolved);
    if (status != Status::Ok) return status;
    if (resolved.exists)
        return (resolved.entry[11] & 0x10u) != 0 ? Status::IsDirectory : Status::Ok;
    uint8_t entry[32] = {};
    entry[11] = 0x20;
    DirectorySlot slot = {};
    char name[13];
    short_component(resolved.name, name);
    return create_directory_entry(resolved.parent_cluster, name, entry, slot);
}

Status mkdir(const char* path)
{
    ResolvedPath resolved = {};
    Status status = resolve_path(path, resolved);
    if (status != Status::Ok) return status;
    if (resolved.exists) return Status::AlreadyExists;
    helpers::BpbGeometry g = {};
    status = geometry_for_write(g);
    if (status != Status::Ok) return status;

    uint32_t fresh = 0;
    status = allocate_chain(1, fresh);
    if (status != Status::Ok) return status;

    uint8_t sector[512];
    volatile uint8_t* clear = sector;
    for (uint32_t i = 0; i < 512; ++i) clear[i] = 0;
    for (uint32_t i = 0; i < g.sectors_per_cluster; ++i) {
        uint32_t lba = 0;
        if (!directory_lba(g, fresh, i, lba)) {
            status = Status::Corrupt;
            goto preparation_failure;
        }
        if (!storage::write_sector(storage::DiskId::Test, lba, sector)) {
            status = Status::IoError;
            goto preparation_failure;
        }
    }

    for (uint32_t i = 0; i < 11; ++i) {
        sector[i] = ' ';
        sector[32 + i] = ' ';
    }
    sector[0] = '.';
    sector[11] = 0x10;
    set_first_cluster(sector, fresh);
    sector[32] = '.';
    sector[33] = '.';
    sector[32 + 11] = 0x10;
    set_first_cluster(sector + 32, resolved.parent_cluster == g.root_cluster
                                          ? 0 : resolved.parent_cluster);
    {
        uint32_t lba = 0;
        if (!directory_lba(g, fresh, 0, lba)) {
            status = Status::Corrupt;
            goto preparation_failure;
        }
        if (!storage::write_sector(storage::DiskId::Test, lba, sector)) {
            status = Status::IoError;
            goto preparation_failure;
        }
    }

    {
        uint8_t entry[32] = {};
        entry[11] = 0x10;
        set_first_cluster(entry, fresh);
        char name[13];
        short_component(resolved.name, name);
        DirectorySlot slot = {};
        status = create_directory_entry(resolved.parent_cluster, name, entry, slot);
        if (status != Status::Ok) {
            ResolvedPath observed = {};
            if (resolve_path(path, observed) == Status::Ok && !observed.exists &&
                free_chain(fresh) != Status::Ok) return Status::IoError;
            return status;
        }
    }
    return Status::Ok;

preparation_failure:
    return free_chain(fresh) == Status::Ok ? status : Status::IoError;
}

Status remove(const char* path)
{
    ResolvedPath resolved = {};
    Status status = resolve_path(path, resolved);
    if (status != Status::Ok) return status;
    if (!resolved.exists) return Status::NotFound;
    helpers::BpbGeometry g = {};
    status = geometry_for_write(g);
    if (status != Status::Ok) return status;

    const uint32_t first = first_cluster(resolved.entry);
    if ((resolved.entry[11] & 0x10u) != 0) {
        status = directory_is_empty(g, first);
    } else {
        status = validate_file_chain(g, resolved.entry, first);
    }
    if (status != Status::Ok) return status;
    status = delete_directory_entry(resolved.slot);
    if (status != Status::Ok) return status;
    return first == 0 || free_chain(first) == Status::Ok
        ? Status::Ok : Status::IoError;
}

Status write_file(const char* path, const uint8_t* data, size_t size)
{
    if (size > 0xffffffffull) return Status::Unsupported;
    if (size != 0 && data == nullptr) return Status::Unsupported;
    ResolvedPath resolved = {};
    Status status = resolve_path(path, resolved);
    if (status != Status::Ok) return status;
    if (resolved.exists && (resolved.entry[11] & 0x10u) != 0)
        return Status::IsDirectory;
    helpers::BpbGeometry g = {};
    status = geometry_for_write(g);
    if (status != Status::Ok) return status;
    uint32_t cluster_bytes = 0;
    if (!helpers::checked_mul_u32(g.sectors_per_cluster, 512u, cluster_bytes))
        return Status::Corrupt;
    uint32_t clusters = 0;
    status = file_cluster_count(size, cluster_bytes, clusters);
    if (status != Status::Ok) return status;

    const uint32_t old_cluster = resolved.exists
        ? (static_cast<uint32_t>(helpers::le16(resolved.entry + 20) & 0x0fffu) << 16) |
          helpers::le16(resolved.entry + 26) : 0;
    if (resolved.exists) {
        const uint32_t old_size = helpers::le32(resolved.entry + 28);
        uint32_t old_clusters = 0;
        status = file_cluster_count(old_size, cluster_bytes, old_clusters);
        if (status != Status::Ok) return status;
        if ((old_clusters == 0) != (old_cluster == 0) ||
            old_clusters > g.cluster_count ||
            (old_cluster != 0 && !valid_cluster(g, old_cluster)))
            return Status::Corrupt;
        uint32_t current = old_cluster;
        for (uint32_t walked = 0; walked < old_clusters; ++walked) {
            uint32_t next = 0;
            bool end = false;
            status = chain_next(g, current, next, end, true);
            if (status != Status::Ok) return status;
            if (end != (walked + 1 == old_clusters)) return Status::Corrupt;
            current = next;
        }
    }

    uint32_t fresh = 0;
    status = allocate_chain(clusters, fresh);
    if (status != Status::Ok) return status;
    uint32_t remaining = static_cast<uint32_t>(size);
    uint32_t cluster = fresh;
    size_t consumed = 0;
    for (uint32_t index = 0; index < clusters; ++index) {
        for (uint32_t sector_index = 0; sector_index < g.sectors_per_cluster && remaining != 0;
             ++sector_index) {
            uint32_t lba = 0;
            if (!directory_lba(g, cluster, sector_index, lba)) {
                status = Status::Corrupt;
                goto unpublished_failure;
            }
            uint8_t sector[512];
            const uint32_t take = remaining < 512u ? remaining : 512u;
            for (uint32_t byte = 0; byte < take; ++byte)
                sector[byte] = data[consumed + byte];
            volatile uint8_t* clear = sector;
            for (uint32_t byte = take; byte < 512u; ++byte) clear[byte] = 0;
            if (!storage::write_sector(storage::DiskId::Test, lba, sector)) {
                status = Status::IoError;
                goto unpublished_failure;
            }
            consumed += take;
            remaining -= take;
        }
        if (index + 1 < clusters) {
            uint32_t next = 0;
            bool end = false;
            status = chain_next(g, cluster, next, end);
            if (status != Status::Ok) goto unpublished_failure;
            if (end) { status = Status::Corrupt; goto unpublished_failure; }
            cluster = next;
        }
    }
    if (remaining != 0) { status = Status::Corrupt; goto unpublished_failure; }

    if (resolved.exists) {
        status = update_directory_entry(resolved.slot, resolved.name, fresh,
                                        static_cast<uint32_t>(size));
        if (status != Status::Ok) {
            uint8_t observed[32] = {};
            if (read_directory_entry(resolved.slot, observed) == Status::Ok) {
                bool unchanged = true;
                for (uint32_t i = 0; i < 32; ++i)
                    if (observed[i] != resolved.entry[i]) unchanged = false;
                if (unchanged && free_chain(fresh) != Status::Ok) return Status::IoError;
            }
            return status;
        }
    } else {
        uint8_t entry[32] = {};
        for (uint32_t i = 0; i < 11; ++i) entry[i] = resolved.name[i];
        entry[11] = 0x20;
        entry[20] = static_cast<uint8_t>(fresh >> 16);
        entry[21] = static_cast<uint8_t>(fresh >> 24);
        entry[26] = static_cast<uint8_t>(fresh);
        entry[27] = static_cast<uint8_t>(fresh >> 8);
        put32(entry, 28, static_cast<uint32_t>(size));
        char name[13];
        short_component(resolved.name, name);
        DirectorySlot slot = {};
        status = create_directory_entry(resolved.parent_cluster, name, entry, slot);
        if (status != Status::Ok) {
            ResolvedPath observed = {};
            if (resolve_path(path, observed) == Status::Ok && !observed.exists &&
                free_chain(fresh) != Status::Ok) return Status::IoError;
            return status;
        }
    }
    return old_cluster == 0 || free_chain(old_cluster) == Status::Ok
        ? Status::Ok : Status::IoError;

unpublished_failure:
    if (fresh != 0 && free_chain(fresh) != Status::Ok) return Status::IoError;
    return status;
}

Status resolve_path(const char* path, ResolvedPath& result)
{
    result = {};
    if (path == nullptr) return Status::InvalidName;
    size_t length = 0;
    while (length < vfs::kPathCapacity && path[length] != 0) ++length;
    if (length == 0 || length == vfs::kPathCapacity) return Status::InvalidName;
    size_t begin = path[0] == '/' ? 1 : 0;
    if (begin == length) return Status::InvalidName;
    // Validate the complete path before any I/O or dependent mutation.
    for (size_t start = begin; start < length;) {
        size_t end = start;
        while (end < length && path[end] != '/') ++end;
        uint8_t ignored[11];
        if (!helpers::encode_short_name(path + start, end - start, ignored))
            return Status::InvalidName;
        start = end + 1;
        if (end + 1 == length) return Status::InvalidName;
    }
    helpers::BpbGeometry g = {};
    const Status ready = geometry_for_write(g);
    if (ready != Status::Ok) return ready;
    uint32_t parent = g.root_cluster;
    for (size_t start = begin; start < length;) {
        size_t end = start;
        while (end < length && path[end] != '/') ++end;
        uint8_t encoded[11];
        (void)helpers::encode_short_name(path + start, end - start, encoded);
        DirectorySlot slot = {};
        uint8_t entry[32] = {};
        const Status found = find_in_directory(g, parent, encoded, slot, entry,
                                                nullptr, nullptr);
        if (end == length) {
            if (found != Status::Ok && found != Status::NotFound) return found;
            result.parent_cluster = parent;
            for (uint32_t i = 0; i < 11; ++i) result.name[i] = encoded[i];
            result.exists = found == Status::Ok;
            if (result.exists) {
                result.slot = slot;
                for (uint32_t i = 0; i < 32; ++i) result.entry[i] = entry[i];
            }
            return Status::Ok;
        }
        if (found != Status::Ok) return found;
        if ((entry[11] & 0x10) == 0) return Status::NotDirectory;
        const uint32_t child = (static_cast<uint32_t>(helpers::le16(entry + 20)) << 16) |
                               helpers::le16(entry + 26);
        if (!valid_cluster(g, child)) return Status::Corrupt;
        parent = child;
        start = end + 1;
    }
    return Status::InvalidName;
}

Status find_directory_entry(uint32_t directory_cluster, const char* name,
                            DirectorySlot& slot, uint8_t entry[32])
{
    uint8_t encoded[11];
    if (entry == nullptr || !encode_component(name, encoded)) return Status::InvalidName;
    helpers::BpbGeometry g = {};
    const Status ready = geometry_for_write(g);
    if (ready != Status::Ok) return ready;
    return find_in_directory(g, directory_cluster, encoded, slot, entry,
                             nullptr, nullptr);
}

Status read_directory_entry(const DirectorySlot& slot, uint8_t entry[32])
{
    if (entry == nullptr) return Status::InvalidName;
    helpers::BpbGeometry g = {};
    const Status ready = geometry_for_write(g);
    if (ready != Status::Ok) return ready;
    if (!valid_slot(g, slot)) return Status::Corrupt;
    uint8_t sector[512];
    if (!storage::read_sector(storage::DiskId::Test, slot.lba, sector))
        return Status::IoError;
    for (uint32_t i = 0; i < 32; ++i) entry[i] = sector[slot.offset + i];
    return Status::Ok;
}

Status update_directory_entry(const DirectorySlot& slot, const uint8_t name[11],
                              uint32_t first_cluster, uint32_t size)
{
    if (name == nullptr) return Status::InvalidName;
    helpers::BpbGeometry g = {};
    const Status ready = geometry_for_write(g);
    if (ready != Status::Ok) return ready;
    if (first_cluster != 0 && !valid_cluster(g, first_cluster)) return Status::Corrupt;
    uint8_t entry[32];
    const Status read = read_directory_entry(slot, entry);
    if (read != Status::Ok) return read;
    if (!live_short_entry(entry)) return Status::NotFound;
    for (uint32_t i = 0; i < 11; ++i) entry[i] = name[i];
    entry[20] = static_cast<uint8_t>(first_cluster >> 16);
    entry[21] = static_cast<uint8_t>((entry[21] & 0xf0u) |
                                     ((first_cluster >> 24) & 0x0fu));
    entry[26] = static_cast<uint8_t>(first_cluster);
    entry[27] = static_cast<uint8_t>(first_cluster >> 8);
    put32(entry, 28, size);
    return put_directory_entry(g, slot, entry, nullptr);
}

Status delete_directory_entry(const DirectorySlot& slot)
{
    uint8_t entry[32];
    const Status read = read_directory_entry(slot, entry);
    if (read != Status::Ok) return read;
    if (!live_short_entry(entry)) return Status::NotFound;
    entry[0] = 0xe5;
    helpers::BpbGeometry g = {};
    const Status ready = geometry_for_write(g);
    if (ready != Status::Ok) return ready;
    return put_directory_entry(g, slot, entry, nullptr);
}

Status create_directory_entry(uint32_t directory_cluster, const char* name,
                              const uint8_t entry[32], DirectorySlot& slot)
{
    uint8_t encoded[11];
    if (entry == nullptr || !encode_component(name, encoded)) return Status::InvalidName;
    helpers::BpbGeometry g = {};
    const Status ready = geometry_for_write(g);
    if (ready != Status::Ok) return ready;
    DirectorySlot found = {};
    DirectorySlot vacant = {};
    uint8_t existing[32] = {};
    uint32_t tail = 0;
    bool vacant_is_end = false;
    const Status search = find_in_directory(g, directory_cluster, encoded,
                                             found, existing, &vacant, &tail,
                                             &vacant_is_end);
    if (search == Status::Ok) return Status::AlreadyExists;
    if (search != Status::NotFound) return search;
    DirectorySlot next_marker = {};
    bool need_growth = vacant.lba == 0;
    if (vacant_is_end) {
        if (vacant.offset < 512 - 32) {
            next_marker = {vacant.lba, static_cast<uint16_t>(vacant.offset + 32)};
        } else {
            const uint32_t sector_index =
                (vacant.lba - g.first_data_lba) % g.sectors_per_cluster;
            if (sector_index + 1 < g.sectors_per_cluster) {
                next_marker = {vacant.lba + 1, 0};
            } else {
                uint32_t next = 0;
                bool end = false;
                const Status step = chain_next(g, tail, next, end);
                if (step != Status::Ok) return step;
                if (end) {
                    need_growth = true;
                } else {
                    uint32_t checked_next = 0;
                    bool checked_end = false;
                    const Status check = chain_next(g, next, checked_next, checked_end);
                    if (check != Status::Ok) return check;
                    uint32_t lba = 0;
                    if (!directory_lba(g, next, 0, lba)) return Status::Corrupt;
                    next_marker = {lba, 0};
                }
            }
        }
    }
    uint32_t fresh = 0;
    if (need_growth) {
        Status status = allocate_chain(1, fresh);
        if (status != Status::Ok) return status;
        uint8_t empty[512];
        volatile uint8_t* clear = empty;
        for (uint32_t i = 0; i < 512; ++i) clear[i] = 0;
        for (uint32_t i = 0; i < g.sectors_per_cluster; ++i) {
            uint32_t lba = 0;
            if (!directory_lba(g, fresh, i, lba)) {
                (void)free_chain(fresh);
                return Status::Corrupt;
            }
            if (!storage::write_sector(storage::DiskId::Test, lba, empty)) {
                (void)free_chain(fresh);
                return Status::IoError;
            }
        }
        status = write_fat_entry(fresh, 0x0fffffffu);
        if (status != Status::Ok) {
            (void)free_chain(fresh);
            return status;
        }
        status = write_fat_entry(tail, fresh);
        if (status != Status::Ok) {
            reclaim_if_unlinked(g, tail, fresh);
            return status;
        }
        uint32_t lba = 0;
        if (!directory_lba(g, fresh, 0, lba)) return Status::Corrupt;
        if (vacant.lba == 0) vacant = {lba, 0};
    } else if (vacant_is_end) {
        const Status marker = clear_end_marker(g, next_marker);
        if (marker != Status::Ok) return marker;
    }
    uint8_t published[32];
    for (uint32_t i = 0; i < 32; ++i) published[i] = entry[i];
    for (uint32_t i = 0; i < 11; ++i) published[i] = encoded[i];
    bool unchanged = false;
    const Status publish = put_directory_entry(g, vacant, published, &unchanged);
    if (publish != Status::Ok) {
        if (fresh != 0 && unchanged &&
            write_fat_entry(tail, 0x0fffffffu) == Status::Ok)
            (void)free_chain(fresh);
        return Status::IoError;
    }
    slot = vacant;
    return Status::Ok;
}

Status read_fat_entry(uint32_t cluster, uint32_t& value)
{
    helpers::BpbGeometry g = {};
    if (!mounted_geometry(g)) return Status::NotMounted;
    uint32_t raw = 0;
    const Status status = read_entry(g, cluster, g.active_fat, raw);
    if (status == Status::Ok) value = helpers::fat28(raw);
    return status;
}

Status write_fat_entry(uint32_t cluster, uint32_t value)
{
    helpers::BpbGeometry g = {};
    const Status ready = geometry_for_write(g);
    if (ready != Status::Ok) return ready;
    if (!valid_cluster(g, cluster) ||
        !(value == 0 || valid_cluster(g, value) || helpers::is_eoc(value)) ||
        value > 0x0fffffffu) return Status::Corrupt;

    uint32_t old[256];
    for (uint32_t copy = 0; copy < g.fat_count; ++copy) {
        const Status status = read_entry(g, cluster, copy, old[copy]);
        if (status != Status::Ok) return status;
        if (copy != 0 && helpers::fat28(old[copy]) != helpers::fat28(old[0]))
            return Status::Corrupt;
    }
    for (uint32_t copy = 0; copy < g.fat_count; ++copy) {
        const uint32_t raw = (old[copy] & 0xf0000000u) | value;
        if (write_raw(g, cluster, copy, raw) != Status::Ok) {
            for (uint32_t restore = 0; restore < copy; ++restore)
                (void)write_raw(g, cluster, restore, old[restore]);
            return Status::IoError;
        }
    }
    return Status::Ok;
}

Status allocate_chain(uint32_t clusters, uint32_t& first_cluster)
{
    helpers::BpbGeometry g = {};
    const Status ready = geometry_for_write(g);
    if (ready != Status::Ok) return ready;
    first_cluster = 0;
    if (clusters == 0) return Status::Ok;
    if (clusters > g.cluster_count) return Status::NoSpace;
    uint32_t previous = 0;
    uint32_t found = 0;
    for (uint32_t index = 0; index < g.cluster_count && found < clusters; ++index) {
        const uint32_t candidate = index + 2u;
        uint32_t value = 0;
        Status status = read_fat_entry(candidate, value);
        if (status != Status::Ok) {
            if (first_cluster != 0 && free_chain(first_cluster) != Status::Ok)
                return Status::IoError;
            return status;
        }
        if (value != 0) continue;
        status = write_fat_entry(candidate, 0x0fffffffu);
        if (status != Status::Ok) {
            if (first_cluster != 0 && free_chain(first_cluster) != Status::Ok)
                return Status::IoError;
            return status;
        }
        if (previous != 0) {
            status = write_fat_entry(previous, candidate);
            if (status != Status::Ok) {
                const Status discard = write_fat_entry(candidate, 0);
                const Status cleanup = free_chain(first_cluster);
                first_cluster = 0;
                return discard == Status::Ok && cleanup == Status::Ok
                    ? status : Status::IoError;
            }
        } else {
            first_cluster = candidate;
        }
        previous = candidate;
        ++found;
    }
    if (found == clusters) return Status::Ok;
    if (first_cluster != 0 && free_chain(first_cluster) != Status::Ok)
        return Status::IoError;
    first_cluster = 0;
    return Status::NoSpace;
}

Status free_chain(uint32_t first_cluster)
{
    helpers::BpbGeometry g = {};
    const Status ready = geometry_for_write(g);
    if (ready != Status::Ok) return ready;
    if (first_cluster == 0) return Status::Ok;
    if (!valid_cluster(g, first_cluster)) return Status::Corrupt;

    // Validate the entire chain before freeing any entry.
    uint32_t cluster = first_cluster;
    bool terminated = false;
    for (uint32_t walked = 0; walked < g.cluster_count; ++walked) {
        uint32_t next = 0;
        bool end = false;
        const Status status = chain_next(g, cluster, next, end);
        if (status != Status::Ok) return status;
        if (end) { terminated = true; break; }
        cluster = next;
    }
    if (!terminated) return Status::Corrupt;

    cluster = first_cluster;
    for (uint32_t walked = 0; walked < g.cluster_count; ++walked) {
        uint32_t next = 0;
        bool end = false;
        const Status status = chain_next(g, cluster, next, end);
        if (status != Status::Ok) return status;
        const Status clear = write_fat_entry(cluster, 0);
        if (clear != Status::Ok) return clear;
        if (end) return Status::Ok;
        cluster = next;
    }
    return Status::Corrupt;
}

Status file_cluster_count(uint64_t size, uint32_t cluster_bytes, uint32_t& count)
{
    count = 0;
    if (size > 0xffffffffull) return Status::Unsupported;
    if (cluster_bytes == 0) return Status::Corrupt;
    count = static_cast<uint32_t>(size / cluster_bytes +
        (size % cluster_bytes != 0 ? 1u : 0u));
    return Status::Ok;
}

} // namespace linux95::filesystem::fat32::write
