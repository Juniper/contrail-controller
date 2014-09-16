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
#include <ksync/ksync_init.h>
#include <uve/agent_uve.h>
#include <pkt/proto_handler.h>
#include <pkt/agent_stats.h>
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
    stats_.reset();
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
    stats_.reset(new AgentStats(agent()));
    agent()->set_stats(stats_.get());

    pkt_.reset(new PktModule(agent()));
    agent()->set_pkt(pkt_.get());

    services_.reset
        (new ServicesModule(agent(), agent_param()->metadata_shared_secret()));
    agent()->set_services(services_.get());
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

void ContrailInitCommon::CreateInterfaces() {
    InterfaceTable *table = agent()->interface_table();

    PhysicalInterface::Create(table, agent_param()->eth_port(),
                              agent()->fabric_vrf_name(), false);
    InetInterface::Create(table, agent_param()->vhost_name(),
                          InetInterface::VHOST, agent()->fabric_vrf_name(),
                          agent_param()->vhost_addr(),
                          agent_param()->vhost_plen(),
                          agent_param()->vhost_gw(),
                          agent_param()->eth_port(),
                          agent()->fabric_vrf_name());
    agent()->InitXenLinkLocalIntf();
    if (agent_param()->isVmwareMode()) {
        PhysicalInterface::Create(agent()->interface_table(),
                                  agent_param()->vmware_physical_port(),
                                  agent()->fabric_vrf_name(), true);
    }

    InetInterfaceKey key(agent()->vhost_interface_name());
    agent()->set_vhost_interface
        (static_cast<Interface *>(table->FindActiveEntry(&key)));
    assert(agent()->vhost_interface());

    PhysicalInterfaceKey physical_key(agent()->fabric_interface_name());
    assert(table->FindActiveEntry(&physical_key));

    agent()->set_router_id(agent_param()->vhost_addr());
    agent()->set_vhost_prefix_len(agent_param()->vhost_plen());
    agent()->set_vhost_default_gateway(agent_param()->vhost_gw());

    if (agent()->pkt()) {
        agent()->pkt()->CreateInterfaces();
    }

    if (agent()->vgw()) {
        agent()->vgw()->CreateInterfaces();
    }
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
