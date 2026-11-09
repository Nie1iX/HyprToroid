#pragma once

#include <linux/input-event-codes.h>

#include <bitset>

class ConsumedKeys {
  public:
    enum class Result { Pass, Press, Consumed };

    Result handle(unsigned int keycode, bool pressed, bool eligible) {
        if (keycode > KEY_MAX)
            return Result::Pass;
        if (owned[keycode]) {
            if (!pressed)
                owned.reset(keycode);
            return Result::Consumed;
        }
        if (!pressed || !eligible)
            return Result::Pass;
        owned.set(keycode);
        return Result::Press;
    }

    bool ready() const {
        return owned.none();
    }

  private:
    std::bitset<KEY_MAX + 1> owned;
};
