#ifndef agent_resource_manager_factory_h
#define agent_resource_manager_factory_h
#include <base/timer.h>
#include <agent.h>
#include "resource_manager.h"
#include "mpls_resource_manager.h"
class ResourceManagerFactory {
public:
    enum ResourceType {
        MPLS_TYPE =1,
    };
    ResourceManagerFactory(Agent *agent);
    ~ResourceManagerFactory();
    //ResourceMgrFactory() {}
    ResourceManager* GetResourceManager(ResourceType type_);
    void Shutdown();
private:
    std::auto_ptr<MplsResourceManager> mplsrmgr_;
    Agent *agent_;
};
#endif
