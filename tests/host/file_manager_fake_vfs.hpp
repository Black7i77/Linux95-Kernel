#pragma once

#include "filesystem/vfs.hpp"

#include <stddef.h>

namespace file_manager_fake_vfs {

void reset();
void add(const char* directory, const char* name, bool is_directory, uint32_t size);
void set_open_status(const char* path, linux95::filesystem::Status status);
void set_read_error_after(size_t entries, linux95::filesystem::Status status);
void set_close_status(linux95::filesystem::Status status);
size_t close_count();
size_t validate_count();
size_t stat_count();
size_t touch_count();
size_t mkdir_count();
size_t move_count();
size_t remove_count();
void set_mutation_status(linux95::filesystem::Status status);
void set_stat_error(linux95::filesystem::Status status);

} // namespace file_manager_fake_vfs
