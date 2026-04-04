#pragma once

// Base class for all system services
class SystemService {
public:
    virtual const char *name() const = 0;
    virtual bool begin() = 0;
    virtual ~SystemService() = default;
};

// Singleton registry — collects services and starts them in order
class ServiceRegistry {
public:
    static ServiceRegistry &instance();

    void register_service(SystemService *svc);
    // Calls begin() on every registered service in registration order.
    // Returns false if any service fails to start.
    bool begin_all();
    // Look up a service by name. Returns nullptr if not found.
    SystemService *get(const char *name) const;

private:
    static constexpr int MAX_SERVICES = 16;
    SystemService *_services[MAX_SERVICES]{};
    int _count = 0;
};

// Place SERVICE_REGISTER(MyService) in the .cpp file to auto-register the
// singleton on startup.  The singleton is created via MyService::instance().
#define SERVICE_REGISTER(ServiceClass)                                        \
    static bool _svc_reg_##ServiceClass = []() -> bool {                      \
        ServiceRegistry::instance().register_service(&ServiceClass::instance()); \
        return true;                                                           \
    }()
