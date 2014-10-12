/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>

#include <vnc_cfg_types.h> 
#include <agent_types.h>

#include <oper/peer.h>
#include <oper/vrf.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/mirror_table.h>
#include <oper/vxlan.h>
#include <oper/service_instance.h>
#include <filter/acl.h>
#include <oper/mpls.h>
#include <oper/route_common.h>
#include <oper/sg.h>
#include <oper/agent_sandesh.h>
#include <oper/vrf_assign.h>

#include <filter/acl.h>

DBTable *AgentVnSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vn_table());
}

void AgentVnSandesh::Alloc() {
    resp_ = new VnListResp();
}

DBTable *AgentSgSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->sg_table());
}

void AgentSgSandesh::Alloc() {
    resp_ = new SgListResp();
}

DBTable *AgentVmSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vm_table());
}

void AgentVmSandesh::Alloc() {
    resp_ = new VmListResp();
}

DBTable *AgentIntfSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->interface_table());
}

void AgentIntfSandesh::Alloc() {
    resp_ = new ItfResp();
}

DBTable *AgentNhSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->nexthop_table());
}

void AgentNhSandesh::Alloc() {
    resp_ = new NhListResp();
}

DBTable *AgentMplsSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->mpls_table());
}

void AgentMplsSandesh::Alloc() {
    resp_ = new MplsResp();
}

DBTable *AgentVrfSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vrf_table());
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
    InetUnicastRouteEntry *rt = static_cast<InetUnicastRouteEntry *>(entry);
    if (dump_table_) {
        return rt->DBEntrySandesh(resp_, stale_);
    }
    return rt->DBEntrySandesh(resp_, addr_, plen_, stale_);
}

DBTable *AgentInet6UcRtSandesh::AgentGetTable() {
    return static_cast<DBTable *>(vrf_->GetInet6UnicastRouteTable());
}

void AgentInet6UcRtSandesh::Alloc() {
    resp_ = new Inet6UcRouteResp();
}

bool AgentInet6UcRtSandesh::UpdateResp(DBEntryBase *entry) {
    InetUnicastRouteEntry *rt = static_cast<InetUnicastRouteEntry *>(entry);
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
    return static_cast<DBTable *>(Agent::GetInstance()->acl_table());
}

void AgentAclSandesh::Alloc() {
    resp_ = new AclResp();
}

bool AgentAclSandesh::UpdateResp(DBEntryBase *entry) {
    AclDBEntry *ent = static_cast<AclDBEntry *>(entry);
    return ent->DBEntrySandesh(resp_, name_);
}

DBTable *AgentMirrorSandesh::AgentGetTable(){
    return static_cast<DBTable *>(Agent::GetInstance()->mirror_table());
}

void AgentMirrorSandesh::Alloc(){
    resp_ = new MirrorEntryResp();
}

DBTable *AgentVrfAssignSandesh::AgentGetTable(){
    return static_cast<DBTable *>(Agent::GetInstance()->vrf_assign_table());
}

void AgentVrfAssignSandesh::Alloc(){
    resp_ = new VrfAssignResp();
}

DBTable *AgentVxLanSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->vxlan_table());
}

void AgentVxLanSandesh::Alloc() {
    resp_ = new VxLanResp();
}

DBTable *AgentServiceInstanceSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->service_instance_table());
}

void AgentServiceInstanceSandesh::Alloc() {
    resp_ = new ServiceInstanceResp();
}

DBTable *AgentLoadBalancerSandesh::AgentGetTable() {
    return static_cast<DBTable *>(Agent::GetInstance()->db()->
                        FindTable("db.loadbalancer-pool.0"));
}

void AgentLoadBalancerSandesh::Alloc() {
    resp_ = new LoadBalancerResp();
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
    if (agent->init_done()) {
        resp->set_state("InitDone");
    } else {
        resp->set_state("InProgress");
    }
    resp->Response();
}
