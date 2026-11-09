#include "grid.hpp"

#include <algorithm>
#include <charconv>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/config/shared/workspace/WorkspaceRuleManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>
#include <limits>
#include <unordered_set>
#include <utility>

#include "../config.hpp"
#include "../globals.hpp"
#include "../overview.hpp"
#include "../render.hpp"
#include "../types.hpp"
#include "src/layout/target/Target.hpp"

using Hyprutils::Utils::CScopeGuard;

HTLayoutGrid::HTLayoutGrid(VIEWID new_view_id) : HTLayoutBase(new_view_id) {
    auto &anim_tree = Config::animationTree();
    transition_config = makeShared<Hyprutils::Animation::SAnimationPropertyConfig>();
    Animation::mgr()->createAnimation(
        {0, 0},
        offset,
        anim_tree->getAnimationPropertyConfig("workspaces"),
        AVARDAMAGE_NONE
    );
    Animation::mgr()->createAnimation(
        1.f,
        scale,
        anim_tree->getAnimationPropertyConfig("workspaces"),
        AVARDAMAGE_NONE
    );

    refresh_workspace_cache();
    init_position();
}

void HTLayoutGrid::animate_to(
    Vector2D target_offset,
    float target_scale,
    float max_duration_ms,
    CallbackFun on_complete,
    bool workspace_transition
) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const Vector2D current_offset = offset->value();
    const float current_scale = scale->value();
    const double open_scale =
        calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x;
    const double distance = std::max({
        std::abs(target_offset.x - current_offset.x) / monitor->m_transformedSize.x,
        std::abs(target_offset.y - current_offset.y) / monitor->m_transformedSize.y,
        std::abs(target_scale - current_scale) / std::max(1.0 - open_scale, 0.001),
    });

    // Give the remaining distance a bounded duration, not the inherited global
    // workspace duration (which can leave a fast swipe settling for 800ms).
    const auto inherited = Config::animationTree()->getAnimationPropertyConfig("workspaces");
    *transition_config = *inherited->pValues;
    transition_config->pValues = transition_config;
    transition_config->internalSpeed = std::clamp(
                                           max_duration_ms * static_cast<float>(distance),
                                           workspace_transition ? 60.f : 16.f,
                                           max_duration_ms
                                       )
        / 100.f;
    if (workspace_transition)
        transition_config->internalBezier = "linear";

    // Freeze the displayed pose before replacing a target. Old close/settle
    // callbacks must never finish a newer opening or a new finger-driven swipe.
    offset->resetAllCallbacks();
    scale->resetAllCallbacks();
    offset->setValueAndWarp(current_offset);
    scale->setValueAndWarp(current_scale);
    offset->setConfig(transition_config);
    scale->setConfig(transition_config);
    *scale = target_scale;
    *offset = target_offset;
    if (on_complete != nullptr) {
        if (offset->isBeingAnimated())
            offset->setCallbackOnEnd(std::move(on_complete));
        else
            scale->setCallbackOnEnd(std::move(on_complete));
    }
}

// Bit layout: [layer:24][y:20][x:20]. Used purely as an unordered_map key;
// limits are implicit and not enforced (grid dims are not validated).
long long HTLayoutGrid::pack_slot(int layer, int x, int y) {
    return ((long long)layer << 40) | ((long long)(uint32_t)y << 20) | (long long)(uint32_t)x;
}

std::optional<WorkspaceNumber> HTLayoutGrid::slot_workspace(int layer, int x, int y) {
    const auto it = slot_ws_cache.find(pack_slot(layer, x, y));
    if (it == slot_ws_cache.end())
        return std::nullopt;
    return it->second;
}

void HTLayoutGrid::refresh_workspace_cache(
    const std::unordered_set<WorkspaceNumber>& extra_off_limits
) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const int ROWS = HTConfig::value<Config::INTEGER>("grid:rows");
    const int COLS = HTConfig::value<Config::INTEGER>("grid:cols");
    const int LAYERS = HTConfig::value<Config::INTEGER>("grid:layers");
    if (ROWS <= 0 || COLS <= 0 || LAYERS <= 0)
        return;

    const auto prior = ws_slot_cache;

    ws_slot_cache.clear();
    slot_ws_cache.clear();

    std::vector<HTGridSlot> slots;
    slots.reserve((size_t)LAYERS * ROWS * COLS);
    for (int l = 0; l < LAYERS; l++)
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                slots.push_back(HTGridSlot {l, x, y});

    std::vector<bool> taken(slots.size(), false);

    auto place = [&](WorkspaceNumber id, size_t slot_idx) {
        const HTGridSlot& s = slots[slot_idx];
        ws_slot_cache[id] = s;
        slot_ws_cache[pack_slot(s.layer, s.x, s.y)] = id;
        taken[slot_idx] = true;
    };

    auto find_slot_index = [&](const HTGridSlot& s) -> long long {
        for (size_t i = 0; i < slots.size(); i++)
            if (slots[i].layer == s.layer && slots[i].x == s.x && slots[i].y == s.y)
                return (long long)i;
        return -1;
    };

    auto next_free_slot = [&](size_t& cursor) -> long long {
        while (cursor < slots.size() && taken[cursor])
            cursor++;
        if (cursor >= slots.size())
            return -1;
        return (long long)cursor;
    };

    auto place_with_prior = [&](WorkspaceNumber id, size_t& cursor) -> bool {
        if (ws_slot_cache.count(id))
            return false;
        const auto pit = prior.find(id);
        if (pit != prior.end()) {
            const long long idx = find_slot_index(pit->second);
            if (idx >= 0 && !taken[(size_t)idx]) {
                place(id, (size_t)idx);
                return true;
            }
        }
        const long long idx = next_free_slot(cursor);
        if (idx < 0)
            return false;
        place(id, (size_t)idx);
        return true;
    };

    // No two grids may map the same numbered workspace, else dragging into a slot
    // could silently switch monitors. extra_off_limits carries IDs already
    // claimed by sibling views in this refresh.
    std::unordered_set<WorkspaceNumber> off_limits = extra_off_limits;
    const auto& ws_manager = Config::workspaceRuleMgr();
    const auto& all_rules = ws_manager->getAllWorkspaceRules();
    std::vector<std::pair<WorkspaceNumber, SP<Config::CWorkspaceRule>>> numbered_rules;
    numbered_rules.reserve(all_rules.size());
    for (const auto& rule : all_rules) {
        const auto& selector = rule->m_workspaceString;
        WorkspaceNumber number = 0;
        const auto [end, error] = std::from_chars(
            selector.data(), selector.data() + selector.size(), number
        );
        if (error != std::errc {} || end != selector.data() + selector.size() || number == 0)
            continue;
        off_limits.insert(number);
        numbered_rules.emplace_back(number, rule);
    }
    for (const auto& w : State::workspaceState()->workspaces()) {
        if (w == nullptr)
            continue;
        if (w->monitorID() != view_id) {
            if (const auto number = w->numberedID())
                off_limits.insert(*number);
        }
    }

    size_t cursor = 0;

    // Sort numbered selectors independently of config-line order.
    std::sort(numbered_rules.begin(), numbered_rules.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    for (const auto& [number, rule] : numbered_rules) {
        if (extra_off_limits.count(number))
            continue;
        const auto bound = ws_manager->getBoundMonitorForWS(rule->m_workspaceString);
        if (bound == nullptr || bound->m_id != view_id)
            continue;
        place_with_prior(number, cursor);
    }

    // Sort by numbered identity independently of Hyprland's workspace storage order.
    std::vector<PHLWORKSPACE> on_monitor;
    for (const auto& w : State::workspaceState()->workspacesCopy()) {
        if (w == nullptr)
            continue;
        if (w->monitorID() != view_id)
            continue;
        const auto number = w->numberedID();
        if (!number)
            continue;
        if (extra_off_limits.count(*number))
            continue;
        on_monitor.push_back(w);
    }
    std::sort(on_monitor.begin(), on_monitor.end(),
              [](const PHLWORKSPACE& a, const PHLWORKSPACE& b) {
                  return *a->numberedID() < *b->numberedID();
              });
    // Settle workspaces that still have a free prior slot before assigning
    // anyone via the cursor — otherwise a migrated workspace with no prior
    // here would steal slot 0 and displace this monitor's resident at (0,0).
    std::vector<PHLWORKSPACE> needs_cursor;
    for (const auto& w : on_monitor) {
        const auto pit = prior.find(*w->numberedID());
        if (pit != prior.end()) {
            const long long idx = find_slot_index(pit->second);
            if (idx >= 0 && !taken[(size_t)idx]) {
                place(*w->numberedID(), (size_t)idx);
                continue;
            }
        }
        needs_cursor.push_back(w);
    }
    for (const auto& w : needs_cursor)
        place_with_prior(*w->numberedID(), cursor);

    WorkspaceNumber synth_candidate = 1;
    auto next_synth = [&]() -> WorkspaceNumber {
        while (true) {
            if (off_limits.count(synth_candidate) || ws_slot_cache.count(synth_candidate)) {
                synth_candidate++;
                continue;
            }
            return synth_candidate++;
        }
    };

    for (size_t i = 0; i < slots.size(); i++) {
        if (taken[i])
            continue;
        const WorkspaceNumber id = next_synth();
        place(id, i);
    }
}

std::string HTLayoutGrid::layout_name() {
    return "grid";
}

std::optional<WorkspaceNumber> HTLayoutGrid::get_ws_id_in_direction(int x, int y, std::string& direction) {
    const int LOOP = HTConfig::value<Config::INTEGER>("grid:loop");
    const int ROWS = HTConfig::value<Config::INTEGER>("grid:rows");
    const int COLS = HTConfig::value<Config::INTEGER>("grid:cols");

    if (direction == "up") {
        y--;
    } else if (direction == "down") {
        y++;
    } else if (direction == "right") {
        x++;
    } else if (direction == "left") {
        x--;
    } else {
        return std::nullopt;
    }

    if (LOOP) {
        x = (x + COLS) % COLS;
        y = (y + ROWS) % ROWS;
    }
    return get_ws_id_from_xy(x, y);
}

void HTLayoutGrid::on_move_swipe(Vector2D delta) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const float MOVE_DISTANCE = HTConfig::value<Config::FLOAT>("gestures:move_distance");
    const int ROWS = HTConfig::value<Config::INTEGER>("grid:rows");
    const int COLS = HTConfig::value<Config::INTEGER>("grid:cols");
    const CBox min_ws = calculate_ws_box(0, 0, HT_VIEW_CLOSED);
    const CBox max_ws = calculate_ws_box(COLS - 1, ROWS - 1, HT_VIEW_CLOSED);

    swipe_accumulated += delta / MOVE_DISTANCE;
    Vector2D filtered = swipe_accumulated;
    static const CConfigValue<Config::INTEGER> MOVE_RAILS("plugin:hyprtasking:gestures:move_rails");
    if (*MOVE_RAILS)
        grid_swipe_project(filtered.x, filtered.y, swipe_rail);
    filtered = {grid_swipe_displacement(filtered.x), grid_swipe_displacement(filtered.y)};
    const Vector2D movement = filtered - swipe_applied;
    swipe_applied = filtered;
    Vector2D new_offset = offset->value() + movement * Vector2D {max_ws.w, max_ws.h};
    new_offset = new_offset.clamp(Vector2D {-max_ws.x, -max_ws.y}, Vector2D {-min_ws.x, -min_ws.y});

    offset->resetAllCallbacks();
    offset->setValueAndWarp(new_offset);
}

std::optional<WorkspaceNumber> HTLayoutGrid::on_move_swipe_end() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return std::nullopt;

    build_overview_layout(HT_VIEW_CLOSED);
    static const CConfigValue<Config::FLOAT> COMMIT_THRESHOLD(
        "plugin:hyprtasking:gestures:move_commit_threshold"
    );
    const double threshold = *COMMIT_THRESHOLD;
    const auto active = monitor->m_activeWorkspace->numberedID();
    if (!active)
        return std::nullopt;
    const auto origin = overview_layout.find(*active);
    if (origin == overview_layout.end())
        return std::nullopt;
    const CBox first = calculate_ws_box(0, 0, HT_VIEW_CLOSED);
    const Vector2D step = calculate_ws_box(1, 1, HT_VIEW_CLOSED).pos() - first.pos();
    const Vector2D progress = swipe_applied * first.size() / step;
    // Use this gesture's net travel, not an unfinished previous transition.
    const Vector2D target_offset = -origin->second.box.pos()
        + Vector2D {
              grid_swipe_target_step(progress.x, threshold),
              grid_swipe_target_step(progress.y, threshold)
          } * step;
    std::optional<WorkspaceNumber> closest;
    double closest_dist = std::numeric_limits<double>::infinity();
    for (const auto& [ws_id, box] : overview_layout) {
        const double dist_sq = target_offset.distanceSq(Vector2D {-box.box.x, -box.box.y});
        if (dist_sq < closest_dist) {
            closest_dist = dist_sq;
            closest = ws_id;
        }
    }
    return closest;
}

void HTLayoutGrid::close_open_lerp(float perc) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    double open_scale =
        calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x; // 1 / ROWS
    Vector2D open_pos = {0, 0};

    build_overview_layout(HT_VIEW_CLOSED);
    double close_scale = 1.;
    const auto number = monitor->m_activeWorkspace->numberedID();
    if (!number)
        return;
    Vector2D close_pos = -overview_layout[*number].box.pos();

    double new_scale = std::lerp(close_scale, open_scale, perc);
    Vector2D new_pos = Vector2D {
        std::lerp(close_pos.x, open_pos.x, perc),
        std::lerp(close_pos.y, open_pos.y, perc)
    };

    scale->resetAllCallbacks();
    offset->resetAllCallbacks();
    scale->setValueAndWarp(new_scale);
    offset->setValueAndWarp(new_pos);
}

void HTLayoutGrid::on_show(CallbackFun on_complete) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const float open_scale =
        calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x;
    animate_to({0, 0}, open_scale, 160.f, std::move(on_complete));
}

void HTLayoutGrid::on_hide(CallbackFun on_complete) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr || monitor->m_activeWorkspace == nullptr)
        return;

    build_overview_layout(HT_VIEW_CLOSED);
    if (const auto number = monitor->m_activeWorkspace->numberedID())
        animate_to(-overview_layout[*number].box.pos(), 1.f, 160.f, std::move(on_complete));
}

void HTLayoutGrid::on_move(
    WorkspaceNumber old_id,
    WorkspaceNumber new_id,
    CallbackFun on_complete
) {
    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr || par_view->active)
        return;

    // Only the plugin's grid moves during this transition.
    State::workspaceState()
        ->query()
        .numbered(Workspace::SWorkspaceNumberedID {old_id})
        .run()
        ->m_renderOffset->warp();
    State::workspaceState()
        ->query()
        .numbered(Workspace::SWorkspaceNumberedID {new_id})
        .run()
        ->m_renderOffset->warp();
    build_overview_layout(HT_VIEW_CLOSED);
    animate_to(-overview_layout[new_id].box.pos(), 1.f, 100.f, std::move(on_complete), true);
}

bool HTLayoutGrid::should_render_window(PHLWINDOW window) {
    bool ori_result = HTLayoutBase::should_render_window(window);

    const PHLMONITOR monitor = get_monitor();
    if (window == nullptr || monitor == nullptr)
        return ori_result;

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target != nullptr && window == target->window())
        return false;

    PHLWORKSPACE workspace = window->m_workspace;
    if (workspace == nullptr)
        return false;

    const auto number = workspace->numberedID();
    if (!number)
        return false;
    CBox window_box = get_global_window_box(window, *number);
    if (window_box.empty())
        return false;
    if (window_box.intersection(monitor->logicalBox()).empty())
        return false;

    return ori_result;
}

float HTLayoutGrid::drag_window_scale() {
    return scale->value();
}

void HTLayoutGrid::init_position() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    if (monitor->m_activeWorkspace == nullptr)
        return;

    // Sync to the layer of whatever workspace is currently active on this
    // monitor. Fresh views (e.g. after monitor reconnect) start at layer 0,
    // so without this the overview would open on the wrong layer.
    const auto number = monitor->m_activeWorkspace->numberedID();
    if (!number)
        return;
    const PHTVIEW view = ht_manager->get_view_from_id(view_id);
    bool preserve_position = view && view->navigating && !view->active;
    const Vector2D current_offset = offset->value();
    Vector2D previous_position;
    if (preserve_position) {
        build_overview_layout(HT_VIEW_CLOSED);
        const auto previous = overview_layout.find(*number);
        if (previous == overview_layout.end())
            preserve_position = false;
        else
            previous_position = previous->second.box.pos();
    }
    const auto sit = ws_slot_cache.find(*number);
    if (sit != ws_slot_cache.end()) {
        layer = sit->second.layer;
        anchor_x = sit->second.x;
        anchor_y = sit->second.y;
    }

    build_overview_layout(HT_VIEW_CLOSED);

    const auto it = overview_layout.find(*number);
    if (it == overview_layout.end() || it->second.box.empty())
        return;

    offset->resetAllCallbacks();
    scale->resetAllCallbacks();
    // Rebase the torus around the new active workspace without moving its
    // displayed tile. A quick next swipe takes over from this exact pose.
    offset->setValueAndWarp(
        preserve_position ? current_offset + previous_position - it->second.box.pos()
                          : -it->second.box.pos()
    );
    scale->setValueAndWarp(1.f);
    swipe_accumulated = {};
    swipe_applied = {};
    swipe_rail = GridSwipeRail::None;
}

CBox HTLayoutGrid::calculate_ws_box(int x, int y, HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    // Monitor may not have its final size yet during connect/reconnect
    if (monitor->m_transformedSize.x < 1 || monitor->m_transformedSize.y < 1)
        return {};

    const int ROWS = HTConfig::value<Config::INTEGER>("grid:rows");
    const int COLS = HTConfig::value<Config::INTEGER>("grid:cols");
    const int GAPS_USE_ASPECT_RATIO = HTConfig::value<Config::INTEGER>("grid:gaps_use_aspect_ratio");
    const float GAP_SIZE = HTConfig::value<Config::FLOAT>("gap_size") * monitor->m_scale;
    const Vector2D gaps = {
        GAP_SIZE,
        GAPS_USE_ASPECT_RATIO
            ? GAP_SIZE * monitor->m_transformedSize.y / monitor->m_transformedSize.x
            : GAP_SIZE
    };

    if (GAP_SIZE > std::min(monitor->m_transformedSize.x, monitor->m_transformedSize.y)
        || GAP_SIZE < 0)
        return {};

    double render_x = (monitor->m_transformedSize.x - gaps.x * (COLS + 1)) / COLS;
    double render_y = (monitor->m_transformedSize.y - gaps.y * (ROWS + 1)) / ROWS;
    const double mon_aspect = monitor->m_transformedSize.x / monitor->m_transformedSize.y;
    Vector2D start_offset {};

    // make correct aspect ratio
    if (render_y * mon_aspect > render_x) {
        start_offset.y = (render_y - render_x / mon_aspect) * ROWS / 2.f;
        render_y = render_x / mon_aspect;
    } else if (render_x / mon_aspect > render_y) {
        start_offset.x = (render_x - render_y * mon_aspect) * COLS / 2.f;
        render_x = render_y * mon_aspect;
    }

    float use_scale = scale->value();
    Vector2D use_offset = offset->value();
    if (stage == HT_VIEW_CLOSED) {
        use_scale = 1;
        use_offset = Vector2D {0, 0};
    } else if (stage == HT_VIEW_OPENED) {
        use_scale = render_x / monitor->m_transformedSize.x;
        use_offset = Vector2D {0, 0};
    }

    const Vector2D ws_sz = monitor->m_transformedSize * use_scale;
    return CBox {Vector2D {x, y} * (ws_sz + gaps) + gaps + use_offset + start_offset, ws_sz};
};

void HTLayoutGrid::build_overview_layout(HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const int ROWS = HTConfig::value<Config::INTEGER>("grid:rows");
    const int COLS = HTConfig::value<Config::INTEGER>("grid:cols");
    const bool LOOP = HTConfig::value<Config::INTEGER>("grid:loop");
    const bool CENTERED = HTConfig::value<Config::INTEGER>("grid:center_current");
    const auto view = ht_manager->get_view_from_id(view_id);
    const bool fixed_overview = LOOP && !CENTERED && stage != HT_VIEW_CLOSED
        && (stage == HT_VIEW_OPENED || (view != nullptr && view->active));
    double progress = stage == HT_VIEW_OPENED ? 1. : 0.;
    if (fixed_overview && stage != HT_VIEW_OPENED) {
        const double open_scale =
            calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x;
        progress =
            open_scale < 1. ? std::clamp((1. - scale->value()) / (1. - open_scale), 0., 1.) : 1.;
    }
    const CBox tile = calculate_ws_box(0, 0, stage);
    const Vector2D step = calculate_ws_box(1, 1, stage).pos() - tile.pos();
    const Vector2D closed_step = fixed_overview ? calculate_ws_box(1, 1, HT_VIEW_CLOSED).pos()
            - calculate_ws_box(0, 0, HT_VIEW_CLOSED).pos()
                                                : Vector2D {};

    const PHLMONITOR last_monitor = Desktop::focusState()->monitor();
    Desktop::focusState()->rawMonitorFocus(monitor);

    overview_layout.clear();
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            const auto ws_id = slot_workspace(layer, x, y);
            if (!ws_id)
                continue;
            const Vector2D position = fixed_overview
                ? Vector2D {
                    grid_overview_position(x, anchor_x, COLS, progress, closed_step.x, step.x),
                    grid_overview_position(y, anchor_y, ROWS, progress, closed_step.y, step.y)
                }
                : Vector2D {
                    LOOP ? grid_relative_coordinate(x, anchor_x, COLS) : x,
                    LOOP ? grid_relative_coordinate(y, anchor_y, ROWS) : y
                } * step;
            CBox ws_box {tile.pos() + position, Vector2D {tile.w, tile.h}};
            ws_box.round();
            overview_layout[*ws_id] = HTWorkspace {x, y, ws_box};
        }
    }

    if (last_monitor != nullptr)
        Desktop::focusState()->rawMonitorFocus(last_monitor);
}

// Render each visible workspace directly into its grid tile via a scaled
// renderWorkspace (renderModif). The renderTexture hook keeps the per-surface
// scissor in sync with that renderModif, so window contents aren't culled near
// tile edges.
void HTLayoutGrid::render() {
    HTLayoutBase::render();
    CScopeGuard x([this] { post_render(); });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return;
    const PHLMONITOR monitor = par_view->get_monitor();
    if (monitor == nullptr)
        return;

    static auto PACTIVECOL = CConfigValue<Config::IComplexConfigValue>("general:col.active_border");
    static auto PINACTIVECOL = CConfigValue<Config::IComplexConfigValue>("general:col.inactive_border");

    auto* const ACTIVECOL = (Config::CGradientValueData*)(PACTIVECOL.ptr());
    auto* const INACTIVECOL = (Config::CGradientValueData*)(PINACTIVECOL.ptr());

    const float BORDERSIZE = HTConfig::value<Config::FLOAT>("border_size");
    const auto time = Time::steadyNow();

    CBox monitor_box = {{0, 0}, monitor->m_transformedSize};

    CRectPassElement::SRectData bg;
    bg.color = CHyprColor {HTConfig::value<Config::INTEGER>("bg_color")}.stripA();
    bg.box = monitor_box;
    IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CRectPassElement>(bg));

    build_overview_layout(HT_VIEW_ANIMATING);

    // Hyprland only fully renders the active workspace, so render_workspace_at_box
    // swaps each tile's workspace in as it renders; capture the real active one to
    // restore at the end and to pick out the active-border color.
    const PHLWORKSPACE start_workspace = monitor->m_activeWorkspace;
    if (start_workspace == nullptr)
        return;
    Animation::Workspace::startAnimation(
        start_workspace, Animation::Workspace::ANIMATION_TYPE_OUT, false, true
    );
    const bool start_visible = start_workspace->visible();
    start_workspace->setVisible(false);

    CBox global_mon_box = {monitor->m_position, monitor->m_transformedSize};
    const auto tile_visible = [&](const CBox& box) {
        if (box.width < 0.01 || box.height < 0.01)
            return false;
        CBox global_box = {box.pos() + monitor->m_position, box.size()};
        return !global_box.expand(BORDERSIZE).intersection(global_mon_box).empty();
    };

    // Borders first, so window contents render on top of them. Workspace contents now
    // render unclipped (a window moving to a new workspace on release can extend past
    // its tile), and should not be covered by the tile borders.
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        if (!tile_visible(ws_layout.box))
            continue;
        CBorderPassElement::SBorderData bdata;
        bdata.box = ws_layout.box;
        bdata.grad1 = start_workspace->numberedID() == ws_id ? *ACTIVECOL : *INACTIVECOL;
        bdata.borderSize = BORDERSIZE;
        IHyprRenderer::currentPass(g_pHyprRenderer->context()).add(makeUnique<CBorderPassElement>(bdata));
    }

    // workspace may be nullptr for empty/never-visited slots: render_workspace_at_box
    // still draws their background + wallpaper layers so the tile isn't blank. Render
    // the active workspace last so its windows (e.g. one just dropped) stay on top of
    // the neighbouring tiles.
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        if (!tile_visible(ws_layout.box) || ws_id == start_workspace->numberedID())
            continue;
        render_workspace_at_box(monitor, State::workspaceState()->query().numbered(Workspace::SWorkspaceNumberedID {ws_id}).run(), time, ws_layout.box);
    }
    if (const auto number = start_workspace->numberedID(); number)
    if (const auto it = overview_layout.find(*number);
        it != overview_layout.end() && tile_visible(it->second.box))
        render_workspace_at_box(monitor, start_workspace, time, it->second.box);

    monitor->m_activeWorkspace = start_workspace;
    Animation::Workspace::startAnimation(
        start_workspace, Animation::Workspace::ANIMATION_TYPE_IN, false, true
    );
    start_workspace->setVisible(start_visible);

    g_pHyprRenderer->damageMonitor(monitor);

    // Dragged window rendered on top, following the cursor.
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;
    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return;
    const PHLWINDOW dragged_window = target->window();
    if (dragged_window == nullptr)
        return;
    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const CBox window_box = dragged_window->getWindowMainSurfaceBox()
                                .translate(-mouse_coords)
                                .scale(cursor_view->layout->drag_window_scale())
                                .translate(mouse_coords);
    if (!window_box.intersection(monitor->logicalBox()).empty())
        render_window_at_box(dragged_window, monitor, time, window_box);
}
