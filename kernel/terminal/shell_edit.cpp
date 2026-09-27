#include "terminal/shell_edit.hpp"

namespace linux95::shell {

EditArgumentStatus parse_edit_argument(
    const char* argument,
    char (&path)[filesystem::vfs::kPathCapacity])
{
    path[0] = '\0';
    if (argument == nullptr) return EditArgumentStatus::Missing;
    while (*argument == ' ') ++argument;
    if (*argument == '\0') return EditArgumentStatus::Missing;

    size_t length = 0;
    const char* cursor = argument;
    while (*cursor != '\0' && *cursor != ' ') {
        if (length + 1 >= sizeof(path)) return EditArgumentStatus::TooLong;
        ++length;
        ++cursor;
    }
    const char* remainder = cursor;
    while (*remainder == ' ') ++remainder;
    if (*remainder != '\0') return EditArgumentStatus::Extra;

    for (size_t i = 0; i < length; ++i) path[i] = argument[i];
    path[length] = '\0';
    return EditArgumentStatus::Ok;
}

CommandResult report_editor_unavailable(
    terminal::Output& output,
    CommandResult result)
{
    if (result.action == CommandAction::OpenEditor) {
        terminal::write(output, "edit: unavailable in VGA shell\n");
        result = CommandResult{};
    }
    return result;
}

} // namespace linux95::shell
