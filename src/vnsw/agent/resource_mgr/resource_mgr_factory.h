#ifndef agent_resource_mgr_factory_h
#define agent_resource_mgr_factory_h
#include <base/timer.h>
#include <agent.h>
#include "resource_mgr.h"
#include "mpls_resource_mgr.h"
class ResourceMgrFactory {
public:
    enum ResourceType {
        MPLS_TYPE =1,
    };
    ResourceMgrFactory(Agent *agent);
    ~ResourceMgrFactory();
    ResourceMgrFactory() {}
    ResourceManager* GetResourceMgr(ResourceType type_);
    void Shutdown();
private:
    std::auto_ptr<MplsResourceManager> mplsrmgr_;
    Agent *agent_;
};

#endif
