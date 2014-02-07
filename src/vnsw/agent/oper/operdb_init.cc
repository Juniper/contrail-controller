/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <db/db.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>
#include <cfg/cfg_init.h>
#include <oper/route_common.h>
#include <oper/operdb_init.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/vrf.h>
#include <oper/mpls.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/sg.h>
#include <oper/mirror_table.h>
#include <oper/vrf_assign.h>
#include <oper/vxlan.h>
#include <oper/multicast.h>
#include <oper/global_vrouter.h>
#include <oper/agent_route_encap.h>
#include <base/task_trigger.h>

OperDB *OperDB::singleton_ = NULL;
SandeshTraceBufferPtr OperDBTraceBuf(SandeshTraceBufferCreate("Oper DB", 5000));

void OperDB::CreateDBTables(DB *db) {
    DB::RegisterFactory("db.interface.0", &InterfaceTable::CreateTable);
    DB::RegisterFactory("db.nexthop.0", &NextHopTable::CreateTable);
    DB::RegisterFactory("uc.route.0",
                        &Inet4UnicastAgentRouteTable::CreateTable);
    DB::RegisterFactory("mc.route.0",
                        &Inet4MulticastAgentRouteTable::CreateTable);
    DB::RegisterFactory("l2.route.0", &Layer2AgentRouteTable::CreateTable);
    DB::RegisterFactory("db.vrf.0", &VrfTable::CreateTable);
    DB::RegisterFactory("db.vn.0", &VnTable::CreateTable);
    DB::RegisterFactory("db.vm.0", &VmTable::CreateTable);
    DB::RegisterFactory("db.sg.0", &SgTable::CreateTable);
    DB::RegisterFactory("db.mpls.0", &MplsTable::CreateTable);
    DB::RegisterFactory("db.acl.0", &AclTable::CreateTable);
    DB::RegisterFactory("db.mirror_table.0", &MirrorTable::CreateTable);
    DB::RegisterFactory("db.vrf_assign.0", &VrfAssignTable::CreateTable);
    DB::RegisterFactory("db.vxlan.0", &VxLanTable::CreateTable);

    InterfaceTable *intf_table;
    intf_table = static_cast<InterfaceTable *>(db->CreateTable("db.interface.0"));
    assert(intf_table);
    agent_->SetInterfaceTable(intf_table);
    intf_table->Init(this);
    intf_table->set_agent(agent_);

    NextHopTable *nh_table;
    nh_table = static_cast<NextHopTable *>(db->CreateTable("db.nexthop.0"));
    assert(nh_table);
    agent_->SetNextHopTable(nh_table);
    nh_table->set_agent(agent_);

    VrfTable *vrf_table;
    vrf_table = static_cast<VrfTable *>(db->CreateTable("db.vrf.0"));
    assert(vrf_table);
    agent_->SetVrfTable(vrf_table);
    vrf_table->set_agent(agent_);

    VmTable *vm_table;
    vm_table = static_cast<VmTable *>(db->CreateTable("db.vm.0"));
    assert(vm_table);
    agent_->SetVmTable(vm_table);
    vm_table->set_agent(agent_);

    SgTable *sg_table;
    sg_table = static_cast<SgTable *>(db->CreateTable("db.sg.0"));
    assert(sg_table);
    agent_->SetSgTable(sg_table);
    sg_table->set_agent(agent_);

    VnTable *vn_table;
    vn_table = static_cast<VnTable *>(db->CreateTable("db.vn.0"));
    assert(vn_table);
    agent_->SetVnTable(vn_table);
    vn_table->set_agent(agent_);

    MplsTable *mpls_table;
    mpls_table = static_cast<MplsTable *>(db->CreateTable("db.mpls.0"));
    assert(mpls_table);
    agent_->SetMplsTable(mpls_table);
    mpls_table->set_agent(agent_);

    AclTable *acl_table;
    acl_table = static_cast<AclTable *>(db->CreateTable("db.acl.0"));
    assert(acl_table);
    agent_->SetAclTable(acl_table);
    acl_table->set_agent(agent_);

    MirrorTable *mirror_table;
    mirror_table = static_cast<MirrorTable *>
                   (db->CreateTable("db.mirror_table.0"));
    assert(mirror_table);
    agent_->SetMirrorTable(mirror_table);
    mirror_table->set_agent(agent_);

    VrfAssignTable *vassign_table = static_cast<VrfAssignTable *>
                   (db->CreateTable("db.vrf_assign.0"));
    assert(vassign_table);
    agent_->SetVrfAssignTable(vassign_table);
    vassign_table->set_agent(agent_);

    DomainConfig *domain_config_table = new DomainConfig();
    agent_->SetDomainConfigTable(domain_config_table);

    VxLanTable *vxlan_table;
    vxlan_table = static_cast<VxLanTable *>(db->CreateTable("db.vxlan.0"));
    assert(vxlan_table);
    agent_->SetVxLanTable(vxlan_table);
    vxlan_table->set_agent(agent_);

    multicast_ = std::auto_ptr<MulticastHandler>(new MulticastHandler(agent_));
    global_vrouter_ = std::auto_ptr<GlobalVrouter> (new GlobalVrouter(this));
}

void OperDB::Init() {
    Peer *local_peer;
    local_peer = new Peer(Peer::LOCAL_PEER, LOCAL_PEER_NAME);
    agent_->SetLocalPeer(local_peer);

    Peer *local_vm_peer;
    local_vm_peer = new Peer(Peer::LOCAL_VM_PEER, LOCAL_VM_PEER_NAME);
    agent_->SetLocalVmPeer(local_vm_peer);

    Peer *linklocal_peer;
    linklocal_peer = new Peer(Peer::LINKLOCAL_PEER, LINKLOCAL_PEER_NAME);
    agent_->SetLinkLocalPeer(linklocal_peer);
}

void OperDB::CreateDBClients() {
    multicast_.get()->Register();
    global_vrouter_.get()->CreateDBClients();
}

OperDB::OperDB(Agent *agent) : agent_(agent) {
    assert(singleton_ == NULL);
    singleton_ = this;
}

OperDB::~OperDB() {
}

void OperDB::Shutdown() {
    global_vrouter_.reset();

    agent_->GetDB()->RemoveTable(agent_->GetVnTable());
    delete agent_->GetVnTable();
    agent_->SetVnTable(NULL);

    agent_->GetDB()->RemoveTable(agent_->GetVmTable());
    delete agent_->GetVmTable();
    agent_->SetVmTable(NULL);

    agent_->GetDB()->RemoveTable(agent_->GetSgTable());
    delete agent_->GetSgTable();
    agent_->SetSgTable(NULL);

    agent_->GetDB()->RemoveTable(agent_->GetInterfaceTable());
    delete agent_->GetInterfaceTable();
    agent_->SetInterfaceTable(NULL);

    agent_->GetDB()->RemoveTable(agent_->GetVrfTable());
    delete agent_->GetVrfTable();
    agent_->SetVrfTable(NULL);

    agent_->GetDB()->RemoveTable(agent_->GetMplsTable());
    delete agent_->GetMplsTable();
    agent_->SetMplsTable(NULL);

    agent_->GetDB()->RemoveTable(agent_->GetNextHopTable());
    delete agent_->GetNextHopTable();
    agent_->SetNextHopTable(NULL);

    agent_->GetDB()->RemoveTable(agent_->GetMirrorTable());
    delete agent_->GetMirrorTable();
    agent_->SetMirrorTable(NULL);

    agent_->GetDB()->RemoveTable(agent_->GetVrfAssignTable());
    delete agent_->GetVrfAssignTable();
    agent_->SetVrfAssignTable(NULL);

    agent_->GetDB()->RemoveTable(agent_->GetVxLanTable());
    delete agent_->GetVxLanTable();
    agent_->SetVxLanTable(NULL);

    delete agent_->GetLocalPeer();
    agent_->SetLocalPeer(NULL);

    delete agent_->GetLocalVmPeer();
    agent_->SetLocalVmPeer(NULL);

    delete agent_->GetLinkLocalPeer();
    agent_->SetLinkLocalPeer(NULL);

    delete agent_->GetDomainConfigTable();
    agent_->SetDomainConfigTable(NULL);
}
