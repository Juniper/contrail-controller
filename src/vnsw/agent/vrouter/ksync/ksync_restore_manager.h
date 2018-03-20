/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_ksync_restore_manager_h
#define vnsw_agent_ksync_restore_manager_h

#include <ksync/ksync_object.h>
#define LLGR_KSYNC_RESTORE_CONTEXT   "llgr ksync restore"

class KSyncDBObject;
class KSync;

class KSyncRestoreManager {
public:
    static const uint32_t StaleEntryCleanupTimer = 300000;          // 5 mins
    static const uint32_t StaleEntryYeildTimer = 100;               // 100 ms
    static const uint16_t StaleEntryDeletePerIteration = 32;

    typedef boost::shared_ptr<KSyncRestoreData> RestoreDataPtr;
    enum KSyncType {
        KSYNC_TYPE_INVALID = 0,
        KSYNC_TYPE_INTERFACE,
        KSYNC_TYPE_NEXTHOP,
        KSYNC_TYPE_FLOW,
        KSYNC_TYPE_ROUTE,
        KSYNC_TYPE_MPLS,
        KSYNC_TYPE_FORWARDING_CLASS,
        KSYNC_TYPE_QOS_CONFIG,
        KSYNC_TYPE_MIRROR,
        KSYNC_TYPE_MAX
    };

    
    KSyncRestoreManager(KSync *ksync);
    ~KSyncRestoreManager();
    
    void Init();
    void EnqueueRestoreData(KSyncRestoreData::Ptr data);

    bool  WorkQueueProcess(KSyncRestoreData::Ptr data);


    void  UpdateKSyncRestoreStatus(KSyncType type);


private:
    void CheckAndInitiateKSyncRestore();
    KSync *ksync_;
    uint32_t ksync_restore_status_flag_;
    WorkQueue<KSyncRestoreData::Ptr> restore_work_queue_;
    DISALLOW_COPY_AND_ASSIGN(KSyncRestoreManager);
};

class KSyncRestoreEndData : public KSyncRestoreData {
public:
    KSyncRestoreEndData(KSyncDBObject *obj);
    ~KSyncRestoreEndData();
    virtual const std::string ToString() { return 
                    "KSyncRestoreEndData";}
};
#endif
