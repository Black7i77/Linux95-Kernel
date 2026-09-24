#pragma once

#include "filesystem/filesystem.hpp"

namespace linux95::filesystem::fat32 {

bool mount(VolumeInfo& volume);

} // namespace linux95::filesystem::fat32
