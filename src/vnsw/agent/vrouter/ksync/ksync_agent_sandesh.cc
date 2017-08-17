/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vrouter/ksync/agent_ksync_types.h"
#include "ksync_agent_sandesh.h"
#include "ksync_init.h"


AgentKsyncSandesh::AgentKsyncSandesh(const std::string &context):
    context_(context), name_("") {
    agent_ = Agent::GetInstance();
}

void AgentKsyncSandesh::SetResp() {
    Alloc();
    resp_->set_context(context_);
}

void AgentKsyncSandesh::DoKsyncSandeshInternal(AgentKsyncSandeshPtr sandesh) {
    InterfaceKSyncObject *ksyncobj =
        static_cast<InterfaceKSyncObject *>(AgentGetKsyncObject());
    KSyncEntry *entry = NULL;

    SetResp();
    entry = ksyncobj->Next(NULL);
    while (entry != NULL) {
        UpdateResp(entry);
        entry = ksyncobj->Next(entry);
    }
    resp_->Response();
}


void AgentKsyncSandesh::DoKsyncSandesh(AgentKsyncSandeshPtr sandesh) {
    sandesh->DoKsyncSandeshInternal(sandesh);
    return;
}

////////////////////////////
//// ksync interface //////
///////////////////////////

AgentKsyncIntfSandesh::AgentKsyncIntfSandesh(const std::string &context) :
    AgentKsyncSandesh(context){
}

bool AgentKsyncIntfSandesh::UpdateResp(KSyncEntry *entry) {
    InterfaceKSyncEntry *intf = static_cast< InterfaceKSyncEntry *>(entry);
    return intf->KSyncEntrySandesh(resp_);
}

KSyncDBObject *AgentKsyncIntfSandesh::AgentGetKsyncObject() {
    return agent_->ksync()->interface_ksync_obj();
}

void AgentKsyncIntfSandesh::Alloc() {
    resp_ = new KSyncItfResp();
}

////////////////////////////
//// ksync NextHopList //////
///////////////////////////

AgentKsyncNhListSandesh::AgentKsyncNhListSandesh(const std::string &context) :
    AgentKsyncSandesh(context){
}

bool AgentKsyncNhListSandesh::UpdateResp(KSyncEntry *entry) {
    NHKSyncEntry *nh_list = static_cast< NHKSyncEntry *>(entry);
    return nh_list->KSyncEntrySandesh(resp_);
}

KSyncDBObject *AgentKsyncNhListSandesh::AgentGetKsyncObject() {
    return agent_->ksync()->nh_ksync_obj();
}

void AgentKsyncNhListSandesh::Alloc() {
    resp_ = new KSyncNhListResp();
}

