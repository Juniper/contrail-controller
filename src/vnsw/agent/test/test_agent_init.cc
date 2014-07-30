/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/stat.h>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>

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
#include <cmn/agent_factory.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_mirror.h>
#include <cfg/discovery_agent.h>

#include <cmn/agent_param.h>

#include <oper/operdb_init.h>
#include <oper/vrf.h>
#include <oper/multicast.h>
#include <oper/mirror_table.h>
#include <oper/sg.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <uve/agent_uve.h>
#include <kstate/kstate.h>
#include <pkt/proto.h>
#include <pkt/proto_handler.h>
#include <pkt/agent_stats.h>
#include <diag/diag.h>
#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>

#include "test_agent_init.h"
TestAgentInit::TestAgentInit() :
        agent_(NULL), params_(NULL), create_vhost_(true), ksync_enable_(true),
        services_enable_(true), packet_enable_(true), uve_enable_(true),
        vgw_enable_(true), router_id_dep_enable_(true), trigger_() { }

TestAgentInit::~TestAgentInit() {
        trigger_->Reset();
    }
/****************************************************************************
 * Initialization routines
****************************************************************************/
// Initialization for VMWare specific interfaces
void TestAgentInit::InitVmwareInterface() {
    if (!params_->isVmwareMode())
        return;

    PhysicalInterface::Create(agent_->interface_table(),
                              params_->vmware_physical_port(),
                              agent_->fabric_vrf_name(), true);
}

void TestAgentInit::InitLogging() {
    Sandesh::SetLoggingParams(params_->log_local(),
                              params_->log_category(),
                              params_->log_level());
}

// Connect to collector specified in config, if discovery server is not set
void TestAgentInit::InitCollector() {
    agent_->InitCollector();
}

// Create the basic modules for agent operation.
// Optional modules or modules that have different implementation are created
// by init module
void TestAgentInit::CreateModules() {
    cfg_.reset(new AgentConfig(agent_));
    agent_->set_cfg(cfg_.get());

    stats_.reset(new AgentStats(agent_));
    agent_->set_stats(stats_.get());

    agent_->set_oper_db(new OperDB(agent_));


    uve_.reset(AgentObjectFactory::Create<AgentUve>
               (agent_, AgentUve::kBandwidthInterval));
    agent_->set_uve(uve_.get());

    ksync_.reset(AgentObjectFactory::Create<KSync>(agent_));
    agent_->set_ksync(ksync_.get());

    pkt_.reset(new PktModule(agent_));
    agent_->set_pkt(pkt_.get());

    services_.reset(new ServicesModule(agent_,
                                       params_->metadata_shared_secret()));
    agent_->set_services(services_.get());
    if (vgw_enable_) {
        vgw_.reset(new VirtualGateway(agent_));
        agent_->set_vgw(vgw_.get());
    }

    controller_.reset(new VNController(agent_));
    agent_->set_controller(controller_.get());
}

void TestAgentInit::CreateDBTables() {
    if (agent_->cfg()) {
        agent_->cfg()->CreateDBTables(agent_->db());
    }

    if (agent_->oper_db()) {
        agent_->oper_db()->CreateDBTables(agent_->db());
    }
}

void TestAgentInit::RegisterDBClients() {
    if (agent_->cfg()) {
        agent_->cfg()->RegisterDBClients(agent_->db());
    }

    if (agent_->oper_db()) {
        agent_->oper_db()->RegisterDBClients();
    }

    if (agent_->uve()) {
        agent_->uve()->RegisterDBClients();
    }

    if (agent_->ksync()) {
        agent_->ksync()->RegisterDBClients(agent_->db());
    }

    if (agent_->vgw()) {
        agent_->vgw()->RegisterDBClients();
    }
}

void TestAgentInit::InitPeers() {
    agent_->InitPeers();
}

void TestAgentInit::InitModules() {
    if (agent_->cfg()) {
        agent_->cfg()->Init();
    }

    if (agent_->oper_db()) {
        agent_->oper_db()->Init();
    }

    if (agent_->ksync()) {
        agent_->ksync()->Init(create_vhost_);
    }

    if (agent_->pkt()) {
        agent_->pkt()->Init(ksync_enable());
    }

    if (agent_->services()) {
        agent_->services()->Init(ksync_enable());
    }

    if (agent_->uve()) {
        agent_->uve()->Init();
    }
}

void TestAgentInit::CreateVrf() {
    // Create the default VRF
    VrfTable *vrf_table = agent_->vrf_table();

    if (agent_->isXenMode()) {
        vrf_table->CreateStaticVrf(agent_->linklocal_vrf_name());
    }
    vrf_table->CreateStaticVrf(agent_->fabric_vrf_name());

    VrfEntry *vrf = vrf_table->FindVrfFromName(agent_->fabric_vrf_name());
    assert(vrf);

    // Default VRF created; create nexthops
    agent_->set_fabric_inet4_unicast_table(vrf->GetInet4UnicastRouteTable());
    agent_->set_fabric_inet4_multicast_table
        (vrf->GetInet4MulticastRouteTable());
    agent_->set_fabric_l2_unicast_table(vrf->GetLayer2RouteTable());

    // Create VRF for VGw
    if (agent_->vgw()) {
        agent_->vgw()->CreateVrf();
    }
}

void TestAgentInit::CreateNextHops() {
    DiscardNH::Create();
    ResolveNH::Create();

    DiscardNHKey key;
    NextHop *nh = static_cast<NextHop *>
                (agent_->nexthop_table()->FindActiveEntry(&key));
    agent_->nexthop_table()->set_discard_nh(nh);
}

void TestAgentInit::CreateInterfaces() {
    InterfaceTable *table = agent_->interface_table();

    PhysicalInterface::Create(table, params_->eth_port(),
                              agent_->fabric_vrf_name(), false);
    InetInterface::Create(table, params_->vhost_name(), InetInterface::VHOST,
                          agent_->fabric_vrf_name(), params_->vhost_addr(),
                          params_->vhost_plen(), params_->vhost_gw(),
                          params_->eth_port(), agent_->fabric_vrf_name());
    agent_->InitXenLinkLocalIntf();
    InitVmwareInterface();

    // Set VHOST interface
    InetInterfaceKey key(agent_->vhost_interface_name());
    agent_->set_vhost_interface
        (static_cast<Interface *>(table->FindActiveEntry(&key)));
    assert(agent_->vhost_interface());

    // Validate physical interface
    PhysicalInterfaceKey physical_key(agent_->fabric_interface_name());
    assert(table->FindActiveEntry(&physical_key));

    agent_->set_router_id(params_->vhost_addr());
    agent_->set_vhost_prefix_len(params_->vhost_plen());
    agent_->set_vhost_default_gateway(params_->vhost_gw());

    if (agent_->pkt()) {
        agent_->pkt()->CreateInterfaces();
    }

    if (agent_->vgw()) {
        agent_->vgw()->CreateInterfaces();
    }
}

void TestAgentInit::InitDiscovery() {
    if (agent_->cfg()) {
        agent_->cfg()->InitDiscovery();
    }
}

void TestAgentInit::InitDone() {
    //Open up mirror socket
    agent_->mirror_table()->MirrorSockInit();

    if (agent_->services()) {
        agent_->services()->ConfigInit();
    }

    // Diag module needs PktModule
    if (agent_->pkt()) {
        diag_table_.reset(new DiagTable(agent_));
        agent_->set_diag_table(diag_table_.get());
    }

    if (create_vhost_) {
        //Update mac address of vhost interface with
        //that of ethernet interface
        agent_->ksync()->UpdateVhostMac();
    }

    if (ksync_enable_) {
        agent_->ksync()->VnswInterfaceListenerInit();
    }

    if (router_id_dep_enable() && agent_->router_id_configured()) {
        RouterIdDepInit(agent_);
    } else {
        LOG(DEBUG, 
            "Router ID Dependent modules (Nova & BGP) not initialized");
    }

    if (agent_->cfg()) {
        agent_->cfg()->InitDone();
    }

    if (agent_->pkt()) {
        agent_->pkt()->InitDone();
    }
}

// Start init sequence
bool TestAgentInit::Run() {
    InitLogging();
    InitCollector();
    InitPeers();
    CreateModules();
    CreateDBTables();
    RegisterDBClients();
    InitModules();
    CreateVrf();
    CreateNextHops();
    InitDiscovery();
    CreateInterfaces();
    InitDone();

    agent_->set_init_done(true);
    return true;
}

void TestAgentInit::Init(AgentParam *param, Agent *agent,
                     const boost::program_options::variables_map &var_map) {

    if (var_map.count("disable-vhost")) {
        create_vhost_ = false;
    }

    if (var_map.count("disable-ksync")) {
        ksync_enable_ = false;
    }

    if (var_map.count("disable-services")) {
        services_enable_ = false;
    }

    if (var_map.count("disable-packet")) {
        packet_enable_ = false;
    }
    params_ = param;
    agent_ = agent;
}

// Trigger inititlization in context of DBTable
void TestAgentInit::Start() {
    if (params_->log_file() == "") {
        LoggingInit();
    } else {
        LoggingInit(params_->log_file(), 1000000, 3, false, std::string(),
                    std::string());
    }

    params_->LogConfig();
    params_->Validate();

    int task_id = TaskScheduler::GetInstance()->GetTaskId("db::DBTable");
    trigger_.reset(new TaskTrigger(boost::bind(&TestAgentInit::Run, this), 
                                   task_id, 0));
    trigger_->Set();
    return;
}

/****************************************************************************
 * Cleanup routines on shutdown
****************************************************************************/
DBTableWalker *TestAgentInit::DeleteNextHops() {
    agent_->nexthop_table()->Delete(new DiscardNHKey());
    agent_->nexthop_table()->Delete(new ResolveNHKey());
    agent_->nexthop_table()->set_discard_nh(NULL);

    NextHopKey *key = new InterfaceNHKey
        (new PacketInterfaceKey(nil_uuid(), agent_->GetHostInterfaceName()),
         false, InterfaceNHFlags::INET4);
    agent_->nexthop_table()->Delete(key);
    return agent_->nexthop_table()->Flush();
}

DBTableWalker *TestAgentInit::DeleteInterfaces() {
    PacketInterface::DeleteReq(agent_->interface_table(),
                               agent_->GetHostInterfaceName());
    agent_->set_vhost_interface(NULL);
    InetInterface::DeleteReq(agent_->interface_table(),
                             agent_->vhost_interface_name());
    PhysicalInterface::DeleteReq(agent_->interface_table(),
                                 agent_->fabric_interface_name());

    if (!params_->isVmwareMode()) {
        PhysicalInterface::DeleteReq(agent_->interface_table(),
                                     params_->vmware_physical_port());
    }
    return agent_->interface_table()->Flush();
}

DBTableWalker *TestAgentInit::DeleteVms() {
    return agent_->vm_table()->Flush();
}

DBTableWalker *TestAgentInit::DeleteVns() {
    return agent_->vn_table()->Flush();
}

DBTableWalker *TestAgentInit::DeleteVrfs() {
    agent_->vrf_table()->DeleteStaticVrf(agent_->fabric_vrf_name());
    return agent_->vrf_table()->Flush();
}

DBTableWalker *TestAgentInit::DeleteSecurityGroups() {
    return agent_->sg_table()->Flush();
}

DBTableWalker *TestAgentInit::DeleteAcls() {
    return agent_->acl_table()->Flush();
}

void TestAgentInit::Shutdown() {
    if (agent_->oper_db()) {
        agent_->oper_db()->Shutdown();
    }

    agent_->cfg()->Shutdown();
    agent_->diag_table()->Shutdown();
}

// Shutdown IO channel to controller+DNS and Packet interface
void TestAgentInit::IoShutdown() {
    agent_->controller()->DisConnect();

    if (agent_->pkt())
        agent_->pkt()->IoShutdown();

    if (agent_->services())
        agent_->services()->IoShutdown();
}

void TestAgentInit::FlushFlows() {
    if (agent_->pkt())
        agent_->pkt()->FlushFlows();
}

void TestAgentInit::ServicesShutdown() {
    if (agent_->services())
        agent_->services()->Shutdown();
}

void TestAgentInit::DeleteRoutes() {
    if (agent_->oper_db())
        agent_->oper_db()->DeleteRoutes();
}

