#include "overview.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/pointer/cursor/CursorShapeOverrideController.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprutils/math/Box.hpp>

#include "config.hpp"
#include "globals.hpp"
#include "layout/grid.hpp"
#include "layout/linear.hpp"
#include "src/desktop/state/FocusState.hpp"

HTView::HTView(MONITORID in_monitor_id) {
    monitor_id = in_monitor_id;
    active = false;
    closing = false;
    navigating = false;

    std::string val = HTConfig::value<Config::STRING>("layout");
    change_layout(val);
}

void HTView::change_layout(const std::string& layout_name) {
    if (layout != nullptr && layout->layout_name() == layout_name) {
        layout->init_position();
        return;
    }

    if (layout_name == "grid") {
        layout = makeShared<HTLayoutGrid>(monitor_id);
    } else if (layout_name == "linear") {
        layout = makeShared<HTLayoutLinear>(monitor_id);
    } else {
        fail_exit(
            "Bad overview layout name {}, supported ones are 'grid' and 'linear'",
            layout_name
        );
    }
}

void HTView::do_exit_behavior(bool exit_on_mouse) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr) //???
        return;

    auto try_get_hover_id = [this, &monitor]() -> std::optional<WorkspaceNumber> {
        const PHLMONITOR cursor_monitor =
            State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
        if (cursor_monitor != monitor)
            return std::nullopt;

        const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
        return layout->get_ws_id_from_global(mouse_coords);
    };

    const int EXIT_ON_HOVERED = HTConfig::value<Config::INTEGER>("exit_on_hovered");

    const auto ws_id = (exit_on_mouse || EXIT_ON_HOVERED)
        ? try_get_hover_id() : monitor->m_activeWorkspace->numberedID();
    if (!ws_id)
        return;
    PHLWORKSPACE workspace = State::workspaceState()->query()
        .numbered(Workspace::SWorkspaceNumberedID {*ws_id}).run();

    if (workspace == nullptr)
        workspace = State::workspaceState()->createNumbered(Workspace::SWorkspaceNumberedID {*ws_id}, monitor);
    if (workspace == nullptr)
        return;

    monitor->changeWorkspace(workspace);
    workspace->ready();
}

void HTView::show(bool recalculate) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->m_activeWorkspace;
    if (active_workspace == nullptr)
        return;
    ht_manager->cancel_pending_window_drag();

    const bool already_rendered = active || navigating;
    active = true;
    closing = false;
    navigating = false;

    if (recalculate && !already_rendered) {
        layout->init_position();
    }
    layout->on_show();

    Pointer::Cursor::overrideController->setOverride("left_ptr", Pointer::Cursor::CURSOR_OVERRIDE_UNKNOWN);

    g_pHyprRenderer->damageMonitor(monitor);
    monitor->scheduleFrame();
}

void HTView::hide(bool exit_on_mouse, std::optional<WorkspaceNumber> target_workspace) {
    ht_manager->cancel_pending_window_drag();
    if (!active && !navigating)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->m_activeWorkspace;
    if (active_workspace == nullptr)
        return;

    if (target_workspace.has_value()) {
        PHLWORKSPACE workspace = State::workspaceState()->query()
            .numbered(Workspace::SWorkspaceNumberedID {*target_workspace}).run();
        if (workspace == nullptr)
            workspace = State::workspaceState()->createNumbered(
                Workspace::SWorkspaceNumberedID {*target_workspace}, monitor
            );
        if (workspace == nullptr)
            return;
        monitor->changeWorkspace(workspace);
        workspace->ready();
    } else if (!closing) {
        do_exit_behavior(exit_on_mouse);
    }

    active = true;
    closing = true;
    navigating = false;

    // Finger-driven closing clears animation callbacks. Re-arm the final
    // transition on release even if the view is already marked as closing.
    layout->on_hide([this](auto self) {
        active = false;
        closing = false;
    });

    Pointer::Cursor::overrideController->unsetOverride(Pointer::Cursor::CURSOR_OVERRIDE_UNKNOWN);

    g_pHyprRenderer->damageMonitor(monitor);
    monitor->scheduleFrame();
}

void HTView::warp_window(Config::INTEGER warp, PHLWINDOW window) {
    // taken from Hyprland:
    // https://github.com/hyprwm/Hyprland/blob/ea42041f936d5810c5cfa45d6bece12dde2fd9b6/src/managers/KeybindManager.cpp#L1319
    if (warp > 0) {
        auto HLSurface = Desktop::View::CWLSurface::fromResource(g_pSeatManager->m_state.pointerFocus.lock());

        if (window && (!HLSurface || HLSurface->view()))
            window->warpCursor(warp == 2);
    }
}

void HTView::move_id(std::optional<WorkspaceNumber> ws_id, bool move_window) {
    navigating = false;
    if (closing || !ws_id)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->m_activeWorkspace;
    if (active_workspace == nullptr)
        return;
    const auto source_id = active_workspace->numberedID();
    if (!source_id)
        return;

    // FIXME: weird hovered window duplicate code
    PHLWINDOW hovered_window = ht_manager->get_window_from_cursor();
    bool should_move = true;
    if (hovered_window == nullptr && move_window)
        should_move = false;

    PHLWORKSPACE other_workspace = State::workspaceState()->query()
        .numbered(Workspace::SWorkspaceNumberedID {*ws_id}).run();
    if (other_workspace == nullptr)
        other_workspace = State::workspaceState()->createNumbered(
            Workspace::SWorkspaceNumberedID {*ws_id}, monitor
        );
    if (other_workspace == nullptr)
        return;

    monitor->changeWorkspace(other_workspace);
    other_workspace->ready();
    if (move_window && should_move) {
        Desktop::globalWindowController()->moveWindowToWorkspace(hovered_window, other_workspace);
    }

    Config::INTEGER warp;

    if (move_window) {
        Desktop::focusState()->fullWindowFocus(hovered_window, Desktop::FOCUS_REASON_CLICK);
        warp = *CConfigValue<Config::INTEGER>("plugin:hyprtasking:warp_on_move_window");
    } else {
        warp = *CConfigValue<Config::INTEGER>("cursor:warp_on_change_workspace");
    }
    warp_window(warp, hovered_window);

    navigating = true;
    layout->on_move(*source_id, *ws_id, [this](auto self) {
        navigating = false;
    });
}

void HTView::move(std::string arg, bool move_window) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->m_activeWorkspace;
    if (active_workspace == nullptr)
        return;
    PHLWINDOW hovered_window = ht_manager->get_window_from_cursor();
    if (hovered_window == nullptr && move_window)
        return;

    // if moving a window, the up/down/left/right should be relative to the window (and cursor) and not necessarily the active workspace
    const PHLWORKSPACE source_workspace =
        move_window ? hovered_window->m_workspace : active_workspace;
    if (!source_workspace)
        return;
    const auto source_ws_id = source_workspace->numberedID();
    if (!source_ws_id)
        return;
    if (!navigating && !active) {
        layout->init_position();
    } else {
        layout->build_overview_layout(HT_VIEW_CLOSED);
    }
    const auto it = layout->overview_layout.find(*source_ws_id);
    if (it == layout->overview_layout.end())
        return;
    const auto& ws_layout = it->second;
    const auto id = layout->get_ws_id_in_direction(ws_layout.x, ws_layout.y, arg);

    move_id(id, move_window);
}

PHLMONITOR HTView::get_monitor() {
    const PHLMONITOR monitor = State::monitorState()->query().id(monitor_id).run();
    if (monitor == nullptr)
        Log::logger->log(Log::WARN, "[Hyprtasking] Returning null monitor from get_monitor!");
    return monitor;
}
