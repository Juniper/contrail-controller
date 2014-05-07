/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/stat.h>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/uuid/string_generator.hpp>

#include <db/db.h>
#include <db/db_graph.h>
#include <base/logging.h>

#include <vnc_cfg_types.h> 
#include <bgp_schema_types.h>
#include <pugixml/pugixml.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <cfg/cfg_interface_listener.h>
#include <cfg/cfg_filter.h>
#include <cfg/cfg_mirror.h>
#include <cfg/discovery_agent.h>

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

#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>

using namespace std;
using namespace autogen;
using namespace boost::property_tree;
using namespace boost::uuids;
using boost::optional;

void IFMapAgentSandeshInit(DB *db);

SandeshTraceBufferPtr CfgTraceBuf(SandeshTraceBufferCreate("Config", 100));

AgentConfig::AgentConfig(Agent *agent) : agent_(agent) {
    cfg_filter_ = std::auto_ptr<CfgFilter>(new CfgFilter(this));

    cfg_listener_ = std::auto_ptr<CfgListener>(new CfgListener(this));

    cfg_graph_ = std::auto_ptr<DBGraph>(new DBGraph());
    cfg_interface_client_ = std::auto_ptr<InterfaceCfgClient>
        (new InterfaceCfgClient(this));
    discovery_client_ = std::auto_ptr<DiscoveryAgentClient>
        (new DiscoveryAgentClient(this));

    cfg_mirror_table_ = std::auto_ptr<MirrorCfgTable>(new MirrorCfgTable(this));
    agent_->SetMirrorCfgTable(cfg_mirror_table_.get());

    cfg_intf_mirror_table_ = std::auto_ptr<IntfMirrorCfgTable>
       (new IntfMirrorCfgTable(this));
    agent_->SetIntfMirrorCfgTable(cfg_intf_mirror_table_.get());
}

AgentConfig::~AgentConfig() { 
    cfg_filter_.reset();
    cfg_listener_.reset();
    cfg_parser_.reset();
    cfg_graph_.reset();
    cfg_interface_client_.reset();
    discovery_client_.reset();
    cfg_mirror_table_.reset();
    cfg_intf_mirror_table_.reset();
}

void AgentConfig::CreateDBTables(DB *db) {
    CfgIntTable *table;

    DB::RegisterFactory("db.cfg_int.0", &CfgIntTable::CreateTable);
    table = static_cast<CfgIntTable *>(db->CreateTable("db.cfg_int.0"));
    assert(table);
    agent_->SetIntfCfgTable(table);

    // Create parser once we know the db
    cfg_parser_ = std::auto_ptr<IFMapAgentParser>(new IFMapAgentParser(db));
    vnc_cfg_Agent_ModuleInit(db, cfg_graph_.get());
    vnc_cfg_Agent_ParserInit(db, cfg_parser_.get());
    bgp_schema_Agent_ModuleInit(db, cfg_graph_.get());
    bgp_schema_Agent_ParserInit(db, cfg_parser_.get());
    IFMapAgentLinkTable_Init(db, cfg_graph_.get());
    agent_->SetIfMapAgentParser(cfg_parser_.get());
    IFMapAgentStaleCleaner *cl = 
        new IFMapAgentStaleCleaner(db, cfg_graph_.get(),
                                   *(agent_->GetEventManager()->io_service()));
    agent_->SetAgentStaleCleaner(cl);

    IFMapAgentSandeshInit(db);
}

void AgentConfig::RegisterDBClients(DB *db) {
    cfg_listener_.get()->Register("virtual-network", agent_->GetVnTable(),
                            VirtualNetwork::ID_PERMS);
    cfg_listener_.get()->Register("security-group", agent_->GetSgTable(),
                            SecurityGroup::ID_PERMS);
    cfg_listener_.get()->Register("virtual-machine", agent_->GetVmTable(),
                            VirtualMachine::ID_PERMS);
    cfg_listener_.get()->Register("virtual-machine-interface",
                            agent_->GetInterfaceTable(),
                            VirtualMachineInterface::ID_PERMS);
    cfg_listener_.get()->Register("access-control-list", agent_->GetAclTable(),
                            AccessControlList::ID_PERMS);
    cfg_listener_.get()->Register("routing-instance", agent_->GetVrfTable(), -1);
    cfg_listener_.get()->Register("virtual-network-network-ipam", 
                            boost::bind(&VnTable::IpamVnSync, _1), -1);
    cfg_listener_.get()->Register("network-ipam", boost::bind(&DomainConfig::IpamSync,
                            agent_->GetDomainConfigTable(), _1), -1);
    cfg_listener_.get()->Register("virtual-DNS", boost::bind(&DomainConfig::VDnsSync, 
                            agent_->GetDomainConfigTable(), _1), -1);
    cfg_listener_.get()->Register
        ("virtual-machine-interface-routing-instance", 
         boost::bind(&InterfaceTable::VmInterfaceVrfSync,
                     agent_->GetInterfaceTable(), _1), -1);

    cfg_listener_.get()->Register
        ("instance-ip", boost::bind(&VmInterface::InstanceIpSync,
                                    agent_->GetInterfaceTable(), _1), -1);

    cfg_listener_.get()->Register
        ("floating-ip", boost::bind(&VmInterface::FloatingIpVnSync,
                                    agent_->GetInterfaceTable(), _1), -1);

    cfg_listener_.get()->Register
        ("floating-ip-pool", boost::bind(&VmInterface::FloatingIpPoolSync,
                                         agent_->GetInterfaceTable(), _1), -1);

    cfg_listener_.get()->Register
        ("global-vrouter-config",
         boost::bind(&GlobalVrouter::GlobalVrouterConfig,
                     agent_->oper_db()->global_vrouter(), _1), -1);

    cfg_vm_interface_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), "virtual-machine-interface")));
    assert(cfg_vm_interface_table_);

    cfg_acl_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), "access-control-list")));
    assert(cfg_acl_table_);

    cfg_vm_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), "virtual-machine")));
    assert(cfg_vm_table_);

    cfg_vn_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), "virtual-network")));
    assert(cfg_vn_table_);

    cfg_sg_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), "security-group")));
    assert(cfg_sg_table_);         

    cfg_vrf_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), "routing-instance")));
    assert(cfg_vrf_table_);

    cfg_instanceip_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), "instance-ip")));
    assert(cfg_instanceip_table_);

    cfg_floatingip_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), "floating-ip")));
    assert(cfg_floatingip_table_);

    cfg_floatingip_pool_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), "floating-ip-pool")));
    assert(cfg_floatingip_pool_table_);

    cfg_network_ipam_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), "network-ipam")));
    assert(cfg_network_ipam_table_);

    cfg_vn_network_ipam_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), 
                               "virtual-network-network-ipam")));
    assert(cfg_vn_network_ipam_table_);

    cfg_vm_port_vrf_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->GetDB(), 
                               "virtual-machine-interface-routing-instance")));
    assert(cfg_vm_port_vrf_table_);

    cfg_route_table_ = (static_cast<IFMapAgentTable *>
         (IFMapTable::FindTable(agent_->GetDB(), 
                               "interface-route-table")));
    assert(cfg_route_table_);

    cfg_interface_client_.get()->Init();
}

void AgentConfig::Init() {
    cfg_filter_.get()->Init();
    cfg_listener_.get()->Init();
    cfg_mirror_table_.get()->Init();
    cfg_intf_mirror_table_.get()->Init();
}

void AgentConfig::InitDiscovery() {
    agent_->discovery_client()->Init(agent_->params());
}

void AgentConfig::InitDone() {
    discovery_client_.get()->DiscoverServices();
}

void AgentConfig::Shutdown() {
    cfg_listener_.get()->Shutdown();
    cfg_filter_.get()->Shutdown();

    cfg_interface_client_.get()->Shutdown();
    discovery_client_.get()->Shutdown();

    cfg_mirror_table_.get()->Shutdown();
    cfg_intf_mirror_table_.get()->Shutdown();

    agent_->GetDB()->RemoveTable(agent_->GetIntfCfgTable());
    delete agent_->GetIntfCfgTable();
    agent_->SetIntfCfgTable(NULL);

    agent_->SetIfMapAgentParser(NULL);

    agent_->GetIfMapAgentStaleCleaner()->Clear();
    delete agent_->GetIfMapAgentStaleCleaner();
    agent_->SetAgentStaleCleaner(NULL);
}
