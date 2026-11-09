#ifdef NDEBUG
    #undef NDEBUG
#endif
#include "../src/consumed_keys.hpp"

#include <linux/input-event-codes.h>

#include <cassert>
#include <limits>

int main() {
    using Result = ConsumedKeys::Result;
    ConsumedKeys keys;

    assert(keys.ready());
    assert(keys.handle(KEY_ESC, true, false) == Result::Pass);
    assert(keys.handle(KEY_ESC, false, true) == Result::Pass);
    assert(keys.ready());

    assert(keys.handle(KEY_ESC, true, true) == Result::Press);
    assert(!keys.ready());
    assert(keys.handle(KEY_ESC, true, false) == Result::Consumed);
    assert(keys.handle(KEY_ESC, false, false) == Result::Consumed);
    assert(keys.ready());
    assert(keys.handle(KEY_ESC, false, false) == Result::Pass);

    assert(keys.handle(KEY_A, true, true) == Result::Press);
    assert(keys.handle(KEY_B, true, true) == Result::Press);
    assert(keys.handle(KEY_A, true, false) == Result::Consumed);
    assert(keys.handle(KEY_C, false, true) == Result::Pass);
    assert(keys.handle(KEY_A, false, false) == Result::Consumed);
    assert(!keys.ready());
    assert(keys.handle(KEY_B, false, false) == Result::Consumed);
    assert(keys.ready());
    assert(keys.handle(KEY_B, false, false) == Result::Pass);

    // Invalid events cannot clear another key's outstanding release.
    assert(keys.handle(KEY_MAX, true, true) == Result::Press);
    constexpr auto invalid = std::numeric_limits<unsigned int>::max();
    assert(keys.handle(invalid, true, true) == Result::Pass);
    assert(keys.handle(invalid, false, true) == Result::Pass);
    assert(!keys.ready());
    assert(keys.handle(KEY_MAX, false, false) == Result::Consumed);
    assert(keys.ready());
}
