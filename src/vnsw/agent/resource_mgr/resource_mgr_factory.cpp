#include "resource_mgr_factory.h"

ResourceMgrFactory *ResourceMgrFactory::resource_mgr_factory_ = NULL;

ResourceMgrFactory::ResourceMgrFactory() {
  Agent *agent = Agent::GetInstance(); 
  //timer_ =  
}

ResourceMgrFactory::~ResourceMgrFactory() {

}

ResourceMgrFactory *
ResourceMgrFactory::GetInstance() {
   if (!resource_mgr_factory_) {
        resource_mgr_factory_ = new ResourceMgrFactory();
   }  
   return resource_mgr_factory_;     
} 

bool ResourceMgrFactory::TimerExpiry() {

}

ResourceManager* 
ResourceMgrFactory::GetResourceMgr(ResourceType type_) {
    switch(type_) {
        case MPLS_TYPE:
        {
           if(mplsrmgr_.get() == NULL) {
                mplsrmgr_.reset(MplsMgr(MplsResourceManager::mpls_file_)); 
           }
          return mplsrmgr_.get(); 
        }
        default:
            return NULL;
    }
}
