#include "terminal/shell_session.hpp"

namespace linux95::terminal {

ShellSession::ShellSession(
    Output& output,
    void* context,
    ExecuteCallback execute)
    : output_(output),
      execute_context_(context),
      execute_(execute),
      command_{},
      length_(0)
{
}

void ShellSession::prompt()
{
    write(
        output_,
        "linux95> ");
}

void ShellSession::begin()
{
    length_ = 0;
    command_[0] = '\0';

    prompt();
}

void ShellSession::on_char(char c)
{
    if (c == '\n') {
        if (output_.put_char != nullptr) {
            output_.put_char(
                output_.context,
                '\n');
        }

        command_[length_] = '\0';

        if (execute_ != nullptr) {
            execute_(
                execute_context_,
                output_,
                command_);
        }

        length_ = 0;
        command_[0] = '\0';

        prompt();
        return;
    }

    if (c == '\b') {
        if (length_ == 0) {
            return;
        }

        --length_;
        command_[length_] = '\0';

        if (output_.put_char != nullptr) {
            output_.put_char(
                output_.context,
                '\b');
        }

        return;
    }

    if (c < 32 ||
        c > 126) {
        return;
    }

    if (length_ + 1 >= kCommandCapacity) {
        return;
    }

    command_[length_] = c;
    ++length_;
    command_[length_] = '\0';

    if (output_.put_char != nullptr) {
        output_.put_char(
            output_.context,
            c);
    }
}

} // namespace linux95::terminal
