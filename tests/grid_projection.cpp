// Test contracts must remain checked even when the plugin is built with NDEBUG.
#ifdef NDEBUG
    #undef NDEBUG
#endif

#include "../src/layout/grid_projection.hpp"

#include <array>
#include <cassert>
#include <cmath>

int main() {
    // Single-cell, even and odd axes must remain permutations with cyclic adjacency.
    const int axes[][2] = {{1, 0}, {2, 1}, {3, 1}, {4, 2}, {5, 2}};
    for (const auto& [count, center] : axes) {
        for (int anchor = 0; anchor < count; ++anchor) {
            std::array<bool, 5> seen {};
            for (int coordinate = 0; coordinate < count; ++coordinate) {
                const int position = grid_relative_coordinate(coordinate, anchor, count);
                assert(position >= 0 && position < count);
                assert(!seen[position]);
                seen[position] = true;
                const int next = grid_relative_coordinate((coordinate + 1) % count, anchor, count);
                assert(next == (position + 1) % count);
            }
            for (int position = 0; position < count; ++position)
                assert(seen[position]);
            assert(grid_relative_coordinate(anchor, anchor, count) == center);
        }
    }
    // A fixed overview must preserve reading order throughout its reveal.
    for (int anchor = 0; anchor < 3; ++anchor) {
        for (double progress : {0., 0.01, 0.25, 0.5, 0.75, 1.}) {
            const double step = 1920. * (1. - 2. * progress / 3.);
            const auto x0 = grid_overview_position(0, anchor, 3, progress, 1920., step);
            const auto x1 = grid_overview_position(1, anchor, 3, progress, 1920., step);
            const auto x2 = grid_overview_position(2, anchor, 3, progress, 1920., step);
            const auto active = grid_overview_position(anchor, anchor, 3, progress, 1920., step)
                - (1. - progress) * 1920.;
            assert(std::abs(active - progress * anchor * 640.) < 1e-9);
            assert(x0 < x1 && x1 < x2);
        }
    }

    // Small perpendicular drift stays still; deliberate diagonals remain continuous.
    const double threshold = GRID_SWIPE_DEADZONE;
    assert(grid_swipe_displacement(threshold) == 0.);
    assert(grid_swipe_displacement(-threshold) == 0.);
    assert(grid_swipe_displacement(threshold / 2.) == 0.);
    assert(grid_swipe_displacement(2. * threshold) > 0.);
    assert(grid_swipe_displacement(-2. * threshold) == -grid_swipe_displacement(2. * threshold));
    assert(std::abs(grid_swipe_displacement(threshold + 1e-6) - 1e-6) < 1e-12);
    // Returning to the neutral zone removes all displacement.
    assert(grid_swipe_displacement(0.) == 0.);

    // Initial noise must not choose a rail before the existing movement threshold.
    GridSwipeRail rail = GridSwipeRail::None;
    double x = 0.1, y = 0.12;
    grid_swipe_project(x, y, rail);
    assert(rail == GridSwipeRail::None && x == 0. && y == 0.);
    x = threshold + 1e-6;
    y = 0.04;
    grid_swipe_project(x, y, rail);
    assert(std::abs(grid_swipe_displacement(x) - 1e-6) < 1e-12 && y == 0.);

    // Nearest-direction selection changes on each side of the 22.5-degree boundary.
    rail = GridSwipeRail::None;
    x = 1.;
    y = 0.41;
    grid_swipe_project(x, y, rail);
    assert(rail == GridSwipeRail::Horizontal && x == 1. && y == 0.);
    rail = GridSwipeRail::None;
    x = 1.;
    y = 0.42;
    grid_swipe_project(x, y, rail);
    assert(rail == GridSwipeRail::Diagonal);
    assert(std::abs(x - 0.71) < 1e-12 && std::abs(y - 0.71) < 1e-12);
    rail = GridSwipeRail::None;
    x = 0.41;
    y = 1.;
    grid_swipe_project(x, y, rail);
    assert(rail == GridSwipeRail::Vertical && x == 0. && y == 1.);
    rail = GridSwipeRail::None;
    x = 0.42;
    y = 1.;
    grid_swipe_project(x, y, rail);
    assert(rail == GridSwipeRail::Diagonal);
    assert(std::abs(x - 0.71) < 1e-12 && std::abs(y - 0.71) < 1e-12);

    for (double sign : {-1., 1.}) {
        rail = GridSwipeRail::None;
        x = sign * 0.8;
        y = 0.2;
        grid_swipe_project(x, y, rail);
        assert(x == sign * 0.8 && y == 0.);
        // Crossing another sector cannot turn a locked gesture; reversal stays on its rail.
        x = -sign * 0.4;
        y = 2.;
        grid_swipe_project(x, y, rail);
        assert(x == -sign * 0.4 && y == 0.);

        rail = GridSwipeRail::None;
        x = 0.2;
        y = sign * 0.8;
        grid_swipe_project(x, y, rail);
        assert(x == 0. && y == sign * 0.8);

        for (double other_sign : {-1., 1.}) {
            rail = GridSwipeRail::None;
            x = sign;
            y = other_sign * 0.5;
            grid_swipe_project(x, y, rail);
            assert(x == sign * 0.75 && y == other_sign * 0.75);
            x = sign * 0.1;
            y = other_sign * 1.9;
            grid_swipe_project(x, y, rail);
            assert(x == sign && y == other_sign);
            x = -sign * 0.4;
            y = -other_sign * 0.2;
            grid_swipe_project(x, y, rail);
            assert(std::abs(x + sign * 0.3) < 1e-12);
            assert(std::abs(y + other_sign * 0.3) < 1e-12);
        }
    }

    // Completion threshold is independent from finger tracking and remains reversible.
    assert(grid_swipe_target_step(0.199, 0.2) == 0.);
    assert(grid_swipe_target_step(0.2, 0.2) == 1.);
    assert(grid_swipe_target_step(-0.2, 0.2) == -1.);
    assert(grid_swipe_target_step(0., 0.2) == 0.);
    assert(grid_swipe_target_step(0.25, 0.5) == 0.);
    assert(grid_swipe_target_step(0.501, 0.5) == 1.);
    assert(grid_swipe_target_step(2.25, 0.2) == 3.);
    assert(grid_swipe_target_step(-2.1, 0.2) == -2.);
    assert(grid_swipe_target_step(0.99, 1.) == 0.);
    assert(grid_swipe_target_step(1., 1.) == 1.);
    // Zero means any actual travel, not an unconditional extra workspace.
    assert(grid_swipe_target_step(0., 0.) == 0.);
    assert(grid_swipe_target_step(-0., 0.) == 0.);
    assert(grid_swipe_target_step(0.001, 0.) == 1.);
    assert(grid_swipe_target_step(-0.001, 0.) == -1.);
    assert(grid_swipe_target_step(1., 0.) == 1.);
    assert(grid_swipe_target_step(-2., 0.) == -2.);

    // Preserve both sides of a completion boundary without rounding a near-integer up twice.
    for (double sign : {-1., 1.}) {
        assert(grid_swipe_target_step(sign * std::nextafter(0.5, 0.), 0.5) == 0.);
        assert(grid_swipe_target_step(sign * 0.5, 0.5) == sign);
        assert(grid_swipe_target_step(sign * std::nextafter(0.5, 1.), 0.5) == sign);
        assert(grid_swipe_target_step(sign * std::nextafter(1., 0.), 0.) == sign);
        assert(grid_swipe_target_step(sign * std::nextafter(1., 2.), 0.) == sign * 2.);
    }
}
