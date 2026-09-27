#include "gui/terminal_app.hpp"
#include "gui/editor_model.hpp"
#include "filesystem/vfs.hpp"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

using namespace linux95;

namespace {

size_t ping_polls;
size_t shell_commands;
size_t read_count;
size_t write_count;
char last_command[300];
uint8_t file_bytes[gui::editor::kTextCapacity];
size_t file_size;
bool file_exists = true;
filesystem::Status file_stat_status = filesystem::Status::Ok;

void send(gui::TerminalApp& app, keyboard::KeyEvent event)
{
    app.instance().callbacks.on_key(&app, event);
}

void type(gui::TerminalApp& app, const char* text)
{
    while (*text != '\0') {
        send(app, {keyboard::KeyCode::Character, *text++, false, false, true});
    }
}

void enter(gui::TerminalApp& app)
{
    send(app, {keyboard::KeyCode::Enter, 0, false, false, true});
}

void ctrl(gui::TerminalApp& app, char value)
{
    send(app, {keyboard::KeyCode::Character, value, true, false, true});
}

void test_shell_editor_lifecycle_and_polling()
{
    gui::TerminalApp app;
    assert(app.mode() == gui::TerminalApp::Mode::Shell);
    app.poll();
    const size_t polls_before_editor = ping_polls;

    type(app, "edit EXIST.TXT");
    enter(app);
    assert(app.mode() == gui::TerminalApp::Mode::Editor);
    assert(read_count == 1 && write_count == 0);
    const size_t polls_at_entry = ping_polls;
    app.poll();
    assert(ping_polls == polls_at_entry);

    const uint8_t before_length = static_cast<uint8_t>(file_size);
    send(app, {keyboard::KeyCode::Character, '!', false, false, true});
    graphics::Framebuffer empty{};
    app.instance().callbacks.draw(&app, empty, {0, 0, 0, 0});
    uint32_t pixels[64 * 32]{};
    graphics::Framebuffer framebuffer{
        reinterpret_cast<volatile uint8_t*>(pixels), 64, 32, 64 * 4,
        {8, 16, 8, 8, 8, 0}};
    app.instance().callbacks.draw(&app, framebuffer, {0, 0, 64, 32});
    assert(file_size == before_length);

    ctrl(app, 's');
    assert(write_count == 1);
    assert(file_size == 5 && memcmp(file_bytes, "old\n!", 5) == 0);
    ctrl(app, 'q');
    assert(app.mode() == gui::TerminalApp::Mode::Shell);

    type(app, "version");
    enter(app);
    assert(shell_commands == 2);
    assert(strcmp(last_command, "version") == 0);
    assert(app.poll() == false);
    assert(ping_polls > polls_before_editor);
}

void test_failed_open_and_missing_file_clean_quit()
{
    gui::TerminalApp app;
    file_stat_status = filesystem::Status::IoError;
    type(app, "edit BAD.TXT");
    enter(app);
    assert(app.mode() == gui::TerminalApp::Mode::Shell);

    file_stat_status = filesystem::Status::NotFound;
    file_exists = false;
    const size_t writes_before = write_count;
    type(app, "edit NEW.TXT");
    enter(app);
    assert(app.mode() == gui::TerminalApp::Mode::Editor);
    ctrl(app, 'q');
    assert(app.mode() == gui::TerminalApp::Mode::Shell);
    assert(write_count == writes_before && !file_exists);
}

} // namespace

namespace linux95::shell {
CommandResult execute_command(terminal::Output&, char* command)
{
    ++shell_commands;
    size_t length = 0;
    while (command[length] != '\0' && length + 1 < sizeof(last_command)) {
        last_command[length] = command[length];
        ++length;
    }
    last_command[length] = '\0';
    CommandResult result{};
    if (strncmp(command, "edit ", 5) == 0) {
        result.action = CommandAction::OpenEditor;
        const char* path = command + 5;
        size_t i = 0;
        while (path[i] != '\0' && i + 1 < sizeof(result.path)) {
            result.path[i] = path[i];
            ++i;
        }
    }
    return result;
}
}

namespace linux95::network {
Status status() { return {}; }
bool start_ping(net::Ipv4Address) { return false; }
net::icmp::PingResult ping_result() { ++ping_polls; return {}; }
void clear_ping_result() {}
}

namespace linux95::net::dns {
Status begin_lookup(const char*) { return Status::NetworkUnavailable; }
Status lookup_status() { return Status::Idle; }
size_t result_count() { return 0; }
bool result_address(size_t, net::Ipv4Address&) { return false; }
net::Ipv4Address server() { return {}; }
Status set_server(const net::Ipv4Address&) { return Status::Idle; }
}

namespace linux95::filesystem::vfs {
Status stat(const char* path, FileStat& info)
{
    if (strcmp(path, "/") == 0) {
        info = {true, 0};
        return Status::Ok;
    }
    if (file_stat_status != Status::Ok) return file_stat_status;
    if (strcmp(path, "EXIST.TXT") == 0 && file_exists) {
        info = {false, static_cast<uint32_t>(file_size)};
        return Status::Ok;
    }
    return Status::NotFound;
}
int open(const char*, Status& status) { read_count++; status = Status::Ok; return 3; }
Status read(int, uint8_t* output, size_t capacity, size_t& read_bytes)
{
    read_bytes = file_size < capacity ? file_size : capacity;
    if (read_bytes != 0) memcpy(output, file_bytes, read_bytes);
    return Status::Ok;
}
Status close(int) { return Status::Ok; }
Status write_file(const char*, const uint8_t* data, size_t size)
{
    ++write_count;
    assert(size <= sizeof(file_bytes));
    if (size != 0) memcpy(file_bytes, data, size);
    file_size = size;
    file_exists = true;
    return Status::Ok;
}
}

int main()
{
    memcpy(file_bytes, "old\n", 4);
    file_size = 4;
    test_shell_editor_lifecycle_and_polling();
    test_failed_open_and_missing_file_clean_quit();
    return 0;
}
