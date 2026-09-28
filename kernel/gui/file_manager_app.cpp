#include "gui/file_manager_app.hpp"

#include <stddef.h>
#include <string.h>

namespace linux95::gui::file_manager {
namespace {

constexpr int32_t kToolbarHeight = 24;
constexpr int32_t kDialogTop = 56;
constexpr int32_t kDialogBottom = 96;

size_t name_length(const char* value)
{
    size_t length = 0;
    while (value[length] != '\0') ++length;
    return length;
}
constexpr int32_t kParentTop = 24;
constexpr int32_t kParentBottom = 48;

int32_t g_content_width;
int32_t g_content_height;

char lower_ascii(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

const char* status_text(filesystem::Status status)
{
    switch (status) {
    case filesystem::Status::Ok: return "OK";
    case filesystem::Status::NotMounted: return "filesystem not mounted";
    case filesystem::Status::NotFound: return "entry not found";
    case filesystem::Status::NotDirectory: return "not a directory";
    case filesystem::Status::IsDirectory: return "is a directory";
    case filesystem::Status::AlreadyExists: return "name already exists";
    case filesystem::Status::InvalidName: return "invalid 8.3 name";
    case filesystem::Status::NoSpace: return "disk is full";
    case filesystem::Status::DirectoryNotEmpty: return "directory is not empty";
    case filesystem::Status::ReadOnly: return "filesystem is read only";
    case filesystem::Status::IoError: return "disk I/O error";
    case filesystem::Status::Corrupt: return "filesystem is corrupt";
    case filesystem::Status::Unsupported: return "operation unavailable";
    default: return "filesystem operation failed";
    }
}

const char* toolbar_label(size_t index)
{
    switch (index) {
    case 0: return "NewFold";
    case 1: return "NewFile";
    case 2: return "Rename";
    case 3: return "Delete";
    default: return "View";
    }
}

} // namespace

namespace presentation {

struct Layout {
    graphics::Rect toolbar;
    graphics::Rect parent;
    graphics::Rect path;
    graphics::Rect entries;
    graphics::Rect status;
    graphics::Rect dialog;
};

bool format_delete_confirmation(const char* name, char* output, size_t capacity)
{
    if (!name || !output || capacity == 0) return false;
    constexpr char prefix[] = "Delete ";
    size_t name_size = 0;
    while (name[name_size] != '\0') ++name_size;
    constexpr size_t prefix_size = sizeof(prefix) - 1;
    if (name_size > capacity || prefix_size + name_size + 2 > capacity) {
        output[0] = '\0';
        return false;
    }
    for (size_t i = 0; i < prefix_size; ++i) output[i] = prefix[i];
    for (size_t i = 0; i < name_size; ++i) output[prefix_size + i] = name[i];
    output[prefix_size + name_size] = '?';
    output[prefix_size + name_size + 1] = '\0';
    return true;
}

Layout layout(graphics::Rect content, size_t entry_count, ViewMode view)
{
    (void)entry_count;
    Layout result{};
    const int32_t bottom = content.y + (content.height > 0 ? content.height : 0);
    result.toolbar = graphics::Rect{content.x, content.y, content.width, content.height >= 24 ? 24 : content.height};
    result.parent = graphics::Rect{content.x, content.y + 24, content.width, content.height > 24 ? (content.height - 24 < 24 ? content.height - 24 : 24) : 0};
    result.path = graphics::Rect{content.x, content.y + 48, content.width, content.height > 48 ? (content.height - 48 < 16 ? content.height - 48 : 16) : 0};
    const int32_t entries_top = content.y + 64;
    const int32_t entries_bottom = bottom > content.y + 20 ? bottom - 20 : entries_top;
    result.entries = graphics::Rect{content.x, entries_top, content.width, entries_bottom > entries_top ? entries_bottom - entries_top : 0};
    result.status = graphics::Rect{content.x, bottom > content.y + 20 ? bottom - 20 : content.y, content.width, content.height < 20 ? content.height : 20};
    result.dialog = graphics::Rect{content.x + 12, content.y + kDialogTop, content.width > 24 ? content.width - 24 : 0,
        content.height > kDialogTop ? (content.height - kDialogTop < 40 ? content.height - kDialogTop : 40) : 0};
    if (view == ViewMode::Icons && result.entries.width > 0 && result.entries.height > 0) {
        const int32_t columns = result.entries.width / 80;
        if (columns == 0) result.entries.width = 0;
    }
    return result;
}

int hit_test(graphics::Rect content, size_t entry_count, ViewMode view, int32_t x, int32_t y)
{
    if (entry_count > kMaxEntries) entry_count = kMaxEntries;
    const Layout areas = layout(content, entry_count, view);
    const graphics::Rect bounds = areas.entries;
    if (entry_count == 0 || bounds.width <= 0 || bounds.height <= 0 ||
        x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) return -1;

    size_t index = 0;
    if (view == ViewMode::Details) {
        const int32_t row_height = 18;
        index = static_cast<size_t>((y - bounds.y) / row_height);
        if ((y - bounds.y) / row_height >= bounds.height / row_height) return -1;
    } else {
        const int32_t columns = bounds.width / 80;
        if (columns <= 0) return -1;
        const int32_t column = (x - bounds.x) / 80;
        const int32_t row = (y - bounds.y) / 52;
        if (column >= columns || row >= bounds.height / 52) return -1;
        index = static_cast<size_t>(row * columns + column);
    }
    return index < entry_count ? static_cast<int>(index) : -1;
}

} // namespace presentation

namespace {

graphics::Rect intersect(graphics::Rect a, graphics::Rect b)
{
    const int32_t left = a.x > b.x ? a.x : b.x;
    const int32_t top = a.y > b.y ? a.y : b.y;
    const int32_t ar = a.x + (a.width > 0 ? a.width : 0);
    const int32_t br = b.x + (b.width > 0 ? b.width : 0);
    const int32_t ab = a.y + (a.height > 0 ? a.height : 0);
    const int32_t bb = b.y + (b.height > 0 ? b.height : 0);
    const int32_t right = ar < br ? ar : br;
    const int32_t bottom = ab < bb ? ab : bb;
    return graphics::Rect{left, top, right > left ? right - left : 0, bottom > top ? bottom - top : 0};
}

void fill_clipped(graphics::Framebuffer& framebuffer, graphics::Rect clip, graphics::Rect rect, graphics::Color color)
{
    graphics::fill_rect(framebuffer, intersect(intersect(clip, rect),
        graphics::Rect{0, 0, static_cast<int32_t>(framebuffer.width), static_cast<int32_t>(framebuffer.height)}), color);
}

void text_clipped(graphics::Framebuffer& framebuffer, graphics::Rect clip, int32_t x, int32_t y,
    const char* text, graphics::Color color)
{
    if (!text || y < clip.y || y + 8 > clip.y + clip.height) return;
    size_t max_chars = x < clip.x ? 0 : static_cast<size_t>((clip.x + clip.width - x) / 8);
    for (size_t i = 0; text[i] && i < max_chars; ++i) graphics::draw_char(framebuffer, x + static_cast<int32_t>(i * 8), y, text[i], color);
}

} // namespace

FileManagerApp::FileManagerApp()
    : model_(), dialog_mode_(DialogMode::None), pending_operation_(PendingOperation::None),
      name_buffer_{}, status_message_{}
{}

AppInstance FileManagerApp::instance()
{
    return AppInstance{
        this,
        AppCallbacks{draw_callback, key_callback, close_callback, mouse_callback},
    };
}

filesystem::Status FileManagerApp::open()
{
    const filesystem::Status status = model_.load_root();
    set_status_for(status, "open");
    return status;
}

FileManagerModel& FileManagerApp::model() { return model_; }
const FileManagerModel& FileManagerApp::model() const { return model_; }
DialogMode FileManagerApp::dialog_mode() const { return dialog_mode_; }
PendingOperation FileManagerApp::pending_operation() const { return pending_operation_; }
const char* FileManagerApp::name_buffer() const { return name_buffer_; }
const char* FileManagerApp::status_message() const { return status_message_; }

void FileManagerApp::draw(graphics::Framebuffer& framebuffer, graphics::Rect content)
{
    g_content_width = content.width;
    g_content_height = content.height;
    const graphics::Color black{0, 0, 0};
    const graphics::Color white{255, 255, 255};
    const graphics::Color navy{0, 0, 128};
    const graphics::Color gray{192, 192, 192};
    const graphics::Color cyan{0, 128, 128};
    const auto areas = presentation::layout(content, model_.entry_count(), model_.view_mode());
    fill_clipped(framebuffer, content, content, gray);
    fill_clipped(framebuffer, content, areas.toolbar, navy);

    const int32_t starts[] = {0, 76, 136, 196, 256};
    const int32_t ends[] = {76, 136, 196, 256, 316};
    for (size_t i = 0; i < 5; ++i) {
        fill_clipped(framebuffer, content, graphics::Rect{content.x + starts[i] + 1, content.y + 2,
            ends[i] - starts[i] - 3, 20}, graphics::Color{0, 0, 160});
        text_clipped(framebuffer, content, content.x + starts[i] + 4, content.y + 8, toolbar_label(i), white);
    }

    fill_clipped(framebuffer, content, areas.parent, gray);
    fill_clipped(framebuffer, content, graphics::Rect{content.x + 2, content.y + 26, 68, 20}, cyan);
    text_clipped(framebuffer, content, content.x + 8, content.y + 32, "< Parent", white);
    fill_clipped(framebuffer, content, areas.path, graphics::Color{224, 224, 224});
    text_clipped(framebuffer, content, content.x + 4, content.y + 52, model_.current_path(), black);

    fill_clipped(framebuffer, content, areas.entries, white);
    for (size_t i = 0; i < model_.entry_count(); ++i) {
        const auto* entry = model_.entry(i);
        if (!entry) continue;
        const bool selected = model_.selected_index() == static_cast<int>(i);
        char label[32]{};
        if (model_.view_mode() == ViewMode::Details) {
            size_t n = 0;
            const char* prefix = entry->is_directory ? "[DIR] " : "[FILE] ";
            while (prefix[n] && n + 1 < sizeof(label)) { label[n] = prefix[n]; ++n; }
            for (size_t j = 0; entry->name[j] && n + 1 < sizeof(label); ++j) label[n++] = entry->name[j];
            if (!entry->is_directory && n + 1 < sizeof(label)) {
                label[n++] = ' ';
                label[n++] = ' ';
                uint32_t size = entry->size;
                char digits[10]{};
                size_t digit_count = 0;
                do {
                    digits[digit_count++] = static_cast<char>('0' + (size % 10));
                    size /= 10;
                } while (size != 0 && digit_count < sizeof(digits));
                while (digit_count > 0 && n + 1 < sizeof(label)) label[n++] = digits[--digit_count];
                if (n + 2 < sizeof(label)) { label[n++] = ' '; label[n++] = 'B'; }
            }
            label[n] = '\0';
            const int32_t row_y = areas.entries.y + static_cast<int32_t>(i * 18);
            if (row_y + 18 > areas.entries.y + areas.entries.height) continue;
            if (selected) fill_clipped(framebuffer, content, graphics::Rect{areas.entries.x, row_y, areas.entries.width, 18}, navy);
            text_clipped(framebuffer, content, areas.entries.x + 4, row_y + 5, label, selected ? white : black);
        } else {
            const int32_t columns = areas.entries.width / 80;
            if (columns <= 0) continue;
            const int32_t cell_x = areas.entries.x + static_cast<int32_t>((i % static_cast<size_t>(columns)) * 80);
            const int32_t cell_y = areas.entries.y + static_cast<int32_t>((i / static_cast<size_t>(columns)) * 52);
            if (cell_y + 52 > areas.entries.y + areas.entries.height) continue;
            if (selected) fill_clipped(framebuffer, content, graphics::Rect{cell_x, cell_y, 80, 52}, graphics::Color{0, 0, 192});
            const graphics::Color icon_color = entry->is_directory ? graphics::Color{224, 176, 0} : graphics::Color{64, 96, 192};
            fill_clipped(framebuffer, content, graphics::Rect{cell_x + 28, cell_y + 4, 24, 20}, icon_color);
            size_t name_length = 0;
            while (entry->name[name_length] && name_length < 8) {
                label[name_length] = entry->name[name_length];
                ++name_length;
            }
            if (entry->name[name_length]) label[name_length++] = '~';
            label[name_length] = '\0';
            text_clipped(framebuffer, content, cell_x + 4, cell_y + 32, label, selected ? white : black);
        }
    }
    fill_clipped(framebuffer, content, areas.status, gray);
    text_clipped(framebuffer, content, content.x + 4, areas.status.y + 6, status_message_, black);
    if (model_.truncated()) text_clipped(framebuffer, content, content.x + 220, areas.status.y + 6, "LIST TRUNCATED", black);

    if (dialog_mode_ != DialogMode::None) {
        fill_clipped(framebuffer, content, areas.dialog, navy);
        char confirmation[32]{};
        const int selected = model_.selected_index();
        const auto* selected_entry = selected >= 0 ? model_.entry(static_cast<size_t>(selected)) : nullptr;
        if (dialog_mode_ == DialogMode::DeleteConfirm && selected_entry &&
            presentation::format_delete_confirmation(selected_entry->name, confirmation, sizeof(confirmation))) {
            text_clipped(framebuffer, content, areas.dialog.x + 8, areas.dialog.y + 6, confirmation, white);
        } else {
        text_clipped(framebuffer, content, areas.dialog.x + 8, areas.dialog.y + 6,
            dialog_mode_ == DialogMode::Name ? name_buffer_ : "Delete selected entry?", white);
        }
        fill_clipped(framebuffer, content, graphics::Rect{content.x + 24, content.y + 58, 80, 24}, cyan);
        fill_clipped(framebuffer, content, graphics::Rect{content.x + 112, content.y + 58, 80, 24}, cyan);
        text_clipped(framebuffer, content, content.x + 36, content.y + 66, "OK / Delete", white);
        text_clipped(framebuffer, content, content.x + 124, content.y + 66, "Cancel", white);
    }
}

void FileManagerApp::on_key(const keyboard::KeyEvent& event)
{
    if (!event.pressed) return;

    if (dialog_mode_ == DialogMode::Name) {
        if (event.key == keyboard::KeyCode::Escape) {
            cancel_dialog();
        } else if (event.key == keyboard::KeyCode::Backspace) {
            const size_t length = name_length(name_buffer_);
            if (length != 0) name_buffer_[length - 1] = '\0';
        } else if (event.key == keyboard::KeyCode::Enter) {
            confirm_name();
        } else if (event.key == keyboard::KeyCode::Character &&
                   event.character >= 32 && event.character <= 126 && event.character != '/') {
            const size_t length = name_length(name_buffer_);
            if (length < sizeof(name_buffer_) - 1) {
                name_buffer_[length] = event.character;
                name_buffer_[length + 1] = '\0';
            }
        }
        return;
    }

    if (dialog_mode_ == DialogMode::DeleteConfirm) {
        if (event.key == keyboard::KeyCode::Escape) cancel_dialog();
        else if (event.key == keyboard::KeyCode::Enter) confirm_delete();
        return;
    }

    if (event.ctrl && event.key == keyboard::KeyCode::Character) {
        switch (lower_ascii(event.character)) {
        case 'n': activate_action(PendingOperation::NewFolder); return;
        case 'f': activate_action(PendingOperation::NewFile); return;
        case 'r': activate_action(PendingOperation::Rename); return;
        case 'v': toggle_view(); return;
        default: return;
        }
    }

    if (event.key == keyboard::KeyCode::Delete) {
        begin_delete_confirmation();
    } else if (event.key == keyboard::KeyCode::ArrowUp || event.key == keyboard::KeyCode::ArrowDown) {
        const size_t count = model_.entry_count();
        if (count == 0) return;
        const int selected = model_.selected_index();
        size_t next = 0;
        if (event.key == keyboard::KeyCode::ArrowUp) {
            next = selected < 0 ? count - 1 : (selected == 0 ? 0 : static_cast<size_t>(selected - 1));
        } else {
            next = selected < 0 ? 0 : (static_cast<size_t>(selected + 1) < count
                ? static_cast<size_t>(selected + 1) : count - 1);
        }
        model_.select(next);
    } else if (event.key == keyboard::KeyCode::Enter) {
        const int selected = model_.selected_index();
        const auto* entry = selected < 0 ? nullptr : model_.entry(static_cast<size_t>(selected));
        if (entry && entry->is_directory) {
            set_status_for(model_.navigate_into(static_cast<size_t>(selected)), "open directory");
        }
    } else if (event.key == keyboard::KeyCode::Backspace) {
        set_status_for(model_.navigate_parent(), "go to parent");
    }
}

void FileManagerApp::on_mouse(const AppMouseEvent& event)
{
    if (!event.left_pressed) return;
    if (dialog_mode_ != DialogMode::None) {
        if (event.y >= kDialogTop && event.y < kDialogBottom) {
            if (event.x >= 24 && event.x < 104) {
                if (dialog_mode_ == DialogMode::Name) confirm_name();
                else confirm_delete();
            } else if (event.x >= 112 && event.x < 192) {
                cancel_dialog();
            }
        }
        return;
    }
    if (event.x < 0 || event.y < 0) return;
    if (event.y >= kParentTop && event.y < kParentBottom && event.x < 72) {
        set_status_for(model_.navigate_parent(), "go to parent");
        return;
    }
    if (event.y >= kToolbarHeight || event.x < 0) {
        if (g_content_width <= 0 || g_content_height <= 0) return;
        const int index = presentation::hit_test(
            graphics::Rect{0, 0, g_content_width, g_content_height}, model_.entry_count(),
            model_.view_mode(), event.x, event.y);
        if (index >= 0) model_.select(static_cast<size_t>(index));
        return;
    }
    if (event.x < 76) activate_action(PendingOperation::NewFolder);
    else if (event.x < 136) activate_action(PendingOperation::NewFile);
    else if (event.x < 196) activate_action(PendingOperation::Rename);
    else if (event.x < 256) begin_delete_confirmation();
    else if (event.x < 316) toggle_view();
}

void FileManagerApp::on_close() { cancel_dialog(); }

void FileManagerApp::begin_name_dialog(PendingOperation operation)
{
    pending_operation_ = operation;
    dialog_mode_ = DialogMode::Name;
    name_buffer_[0] = '\0';
    if (operation == PendingOperation::Rename) {
        const int selected = model_.selected_index();
        if (selected < 0) {
            cancel_dialog();
            set_status("select an entry first");
            return;
        }
        const auto* entry = model_.entry(static_cast<size_t>(selected));
        size_t i = 0;
        while (entry->name[i] && i + 1 < sizeof(name_buffer_)) {
            name_buffer_[i] = entry->name[i];
            ++i;
        }
        name_buffer_[i] = '\0';
    }
    set_status(operation == PendingOperation::NewFolder ? "new folder name" :
        operation == PendingOperation::NewFile ? "new file name" : "rename entry");
}

void FileManagerApp::begin_delete_confirmation()
{
    if (model_.selected_index() < 0) {
        set_status("select an entry first");
        return;
    }
    pending_operation_ = PendingOperation::None;
    dialog_mode_ = DialogMode::DeleteConfirm;
    set_status("confirm delete");
}

void FileManagerApp::cancel_dialog()
{
    dialog_mode_ = DialogMode::None;
    pending_operation_ = PendingOperation::None;
    name_buffer_[0] = '\0';
}

void FileManagerApp::confirm_name()
{
    filesystem::Status result = filesystem::Status::InvalidName;
    if (pending_operation_ == PendingOperation::NewFolder) result = model_.create_folder(name_buffer_);
    else if (pending_operation_ == PendingOperation::NewFile) result = model_.create_file(name_buffer_);
    else if (pending_operation_ == PendingOperation::Rename) result = model_.rename_selected(name_buffer_);
    set_status_for(result, pending_operation_ == PendingOperation::NewFolder ? "create folder" :
        pending_operation_ == PendingOperation::NewFile ? "create file" : "rename");
    if (result == filesystem::Status::Ok) cancel_dialog();
}

void FileManagerApp::confirm_delete()
{
    const filesystem::Status result = model_.remove_selected();
    set_status_for(result, "delete");
    cancel_dialog();
}

void FileManagerApp::activate_action(PendingOperation operation)
{
    if (operation == PendingOperation::Rename && model_.selected_index() < 0) {
        set_status("select an entry first");
        return;
    }
    begin_name_dialog(operation);
}

void FileManagerApp::toggle_view()
{
    model_.set_view(model_.view_mode() == ViewMode::Icons ? ViewMode::Details : ViewMode::Icons);
}

void FileManagerApp::set_status(const char* message)
{
    if (!message) message = "";
    size_t length = 0;
    while (message[length] && length + 1 < sizeof(status_message_)) ++length;
    for (size_t i = 0; i < length; ++i) status_message_[i] = message[i];
    status_message_[length] = '\0';
}

void FileManagerApp::set_status_for(filesystem::Status status, const char* operation)
{
    if (status == filesystem::Status::Ok) {
        set_status("OK");
        return;
    }
    char message[64]{};
    size_t offset = 0;
    while (operation[offset] && offset + 3 < sizeof(message)) {
        message[offset] = operation[offset];
        ++offset;
    }
    message[offset++] = ':';
    message[offset++] = ' ';
    const char* reason = status_text(status);
    size_t index = 0;
    while (reason[index] && offset + 1 < sizeof(message)) message[offset++] = reason[index++];
    message[offset] = '\0';
    set_status(message);
}

void FileManagerApp::draw_callback(void* context, graphics::Framebuffer& framebuffer, graphics::Rect content)
{
    if (context) static_cast<FileManagerApp*>(context)->draw(framebuffer, content);
}

void FileManagerApp::key_callback(void* context, const keyboard::KeyEvent& event)
{
    if (context) static_cast<FileManagerApp*>(context)->on_key(event);
}

void FileManagerApp::close_callback(void* context)
{
    if (context) static_cast<FileManagerApp*>(context)->on_close();
}

void FileManagerApp::mouse_callback(void* context, const AppMouseEvent& event)
{
    if (context) static_cast<FileManagerApp*>(context)->on_mouse(event);
}

} // namespace linux95::gui::file_manager
