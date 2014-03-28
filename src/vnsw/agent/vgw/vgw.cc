/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <iostream>

#include <base/logging.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <cfg/cfg_init.h>

#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/route_common.h>
#include <oper/interface_common.h>
#include <oper/vrf_assign.h>

#include <vgw/cfg_vgw.h>
#include <vgw/vgw.h>

using namespace std;

VirtualGateway::VirtualGateway(Agent *agent) : agent_(agent) {
    vgw_config_table_ = agent->params()->vgw_config_table();
}

struct VirtualGatewayState : DBState {
    VirtualGatewayState() : active_(false) { }
    ~VirtualGatewayState() { }
    bool active_;
};

void VirtualGateway::InterfaceNotify(DBTablePartBase *partition,
                                     DBEntryBase *entry) {
    if ((static_cast<Interface *>(entry))->type() != Interface::INET)
        return;

    InetInterface *intf = static_cast<InetInterface *>(entry);
    if (intf->sub_type() != InetInterface::SIMPLE_GATEWAY)
        return;

    VirtualGatewayConfig cfg(intf->name());
    VirtualGatewayConfigTable::Table::iterator it = 
        vgw_config_table_->table().find(cfg);
    if (it == vgw_config_table_->table().end())
        return;

    VirtualGatewayState *state = static_cast<VirtualGatewayState *>
        (entry->GetState(partition->parent(), listener_id_));
    bool active = intf->ipv4_active();

    if (entry->IsDeleted()) {
        active = false;
    } else {
        if (state == NULL) {
            state = new VirtualGatewayState();
            entry->SetState(partition->parent(), listener_id_, state);
        }
    }

    if (active == state->active_) {
        if (entry->IsDeleted()) {
            entry->ClearState(partition->parent(), listener_id_);
        }
        return;
    }

    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (agent_->vrf_table()->GetInet4UnicastRouteTable(it->vrf()));

    state->active_ = active;

    // Add gateway routes in virtual-network. 
    // BGP will export the route to other compute nodes
    uint32_t idx = 0;
    if (active) {
        // Add routes configured. Add default route if none configured
        if (it->routes().size() == 0) {
            rt_table->AddInetInterfaceRoute(agent_->local_vm_peer(), it->vrf(),
                                            Ip4Address(0), 0, it->interface(),
                                            intf->label(), it->vrf());
        } else {
            for (idx = 0; idx < it->routes().size(); idx++) {
                Ip4Address addr = GetIp4SubnetAddress(it->routes()[idx].ip_,
                                                      it->routes()[idx].plen_);
                rt_table->AddInetInterfaceRoute(agent_->local_vm_peer(),
                                                it->vrf(), addr,
                                                it->routes()[idx].plen_,
                                                it->interface(), intf->label(),
                                                it->vrf());
            }
        }
    } else {
        // Delete the routes added in virtual-network
        if (it->routes().size() == 0) {
            rt_table->DeleteReq(agent_->local_vm_peer(), it->vrf(),
                                Ip4Address(0), 0);
        } else {
            for (idx = 0; idx < it->routes().size(); idx++) {
                Ip4Address addr = GetIp4SubnetAddress(it->routes()[idx].ip_,
                                                      it->routes()[idx].plen_);
                rt_table->DeleteReq(agent_->local_vm_peer(), it->vrf(),
                                    addr, it->routes()[idx].plen_);
            }
        }
    }

    for (idx = 0; idx < it->subnets().size(); idx++) {
        Ip4Address addr = GetIp4SubnetAddress(it->subnets()[idx].ip_,
                                              it->subnets()[idx].plen_);
        if (active) {
            // Packets received on fabric vrf and destined to IP address in
            // "public" network must reach kernel. Add a route in "fabric" VRF 
            // inside vrouter to trap packets destined to "public" network
            rt_table->AddVHostRecvRoute(agent_->local_vm_peer(),
                                        agent_->GetDefaultVrf(),
                                        agent_->vhost_interface_name(),
                                        addr, it->subnets()[idx].plen_,
                                        it->vrf(), false);
        } else {
            // Delete the trap route added above
            rt_table->DeleteReq(agent_->local_vm_peer(),
                                agent_->GetDefaultVrf(),
                                addr, it->subnets()[idx].plen_);
        }
    }

    if (entry->IsDeleted()) {
        entry->ClearState(partition->parent(), listener_id_);
    }
}

void VirtualGateway::RegisterDBClients() {
   listener_id_ = agent_->GetInterfaceTable()->Register
       (boost::bind(&VirtualGateway::InterfaceNotify, this, _1, _2));
}

// Create VRF for "public" virtual-network
void VirtualGateway::CreateVrf() {
    if (vgw_config_table_ == NULL) {
        return;
    }
    VirtualGatewayConfigTable::Table::iterator it;
    const VirtualGatewayConfigTable::Table &table = vgw_config_table_->table();
    for (it = table.begin(); it != table.end(); it++) {
        agent_->GetVrfTable()->CreateStaticVrf(it->vrf());
    }
}

// Create virtual-gateway interface
void VirtualGateway::CreateInterfaces() {
    if (vgw_config_table_ == NULL) {
        return;
    }

    VirtualGatewayConfigTable::Table::iterator it;
    const VirtualGatewayConfigTable::Table &table = vgw_config_table_->table();
    for (it = table.begin(); it != table.end(); it++) {
        InetInterface::CreateReq(agent_->GetInterfaceTable(), it->interface(),
                                 InetInterface::SIMPLE_GATEWAY, it->vrf(),
                                 Ip4Address(0), 0, Ip4Address(0), "");
    }
}

void VirtualGateway::Init() {
}

void VirtualGateway::Shutdown() {
    if (vgw_config_table_ == NULL)
        return;

    VirtualGatewayConfigTable::Table::iterator it;
    const VirtualGatewayConfigTable::Table &table = vgw_config_table_->table();
    for (it = table.begin(); it != table.end(); it++) {
        // Delete Interface
        InetInterface::DeleteReq(agent_->GetInterfaceTable(),
                                 it->interface());

        // Delete VRF for "public" virtual-network
        agent_->GetVrfTable()->DeleteStaticVrf(it->vrf());
    }
}
