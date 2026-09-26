#pragma once

#include "filesystem/filesystem.hpp"

namespace linux95::filesystem::fat32::write {

Status read_fat_entry(uint32_t cluster, uint32_t& value);
Status write_fat_entry(uint32_t cluster, uint32_t value);
Status allocate_chain(uint32_t clusters, uint32_t& first_cluster);
Status free_chain(uint32_t first_cluster);
Status file_cluster_count(uint64_t size, uint32_t cluster_bytes, uint32_t& count);

} // namespace linux95::filesystem::fat32::write
