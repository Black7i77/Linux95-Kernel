#pragma once

#include "terminal/shell.hpp"

namespace linux95::shell {

enum class EditArgumentStatus : uint8_t {
    Ok,
    Missing,
    Extra,
    TooLong,
};

EditArgumentStatus parse_edit_argument(
    const char* argument,
    char (&path)[filesystem::vfs::kPathCapacity]);

CommandResult report_editor_unavailable(
    terminal::Output& output,
    CommandResult result);

} // namespace linux95::shell
