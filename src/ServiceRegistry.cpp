#include "ServiceRegistry.h"
#include <cstring>

ServiceRegistry &ServiceRegistry::instance()
{
    static ServiceRegistry inst;
    return inst;
}

void ServiceRegistry::register_service(SystemService *svc)
{
    if (_count < MAX_SERVICES)
        _services[_count++] = svc;
}

bool ServiceRegistry::begin_all()
{
    bool ok = true;
    for (int i = 0; i < _count; ++i)
        if (!_services[i]->begin()) ok = false;
    return ok;
}

SystemService *ServiceRegistry::get(const char *name) const
{
    for (int i = 0; i < _count; ++i)
        if (std::strcmp(_services[i]->name(), name) == 0)
            return _services[i];
    return nullptr;
}
