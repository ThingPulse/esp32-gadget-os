#pragma once

enum class EventType {
    WifiConnecting,
    WifiConnected,
    WifiDisconnected,
    TimeSynchronized,
};

struct SystemEvent {
    EventType type;
};

using EventCallback = void (*)(const SystemEvent &event, void *user_data);
using SubscriptionId = int;

class EventBus {
public:
    static EventBus &instance();

    SubscriptionId subscribe(EventType type, EventCallback callback, void *user_data);
    void unsubscribe(SubscriptionId id);
    void publish(const SystemEvent &event);

private:
    struct Listener {
        SubscriptionId id = 0;
        EventType type = EventType::WifiDisconnected;
        EventCallback callback = nullptr;
        void *user_data = nullptr;
        bool active = false;
    };

    static constexpr int MAX_LISTENERS = 32;
    Listener _listeners[MAX_LISTENERS]{};
    SubscriptionId _next_id = 1;
};