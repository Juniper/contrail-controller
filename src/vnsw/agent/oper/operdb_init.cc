/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <db/db.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>
#include <oper/operdb_init.h>
#include "oper/interface.h"
#include "oper/nexthop.h"
#include "oper/inet4_ucroute.h"
#include "oper/inet4_mcroute.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/sg.h"
#include "oper/mirror_table.h"
#include "oper/vrf_assign.h"
#include "cfg/init_config.h"
#include <base/task_trigger.h>

OperDB *OperDB::singleton_ = NULL;
SandeshTraceBufferPtr OperDBTraceBuf(SandeshTraceBufferCreate("Oper DB", 5000));

void OperDB::CreateDBTables(DB *db) {
    DB::RegisterFactory("db.interface.0", &InterfaceTable::CreateTable);
    DB::RegisterFactory("db.nexthop.0", &NextHopTable::CreateTable);
    DB::RegisterFactory("uc.route.0", &Inet4UcRouteTable::CreateTable);
    DB::RegisterFactory("mc.route.0", &Inet4McRouteTable::CreateTable);
    DB::RegisterFactory("db.vrf.0", &VrfTable::CreateTable);
    DB::RegisterFactory("db.vn.0", &VnTable::CreateTable);
    DB::RegisterFactory("db.vm.0", &VmTable::CreateTable);
    DB::RegisterFactory("db.sg.0", &SgTable::CreateTable);
    DB::RegisterFactory("db.mpls.0", &MplsTable::CreateTable);
    DB::RegisterFactory("db.acl.0", &AclTable::CreateTable);
    DB::RegisterFactory("db.mirror_table.0", &MirrorTable::CreateTable);
    DB::RegisterFactory("db.vrf_assign.0", &VrfAssignTable::CreateTable);

    InterfaceTable *intf_table;
    intf_table = static_cast<InterfaceTable *>(db->CreateTable("db.interface.0"));
    assert(intf_table);
    Agent::GetInstance()->SetInterfaceTable(intf_table);

    NextHopTable *nh_table;
    nh_table = static_cast<NextHopTable *>(db->CreateTable("db.nexthop.0"));
    assert(nh_table);
    Agent::GetInstance()->SetNextHopTable(nh_table);

    VrfTable *vrf_table;
    vrf_table = static_cast<VrfTable *>(db->CreateTable("db.vrf.0"));
    assert(vrf_table);
    Agent::GetInstance()->SetVrfTable(vrf_table);

    VmTable *vm_table;
    vm_table = static_cast<VmTable *>(db->CreateTable("db.vm.0"));
    assert(vm_table);
    Agent::GetInstance()->SetVmTable(vm_table);

    SgTable *sg_table;
    sg_table = static_cast<SgTable *>(db->CreateTable("db.sg.0"));
    assert(sg_table);
    Agent::GetInstance()->SetSgTable(sg_table);

    VnTable *vn_table;
    vn_table = static_cast<VnTable *>(db->CreateTable("db.vn.0"));
    assert(vn_table);
    Agent::GetInstance()->SetVnTable(vn_table);

    MplsTable *mpls_table;
    mpls_table = static_cast<MplsTable *>(db->CreateTable("db.mpls.0"));
    assert(mpls_table);
    Agent::GetInstance()->SetMplsTable(mpls_table);

    AclTable *acl_table;
    acl_table = static_cast<AclTable *>(db->CreateTable("db.acl.0"));
    assert(acl_table);
    Agent::GetInstance()->SetAclTable(acl_table);

    MirrorTable *mirror_table;
    mirror_table = static_cast<MirrorTable *>
                   (db->CreateTable("db.mirror_table.0"));
    assert(mirror_table);
    Agent::GetInstance()->SetMirrorTable(mirror_table);

    VrfAssignTable *vassign_table = static_cast<VrfAssignTable *>
                   (db->CreateTable("db.vrf_assign.0"));
    assert(vassign_table);
    Agent::GetInstance()->SetVrfAssignTable(vassign_table);

    DomainConfig *domain_config_table = new DomainConfig();
    Agent::GetInstance()->SetDomainConfigTable(domain_config_table);
}

void OperDB::CreateStaticObjects(Callback cb)
{
    Peer *local_peer;
    Peer *local_vm_peer;

    local_peer = new Peer(Peer::LOCAL_PEER, LOCAL_PEER_NAME);
    Agent::GetInstance()->SetLocalPeer(local_peer);

    local_vm_peer = new Peer(Peer::LOCAL_VM_PEER, LOCAL_VM_PEER_NAME);
    Agent::GetInstance()->SetLocalVmPeer(local_vm_peer);

    Agent::GetInstance()->SetMdataPeer(new Peer(Peer::MDATA_PEER, MDATA_PEER_NAME));

    // Create "DEFAULT" VRF and trigger init after entry is created
    OperDB *oper_inst = new OperDB(cb);
    oper_inst->CreateDefaultVrf();

    //Open up mirror socket
    Agent::GetInstance()->GetMirrorTable()->MirrorSockInit();
}

void OperDB::CreateDefaultVrf() {
    AgentConfig *config = AgentConfig::GetInstance();

    VrfTable *vrf_table = Agent::GetInstance()->GetVrfTable();
    vid_ = vrf_table->Register(boost::bind(&OperDB::OnVrfCreate, this, _2));
    vrf_table->CreateVrf(Agent::GetInstance()->GetDefaultVrf());
    if (config->isXenMode()) {
        vrf_table->CreateVrf(Agent::GetInstance()->GetLinkLocalVrfName());
    }
}

void OperDB::OnVrfCreate(DBEntryBase *entry) {
    if (entry->IsDeleted())
        return;

    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (vrf->GetName() == Agent::GetInstance()->GetDefaultVrf() && trigger_ == NULL) {
        // Default VRF created; create nexthops, unregister in DB Task context
        Agent::GetInstance()->SetDefaultInet4UcRouteTable(vrf->GetInet4UcRouteTable());
        Agent::GetInstance()->SetDefaultInet4McRouteTable(vrf->GetInet4McRouteTable());
        DiscardNH::CreateReq();
        ResolveNH::CreateReq();
        trigger_ = SafeDBUnregister(Agent::GetInstance()->GetVrfTable(), vid_);

        if (cb_)
            cb_();
    }
}


OperDB::OperDB(Callback cb) : vid_(-1), cb_(cb), trigger_(NULL) {
    assert(singleton_ == NULL);
    singleton_ = this;
}

OperDB::~OperDB() {
    assert(trigger_);
    trigger_->Reset();
    delete trigger_;
}

void OperDB::Shutdown() {
    Agent::GetInstance()->GetDB()->RemoveTable(Agent::GetInstance()->GetVnTable());
    delete Agent::GetInstance()->GetVnTable();
    Agent::GetInstance()->SetVnTable(NULL);

    Agent::GetInstance()->GetDB()->RemoveTable(Agent::GetInstance()->GetVmTable());
    delete Agent::GetInstance()->GetVmTable();
    Agent::GetInstance()->SetVmTable(NULL);

    Agent::GetInstance()->GetDB()->RemoveTable(Agent::GetInstance()->GetSgTable());
    delete Agent::GetInstance()->GetSgTable();
    Agent::GetInstance()->SetSgTable(NULL);

    Agent::GetInstance()->GetDB()->RemoveTable(Agent::GetInstance()->GetInterfaceTable());
    delete Agent::GetInstance()->GetInterfaceTable();
    Agent::GetInstance()->SetInterfaceTable(NULL);

    Agent::GetInstance()->GetDB()->RemoveTable(Agent::GetInstance()->GetVrfTable());
    delete Agent::GetInstance()->GetVrfTable();
    Agent::GetInstance()->SetVrfTable(NULL);

    Agent::GetInstance()->GetDB()->RemoveTable(Agent::GetInstance()->GetMplsTable());
    delete Agent::GetInstance()->GetMplsTable();
    Agent::GetInstance()->SetMplsTable(NULL);

    Agent::GetInstance()->GetDB()->RemoveTable(Agent::GetInstance()->GetNextHopTable());
    delete Agent::GetInstance()->GetNextHopTable();
    Agent::GetInstance()->SetNextHopTable(NULL);

    Agent::GetInstance()->GetDB()->RemoveTable(Agent::GetInstance()->GetMirrorTable());
    delete Agent::GetInstance()->GetMirrorTable();
    Agent::GetInstance()->SetMirrorTable(NULL);

    Agent::GetInstance()->GetDB()->RemoveTable(Agent::GetInstance()->GetVrfAssignTable());
    delete Agent::GetInstance()->GetVrfAssignTable();
    Agent::GetInstance()->SetVrfAssignTable(NULL);

    assert(OperDB::singleton_);
    delete OperDB::singleton_;
    OperDB::singleton_ = NULL;

    delete Agent::GetInstance()->GetLocalPeer();
    Agent::GetInstance()->SetLocalPeer(NULL);

    delete Agent::GetInstance()->GetLocalVmPeer();
    Agent::GetInstance()->SetLocalVmPeer(NULL);

    delete Agent::GetInstance()->GetMdataPeer();
    Agent::GetInstance()->SetMdataPeer(NULL);

    delete Agent::GetInstance()->GetDomainConfigTable();
    Agent::GetInstance()->SetDomainConfigTable(NULL);
}
