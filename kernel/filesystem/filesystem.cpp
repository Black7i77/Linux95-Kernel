#include "filesystem/filesystem.hpp"

#include "filesystem/fat32.hpp"

namespace linux95::filesystem {

namespace {

VolumeInfo current_volume = {};

} // namespace

bool initialize()
{
    current_volume = VolumeInfo{};

    VolumeInfo mounted_volume = {};

    if (!fat32::mount(mounted_volume)) {
        return false;
    }

    current_volume = mounted_volume;
    return true;
}

const VolumeInfo& volume_info()
{
    return current_volume;
}

} // namespace linux95::filesystem
