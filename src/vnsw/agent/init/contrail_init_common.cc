/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>

#include <cmn/agent_cmn.h>
#include <init/agent_init.h>
#include <cmn/agent_factory.h>
#include <init/agent_param.h>

#include <cfg/cfg_init.h>

#include <oper/operdb_init.h>
#include <oper/vrf.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <uve/agent_uve.h>
#include <pkt/proto_handler.h>
#include <diag/diag.h>
#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>

#include "contrail_init_common.h"

ContrailInitCommon::ContrailInitCommon() : AgentInit(), create_vhost_(true),
    ksync_enable_(true), services_enable_(true), packet_enable_(true),
    uve_enable_(true), vgw_enable_(true), router_id_dep_enable_(true) {
}

ContrailInitCommon::~ContrailInitCommon() {
    vgw_.reset();
    ksync_.reset();
    uve_.reset();

    diag_table_.reset();
    services_.reset();
    pkt_.reset();
}

void ContrailInitCommon::ProcessOptions
    (const std::string &config_file, const std::string &program_name) {
    AgentInit::ProcessOptions(config_file, program_name);
}

int ContrailInitCommon::Start() {
    return AgentInit::Start();
}

/****************************************************************************
 * Initialization routines
 ***************************************************************************/
void ContrailInitCommon::CreateModules() {
    pkt_.reset(new PktModule(agent()));
    agent()->set_pkt(pkt_.get());

    if (services_enable_) {
        services_.reset
            (new ServicesModule(agent(), agent_param()->metadata_shared_secret()));
        agent()->set_services(services_.get());
    }
    if (vgw_enable_) {
        vgw_.reset(new VirtualGateway(agent()));
        agent()->set_vgw(vgw_.get());
    }
}

void ContrailInitCommon::RegisterDBClients() {
    if (agent()->uve()) {
        agent()->uve()->RegisterDBClients();
    }

    if (agent()->ksync()) {
        agent()->ksync()->RegisterDBClients(agent()->db());
    }

    if (agent()->vgw()) {
        agent()->vgw()->RegisterDBClients();
    }
}

void ContrailInitCommon::InitModules() {
    if (agent()->pkt()) {
        agent()->pkt()->Init(ksync_enable());
    }

    if (agent()->services()) {
        agent()->services()->Init(ksync_enable());
    }

    if (agent()->uve()) {
        agent()->uve()->Init();
    }

    if (agent()->ksync()) {
        agent()->ksync()->Init(create_vhost_);
    }

}

void ContrailInitCommon::CreateVrf() {
    VrfTable *vrf_table = agent()->vrf_table();

    if (agent()->isXenMode()) {
        vrf_table->CreateStaticVrf(agent()->linklocal_vrf_name());
    }

    // Create VRF for VGw
    if (agent()->vgw()) {
        agent()->vgw()->CreateVrf();
    }
}

static PhysicalInterface::EncapType ComputeEncapType(const string &encap) {
    if (encap == "none") {
        return PhysicalInterface::RAW_IP;
    }
    return PhysicalInterface::ETHERNET;
}

void ContrailInitCommon::ProcessComputeAddress(AgentParam *param) {
    InetUnicastAgentRouteTable *rt_table =
        agent()->fabric_inet4_unicast_table();

    const AgentParam::AddressList &addr_list =
        param->compute_node_address_list();
    AgentParam::AddressList::const_iterator it = addr_list.begin();
    while (it != addr_list.end()) {
        rt_table->AddVHostRecvRouteReq(agent()->local_peer(),
                                       agent()->fabric_vrf_name(),
                                       param->vhost_name(), *it, 32,
                                       agent()->fabric_vn_name(), false);
        it++;
    }

    // If compute_node_address are specified, it will mean user wants
    // to run services such as metadata on an IP different than vhost.
    // Set compute_node_ip_ to vhost_addr if no compute_node_address are 
    // specified. Else, pick first address to run in compute_node_address_list
    //
    // The compute_node_ip is used only in adding Flow NAT rules.
    if (param->compute_node_address_list().size()) {
        agent()->set_compute_node_ip(param->compute_node_address_list()[0]);
    }
}

void ContrailInitCommon::CreateInterfaces() {
    InterfaceTable *table = agent()->interface_table();
    PhysicalInterface::EncapType type;

    Interface::Transport physical_transport = Interface::TRANSPORT_ETHERNET;
    Interface::Transport vhost_transport = Interface::TRANSPORT_ETHERNET;
    if (agent_param()->vrouter_on_host_dpdk()) {
        //TODO : transport type for vhost interface should
        //be set to KNI
        vhost_transport = Interface::TRANSPORT_PMD;
        physical_transport = Interface::TRANSPORT_PMD;
    }

    if (agent_param()->vrouter_on_nic_mode()) {
        vhost_transport = Interface::TRANSPORT_PMD;
        physical_transport = Interface::TRANSPORT_PMD;
    }

    type = ComputeEncapType(agent_param()->eth_port_encap_type());
    PhysicalInterface::Create(table, agent_param()->eth_port(),
                              agent()->fabric_vrf_name(),
                              PhysicalInterface::FABRIC, type,
                              agent_param()->eth_port_no_arp(), nil_uuid(),
                              agent_param()->vhost_addr(),
                              physical_transport);
    PhysicalInterfaceKey physical_key(agent()->fabric_interface_name());
    assert(table->FindActiveEntry(&physical_key));

    InetInterface::Create(table, agent_param()->vhost_name(),
                          InetInterface::VHOST, agent()->fabric_vrf_name(),
                          agent_param()->vhost_addr(),
                          agent_param()->vhost_plen(),
                          agent_param()->vhost_gw(),
                          agent_param()->eth_port(),
                          agent()->fabric_vn_name(), vhost_transport);
    InetInterfaceKey key(agent()->vhost_interface_name());
    agent()->set_vhost_interface
        (static_cast<Interface *>(table->FindActiveEntry(&key)));
    assert(agent()->vhost_interface());

    // Add L2 Receive route for vhost. We normally add L2 Receive route on
    // VRF creation, but vhost is not present when fabric-vrf is created.
    // So, add it now
    BridgeAgentRouteTable *l2_table = agent()->fabric_l2_unicast_table();
    const InetInterface *vhost = static_cast<const InetInterface *>
        (agent()->vhost_interface());
    l2_table->AddBridgeReceiveRoute(agent()->local_vm_peer(),
                                    agent()->fabric_vrf_name(), 0,
                                    vhost->xconnect()->mac(), "");

    agent()->InitXenLinkLocalIntf();
    if (agent_param()->isVmwareMode()) {
        PhysicalInterface::Create(agent()->interface_table(),
                                  agent_param()->vmware_physical_port(),
                                  agent()->fabric_vrf_name(),
                                  PhysicalInterface::VMWARE,
                                  PhysicalInterface::ETHERNET, false,
                                  nil_uuid(), Ip4Address(0),
                                  physical_transport);
    }

    agent()->set_router_id(agent_param()->vhost_addr());
    agent()->set_vhost_prefix_len(agent_param()->vhost_plen());
    agent()->set_vhost_default_gateway(agent_param()->vhost_gw());

    if (agent()->pkt()) {
        agent()->pkt()->CreateInterfaces();
    }

    if (agent()->vgw()) {
        agent()->vgw()->CreateInterfaces(vhost_transport);
    }

    ProcessComputeAddress(agent_param());
}

void ContrailInitCommon::InitDone() {
    //Open up mirror socket
    agent()->mirror_table()->MirrorSockInit();

    if (agent()->services()) {
        agent()->services()->ConfigInit();
    }

    // Diag module needs PktModule
    if (agent()->pkt()) {
        diag_table_.reset(new DiagTable(agent()));
        agent()->set_diag_table(diag_table_.get());
    }

    if (create_vhost_) {
        //Update mac address of vhost interface with
        //that of ethernet interface
        agent()->ksync()->UpdateVhostMac();
    }

    if (ksync_enable_) {
        agent()->ksync()->VnswInterfaceListenerInit();
    }

    if (agent()->pkt()) {
        agent()->pkt()->InitDone();
    }

    if (agent()->ksync()) {
        agent()->ksync()->InitDone();
    }

    if (agent()->oper_db()) {
        agent()->oper_db()->InitDone();
    }
}

/****************************************************************************
 * Shutdown routines
 ***************************************************************************/
void ContrailInitCommon::IoShutdown() {
    if (agent()->pkt())
        agent()->pkt()->IoShutdown();

    if (agent()->services())
        agent()->services()->IoShutdown();
}

void ContrailInitCommon::FlushFlows() {
    if (agent()->pkt())
        agent()->pkt()->FlushFlows();
}

void ContrailInitCommon::ServicesShutdown() {
    if (agent()->services())
        agent()->services()->Shutdown();
}

void ContrailInitCommon::PktShutdown() {
    if (agent()->pkt()) {
        agent()->pkt()->Shutdown();
    }
}

void ContrailInitCommon::ModulesShutdown() {
    if (agent()->diag_table()) {
        agent()->diag_table()->Shutdown();
    }
}
