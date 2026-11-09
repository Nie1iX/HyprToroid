#pragma once

#include <cmath>
#include <numbers>

inline constexpr double GRID_SWIPE_DEADZONE = 0.15;

inline double grid_swipe_displacement(double accumulated) {
    if (accumulated > GRID_SWIPE_DEADZONE)
        return accumulated - GRID_SWIPE_DEADZONE;
    if (accumulated < -GRID_SWIPE_DEADZONE)
        return accumulated + GRID_SWIPE_DEADZONE;
    return 0.;
}

enum class GridSwipeRail {
    None,
    Horizontal,
    Vertical,
    Diagonal,
    AntiDiagonal,
};

inline void grid_swipe_project(double& x, double& y, GridSwipeRail& rail) {
    if (rail == GridSwipeRail::None) {
        if (std::abs(x) <= GRID_SWIPE_DEADZONE && std::abs(y) <= GRID_SWIPE_DEADZONE) {
            x = y = 0.;
            return;
        }
        constexpr double boundary = std::numbers::sqrt2 - 1.; // tan(22.5 degrees)
        if (std::abs(y) <= std::abs(x) * boundary)
            rail = GridSwipeRail::Horizontal;
        else if (std::abs(x) <= std::abs(y) * boundary)
            rail = GridSwipeRail::Vertical;
        else
            rail = x * y > 0. ? GridSwipeRail::Diagonal : GridSwipeRail::AntiDiagonal;
    }
    switch (rail) {
        case GridSwipeRail::Horizontal:
            y = 0.;
            break;
        case GridSwipeRail::Vertical:
            x = 0.;
            break;
        case GridSwipeRail::Diagonal:
            x = y = (x + y) / 2.;
            break;
        case GridSwipeRail::AntiDiagonal:
            x = (x - y) / 2.;
            y = -x;
            break;
        case GridSwipeRail::None:
            break;
    }
}

inline double grid_swipe_target_step(double progress, double threshold) {
    const double whole = std::floor(std::abs(progress));
    const double fraction = std::abs(progress) - whole;
    return std::copysign(whole + (fraction > 0. && fraction >= threshold ? 1. : 0.), progress);
}

inline int grid_relative_coordinate(int coordinate, int anchor, int count) {
    return (coordinate - anchor + count + count / 2) % count;
}

inline double grid_overview_position(
    int coordinate,
    int anchor,
    int count,
    double progress,
    double closed_step,
    double current_step
) {
    return coordinate * current_step + (count / 2 - anchor) * closed_step * (1. - progress);
}
