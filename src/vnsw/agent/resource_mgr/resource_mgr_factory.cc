#include "resource_mgr_factory.h"


ResourceMgrFactory::ResourceMgrFactory(Agent *agent) {
  timer_ = TimerManager::CreateTimer(*(agent->event_manager()->io_service()),
                                    "sync resource to file");
  timer_->Start(kResourceSyncTimeout, 
                boost::bind(&ResourceMgrFactory::TimerExpiry, this));
}

ResourceMgrFactory::~ResourceMgrFactory() {
}

void ResourceMgrFactory::Shutdown() {
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
}

bool ResourceMgrFactory::TimerExpiry() {
    ResourceManager *mgr = NULL;
    if (mplsrmgr_.get()) {
        mgr = mplsrmgr_.get();
        if (mgr->Changed()) {
            mgr->WriteToFile();
            mgr->SetChanged(false);
        }
    }
    return true;
}

ResourceManager* 
ResourceMgrFactory::GetResourceMgr(ResourceType type_) {
    switch(type_) {
        case MPLS_TYPE:
        {
           if(mplsrmgr_.get() == NULL) {
                mplsrmgr_.reset(new MplsResourceManager(MplsResourceManager::mpls_file_)); 
           }
          return mplsrmgr_.get(); 
        }
        default:
            return NULL;
    }
}
