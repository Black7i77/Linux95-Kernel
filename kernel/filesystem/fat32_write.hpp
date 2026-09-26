#pragma once

#include "filesystem/filesystem.hpp"

namespace linux95::filesystem::fat32::write {

struct DirectorySlot {
    uint32_t lba;
    uint16_t offset;
};

struct ResolvedPath {
    uint32_t parent_cluster;
    uint8_t name[11];
    bool exists;
    DirectorySlot slot;
    uint8_t entry[32];
};

Status resolve_path(const char* path, ResolvedPath& result);
Status find_directory_entry(uint32_t directory_cluster, const char* name,
                            DirectorySlot& slot, uint8_t entry[32]);
Status create_directory_entry(uint32_t directory_cluster, const char* name,
                              const uint8_t entry[32], DirectorySlot& slot);
Status read_directory_entry(const DirectorySlot& slot, uint8_t entry[32]);
Status update_directory_entry(const DirectorySlot& slot, const uint8_t name[11],
                              uint32_t first_cluster, uint32_t size);
Status delete_directory_entry(const DirectorySlot& slot);

Status read_fat_entry(uint32_t cluster, uint32_t& value);
Status write_fat_entry(uint32_t cluster, uint32_t value);
Status allocate_chain(uint32_t clusters, uint32_t& first_cluster);
Status free_chain(uint32_t first_cluster);
Status file_cluster_count(uint64_t size, uint32_t cluster_bytes, uint32_t& count);

} // namespace linux95::filesystem::fat32::write
