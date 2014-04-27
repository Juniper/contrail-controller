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

void VirtualGateway::InterfaceNotify(DBTablePartBase *partition, DBEntryBase *entry) {
    if ((static_cast<Interface *>(entry))->type() != Interface::INET)
        return;

    InetInterface *intf = static_cast<InetInterface *>(entry);
    if (intf->sub_type() != InetInterface::SIMPLE_GATEWAY)
        return;

    if (entry->IsDeleted()) {
        entry->ClearState(partition->parent(), listener_id_);
        return;
    }

    VirtualGatewayState *state = static_cast<VirtualGatewayState *>
        (entry->GetState(partition->parent(), listener_id_));

    bool active = intf->ipv4_active();
    VirtualGatewayConfig cfg(intf->name());
    VirtualGatewayConfigTable::Table::iterator it = 
        vgw_config_table_->table().find(cfg);
    if (it == vgw_config_table_->table().end())
        return;

    if (state == NULL) {
        state = new VirtualGatewayState();
        entry->SetState(partition->parent(), listener_id_, state);
        it->set_interface(intf);
    }

    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (agent_->vrf_table()->GetInet4UnicastRouteTable(it->vrf_name()));

    state->active_ = active;

    // Add gateway routes in virtual-network. 
    // BGP will export the route to other compute nodes
    VirtualGatewayConfig::SubnetList empty_list;
    if (active) {
        RouteUpdate(*it, it->routes().size(), rt_table,
                    it->routes(), empty_list, true, false);
    } else {
        RouteUpdate(*it, it->routes().size(), rt_table,
                    empty_list, it->routes(), false, true);
    }

    if (active) {
        // Packets received on fabric vrf and destined to IP address in
        // "public" network must reach kernel. Add a route in "fabric" VRF 
        // inside vrouter to trap packets destined to "public" network
        SubnetUpdate(it->vrf_name(), rt_table, it->subnets(), empty_list);
    } else {
        // Delete the trap route added above
        SubnetUpdate(it->vrf_name(), rt_table, empty_list, it->subnets());
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
        CreateVrf(it->vrf_name());
    }
}

void VirtualGateway::CreateVrf(const std::string &vrf_name) {
    agent_->GetVrfTable()->CreateVrf(vrf_name, VrfData::GwVrf);
}

void VirtualGateway::DeleteVrf(const std::string &vrf_name) {
    agent_->GetVrfTable()->DeleteVrf(vrf_name, VrfData::GwVrf);
}

// Create virtual-gateway interface
void VirtualGateway::CreateInterfaces() {
    if (vgw_config_table_ == NULL) {
        return;
    }

    VirtualGatewayConfigTable::Table::iterator it;
    const VirtualGatewayConfigTable::Table &table = vgw_config_table_->table();
    for (it = table.begin(); it != table.end(); it++) {
        CreateInterface(it->interface_name(), it->vrf_name());
    }
}

void VirtualGateway::CreateInterface(const std::string &interface_name,
                                     const std::string &vrf_name) {
    InetInterface::Create(agent_->GetInterfaceTable(), interface_name,
                          InetInterface::SIMPLE_GATEWAY, vrf_name,
                          Ip4Address(0), 0, Ip4Address(0), "");
}

void VirtualGateway::DeleteInterface(const std::string &interface_name) {
    InetInterface::Delete(agent_->GetInterfaceTable(), interface_name);
}

void
VirtualGateway::SubnetUpdate(const VirtualGatewayConfig &vgw,
                             const VirtualGatewayConfig::SubnetList &add_list,
                             const VirtualGatewayConfig::SubnetList &del_list) {
    if (vgw.interface() && !vgw.interface()->ipv4_active())
        return;

    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (agent_->vrf_table()->GetInet4UnicastRouteTable(vgw.vrf_name()));
    if (!rt_table)
        return;

    SubnetUpdate(vgw.vrf_name(), rt_table, add_list, del_list);
}

void
VirtualGateway::SubnetUpdate(const std::string &vrf,
                             Inet4UnicastAgentRouteTable *rt_table,
                             const VirtualGatewayConfig::SubnetList &add_list,
                             const VirtualGatewayConfig::SubnetList &del_list) {
    for (uint32_t idx = 0; idx < add_list.size(); idx++) {
        Ip4Address addr = GetIp4SubnetAddress(add_list[idx].ip_,
                                              add_list[idx].plen_);
        rt_table->AddVHostRecvRouteReq(agent_->vgw_peer(),
                                       agent_->GetDefaultVrf(),
                                       agent_->vhost_interface_name(),
                                       addr, add_list[idx].plen_,
                                       vrf, false);
    }
    for (uint32_t idx = 0; idx < del_list.size(); idx++) {
        Ip4Address addr = GetIp4SubnetAddress(del_list[idx].ip_,
                                              del_list[idx].plen_);
        rt_table->DeleteReq(agent_->vgw_peer(), agent_->GetDefaultVrf(),
                            addr, del_list[idx].plen_);
    }
}

void
VirtualGateway::RouteUpdate(const VirtualGatewayConfig &vgw,
                            const VirtualGatewayConfig::SubnetList &new_list,
                            const VirtualGatewayConfig::SubnetList &add_list,
                            const VirtualGatewayConfig::SubnetList &del_list,
                            bool add_default_route) {
    if (vgw.interface() && !vgw.interface()->ipv4_active())
        return;

    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (agent_->vrf_table()->GetInet4UnicastRouteTable(vgw.vrf_name()));

    RouteUpdate(vgw, new_list.size(), rt_table, add_list, del_list,
                add_default_route, true);
}

void
VirtualGateway::RouteUpdate(const VirtualGatewayConfig &vgw,
                            uint32_t new_list_size,
                            Inet4UnicastAgentRouteTable *rt_table,
                            const VirtualGatewayConfig::SubnetList &add_list,
                            const VirtualGatewayConfig::SubnetList &del_list,
                            bool add_default_route, bool del_default_route) {
    if (vgw.routes().size() == 0 && del_default_route) {
        // no routes earlier, remove default route
        rt_table->DeleteReq(agent_->vgw_peer(), vgw.vrf_name(),
                            Ip4Address(0), 0);
    } else if (new_list_size == 0 && add_default_route) {
        // no routes now, add a default route
        rt_table->AddInetInterfaceRoute(agent_->vgw_peer(), vgw.vrf_name(),
                                        Ip4Address(0), 0, vgw.interface_name(),
                                        vgw.interface()->label(),
                                        vgw.vrf_name());
    }
    // remove old routes, add new routes
    for (uint32_t idx = 0; idx < del_list.size(); idx++) {
        Ip4Address addr = GetIp4SubnetAddress(del_list[idx].ip_,
                                              del_list[idx].plen_);
        rt_table->DeleteReq(agent_->vgw_peer(), vgw.vrf_name(),
                            addr, del_list[idx].plen_);
    }
    for (uint32_t idx = 0; idx < add_list.size(); idx++) {
        Ip4Address addr = GetIp4SubnetAddress(add_list[idx].ip_,
                                              add_list[idx].plen_);
        rt_table->AddInetInterfaceRoute(agent_->vgw_peer(),
                                        vgw.vrf_name(), addr, add_list[idx].plen_,
                                        vgw.interface_name(),
                                        vgw.interface()->label(),
                                        vgw.vrf_name());
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
        InetInterface::Delete(agent_->GetInterfaceTable(),
                                 it->interface_name());

        // Delete VRF for "public" virtual-network
        agent_->GetVrfTable()->DeleteStaticVrf(it->vrf_name());
    }
}
