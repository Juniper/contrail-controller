/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <cmn/agent_db.h>
#include <init/agent_param.h>
#include <init/agent_init.h>
#include <oper/agent_sandesh.h>
#include <oper/agent_types.h>
#include <oper/route_common.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/vrf.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/vrf_assign.h>
#include <oper/vxlan.h>
#include <filter/acl.h>
#include <oper/sg.h>

DBTable *AgentVnSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->GetVnTable());
}

void AgentVnSandesh::Alloc() {
    resp_ = new VnListResp();
}

DBTable *AgentSgSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->GetSgTable());
}

void AgentSgSandesh::Alloc() {
    resp_ = new SgListResp();
}

DBTable *AgentVmSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->GetVmTable());
}

void AgentVmSandesh::Alloc() {
    resp_ = new VmListResp();
}

DBTable *AgentIntfSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->GetInterfaceTable());
}

void AgentIntfSandesh::Alloc() {
    resp_ = new ItfResp();
}

DBTable *AgentNhSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->GetNextHopTable());
}

void AgentNhSandesh::Alloc() {
    resp_ = new NhListResp();
}

DBTable *AgentMplsSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->GetMplsTable());
}

void AgentMplsSandesh::Alloc() {
    resp_ = new MplsResp();
}

DBTable *AgentVrfSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->GetVrfTable());
}

void AgentVrfSandesh::Alloc() {
    resp_ = new VrfListResp();
}

DBTable *AgentInet4UcRtSandesh::AgentGetTable() {
    return static_cast<DBTable *>(vrf_->GetInet4UnicastRouteTable());
}

void AgentInet4UcRtSandesh::Alloc() {
    resp_ = new Inet4UcRouteResp();
}

bool AgentInet4UcRtSandesh::UpdateResp(DBEntryBase *entry) {
    Inet4UnicastRouteEntry *rt = static_cast<Inet4UnicastRouteEntry *>(entry);
    if (dump_table_) {
        return rt->DBEntrySandesh(resp_, stale_);
    }
    return rt->DBEntrySandesh(resp_, addr_, plen_, stale_);
}

DBTable *AgentInet4McRtSandesh::AgentGetTable() {
    return static_cast<DBTable *>(vrf_->GetInet4MulticastRouteTable());
}

void AgentInet4McRtSandesh::Alloc() {
    resp_ = new Inet4McRouteResp();
}

bool AgentInet4McRtSandesh::UpdateResp(DBEntryBase *entry) {
    AgentRoute *rt = static_cast<AgentRoute *>(entry);
    return rt->DBEntrySandesh(resp_, stale_);
}

DBTable *AgentLayer2RtSandesh::AgentGetTable() {
    return static_cast<DBTable *>(vrf_->GetLayer2RouteTable());
}

void AgentLayer2RtSandesh::Alloc() {
    resp_ = new Layer2RouteResp();
}

bool AgentLayer2RtSandesh::UpdateResp(DBEntryBase *entry) {
    AgentRoute *rt = static_cast<AgentRoute *>(entry);
    return rt->DBEntrySandesh(resp_, stale_);
}

DBTable *AgentAclSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->GetAclTable());
}

void AgentAclSandesh::Alloc() {
    resp_ = new AclResp();
}

bool AgentAclSandesh::UpdateResp(DBEntryBase *entry) {
    AclDBEntry *ent = static_cast<AclDBEntry *>(entry);
    return ent->DBEntrySandesh(resp_, name_);
}

DBTable *AgentMirrorSandesh::AgentGetTable(){
    return static_cast<DBTable *>(Agent::GetInstance()->GetMirrorTable());
}

void AgentMirrorSandesh::Alloc(){
    resp_ = new MirrorEntryResp();
}

DBTable *AgentVrfAssignSandesh::AgentGetTable(){
    return static_cast<DBTable *>(Agent::GetInstance()->GetVrfAssignTable());
}

void AgentVrfAssignSandesh::Alloc(){
    resp_ = new VrfAssignResp();
}

DBTable *AgentVxLanSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->GetVxLanTable());
}

void AgentVxLanSandesh::Alloc() {
    resp_ = new VxLanResp();
}

void AgentSandesh::DoSandesh() {
    DBTable *table;

    table = AgentGetTable();
    if (table == NULL) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context_);
        resp->Response();
        return;
    }

    SetResp();
    DBTableWalker *walker = table->database()->GetWalker();
    walkid_ = walker->WalkTable(table, NULL,
        boost::bind(&AgentSandesh::EntrySandesh, this, _2),
        boost::bind(&AgentSandesh::SandeshDone, this));
}

void AgentSandesh::SetResp() {
    Alloc();
    resp_->set_context(context_);
}

bool AgentSandesh::UpdateResp(DBEntryBase *entry) {
    AgentDBEntry *ent = static_cast<AgentDBEntry *>(entry);
    return ent->DBEntrySandesh(resp_, name_);
}

bool AgentSandesh::EntrySandesh(DBEntryBase *entry) {
    if (!UpdateResp(entry)) {
        return true;
    }

    count_++;
    if (!(count_ % entries_per_sandesh)) {
        // send partial sandesh
        resp_->set_context(resp_->context());
        resp_->set_more(true);
        resp_->Response();
        SetResp();
    }

    return true;
}

void AgentSandesh::SandeshDone() {
    walkid_ = DBTableWalker::kInvalidWalkerId;
    resp_->Response();
    delete this;
}

void AgentInitStateReq::HandleRequest() const {
    AgentInitState *resp = new AgentInitState();
    resp->set_context(context());
    Agent *agent = Agent::GetInstance();
    if (agent->init()->init_done()) {
        resp->set_state("InitDone");
    } else {
        resp->set_state("InProgress");
    }
    resp->Response();
}
