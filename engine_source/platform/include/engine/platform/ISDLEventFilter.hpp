#pragma once

union SDL_Event;

namespace tryengine::platform {

class ISDLEventFilter {
public:
    virtual ~ISDLEventFilter() = default;

    // Возвращает true, если событие обработано и его не нужно отдавать дальше в InputMapper
    virtual bool OnSDLEvent(const SDL_Event& event) = 0;
};

}  // namespace tryengine::platform