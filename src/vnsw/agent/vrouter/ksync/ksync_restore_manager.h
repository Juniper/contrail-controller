/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_ksync_restore_manager_h
#define vnsw_agent_ksync_restore_manager_h

#include <ksync/ksync_object.h>

class KSyncDBObject;
class KSync;

class KSyncRestoreManager {
public:
    typedef boost::shared_ptr<KSyncRestoreData> RestoreDataPtr;
    
    KSyncRestoreManager(KSync *ksync);
    ~KSyncRestoreManager();
    
    void Init();
    void EnqueueRestoreData(KSyncRestoreData::Ptr data);

    bool  WorkQueueProcess(KSyncRestoreData::Ptr data);


private:
    KSync *ksync_;
    WorkQueue<KSyncRestoreData::Ptr> restore_work_queue_;
    DISALLOW_COPY_AND_ASSIGN(KSyncRestoreManager);
};

#endif
