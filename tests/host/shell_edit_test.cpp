#include "terminal/shell_edit.hpp"

#include <assert.h>
#include <stddef.h>
#include <string.h>

using namespace linux95;

struct CapturedOutput {
    char text[256];
    size_t length = 0;
};

static void put_char(void* context, char c)
{
    auto* output = static_cast<CapturedOutput*>(context);
    if (output->length + 1 >= sizeof(output->text)) return;
    output->text[output->length++] = c;
    output->text[output->length] = '\0';
}

static void test_edit_argument_validation()
{
    char path[filesystem::vfs::kPathCapacity]{};
    assert(shell::parse_edit_argument(nullptr, path) == shell::EditArgumentStatus::Missing);
    assert(shell::parse_edit_argument("   ", path) == shell::EditArgumentStatus::Missing);
    assert(shell::parse_edit_argument("DOCS/NOTE.TXT", path) == shell::EditArgumentStatus::Ok);
    assert(strcmp(path, "DOCS/NOTE.TXT") == 0);
    assert(shell::parse_edit_argument("NOTE.TXT OTHER.TXT", path) == shell::EditArgumentStatus::Extra);
    assert(path[0] == '\0');

    char at_limit[filesystem::vfs::kPathCapacity];
    memset(at_limit, 'p', sizeof(at_limit) - 1);
    at_limit[sizeof(at_limit) - 1] = '\0';
    assert(shell::parse_edit_argument(at_limit, path) == shell::EditArgumentStatus::Ok);
    assert(strlen(path) == filesystem::vfs::kPathCapacity - 1);
    char overlong[filesystem::vfs::kPathCapacity + 1];
    memset(overlong, 'p', filesystem::vfs::kPathCapacity);
    overlong[filesystem::vfs::kPathCapacity] = '\0';
    assert(shell::parse_edit_argument(overlong, path) == shell::EditArgumentStatus::TooLong);
    assert(path[0] == '\0');
}

static void test_vga_unavailable_consumes_editor_request()
{
    CapturedOutput captured{};
    terminal::Output output{&captured, put_char, nullptr, nullptr};
    shell::CommandResult request{};
    request.action = shell::CommandAction::OpenEditor;
    const char path[] = "NOTE.TXT";
    memcpy(request.path, path, sizeof(path));
    const shell::CommandResult result = shell::report_editor_unavailable(output, request);
    assert(result.action == shell::CommandAction::Continue);
    assert(strcmp(captured.text, "edit: unavailable in VGA shell\n") == 0);
}

int main()
{
    test_edit_argument_validation();
    test_vga_unavailable_consumes_editor_request();
    return 0;
}
