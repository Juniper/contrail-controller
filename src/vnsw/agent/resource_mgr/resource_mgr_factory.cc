#include "resource_mgr_factory.h"


ResourceMgrFactory::ResourceMgrFactory(Agent *agent):agent_(agent) {

}

ResourceMgrFactory::~ResourceMgrFactory() {
}

void ResourceMgrFactory::Shutdown() {
    mplsrmgr_.reset();
}

ResourceManager* 
ResourceMgrFactory::GetResourceMgr(ResourceType type_) {
    switch(type_) {
        case MPLS_TYPE:
        {
           if(mplsrmgr_.get() == NULL) {
                mplsrmgr_.reset(new MplsResourceManager(MplsResourceManager::mpls_tmp_file_, 
                                                        agent_)); 
           }
          return mplsrmgr_.get(); 
        }
        default:
            return NULL;
    }
}
