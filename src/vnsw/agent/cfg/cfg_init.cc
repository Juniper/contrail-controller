/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>

#include <boost/property_tree/xml_parser.hpp>
#include <db/db_graph.h>

#include <cmn/agent.h>
#include <cmn/agent_db.h>

#include <bgp_schema_types.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_filter.h>
#include <cfg/cfg_mirror.h>

#include <oper/vn.h>
#include <oper/sg.h>
#include <oper/vm.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <oper/route_common.h>
#include <oper/operdb_init.h>
#include <oper/global_vrouter.h>
#include <oper/service_instance.h>
#include <oper/physical_device.h>

#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>
#include <filter/acl.h>

using namespace std;
using namespace autogen;
using namespace boost::property_tree;
using namespace boost::uuids;
using boost::optional;

void IFMapAgentSandeshInit(DB *, IFMapAgentParser *);

SandeshTraceBufferPtr CfgTraceBuf(SandeshTraceBufferCreate("Config", 2000));

AgentConfig::AgentConfig(Agent *agent)
        : agent_(agent) {
    cfg_filter_ = std::auto_ptr<CfgFilter>(new CfgFilter(this));

    cfg_graph_ = std::auto_ptr<DBGraph>(new DBGraph());
    cfg_mirror_table_ = std::auto_ptr<MirrorCfgTable>(new MirrorCfgTable(this));
    agent_->set_mirror_cfg_table(cfg_mirror_table_.get());

    cfg_intf_mirror_table_ = std::auto_ptr<IntfMirrorCfgTable>
       (new IntfMirrorCfgTable(this));
    agent_->set_interface_mirror_cfg_table(cfg_intf_mirror_table_.get());
}

AgentConfig::~AgentConfig() { 
    cfg_filter_.reset();
    cfg_parser_.reset();
    cfg_graph_.reset();
    cfg_mirror_table_.reset();
    cfg_intf_mirror_table_.reset();
}

void AgentConfig::CreateDBTables(DB *db) {
    // Create parser once we know the db
    cfg_parser_ = std::auto_ptr<IFMapAgentParser>(new IFMapAgentParser(db));
    vnc_cfg_Agent_ModuleInit(db, cfg_graph_.get());
    vnc_cfg_Agent_ParserInit(db, cfg_parser_.get());
    bgp_schema_Agent_ModuleInit(db, cfg_graph_.get());
    bgp_schema_Agent_ParserInit(db, cfg_parser_.get());
    IFMapAgentLinkTable_Init(db, cfg_graph_.get());
    agent_->set_ifmap_parser(cfg_parser_.get());
    IFMapAgentStaleCleaner *cl = 
        new IFMapAgentStaleCleaner(db, cfg_graph_.get());
    agent_->set_ifmap_stale_cleaner(cl);

    IFMapAgentSandeshInit(db, cfg_parser_.get());
}

void AgentConfig::Register(const char *node_name, AgentDBTable *table,
                           int need_property_id) {
}

void AgentConfig::RegisterDBClients(DB *db) {

    cfg_vm_interface_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "virtual-machine-interface")));
    assert(cfg_vm_interface_table_);

    cfg_acl_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "access-control-list")));
    assert(cfg_acl_table_);

    cfg_vm_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "virtual-machine")));
    assert(cfg_vm_table_);

    cfg_vn_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "virtual-network")));
    assert(cfg_vn_table_);

    cfg_sg_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "security-group")));
    assert(cfg_sg_table_);         

    cfg_vrf_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "routing-instance")));
    assert(cfg_vrf_table_);

    cfg_instanceip_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "instance-ip")));
    assert(cfg_instanceip_table_);

    cfg_floatingip_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "floating-ip")));
    assert(cfg_floatingip_table_);

    cfg_aliasip_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "alias-ip")));
    assert(cfg_aliasip_table_);

    cfg_floatingip_pool_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "floating-ip-pool")));
    assert(cfg_floatingip_pool_table_);

    cfg_aliasip_pool_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "alias-ip-pool")));
    assert(cfg_aliasip_pool_table_);

    cfg_network_ipam_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "network-ipam")));
    assert(cfg_network_ipam_table_);

    cfg_vn_network_ipam_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), 
                               "virtual-network-network-ipam")));
    assert(cfg_vn_network_ipam_table_);

    cfg_vm_port_vrf_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), 
                               "virtual-machine-interface-routing-instance")));
    assert(cfg_vm_port_vrf_table_);

    cfg_route_table_ = (static_cast<IFMapAgentTable *>
         (IFMapTable::FindTable(agent_->db(),
                               "interface-route-table")));
    assert(cfg_route_table_);

    cfg_service_instance_table_ = (static_cast<IFMapAgentTable *>
         (IFMapTable::FindTable(agent_->db(),
                               "service_instance")));
    assert(cfg_service_instance_table_);

    cfg_security_group_table_ = (static_cast<IFMapAgentTable *>
         (IFMapTable::FindTable(agent_->db(),
                               "security_group")));
    assert(cfg_security_group_table_);

    cfg_subnet_table_ = (static_cast<IFMapAgentTable *>
                    (IFMapTable::FindTable(agent_->db(),
                      "subnet")));
    assert(cfg_route_table_);

    cfg_logical_port_table_ = (static_cast<IFMapAgentTable *>
                                (IFMapTable::FindTable(agent_->db(),
                                                       "logical-interface")));
    assert(cfg_logical_port_table_);

    cfg_physical_device_table_ = (static_cast<IFMapAgentTable *>
                                (IFMapTable::FindTable(agent_->db(),
                                                       "physical-router")));
    assert(cfg_physical_device_table_);

    cfg_health_check_table_ = (static_cast<IFMapAgentTable *>
                                (IFMapTable::FindTable(agent_->db(),
                                                       "service-health-check")));
    assert(cfg_health_check_table_);

    cfg_qos_table_ = (static_cast<IFMapAgentTable *>
                      (IFMapTable::FindTable(agent_->db(),
                                             "qos-config")));
    assert(cfg_qos_table_);

    cfg_global_qos_table_ = (static_cast<IFMapAgentTable *>
                            (IFMapTable::FindTable(agent_->db(),
                                                   "global-qos-config")));
    assert(cfg_global_qos_table_);

    cfg_qos_queue_table_ = (static_cast<IFMapAgentTable *>
                            (IFMapTable::FindTable(agent_->db(),
                                                   "qos-queue")));
    assert(cfg_qos_queue_table_);

    cfg_forwarding_class_table_ = (static_cast<IFMapAgentTable *>
                            (IFMapTable::FindTable(agent_->db(),
                                                   "forwarding-class")));
    assert(cfg_forwarding_class_table_);

    cfg_bridge_domain_table_ = (static_cast<IFMapAgentTable *>
                               (IFMapTable::FindTable(agent_->db(),
                                                      "bridge-domain")));
    assert(cfg_bridge_domain_table_);

    cfg_vm_port_bridge_domain_table_ = (static_cast<IFMapAgentTable *>
                             (IFMapTable::FindTable(agent_->db(),
                                   "virtual-machine-interface-bridge-domain")));
    assert(cfg_vm_port_bridge_domain_table_);

}

void AgentConfig::Init() {
    cfg_filter_->Init();
    cfg_mirror_table_->Init();
    cfg_intf_mirror_table_->Init();
}

void AgentConfig::InitDone() {
}

void AgentConfig::Shutdown() {
    cfg_filter_->Shutdown();

    cfg_mirror_table_->Shutdown();
    cfg_intf_mirror_table_->Shutdown();

    agent_->set_ifmap_parser(NULL);

    agent_->ifmap_stale_cleaner()->Clear();
    delete agent_->ifmap_stale_cleaner();
    agent_->set_ifmap_stale_cleaner(NULL);
}
