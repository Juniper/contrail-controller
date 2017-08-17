/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vrouter/ksync/agent_ksync_types.h"
#include "ksync_agent_sandesh.h"
#include "ksync_init.h"


AgentKsyncSandesh::AgentKsyncSandesh(const std::string &context): 
    AgentSandesh(context, "") {
}

void AgentKsyncSandesh::SetResp() {
    Alloc();
    resp_->set_context(context());
}


void AgentKsyncSandesh::DoKsyncSandeshInternal(AgentSandeshPtr sandesh) {
    InterfaceKSyncObject *ksyncobj = 
        static_cast<InterfaceKSyncObject *>(AgentGetKsyncObject());
    KSyncEntry *entry = NULL;

//    while((entry = ksyncobj->Next(entry)) != NULL)
    SetResp();
    entry = ksyncobj->Next(NULL);
    while (entry != NULL) {
        UpdateResp(entry);
        entry = ksyncobj->Next(entry);
    }
    resp_->Response();
}


void AgentKsyncSandesh::DoKsyncSandesh(AgentSandeshPtr sandesh) {
    sandesh->DoKsyncSandeshInternal(sandesh);
    return;
}
////////////////////////////
//// ksync interface //////
///////////////////////////

AgentKsyncIntfSandesh::AgentKsyncIntfSandesh(const std::string &context,
                                   const std::string &interface_id,
                                    const std::string &interface_name) :
    AgentKsyncSandesh(context){
    agent_ = Agent::GetInstance();
}

DBTable *AgentKsyncIntfSandesh::AgentGetTable() {
    return NULL;
}

bool AgentKsyncIntfSandesh::UpdateResp(KSyncEntry *entry) {
    InterfaceKSyncEntry *intf = static_cast< InterfaceKSyncEntry *>(entry);
    return intf->KSyncEntrySandesh(resp_, name_);
}

bool AgentKsyncIntfSandesh::Filter(const DBEntryBase *entry) {
    return true;
}    

bool AgentKsyncIntfSandesh::FilterToArgs(AgentSandeshArguments *args) {
    return true;
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
    agent_ = Agent::GetInstance();
}

DBTable *AgentKsyncNhListSandesh::AgentGetTable() {
    return NULL;
}

bool AgentKsyncNhListSandesh::UpdateResp(KSyncEntry *entry) {
    NHKSyncEntry *nh_list = static_cast< NHKSyncEntry *>(entry);
    return nh_list->KSyncEntrySandesh(resp_, name_);
}

bool AgentKsyncNhListSandesh::Filter(const DBEntryBase *entry) {
    return true;
}    

bool AgentKsyncNhListSandesh::FilterToArgs(AgentSandeshArguments *args) {
    return true;
}

KSyncDBObject *AgentKsyncNhListSandesh::AgentGetKsyncObject() {
    return agent_->ksync()->nh_ksync_obj();
}

void AgentKsyncNhListSandesh::Alloc() {
    resp_ = new KSyncNhListResp();
}

