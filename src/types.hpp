#pragma once

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprland/src/workspace/AbstractWorkspace.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>

// Grid slots refer only to numbered workspaces; absent slots use std::optional.
using WorkspaceNumber = Workspace::WorkspaceIDContainer;

using namespace Render;

typedef void (*render_workspace_t)(
    void* thisptr,
    CRenderContext& ctx,
    PHLMONITOR pMonitor,
    PHLWORKSPACE pWorkspace,
    const Time::steady_tp& now,
    const CBox& geometry
);

typedef bool (*should_render_window_t)(void* thisptr, PHLWINDOW pWindow, PHLMONITOR pMonitor);
typedef void (*render_window_t)(
    void* thisptr,
    CRenderContext& ctx,
    PHLWINDOW pWindow,
    PHLMONITOR pMonitor,
    const SWindowRenderPresentation& presentation,
    const Time::steady_tp& time,
    bool decorate,
    eRenderPassMode mode,
    bool ignorePosition,
    bool standalone
);

typedef long VIEWID;
