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
#include <cfg/cfg_interface.h>
#include <cfg/cfg_interface_listener.h>
#include <cfg/cfg_filter.h>
#include <cfg/cfg_mirror.h>
#include <cfg/cfg_listener.h>
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
#include <oper/service_instance.h>

#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>
#include <filter/acl.h>

using namespace std;
using namespace autogen;
using namespace boost::property_tree;
using namespace boost::uuids;
using boost::optional;

void IFMapAgentSandeshInit(DB *db);

SandeshTraceBufferPtr CfgTraceBuf(SandeshTraceBufferCreate("Config", 100));

AgentConfig::AgentConfig(Agent *agent)
        : agent_(agent),
          cfg_listener_(new CfgListener(agent->db())) {
    cfg_filter_ = std::auto_ptr<CfgFilter>(new CfgFilter(this));

    cfg_graph_ = std::auto_ptr<DBGraph>(new DBGraph());
    cfg_interface_client_ = std::auto_ptr<InterfaceCfgClient>
        (new InterfaceCfgClient(this));
    discovery_client_ = std::auto_ptr<DiscoveryAgentClient>
        (new DiscoveryAgentClient(this));

    cfg_mirror_table_ = std::auto_ptr<MirrorCfgTable>(new MirrorCfgTable(this));
    agent_->set_mirror_cfg_table(cfg_mirror_table_.get());

    cfg_intf_mirror_table_ = std::auto_ptr<IntfMirrorCfgTable>
       (new IntfMirrorCfgTable(this));
    agent_->set_interface_mirror_cfg_table(cfg_intf_mirror_table_.get());
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
    agent_->set_interface_config_table(table);

    // Create parser once we know the db
    cfg_parser_ = std::auto_ptr<IFMapAgentParser>(new IFMapAgentParser(db));
    vnc_cfg_Agent_ModuleInit(db, cfg_graph_.get());
    vnc_cfg_Agent_ParserInit(db, cfg_parser_.get());
    bgp_schema_Agent_ModuleInit(db, cfg_graph_.get());
    bgp_schema_Agent_ParserInit(db, cfg_parser_.get());
    IFMapAgentLinkTable_Init(db, cfg_graph_.get());
    agent_->set_ifmap_parser(cfg_parser_.get());
    IFMapAgentStaleCleaner *cl = 
        new IFMapAgentStaleCleaner(db, cfg_graph_.get(),
                                   *(agent_->event_manager()->io_service()));
    agent_->set_ifmap_stale_cleaner(cl);

    IFMapAgentSandeshInit(db);
}

void AgentConfig::RegisterDBClients(DB *db) {
    cfg_listener_->Register("virtual-network", agent_->vn_table(),
                            VirtualNetwork::ID_PERMS);
    cfg_listener_->Register("security-group", agent_->sg_table(),
                            SecurityGroup::ID_PERMS);
    cfg_listener_->Register("virtual-machine", agent_->vm_table(),
                            VirtualMachine::ID_PERMS);
    cfg_listener_->Register("virtual-machine-interface",
                            agent_->interface_table(),
                            VirtualMachineInterface::ID_PERMS);
    cfg_listener_->Register("access-control-list", agent_->acl_table(),
                            AccessControlList::ID_PERMS);
    cfg_listener_->Register("service-instance",
                            agent_->service_instance_table(),
                            ::autogen::ServiceInstance::ID_PERMS);
    cfg_listener_->Register("routing-instance", agent_->vrf_table(), -1);
    cfg_listener_->Register("virtual-network-network-ipam", 
                            boost::bind(&VnTable::IpamVnSync, _1), -1);
    cfg_listener_->Register("network-ipam", boost::bind(&DomainConfig::IpamSync,
                            agent_->domain_config_table(), _1), NetworkIpam::ID_PERMS);
    cfg_listener_->Register("virtual-DNS", boost::bind(&DomainConfig::VDnsSync, 
                            agent_->domain_config_table(), _1), VirtualDns::ID_PERMS);
    cfg_listener_->Register
        ("virtual-machine-interface-routing-instance", 
         boost::bind(&InterfaceTable::VmInterfaceVrfSync,
                     agent_->interface_table(), _1), -1);

    cfg_listener_->Register
        ("instance-ip", boost::bind(&VmInterface::InstanceIpSync,
                                    agent_->interface_table(), _1), -1);

    cfg_listener_->Register
        ("floating-ip", boost::bind(&VmInterface::FloatingIpVnSync,
                                    agent_->interface_table(), _1), -1);

    cfg_listener_->Register
        ("floating-ip-pool", boost::bind(&VmInterface::FloatingIpPoolSync,
                                         agent_->interface_table(), _1), -1);

    cfg_listener_->Register
        ("global-vrouter-config",
         boost::bind(&GlobalVrouter::GlobalVrouterConfig,
                     agent_->oper_db()->global_vrouter(), _1), -1);

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

    cfg_floatingip_pool_table_ = (static_cast<IFMapAgentTable *>
        (IFMapTable::FindTable(agent_->db(), "floating-ip-pool")));
    assert(cfg_floatingip_pool_table_);

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

    cfg_interface_client_->Init();
}

void AgentConfig::Init() {
    cfg_filter_->Init();
    cfg_listener_->Init();
    cfg_mirror_table_->Init();
    cfg_intf_mirror_table_->Init();
}

void AgentConfig::InitDiscovery() {
    agent_->discovery_client()->Init(agent_->params());
}

void AgentConfig::InitDone() {
    discovery_client_->DiscoverServices();
}

void AgentConfig::Shutdown() {
    cfg_listener_->Shutdown();
    cfg_filter_->Shutdown();

    cfg_interface_client_->Shutdown();
    discovery_client_->Shutdown();

    cfg_mirror_table_->Shutdown();
    cfg_intf_mirror_table_->Shutdown();

    agent_->db()->RemoveTable(agent_->interface_config_table());
    delete agent_->interface_config_table();
    agent_->set_interface_config_table(NULL);

    agent_->set_ifmap_parser(NULL);

    agent_->ifmap_stale_cleaner()->Clear();
    delete agent_->ifmap_stale_cleaner();
    agent_->set_ifmap_stale_cleaner(NULL);
}
