#include "EventBus.h"

EventBus &EventBus::instance()
{
    static EventBus inst;
    return inst;
}

SubscriptionId EventBus::subscribe(EventType type, EventCallback callback, void *user_data)
{
    for (int i = 0; i < MAX_LISTENERS; ++i) {
        if (!_listeners[i].active) {
            _listeners[i].id = _next_id++;
            _listeners[i].type = type;
            _listeners[i].callback = callback;
            _listeners[i].user_data = user_data;
            _listeners[i].active = true;
            return _listeners[i].id;
        }
    }

    return 0;
}

void EventBus::unsubscribe(SubscriptionId id)
{
    for (int i = 0; i < MAX_LISTENERS; ++i) {
        if (_listeners[i].active && _listeners[i].id == id) {
            _listeners[i].active = false;
            _listeners[i].callback = nullptr;
            _listeners[i].user_data = nullptr;
            return;
        }
    }
}

void EventBus::publish(const SystemEvent &event)
{
    for (int i = 0; i < MAX_LISTENERS; ++i) {
        if (_listeners[i].active && _listeners[i].type == event.type && _listeners[i].callback)
            _listeners[i].callback(event, _listeners[i].user_data);
    }
}