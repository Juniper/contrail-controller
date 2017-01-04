#include "resource_manager_factory.h"
SandeshTraceBufferPtr ResourceManagerTraceBuf(
        SandeshTraceBufferCreate("ResourceManager", 1000));

ResourceManagerFactory::ResourceManagerFactory(Agent *agent):agent_(agent) {

}

ResourceManagerFactory::~ResourceManagerFactory() {
}

void ResourceManagerFactory::Shutdown() {
    mplsrmgr_.reset();
}

ResourceManager* 
ResourceManagerFactory::GetResourceManager(ResourceType type_) {
    switch(type_) {
        case MPLS_TYPE:
        {
           if(mplsrmgr_.get() == NULL) {
                mplsrmgr_.reset( new MplsResourceManager(
                            MplsResourceManager::mpls_file_, 
                            MplsResourceManager::mpls_tmp_file_, agent_));
           }
          return mplsrmgr_.get();
        }
        default:
            return NULL;
    }
}
