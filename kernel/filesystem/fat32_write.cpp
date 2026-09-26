#include "filesystem/fat32_write.hpp"

#include "filesystem/fat32.hpp"
#include "filesystem/fat32_helpers.hpp"
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
                  uint32_t& next, bool& end)
{
    uint32_t raw = 0;
    const Status status = read_entry(g, cluster, 0, raw);
    if (status != Status::Ok) return status;
    const uint32_t value = helpers::fat28(raw);
    const auto kind = helpers::classify_fat_entry(value);
    if (kind == helpers::FatEntryKind::EndOfChain) {
        end = true; next = 0; return Status::Ok;
    }
    if (kind != helpers::FatEntryKind::Next || !valid_cluster(g, value))
        return Status::Corrupt;
    end = false; next = value; return Status::Ok;
}

} // namespace

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
