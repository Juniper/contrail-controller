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
#include <boost/program_options.hpp>

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
#include <cfg/cfg_mirror.h>
#include <cfg/discovery_agent.h>

#include <init/agent_init.h>
#include <init/agent_param.h>

#include <oper/operdb_init.h>
#include <oper/vrf.h>
#include <oper/multicast.h>
#include <oper/mirror_table.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <uve/uve_init.h>
#include <uve/uve_client.h>
#include <kstate/kstate.h>
#include <pkt/proto.h>
#include <diag/diag.h>
#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>

extern void RouterIdDepInit();

static bool interface_exist(string &name) {
	struct if_nameindex *ifs = NULL;
	struct if_nameindex *head = NULL;
	bool ret = false;
	string tname = "";

	ifs = if_nameindex();
	if (ifs == NULL) {
		LOG(INFO, "No interface exists!");
		return ret;
	}
	head = ifs;
	while (ifs->if_name && ifs->if_index) {
		tname = ifs->if_name;
		if (string::npos != tname.find(name)) {
			ret = true;
			name = tname;
			break;
		}
		ifs++;
	}
	if_freenameindex(head);
	return ret;
}

void AgentInit::DeleteRoutes() {
    boost::system::error_code ec;
    Ip4Address bcast_ip = Ip4Address::from_string("255.255.255.255", ec);
    Ip4Address ip = Ip4Address::from_string("0.0.0.0", ec);
    Inet4UnicastAgentRouteTable *uc_rt_table =
        agent_->GetDefaultInet4UnicastRouteTable();
    Inet4MulticastAgentRouteTable *mc_rt_table =
        agent_->GetDefaultInet4MulticastRouteTable();

    mc_rt_table->DeleteMulticastRoute(agent_->GetDefaultVrf(), ip, bcast_ip);

    uc_rt_table->DeleteReq(agent_->GetLocalPeer(), agent_->GetDefaultVrf(),
                           bcast_ip, 32);
    uc_rt_table->DeleteReq(agent_->GetLocalPeer(), agent_->GetDefaultVrf(),
                           ip, 0);
    uc_rt_table->DeleteReq(agent_->GetLocalPeer(), agent_->GetDefaultVrf(),
                           agent_->GetRouterId(), 32);
    uc_rt_table->DeleteReq(agent_->GetLocalPeer(), agent_->GetDefaultVrf(),
                           agent_->GetGatewayId(), 32);
    uc_rt_table->DeleteReq(agent_->GetLocalPeer(), agent_->GetDefaultVrf(),
                           agent_->GetRouterId(), agent_->GetPrefixLen());
    uc_rt_table->DeleteReq(agent_->GetLocalPeer(), agent_->GetDefaultVrf(),
                           Ip4Address(agent_->GetRouterId().to_ulong() |
                                      ~(0xFFFFFFFF << (32 - agent_->GetPrefixLen()))), 32);
}

void AgentInit::DeleteNextHop() {
    NextHopKey *key = new DiscardNHKey();
    agent_->GetNextHopTable()->Delete(key);

    key = new ResolveNHKey();
    agent_->GetNextHopTable()->Delete(key);

    key = new ReceiveNHKey(new VirtualHostInterfaceKey(
                           agent_->GetVirtualHostInterfaceName()), false);
    agent_->GetNextHopTable()->Delete(key);

    key = new ReceiveNHKey(new VirtualHostInterfaceKey(
                           agent_->GetVirtualHostInterfaceName()), true);
    agent_->GetNextHopTable()->Delete(key);

    key = new InterfaceNHKey(new PacketInterfaceKey(nil_uuid(),
                             agent_->GetHostInterfaceName()), false,
                             InterfaceNHFlags::INET4);
    agent_->GetNextHopTable()->Delete(key);
}

void AgentInit::DeleteVrf() {
    agent_->GetVrfTable()->DeleteVrf(agent_->GetDefaultVrf());
}

void AgentInit::DeleteInterface() {
    PacketInterface::DeleteReq(agent_->GetInterfaceTable(),
                               agent_->GetHostInterfaceName());
    VirtualHostInterface::DeleteReq(agent_->GetInterfaceTable(),
                                    agent_->GetVirtualHostInterfaceName());
    PhysicalInterface::DeleteReq(agent_->GetInterfaceTable(),
                                 agent_->GetIpFabricItfName());
}

void AgentInit::DeleteStaticEntries() {
    DeleteRoutes();
    DeleteNextHop();
    DeleteVrf();
    DeleteInterface();
}

void AgentInit::Shutdown() {
    agent_->cfg()->Shutdown();
}

void AgentInit::OnInterfaceCreate(DBEntryBase *entry) {
    if (entry->IsDeleted())
        return;

    Interface *itf = static_cast<Interface *>(entry);
    Interface::Type type = itf->type();
    if (type != Interface::PHYSICAL)
        return;

    boost::system::error_code ec;
    Ip4Address bcast_ip = Ip4Address::from_string("255.255.255.255", ec);
    Inet4MulticastAgentRouteTable *mc_rt_table = 
        static_cast<Inet4MulticastAgentRouteTable *>
                (agent_->GetVrfTable()->GetRouteTable(agent_->GetDefaultVrf(),
                                   AgentRouteTableAPIS::INET4_MULTICAST));
    mc_rt_table->AddVHostRecvRoute(agent_->GetDefaultVrf(),
                                   agent_->GetVirtualHostInterfaceName(),
                                   bcast_ip, false);

    //Create Receive and resolve route
    if (params_->vhost_addr().to_ulong()) {
        Inet4UnicastAgentRouteTable *rt_table;

        rt_table = static_cast<Inet4UnicastAgentRouteTable *>
            (agent_->GetVrfTable()->GetRouteTable
             (agent_->GetDefaultVrf(), AgentRouteTableAPIS::INET4_UNICAST));

        agent_->SetRouterId(params_->vhost_addr());
        agent_->SetPrefixLen(params_->vhost_plen());
        rt_table->AddVHostRecvRoute(
            agent_->GetDefaultVrf(), agent_->GetVirtualHostInterfaceName(),
            params_->vhost_addr(), false);
        rt_table->AddVHostSubnetRecvRoute(
            agent_->GetDefaultVrf(), agent_->GetVirtualHostInterfaceName(),
            params_->vhost_addr(), params_->vhost_plen(), false);
        rt_table->AddResolveRoute(agent_->GetDefaultVrf(),
                                  params_->vhost_prefix(),
                                  params_->vhost_plen());
    }

    agent_->SetGatewayId(params_->vhost_gw());
    Ip4Address default_dest_ip = Ip4Address::from_string("0.0.0.0", ec);
    agent_->GetDefaultInet4UnicastRouteTable()->AddGatewayRoute
        (agent_->GetLocalPeer(), agent_->GetDefaultVrf(), default_dest_ip, 0,
         params_->vhost_gw());
    // Trigger initialization to continue
    TriggerInit();
    intf_trigger_ = SafeDBUnregister(agent_->GetInterfaceTable(),
                                     intf_client_id_);
}

void AgentInit::InitXenLinkLocalIntf() {
    if (!params_->isXenMode() || params_->xen_ll_name() == "")
        return;

    string dev_name = params_->xen_ll_name();
    if(!interface_exist(dev_name)) {
        LOG(INFO, "Interface " << dev_name << " not found");
        return;
    }
    params_->set_xen_ll_name(dev_name);

    VirtualHostInterface::CreateReq(agent_->GetInterfaceTable(),
                                    params_->xen_ll_name(),
                                    agent_->GetLinkLocalVrfName(), 
                                    VirtualHostInterface::LINK_LOCAL);

    Inet4UnicastAgentRouteTable *rt_table;
    rt_table = static_cast<Inet4UnicastAgentRouteTable *>
        (agent_->GetVrfTable()->GetRouteTable
         (agent_->GetLinkLocalVrfName(), AgentRouteTableAPIS::INET4_UNICAST));
    rt_table->AddVHostRecvRoute(rt_table->GetVrfName(),
                                params_->xen_ll_name(), 
                                params_->xen_ll_addr(), false);

    rt_table->AddVHostSubnetRecvRoute(rt_table->GetVrfName(),
                                      params_->xen_ll_name(),
                                      params_->xen_ll_addr(),
                                      params_->xen_ll_plen(), false);

    rt_table->AddResolveRoute(rt_table->GetVrfName(), params_->xen_ll_prefix(),
                              params_->xen_ll_plen());
}

// Used only during initialization.
// Trigger continuation of initialization after fabric vrf is created
void AgentInit::OnVrfCreate(DBEntryBase *entry) {
    if (entry->IsDeleted())
        return;

    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (vrf->GetName() == agent_->GetDefaultVrf() && vrf_trigger_ == NULL) {
        // Default VRF created; create nexthops
        agent_->SetDefaultInet4UnicastRouteTable(vrf->
                   GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST));
        agent_->SetDefaultInet4MulticastRouteTable(vrf->
                   GetRouteTable(AgentRouteTableAPIS::INET4_MULTICAST));
        agent_->SetDefaultLayer2RouteTable(vrf->
                   GetRouteTable(AgentRouteTableAPIS::LAYER2));
        DiscardNH::CreateReq();
        ResolveNH::CreateReq();

        // The notification is not needed after fabric vrf is created.
        // Unregister client to VRF table (in DB Task contex)
        vrf_trigger_ = SafeDBUnregister(agent_->GetVrfTable(), vrf_client_id_);

        // Trigger initialization to continue
        TriggerInit();
    }
}


// Create VRF for fabric-vrf 
// In case of XEN, create link local VRF also
// We must continue agent initialization after VRF are created.
// Register for VRF table to wait till default-vrf is created
void AgentInit::CreateDefaultVrf() {
    // Register for VRF entry before the entries are created
    VrfTable *vrf_table = agent_->GetVrfTable();
    vrf_client_id_ =
        vrf_table->Register(boost::bind(&AgentInit::OnVrfCreate, this, _2));

    if (agent_->isXenMode()) {
        vrf_table->CreateVrf(agent_->GetLinkLocalVrfName());
    }
    vrf_table->CreateVrf(agent_->GetDefaultVrf());

}

void AgentInit::CreateInterfaces(DB *db) {
    // Register for interface entry before the entries are created
    intf_client_id_ = agent_->GetInterfaceTable()->Register
        (boost::bind(&AgentInit::OnInterfaceCreate, this, _2));

    VirtualHostInterface::CreateReq(agent_->GetInterfaceTable(),
                                    params_->vhost_name(), 
                                    agent_->GetDefaultVrf(), 
                                    VirtualHostInterface::HOST);
    PhysicalInterface::CreateReq(agent_->GetInterfaceTable(), params_->eth_port(),
                            agent_->GetDefaultVrf());
    InitXenLinkLocalIntf();
}
/*
 * Initialization sequence
 * CREATE_MODULES      : Create modules
 * CREATE_DB_TABLES    : Create DB Tables
 * CREATE_DB_CLIENTS   : Create DB Clients
 * INIT_MODULES        : Call module Inits
 * CREATE_VRF          : Create default VRFs
 * CREATE_INTERFACE    : Create default interfaces and routes
 * INIT_DONE           : Finalize the init sequence
 */
bool AgentInit::Run() {
    switch(state_) {
    case CREATE_MODULES:
        agent_->CreateModules();
        state_ = CREATE_DB_TABLES;
        // FALLTHRU

    case CREATE_DB_TABLES:
        agent_->CreateDBTables();
        state_ = CREATE_DB_CLIENTS;
        // FALLTHRU

    case CREATE_DB_CLIENTS:
        agent_->CreateDBClients();
        state_ = INIT_MODULES;
        // FALLTHRU

    case INIT_MODULES:
        agent_->InitModules();
        state_ = CREATE_VRF;
        // FALLTHRU;

    case CREATE_VRF :
        agent_->CreateVrf();
        state_ = CREATE_INTERFACE;
        break;

    case CREATE_INTERFACE:
        agent_->CreateInterfaces();
        state_ = INIT_DONE;
        break;

    case INIT_DONE:
        agent_->InitDone();
        break;

    default:
        assert(0);
        break;
    }
    return true;
}

void AgentInit::TriggerInit() {
    int task_id = TaskScheduler::GetInstance()->GetTaskId("db::DBTable");
    TaskTrigger *t = new TaskTrigger(boost::bind(&AgentInit::Run, this),
                                     task_id, 0);
    t->Set();
    trigger_list_.push_back(t);
}

void AgentInit::Init(AgentParam *param, Agent *agent,
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

// TODO: Move the following code to state based initialization
void AgentInit::Start() {
    if (params_->log_file() == "") {
        LoggingInit();
    } else {
        LoggingInit(params_->log_file());
    }

    TriggerInit();
    return;
}
