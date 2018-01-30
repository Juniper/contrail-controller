/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "ksync_restore_manager.h"
#include <init/agent_init.h>
#include <cmn/agent.h>
#include "vrouter/ksync/ksync_init.h"

KSyncRestoreManager::KSyncRestoreManager(KSync *ksync)
    :ksync_(ksync),
    restore_work_queue_(ksync_->agent()->task_scheduler()->
            GetTaskId(kAgentResourceRestoreTask), 0,
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
    ksync_->interface_ksync_obj()->RestoreVrouterEntriesReq();
}   

void KSyncRestoreManager::EnqueueRestoreData(KSyncRestoreData::Ptr  data) {
    restore_work_queue_.Enqueue(data);
}

bool KSyncRestoreManager::WorkQueueProcess(KSyncRestoreData::Ptr data) {
    data.get()->KSyncObject()->ProcessVrouterEntries(data);
    return true;
}


