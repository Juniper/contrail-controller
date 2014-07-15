/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_factory.h>
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
#include <oper/path_preference.h>
#include <oper/ifmap_dependency_manager.h>
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
    agent_->set_interface_table(intf_table);
    intf_table->Init(this);
    intf_table->set_agent(agent_);

    NextHopTable *nh_table;
    nh_table = static_cast<NextHopTable *>(db->CreateTable("db.nexthop.0"));
    assert(nh_table);
    agent_->set_nexthop_table(nh_table);
    nh_table->set_agent(agent_);

    VrfTable *vrf_table;
    vrf_table = static_cast<VrfTable *>(db->CreateTable("db.vrf.0"));
    assert(vrf_table);
    agent_->set_vrf_table(vrf_table);
    vrf_table->set_agent(agent_);

    VmTable *vm_table;
    vm_table = static_cast<VmTable *>(db->CreateTable("db.vm.0"));
    assert(vm_table);
    agent_->set_vm_table(vm_table);
    vm_table->set_agent(agent_);

    SgTable *sg_table;
    sg_table = static_cast<SgTable *>(db->CreateTable("db.sg.0"));
    assert(sg_table);
    agent_->set_sg_table(sg_table);
    sg_table->set_agent(agent_);

    VnTable *vn_table;
    vn_table = static_cast<VnTable *>(db->CreateTable("db.vn.0"));
    assert(vn_table);
    agent_->set_vn_table(vn_table);
    vn_table->set_agent(agent_);

    MplsTable *mpls_table;
    mpls_table = static_cast<MplsTable *>(db->CreateTable("db.mpls.0"));
    assert(mpls_table);
    agent_->set_mpls_table(mpls_table);
    mpls_table->set_agent(agent_);

    AclTable *acl_table;
    acl_table = static_cast<AclTable *>(db->CreateTable("db.acl.0"));
    assert(acl_table);
    agent_->set_acl_table(acl_table);
    acl_table->set_agent(agent_);

    MirrorTable *mirror_table;
    mirror_table = static_cast<MirrorTable *>
                   (db->CreateTable("db.mirror_table.0"));
    assert(mirror_table);
    agent_->set_mirror_table(mirror_table);
    mirror_table->set_agent(agent_);

    VrfAssignTable *vassign_table = static_cast<VrfAssignTable *>
                   (db->CreateTable("db.vrf_assign.0"));
    assert(vassign_table);
    agent_->set_vrf_assign_table(vassign_table);
    vassign_table->set_agent(agent_);

    DomainConfig *domain_config_table = new DomainConfig();
    agent_->set_domain_config_table(domain_config_table);

    VxLanTable *vxlan_table;
    vxlan_table = static_cast<VxLanTable *>(db->CreateTable("db.vxlan.0"));
    assert(vxlan_table);
    agent_->set_vxlan_table(vxlan_table);
    vxlan_table->set_agent(agent_);

    multicast_ = std::auto_ptr<MulticastHandler>(new MulticastHandler(agent_));
    global_vrouter_ = std::auto_ptr<GlobalVrouter> (new GlobalVrouter(this));
    route_preference_module_ =
        std::auto_ptr<PathPreferenceModule>(new PathPreferenceModule(agent_));
    route_preference_module_->Init();
}

void OperDB::Init() {
}

void OperDB::RegisterDBClients() {
    multicast_.get()->Register();
    global_vrouter_.get()->CreateDBClients();
}

OperDB::OperDB(Agent *agent)
        : agent_(agent),
          dependency_manager_(
              AgentObjectFactory::Create<IFMapDependencyManager>(
                  agent->db(), agent->cfg()->cfg_graph())) {
    assert(singleton_ == NULL);
    singleton_ = this;
}

OperDB::~OperDB() {
}

void OperDB::Shutdown() {
    global_vrouter_.reset();

#if 0
    agent_->interface_table()->Clear();
    agent_->nexthop_table()->Clear();
    agent_->vrf_table()->Clear();
    agent_->vn_table()->Clear();
    agent_->sg_table()->Clear();
    agent_->vm_table()->Clear();
    agent_->mpls_table()->Clear();
    agent_->acl_table()->Clear();
    agent_->mirror_table()->Clear();
    agent_->vrf_assign_table()->Clear();
    agent_->vxlan_table()->Clear();
#endif
}

void OperDB::DeleteRoutes() {
    agent_->vrf_table()->DeleteRoutes();
}
