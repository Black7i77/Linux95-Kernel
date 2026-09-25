#include "gui/desktop.hpp"

#include "arch/debug.hpp"
#include "arch/io.hpp"
#include "arch/keyboard.hpp"
#include "arch/mouse.hpp"
#include "arch/pit.hpp"
#include "gui/system_info_app.hpp"
#include "gui/terminal_app.hpp"

namespace linux95::desktop {
namespace {

constexpr graphics::Color kDesktopColor{
    42, 104, 108,
};

constexpr graphics::Color kPanelColor{
    192, 192, 192,
};

constexpr graphics::Color kPanelDark{
    96, 96, 96,
};

constexpr graphics::Color kPanelLight{
    232, 232, 232,
};

constexpr graphics::Color kWindowFrame{
    192, 192, 192,
};

constexpr graphics::Color kFocusedTitle{
    0, 0, 128,
};

constexpr graphics::Color kInactiveTitle{
    96, 96, 96,
};

constexpr graphics::Color kWhite{
    255, 255, 255,
};

constexpr graphics::Color kBlack{
    0, 0, 0,
};

constexpr int32_t kTaskButtonWidth = 120;
constexpr int32_t kTaskButtonHeight = 24;

constexpr int32_t kMenuWidth = 180;
constexpr int32_t kMenuItemHeight = 24;

graphics::Rect to_graphics(
    gui::Rect rect)
{
    return graphics::Rect{
        rect.x,
        rect.y,
        rect.width,
        rect.height,
    };
}

bool intersects(
    graphics::Rect a,
    graphics::Rect b)
{
    return
        a.width > 0 &&
        a.height > 0 &&
        b.width > 0 &&
        b.height > 0 &&
        a.x < b.x + b.width &&
        b.x < a.x + a.width &&
        a.y < b.y + b.height &&
        b.y < a.y + a.height;
}

graphics::Rect intersection(
    graphics::Rect a,
    graphics::Rect b)
{
    int32_t left =
        a.x > b.x
            ? a.x
            : b.x;

    int32_t top =
        a.y > b.y
            ? a.y
            : b.y;

    int32_t right_a =
        a.x + a.width;

    int32_t right_b =
        b.x + b.width;

    int32_t right =
        right_a < right_b
            ? right_a
            : right_b;

    int32_t bottom_a =
        a.y + a.height;

    int32_t bottom_b =
        b.y + b.height;

    int32_t bottom =
        bottom_a < bottom_b
            ? bottom_a
            : bottom_b;

    if (
        right <= left ||
        bottom <= top) {
        return graphics::Rect{
            0,
            0,
            0,
            0,
        };
    }

    return graphics::Rect{
        left,
        top,
        right - left,
        bottom - top,
    };
}

graphics::Rect clip_to_screen(
    graphics::Rect rect)
{
    return intersection(
        rect,
        graphics::Rect{
            0,
            0,
            kScreenWidth,
            kScreenHeight,
        });
}

int32_t clamp_value(
    int32_t value,
    int32_t minimum,
    int32_t maximum)
{
    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}

gui::Rect menu_bounds()
{
    return gui::Rect{
        0,
        kPanelHeight,
        kMenuWidth,
        kMenuItemHeight * 2,
    };
}

gui::Rect task_button_bounds(
    size_t slot)
{
    return gui::Rect{
        350 +
            static_cast<int32_t>(
                slot) *
                kTaskButtonWidth,
        2,
        kTaskButtonWidth,
        kTaskButtonHeight,
    };
}

graphics::Rect status_bounds()
{
    return graphics::Rect{
        kScreenWidth - 180,
        0,
        180,
        kPanelHeight,
    };
}

void format_u64(
    uint64_t value,
    char* out,
    size_t capacity)
{
    if (capacity == 0) {
        return;
    }

    char reversed[21];
    size_t count = 0;

    do {
        reversed[count++] =
            static_cast<char>(
                '0' +
                (value % 10));

        value /= 10;
    } while (
        value != 0 &&
        count < sizeof(reversed));

    size_t out_count = 0;

    while (
        count > 0 &&
        out_count + 1 <
            capacity) {
        out[out_count++] =
            reversed[--count];
    }

    out[out_count] = '\0';
}

struct RuntimeState {
    graphics::Framebuffer& framebuffer;
    bool mouse_online;

    gui::WindowManager windows;

    gui::TerminalApp terminal_app;
    gui::SystemInfoApp system_info_app;

    gui::AppInstance terminal;
    gui::AppInstance system_info;

    DirtyRegionQueue dirty;

    gui::Point mouse_position;

    uint32_t cursor_backing[
        kCursorWidth *
        kCursorHeight];
    bool cursor_drawn;

    bool left_down;
    gui::WindowId pointer_window;

    bool menu_open;

    uint64_t last_uptime;

    RuntimeState(
        graphics::Framebuffer& framebuffer_in,
        bool mouse_online_in)
        : framebuffer(
              framebuffer_in),
          mouse_online(
              mouse_online_in),
          windows(),
          terminal_app(),
          system_info_app(),
          terminal(
              terminal_app.instance()),
          system_info(
              system_info_app.instance()),
          dirty(),
          mouse_position{
              kScreenWidth / 2,
              kScreenHeight / 2,
          },
          cursor_drawn(false),
          left_down(false),
          pointer_window(0),
          menu_open(false),
          last_uptime(0)
    {
    }
};

gui::AppInstance* app_for(
    RuntimeState& state,
    gui::WindowId id)
{
    if (id == kTerminalWindowId) {
        return &state.terminal;
    }

    if (
        id ==
        kSystemInfoWindowId) {
        return &state.system_info;
    }

    return nullptr;
}

const char* title_for(
    gui::WindowId id)
{
    if (id == kTerminalWindowId) {
        return "Terminal";
    }

    if (
        id ==
        kSystemInfoWindowId) {
        return "System Info";
    }

    return "Linux95";
}

void invalidate_window(
    RuntimeState& state,
    gui::WindowId id)
{
    const gui::Window* window =
        state.windows.find(id);

    if (
        window == nullptr ||
        window->state !=
            gui::WindowState::Open) {
        return;
    }

    state.dirty.invalidate(
        to_graphics(
            window->bounds));
}

void invalidate_panel(
    RuntimeState& state)
{
    state.dirty.invalidate(
        graphics::Rect{
            0,
            0,
            kScreenWidth,
            kPanelHeight,
        });
}

bool focus_runtime_window(
    RuntimeState& state,
    gui::WindowId id)
{
    const gui::WindowId old_focus =
        state.windows.focused();

    if (old_focus == id) {
        return true;
    }

    invalidate_window(
        state,
        old_focus);

    if (!state.windows.focus(id)) {
        return false;
    }

    invalidate_window(
        state,
        id);

    invalidate_panel(state);

    return true;
}

bool activate_runtime_window(
    RuntimeState& state,
    gui::WindowId id)
{
    const gui::WindowId old_focus =
        state.windows.focused();

    invalidate_window(
        state,
        old_focus);

    if (
        !activate_window(
            state.windows,
            id)) {
        return false;
    }

    invalidate_window(
        state,
        id);

    invalidate_panel(state);

    return true;
}

void draw_window(
    RuntimeState& state,
    const gui::Window& window,
    graphics::Rect dirty)
{
    if (
        window.state !=
            gui::WindowState::Open) {
        return;
    }

    const graphics::Rect bounds =
        to_graphics(
            window.bounds);

    if (
        !intersects(
            bounds,
            dirty)) {
        return;
    }

    graphics::fill_rect(
        state.framebuffer,
        bounds,
        kWindowFrame);

    const graphics::Rect title{
        window.bounds.x,
        window.bounds.y,
        window.bounds.width,
        kTitleBarHeight,
    };

    const bool focused =
        state.windows.focused() ==
        window.id;

    graphics::fill_rect(
        state.framebuffer,
        title,
        focused
            ? kFocusedTitle
            : kInactiveTitle);

    graphics::draw_text(
        state.framebuffer,
        window.bounds.x + 6,
        window.bounds.y + 8,
        title_for(window.id),
        kWhite);

    const graphics::Rect close_button{
        window.bounds.x +
            window.bounds.width -
            kChromeButtonWidth,
        window.bounds.y + 3,
        kChromeButtonWidth,
        kChromeButtonHeight,
    };

    const graphics::Rect minimize_button{
        close_button.x -
            kChromeButtonWidth,
        close_button.y,
        kChromeButtonWidth,
        kChromeButtonHeight,
    };

    graphics::fill_rect(
        state.framebuffer,
        minimize_button,
        kPanelColor);

    graphics::draw_rect(
        state.framebuffer,
        minimize_button,
        kBlack);

    graphics::draw_text(
        state.framebuffer,
        minimize_button.x + 6,
        minimize_button.y + 5,
        "_",
        kBlack);

    if (window.closable) {
        graphics::fill_rect(
            state.framebuffer,
            close_button,
            kPanelColor);

        graphics::draw_rect(
            state.framebuffer,
            close_button,
            kBlack);

        graphics::draw_text(
            state.framebuffer,
            close_button.x + 6,
            close_button.y + 5,
            "X",
            kBlack);
    }

    gui::AppInstance* app =
        app_for(
            state,
            window.id);

    if (
        app != nullptr &&
        app->callbacks.draw !=
            nullptr) {
        const gui::Rect gui_content =
            content_rect(
                window.bounds);

        const graphics::Rect content =
            to_graphics(
                gui_content);

        if (
            intersects(
                content,
                dirty)) {
            app->callbacks.draw(
                app->context,
                state.framebuffer,
                content);
        }
    }

    graphics::draw_rect(
        state.framebuffer,
        bounds,
        kBlack);
}

void draw_task_button(
    RuntimeState& state,
    gui::WindowId id,
    size_t slot)
{
    const gui::Window* window =
        state.windows.find(id);

    if (window == nullptr) {
        return;
    }

    const gui::Rect button_gui =
        task_button_bounds(slot);

    const graphics::Rect button =
        to_graphics(button_gui);

    graphics::fill_rect(
        state.framebuffer,
        button,
        state.windows.focused() == id
            ? kPanelLight
            : kPanelColor);

    graphics::draw_rect(
        state.framebuffer,
        button,
        kPanelDark);

    graphics::draw_text(
        state.framebuffer,
        button.x + 6,
        button.y + 8,
        title_for(id),
        kBlack);
}

void draw_panel(
    RuntimeState& state,
    graphics::Rect dirty)
{
    const graphics::Rect panel{
        0,
        0,
        kScreenWidth,
        kPanelHeight,
    };

    if (
        intersects(
            panel,
            dirty)) {
        graphics::draw_text(
            state.framebuffer,
            6,
            10,
            "Applications",
            kBlack);

        graphics::draw_text(
            state.framebuffer,
            130,
            10,
            "Terminal",
            kBlack);

        graphics::draw_text(
            state.framebuffer,
            230,
            10,
            "System Info",
            kBlack);

        size_t task_slot = 0;

        if (
            state.windows.find(
                kTerminalWindowId) !=
            nullptr) {
            draw_task_button(
                state,
                kTerminalWindowId,
                task_slot++);

        }

        if (
            state.windows.find(
                kSystemInfoWindowId) !=
            nullptr) {
            draw_task_button(
                state,
                kSystemInfoWindowId,
                task_slot++);
        }

        char uptime[21];

        format_u64(
            state.last_uptime,
            uptime,
            sizeof(uptime));

        graphics::draw_text(
            state.framebuffer,
            kScreenWidth - 170,
            10,
            "Uptime",
            kBlack);

        graphics::draw_text(
            state.framebuffer,
            kScreenWidth - 112,
            10,
            uptime,
            kBlack);

        graphics::draw_text(
            state.framebuffer,
            kScreenWidth - 48,
            10,
            "s",
            kBlack);
    }

    if (!state.menu_open) {
        return;
    }

    const gui::Rect menu_gui =
        menu_bounds();

    const graphics::Rect menu =
        to_graphics(
            menu_gui);

    if (
        !intersects(
            menu,
            dirty)) {
        return;
    }

    graphics::fill_rect(
        state.framebuffer,
        menu,
        kPanelColor);

    graphics::draw_rect(
        state.framebuffer,
        menu,
        kBlack);

    graphics::draw_text(
        state.framebuffer,
        menu.x + 8,
        menu.y + 8,
        "Terminal",
        kBlack);

    graphics::draw_line(
        state.framebuffer,
        menu.x,
        menu.y +
            kMenuItemHeight,
        menu.x +
            menu.width - 1,
        menu.y +
            kMenuItemHeight,
        kPanelDark);

    graphics::draw_text(
        state.framebuffer,
        menu.x + 8,
        menu.y +
            kMenuItemHeight + 8,
        "System Info",
        kBlack);
}

uint32_t read_framebuffer_pixel(
    RuntimeState& state,
    int32_t x,
    int32_t y)
{
    const uint64_t offset =
        static_cast<uint64_t>(y) *
            state.framebuffer.pitch +
        static_cast<uint64_t>(x) *
            sizeof(uint32_t);

    volatile uint32_t* const pixel =
        reinterpret_cast<volatile uint32_t*>(
            state.framebuffer.data +
            offset);

    return *pixel;
}

void write_framebuffer_pixel(
    RuntimeState& state,
    int32_t x,
    int32_t y,
    uint32_t value)
{
    const uint64_t offset =
        static_cast<uint64_t>(y) *
            state.framebuffer.pitch +
        static_cast<uint64_t>(x) *
            sizeof(uint32_t);

    volatile uint32_t* const pixel =
        reinterpret_cast<volatile uint32_t*>(
            state.framebuffer.data +
            offset);

    *pixel = value;
}

void restore_cursor(
    RuntimeState& state)
{
    if (
        !state.mouse_online ||
        !state.cursor_drawn) {
        return;
    }

    size_t index = 0;

    for (
        int32_t y = 0;
        y < kCursorHeight;
        ++y) {
        for (
            int32_t x = 0;
            x < kCursorWidth;
            ++x) {
            write_framebuffer_pixel(
                state,
                state.mouse_position.x + x,
                state.mouse_position.y + y,
                state.cursor_backing[index++]);
        }
    }

    state.cursor_drawn = false;
}

void draw_cursor_overlay(
    RuntimeState& state)
{
    if (
        !state.mouse_online ||
        state.cursor_drawn) {
        return;
    }

    size_t index = 0;

    for (
        int32_t y = 0;
        y < kCursorHeight;
        ++y) {
        for (
            int32_t x = 0;
            x < kCursorWidth;
            ++x) {
            state.cursor_backing[index++] =
                read_framebuffer_pixel(
                    state,
                    state.mouse_position.x + x,
                    state.mouse_position.y + y);
        }
    }

    const int32_t x =
        state.mouse_position.x;

    const int32_t y =
        state.mouse_position.y;

    // White outside edge keeps the cursor visible over the
    // black terminal; black inside edge keeps it visible over
    // bright windows and the panel.
    graphics::draw_line(
        state.framebuffer,
        x,
        y,
        x,
        y + kCursorHeight - 1,
        kWhite);

    graphics::draw_line(
        state.framebuffer,
        x,
        y,
        x + kCursorWidth - 1,
        y + 10,
        kWhite);

    graphics::draw_line(
        state.framebuffer,
        x,
        y + kCursorHeight - 1,
        x + 4,
        y + 13,
        kWhite);

    graphics::draw_line(
        state.framebuffer,
        x + 1,
        y + 1,
        x + 1,
        y + 12,
        kBlack);

    graphics::draw_line(
        state.framebuffer,
        x + 1,
        y + 1,
        x + kCursorWidth - 2,
        y + 10,
        kBlack);

    state.cursor_drawn = true;
}

void redraw_region(
    RuntimeState& state,
    graphics::Rect dirty)
{
    dirty =
        clip_to_screen(dirty);

    if (
        dirty.width <= 0 ||
        dirty.height <= 0) {
        return;
    }

    // 1. Desktop background and panel base.
    graphics::fill_rect(
        state.framebuffer,
        dirty,
        kDesktopColor);

    const graphics::Rect panel{
        0,
        0,
        kScreenWidth,
        kPanelHeight,
    };

    const graphics::Rect panel_dirty =
        intersection(
            dirty,
            panel);

    if (
        panel_dirty.width > 0 &&
        panel_dirty.height > 0) {
        graphics::fill_rect(
            state.framebuffer,
            panel_dirty,
            kPanelColor);
    }

    // 2. Open windows in ascending z order.
    const gui::Window* ordered[2];
    size_t ordered_count = 0;

    const gui::Window* terminal =
        state.windows.find(
            kTerminalWindowId);

    if (
        terminal != nullptr &&
        terminal->state ==
            gui::WindowState::Open) {
        ordered[ordered_count++] =
            terminal;
    }

    const gui::Window* system_info =
        state.windows.find(
            kSystemInfoWindowId);

    if (
        system_info != nullptr &&
        system_info->state ==
            gui::WindowState::Open) {
        ordered[ordered_count++] =
            system_info;
    }

    if (
        ordered_count == 2 &&
        ordered[0]->z >
            ordered[1]->z) {
        const gui::Window* temp =
            ordered[0];

        ordered[0] =
            ordered[1];

        ordered[1] =
            temp;
    }

    for (
        size_t i = 0;
        i < ordered_count;
        ++i) {
        draw_window(
            state,
            *ordered[i],
            dirty);
    }

    // 3. Panel controls, task buttons and menu.
    draw_panel(
        state,
        dirty);

}

bool redraw_dirty(
    RuntimeState& state)
{
    const size_t count =
        state.dirty.size();

    if (count == 0) {
        return false;
    }

    restore_cursor(state);

    for (
        size_t i = 0;
        i < count;
        ++i) {
        redraw_region(
            state,
            state.dirty.rect(i));
    }

    state.dirty.clear();

    draw_cursor_overlay(state);

    return true;
}

gui::WindowId task_window_at(
    RuntimeState& state,
    gui::Point point)
{
    size_t slot = 0;

    if (
        state.windows.find(
            kTerminalWindowId) !=
        nullptr) {
        if (
            gui::contains(
                task_button_bounds(
                    slot),
                point)) {
            return kTerminalWindowId;
        }

        ++slot;
    }

    if (
        state.windows.find(
            kSystemInfoWindowId) !=
        nullptr) {
        if (
            gui::contains(
                task_button_bounds(
                    slot),
                point)) {
            return kSystemInfoWindowId;
        }
    }

    return 0;
}

bool handle_panel_or_menu_click(
    RuntimeState& state,
    gui::Point point)
{
    const PanelHit hit =
        panel_hit(point);

    if (state.menu_open) {
        const gui::Rect menu =
            menu_bounds();

        if (
            gui::contains(
                menu,
                point)) {
            const gui::WindowId id =
                point.y <
                    menu.y +
                        kMenuItemHeight
                    ? kTerminalWindowId
                    : kSystemInfoWindowId;

            state.menu_open = false;

            state.dirty.invalidate(
                to_graphics(menu));

            activate_runtime_window(
                state,
                id);

            return true;
        }

        if (
            hit ==
            PanelHit::Applications) {
            state.menu_open = false;

            state.dirty.invalidate(
                to_graphics(menu));

            invalidate_panel(state);

            return true;
        }

        state.menu_open = false;

        state.dirty.invalidate(
            to_graphics(menu));
    }

    if (hit == PanelHit::None) {
        return false;
    }

    if (
        hit ==
        PanelHit::Applications) {
        state.menu_open = true;

        invalidate_panel(state);

        state.dirty.invalidate(
            to_graphics(
                menu_bounds()));

        return true;
    }

    if (
        hit ==
        PanelHit::Terminal) {
        activate_runtime_window(
            state,
            kTerminalWindowId);

        return true;
    }

    if (
        hit ==
        PanelHit::SystemInfo) {
        activate_runtime_window(
            state,
            kSystemInfoWindowId);

        return true;
    }

    if (
        hit ==
        PanelHit::TaskArea) {
        const gui::WindowId id =
            task_window_at(
                state,
                point);

        if (id != 0) {
            activate_runtime_window(
                state,
                id);
        }

        return true;
    }

    return true;
}

void handle_window_mouse_down(
    RuntimeState& state,
    gui::Point point)
{
    const gui::WindowId id =
        state.windows.hit_test(
            point);

    if (id == 0) {
        return;
    }

    const gui::Window* window =
        state.windows.find(id);

    if (window == nullptr) {
        return;
    }

    const ChromeHit hit =
        chrome_hit(
            window->bounds,
            point,
            window->resizable,
            window->closable);

    if (hit == ChromeHit::Close) {
        const graphics::Rect old_bounds =
            to_graphics(
                window->bounds);

        gui::AppInstance* app =
            app_for(
                state,
                id);

        if (
            app != nullptr &&
            app->callbacks.on_close !=
                nullptr) {
            app->callbacks.on_close(
                app->context);
        }

        if (
            state.windows.close(id)) {
            state.dirty.invalidate(
                old_bounds);

            invalidate_window(
                state,
                state.windows.focused());

            invalidate_panel(state);
        }

        return;
    }

    if (
        hit ==
        ChromeHit::Minimize) {
        const graphics::Rect old_bounds =
            to_graphics(
                window->bounds);

        if (
            state.windows.minimize(id)) {
            state.dirty.invalidate(
                old_bounds);

            invalidate_window(
                state,
                state.windows.focused());

            invalidate_panel(state);
        }

        return;
    }

    if (
        hit ==
        ChromeHit::Resize) {
        focus_runtime_window(
            state,
            id);

        if (
            state.windows.begin_resize(
                id,
                point)) {
            state.pointer_window = id;
        }

        return;
    }

    if (
        hit ==
        ChromeHit::TitleBar) {
        focus_runtime_window(
            state,
            id);

        if (
            state.windows.begin_drag(
                id,
                point)) {
            state.pointer_window = id;
        }

        return;
    }

    if (
        hit ==
        ChromeHit::Content) {
        focus_runtime_window(
            state,
            id);
    }
}

void process_mouse_event(
    RuntimeState& state,
    const mouse::MouseEvent& event)
{
    restore_cursor(state);

    gui::Point next{
        state.mouse_position.x +
            static_cast<int32_t>(
                event.dx),

        state.mouse_position.y -
            static_cast<int32_t>(
                event.dy),
    };

    state.mouse_position =
        clamp_cursor(next);

    if (
        state.pointer_window != 0 &&
        state.left_down &&
        event.left) {
        const gui::Window* before =
            state.windows.find(
                state.pointer_window);

        graphics::Rect old_bounds{
            0,
            0,
            0,
            0,
        };

        if (before != nullptr) {
            old_bounds =
                to_graphics(
                    before->bounds);
        }

        state.windows.pointer_move(
            state.mouse_position);

        const gui::Window* after =
            state.windows.find(
                state.pointer_window);

        if (before != nullptr) {
            state.dirty.invalidate(
                old_bounds);
        }

        if (after != nullptr) {
            state.dirty.invalidate(
                to_graphics(
                    after->bounds));
        }
    }

    const bool pressed =
        event.left &&
        !state.left_down;

    const bool released =
        !event.left &&
        state.left_down;

    if (pressed) {
        if (
            !handle_panel_or_menu_click(
                state,
                state.mouse_position)) {
            handle_window_mouse_down(
                state,
                state.mouse_position);
        }
    }

    if (released) {
        state.windows.end_pointer_action();
        state.pointer_window = 0;
    }

    state.left_down =
        event.left;

}

void process_keyboard(
    RuntimeState& state)
{
    while (keyboard::has_char()) {
        const char c =
            keyboard::read_char();

        const gui::WindowId focused =
            state.windows.focused();

        if (
            route_key(
                state.windows,
                state.terminal,
                state.system_info,
                c) &&
            focused ==
                kTerminalWindowId) {
            invalidate_window(
                state,
                focused);
        }
    }
}

} // namespace

PanelHit panel_hit(
    gui::Point point)
{
    if (
        point.y < 0 ||
        point.y >= kPanelHeight ||
        point.x < 0 ||
        point.x >= kScreenWidth) {
        return PanelHit::None;
    }

    if (point.x <= 119) {
        return PanelHit::Applications;
    }

    if (point.x <= 219) {
        return PanelHit::Terminal;
    }

    if (point.x <= 339) {
        return PanelHit::SystemInfo;
    }

    if (point.x < 350) {
        return PanelHit::None;
    }

    if (
        point.x >=
        kScreenWidth - 180) {
        return PanelHit::Status;
    }

    return PanelHit::TaskArea;
}

gui::Point clamp_cursor(
    gui::Point point)
{
    point.x =
        clamp_value(
            point.x,
            0,
            kScreenWidth -
                kCursorWidth);

    point.y =
        clamp_value(
            point.y,
            0,
            kScreenHeight -
                kCursorHeight);

    return point;
}

graphics::Rect cursor_rect_at(
    gui::Point point)
{
    return graphics::Rect{
        point.x,
        point.y,
        kCursorWidth,
        kCursorHeight,
    };
}

gui::Rect terminal_default_bounds()
{
    return gui::Rect{
        40,
        70,
        760,
        430,
    };
}

gui::Rect system_info_default_bounds()
{
    return gui::Rect{
        830,
        90,
        410,
        470,
    };
}

bool initialize_default_windows(
    gui::WindowManager& windows)
{
    if (
        !windows.add_window(
            kTerminalWindowId,
            terminal_default_bounds(),
            true,
            true)) {
        return false;
    }

    if (
        !windows.add_window(
            kSystemInfoWindowId,
            system_info_default_bounds(),
            true,
            true)) {
        return false;
    }

    return windows.focus(
        kTerminalWindowId);
}

bool activate_window(
    gui::WindowManager& windows,
    gui::WindowId id)
{
    const gui::Window* window =
        windows.find(id);

    if (window != nullptr) {
        if (
            window->state ==
            gui::WindowState::Minimized) {
            return windows.restore(id);
        }

        if (
            window->state ==
            gui::WindowState::Open) {
            return windows.focus(id);
        }

        return false;
    }

    gui::Rect bounds{};

    if (id == kTerminalWindowId) {
        bounds =
            terminal_default_bounds();
    } else if (
        id ==
        kSystemInfoWindowId) {
        bounds =
            system_info_default_bounds();
    } else {
        return false;
    }

    if (
        !windows.add_window(
            id,
            bounds,
            true,
            true)) {
        return false;
    }

    return windows.focus(id);
}

bool route_key(
    const gui::WindowManager& windows,
    gui::AppInstance& terminal,
    gui::AppInstance& system_info,
    char c)
{
    const gui::WindowId focused =
        windows.focused();

    const gui::Window* window =
        windows.find(focused);

    if (
        window == nullptr ||
        window->state !=
            gui::WindowState::Open) {
        return false;
    }

    gui::AppInstance* app = nullptr;

    if (focused == kTerminalWindowId) {
        app = &terminal;
    } else if (
        focused ==
        kSystemInfoWindowId) {
        app = &system_info;
    } else {
        return false;
    }

    if (
        app->callbacks.on_key ==
        nullptr) {
        return false;
    }

    app->callbacks.on_key(
        app->context,
        c);

    return true;
}

gui::Rect content_rect(
    gui::Rect bounds)
{
    return gui::Rect{
        bounds.x,
        bounds.y + kTitleBarHeight,
        bounds.width,
        bounds.height - kTitleBarHeight,
    };
}

ChromeHit chrome_hit(
    gui::Rect bounds,
    gui::Point point,
    bool resizable,
    bool closable)
{
    if (!gui::contains(bounds, point)) {
        return ChromeHit::None;
    }

    const int32_t right =
        bounds.x + bounds.width;

    const int32_t bottom =
        bounds.y + bounds.height;

    const gui::Rect close_button{
        right - kChromeButtonWidth,
        bounds.y + 3,
        kChromeButtonWidth,
        kChromeButtonHeight,
    };

    const gui::Rect minimize_button{
        right -
            (kChromeButtonWidth * 2),
        bounds.y + 3,
        kChromeButtonWidth,
        kChromeButtonHeight,
    };

    if (
        closable &&
        gui::contains(
            close_button,
            point)) {
        return ChromeHit::Close;
    }

    if (
        gui::contains(
            minimize_button,
            point)) {
        return ChromeHit::Minimize;
    }

    if (
        resizable &&
        (
            point.x >=
                right - kResizeGrip ||
            point.y >=
                bottom - kResizeGrip
        )) {
        return ChromeHit::Resize;
    }

    if (
        point.y <
        bounds.y +
            kTitleBarHeight) {
        return ChromeHit::TitleBar;
    }

    return ChromeHit::Content;
}

DirtyRegionQueue::DirtyRegionQueue()
    : count_(0)
{
}

void DirtyRegionQueue::invalidate(
    graphics::Rect region)
{
    const graphics::Rect clipped =
        clip_to_screen(region);

    if (
        clipped.width <= 0 ||
        clipped.height <= 0) {
        return;
    }

    if (
        count_ >=
        kMaxDirtyRects) {
        rects_[0] =
            graphics::Rect{
                0,
                0,
                kScreenWidth,
                kScreenHeight,
            };

        count_ = 1;
        return;
    }

    rects_[count_] = clipped;
    ++count_;
}

void DirtyRegionQueue::clear()
{
    count_ = 0;
}

size_t DirtyRegionQueue::size() const
{
    return count_;
}

graphics::Rect DirtyRegionQueue::rect(
    size_t index) const
{
    if (index >= count_) {
        return graphics::Rect{
            0,
            0,
            0,
            0,
        };
    }

    return rects_[index];
}

[[noreturn]] void run(
    graphics::Framebuffer& framebuffer,
    bool mouse_online)
{
    RuntimeState state(
        framebuffer,
        mouse_online);

    debug::write(
        "[PASS] terminal_app_ready\n");

    debug::write(
        "[PASS] system_info_app_ready\n");

    if (
        !initialize_default_windows(
            state.windows)) {
        debug::write(
            "[PANIC] desktop_windows\n");

        for (;;) {
            io::halt();
        }
    }

    state.last_uptime =
        pit::uptime_seconds();

    state.dirty.invalidate(
        graphics::Rect{
            0,
            0,
            kScreenWidth,
            kScreenHeight,
        });

    redraw_dirty(state);

    debug::write(
        "[PASS] desktop_online\n");

    for (;;) {
        bool had_event = false;

        if (state.mouse_online) {
            while (mouse::has_event()) {
                had_event = true;

                const mouse::MouseEvent event =
                    mouse::read_event();

                process_mouse_event(
                    state,
                    event);
            }
        }

        if (keyboard::has_char()) {
            had_event = true;
            process_keyboard(state);
        }

        const uint64_t uptime =
            pit::uptime_seconds();

        if (
            uptime !=
            state.last_uptime) {
            state.last_uptime =
                uptime;

            state.dirty.invalidate(
                status_bounds());

            had_event = true;
        }

        const bool had_dirty =
            redraw_dirty(state);

        if (
            state.mouse_online &&
            !state.cursor_drawn) {
            draw_cursor_overlay(state);
        }

        if (
            !had_event &&
            !had_dirty) {
            io::halt();
        }
    }
}

} // namespace linux95::desktop
