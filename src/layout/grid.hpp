#pragma once

#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <unordered_map>
#include <unordered_set>

#include "../types.hpp"
#include "grid_projection.hpp"
#include "layout_base.hpp"

struct HTGridSlot {
    int layer;
    int x;
    int y;
};

class HTLayoutGrid: public HTLayoutBase {
  private:
    PHLANIMVAR<float> scale;
    PHLANIMVAR<Vector2D> offset;
    int anchor_x = 0;
    int anchor_y = 0;
    Vector2D swipe_accumulated;
    Vector2D swipe_applied;
    GridSwipeRail swipe_rail = GridSwipeRail::None;
    SP<Hyprutils::Animation::SAnimationPropertyConfig> transition_config;

    void animate_to(
        Vector2D target_offset,
        float target_scale,
        float max_duration_ms,
        CallbackFun on_complete,
        bool workspace_transition = false
    );

    // Survives workspace destruction so a slot stays sticky for an empty ws.
    std::unordered_map<WorkspaceNumber, HTGridSlot> ws_slot_cache;
    std::unordered_map<long long, WorkspaceNumber> slot_ws_cache;

    static long long pack_slot(int layer, int x, int y);

  public:
    HTLayoutGrid(VIEWID view_id);
    virtual ~HTLayoutGrid() = default;

    virtual std::string layout_name();

    virtual CBox calculate_ws_box(int x, int y, HTViewStage stage);

    virtual void close_open_lerp(float perc);
    virtual void on_show(CallbackFun on_complete);
    virtual void on_hide(CallbackFun on_complete);
    virtual void on_move(WorkspaceNumber old_id, WorkspaceNumber new_id, CallbackFun on_complete);
    virtual void on_move_swipe(Vector2D delta);
    virtual std::optional<WorkspaceNumber> on_move_swipe_end();

    virtual std::optional<WorkspaceNumber> get_ws_id_in_direction(int x, int y, std::string& direction);

    virtual bool should_render_window(PHLWINDOW window);
    virtual float drag_window_scale();
    virtual void init_position();
    virtual void build_overview_layout(HTViewStage stage);
    virtual void render();

    void refresh_workspace_cache(const std::unordered_set<WorkspaceNumber>& extra_off_limits = {});
    std::optional<WorkspaceNumber> slot_workspace(int layer, int x, int y);

    const std::unordered_map<WorkspaceNumber, HTGridSlot>& cache() const { return ws_slot_cache; }
};
