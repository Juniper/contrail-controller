/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "ksync_restore_manager.h"
#include <init/agent_init.h>
#include <cmn/agent.h>
#include "vrouter/ksync/ksync_init.h"

KSyncRestoreManager::KSyncRestoreManager(KSync *ksync)
    :ksync_(ksync),ksync_restore_status_flag_(0),
    restore_work_queue_(ksync_->agent()->task_scheduler()->
            GetTaskId("Agent::KSync"), 0,
            boost::bind(&KSyncRestoreManager::WorkQueueProcess,
            this, _1)) {
}

KSyncRestoreManager::~KSyncRestoreManager() {
}
void KSyncRestoreManager::Init(void) {
    if (!ksync_->agent()->params()->restart_backup_enable()) {
        ksync_->agent()->agent_init()->SetKSyncRestoreDone();
        return;
    }
    // initiate ksync entry restore process
    CheckAndInitiateKSyncRestore();
}   

void KSyncRestoreManager::CheckAndInitiateKSyncRestore() {
    if (!(ksync_restore_status_flag_ & (1 << KSYNC_TYPE_INTERFACE))) {
        ksync_->interface_ksync_obj()->RestoreVrouterEntriesReq();
    } else if (!(ksync_restore_status_flag_ & (1 << KSYNC_TYPE_MPLS))) {
        ksync_->mpls_ksync_obj()->RestoreVrouterEntriesReq();
    } else if (!(ksync_restore_status_flag_ & (1 << KSYNC_TYPE_NEXTHOP))) {
        ksync_->nh_ksync_obj()->RestoreVrouterEntriesReq();
    } else if (!(ksync_restore_status_flag_ & (1 << KSYNC_TYPE_MIRROR))) {
        ksync_->mirror_ksync_obj()->RestoreVrouterEntriesReq();
    } else if (!(ksync_restore_status_flag_ &
                        (1 << KSYNC_TYPE_FORWARDING_CLASS))) {
        ksync_->forwarding_class_ksync_obj()->RestoreVrouterEntriesReq();
    } else if (!(ksync_restore_status_flag_ &
                        (1 << KSYNC_TYPE_QOS_CONFIG))) {
        ksync_->qos_config_ksync_obj()->RestoreVrouterEntriesReq();
    } else {
        ksync_->agent()->agent_init()->SetKSyncRestoreDone();
    }
}

void KSyncRestoreManager::UpdateKSyncRestoreStatus(KSyncType type) {
     ksync_restore_status_flag_ |= (1 << type);
        
     CheckAndInitiateKSyncRestore();
}

void KSyncRestoreManager::EnqueueRestoreData(KSyncRestoreData::Ptr  data) {
    restore_work_queue_.Enqueue(data);
}

bool KSyncRestoreManager::WorkQueueProcess(KSyncRestoreData::Ptr data) {
    data.get()->KSyncObject()->ProcessVrouterEntries(data);
    return true;
}

KSyncRestoreEndData::KSyncRestoreEndData(KSyncDBObject *obj)
    :KSyncRestoreData(obj) {
}
KSyncRestoreEndData::~KSyncRestoreEndData() {
}


