#pragma once

#include "InputState.hpp"

namespace tryengine::core {
class InputService {
public:
    InputService(InputState& input_state) : input_state(input_state) {};
    ~InputService() = default;

    InputState& GetInputState() const { return input_state;};

private:
    InputState& input_state;
};
}  // namespace tryengine::core