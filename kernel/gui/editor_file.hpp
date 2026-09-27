#pragma once

#include "filesystem/filesystem.hpp"
#include "gui/editor_model.hpp"

namespace linux95::gui::editor {

filesystem::Status open_file(EditorModel& model, const char* path);
filesystem::Status save_file(EditorModel& model);

} // namespace linux95::gui::editor
