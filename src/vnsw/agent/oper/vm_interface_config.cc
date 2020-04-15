/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <bgp_schema_types.h>
#include <base/address_util.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <oper/bridge_domain.h>
#include <oper/config_manager.h>
#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <oper/sg.h>
#include <oper/tag.h>
#include <oper/bgp_as_service.h>
#include <init/agent_param.h>
#include "ifmap/ifmap_link.h"

#include <port_ipc/port_ipc_handler.h>
#include <port_ipc/port_subscribe_table.h>

#define LOGICAL_ROUTER_NAME "logical-router"
#define VIRTUAL_PORT_GROUP_CONFIG_NAME "virtual-port-group"

#define VMI_NETWORK_ROUTER_INTERFACE "network:router_interface"

using namespace std;
using namespace boost::uuids;
using namespace autogen;

static void AddFabricFloatingIp(Agent *agent, VmInterfaceConfigData *data,
                                IpAddress src_ip) {
    if (agent->fabric_vn_uuid() == nil_uuid()) {
        return;
    }

    VmInterface::FloatingIp::PortMap src_port_map;
    VmInterface::FloatingIp::PortMap dst_port_map;
    data->floating_ip_list_.list_.insert
        (VmInterface::FloatingIp(agent->router_id(),
                                 agent->fabric_policy_vrf_name(),
                                 agent->fabric_vn_uuid(), src_ip,
                                 VmInterface::FloatingIp::DIRECTION_EGRESS,
                                 false, src_port_map, dst_port_map, true));
}

// Build one VN and VRF for a floating-ip. Can reach here from 2 paths,
// 1. floating-ip <-> floating-ip-pool <-> virtual-network <-> routing-instance
// 2. floating-ip <-> instance-ip <-> virtual-network <-> routing-instance
static bool BuildFloatingIpVnVrf(Agent *agent, VmInterfaceConfigData *data,
                                 IFMapNode *node, IFMapNode *vn_node) {
    ConfigManager *cfg_manager= agent->config_manager();
    if (cfg_manager->SkipNode(vn_node, agent->cfg()->cfg_vn_table())) {
        return false;
    }

    VirtualNetwork *cfg = static_cast<VirtualNetwork *>(vn_node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    boost::uuids::uuid vn_uuid;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, vn_uuid);

    IFMapAgentTable *vn_table = static_cast<IFMapAgentTable *>
        (vn_node->table());
    DBGraph *vn_graph = vn_table->GetGraph();
    // Iterate thru links for virtual-network looking for routing-instance
    for (DBGraphVertex::adjacency_iterator vn_iter = vn_node->begin(vn_graph);
         vn_iter != vn_node->end(vn_graph); ++vn_iter) {

        IFMapNode *vrf_node = static_cast<IFMapNode *>(vn_iter.operator->());
        if (cfg_manager->SkipNode(vrf_node, agent->cfg()->cfg_vrf_table())) {
            continue;
        }
        // We are interested only in default-vrf
        RoutingInstance *ri = static_cast<RoutingInstance *>
            (vrf_node->GetObject());
        if(!(ri->is_default())) {
            continue;
        }
        FloatingIp *fip = static_cast<FloatingIp *>(node->GetObject());
        assert(fip != NULL);

        boost::system::error_code ec;
        IpAddress addr = IpAddress::from_string(fip->address(), ec);
        if (ec.value() != 0) {
            LOG(ERROR, "Error decoding Floating IP address " << fip->address());
            OPER_TRACE_ENTRY(Trace, agent->interface_table(),
                             "Error decoding Floating IP address " +
                             fip->address());
        } else {
            IpAddress fixed_ip_addr =
                IpAddress::from_string(fip->fixed_ip_address(), ec);
            if (ec.value() != 0) {
                fixed_ip_addr = IpAddress();
            }
            if (!fixed_ip_addr.is_unspecified()) {
                if ((addr.is_v4() && !fixed_ip_addr.is_v4()) ||
                    (addr.is_v6() && !fixed_ip_addr.is_v6())) {
                    string msg = "Invalid fixed-ip " + fip->fixed_ip_address() +
                                 " for FloatingIP " + fip->address();
                    LOG(ERROR, msg);
                    OPER_TRACE_ENTRY(Trace, agent->interface_table(), msg);

                    return true;
                }
            }

            if (ri->fabric_snat()) {
                AddFabricFloatingIp(agent, data, fixed_ip_addr);
            }

            VmInterface::FloatingIp::Direction dir =
                VmInterface::FloatingIp::DIRECTION_BOTH;
            // Get direction
            if (boost::iequals(fip->traffic_direction(), "ingress"))
                dir = VmInterface::FloatingIp::DIRECTION_INGRESS;
            else if (boost::iequals(fip->traffic_direction(), "egress"))
                dir = VmInterface::FloatingIp::DIRECTION_EGRESS;

            // Make port-map
            VmInterface::FloatingIp::PortMap src_port_map;
            VmInterface::FloatingIp::PortMap dst_port_map;
            for (PortMappings::const_iterator it = fip->port_mappings().begin();
                 it != fip->port_mappings().end(); it++) {
                uint16_t protocol = Agent::ProtocolStringToInt(it->protocol);
                VmInterface::FloatingIp::PortMapKey dst(protocol,
                                                        it->src_port);
                dst_port_map.insert(std::make_pair(dst, it->dst_port));
                VmInterface::FloatingIp::PortMapKey src(protocol,
                                                        it->dst_port);
                src_port_map.insert(std::make_pair(src, it->src_port));
            }
            data->floating_ip_list_.list_.insert
                (VmInterface::FloatingIp (addr, vrf_node->name(), vn_uuid,
                                          fixed_ip_addr, dir,
                                          fip->port_mappings_enable(),
                                          src_port_map,
                                          dst_port_map, false));
            if (addr.is_v4()) {
                data->floating_ip_list_.v4_count_++;
            } else {
                data->floating_ip_list_.v6_count_++;
            }
        }
        return true;
    }
    return false;
}

// Build one Floating IP entry for a virtual-machine-interface
static void BuildFloatingIpList(Agent *agent, VmInterfaceConfigData *data,
                                IFMapNode *node) {
    ConfigManager *cfg_manager= agent->config_manager();
    if (cfg_manager->SkipNode(node)) {
        return;
    }

    // Find VRF for the floating-ip. Following paths leads to VRF
    // 1. virtual-machine-port <-> floating-ip <-> floating-ip-pool
    //    <-> virtual-network <-> routing-instance
    // 2. virtual-machine-port <-> floating-ip <-> instance-ip
    //    <-> virtual-network <-> routing-instance
    IFMapAgentTable *fip_table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *fip_graph = fip_table->GetGraph();

    // Iterate thru links for floating-ip looking for floating-ip-pool or
    // instance-ip node
    for (DBGraphVertex::adjacency_iterator fip_iter = node->begin(fip_graph);
         fip_iter != node->end(fip_graph); ++fip_iter) {
        IFMapNode *node1 = static_cast<IFMapNode *>(fip_iter.operator->());
        if (cfg_manager->SkipNode(node1)) {
            continue;
        }
        IFMapAgentTable *table1 =
            static_cast<IFMapAgentTable *> (node1->table());

        // We are interested in floating-ip-pool or instance-ip neighbours
        if (table1 == agent->cfg()->cfg_floatingip_pool_table() ||
            table1 == agent->cfg()->cfg_instanceip_table()) {
            // Iterate thru links for floating-ip-pool/instance-ip looking for
            // virtual-network and routing-instance
            DBGraph *pool_graph = table1->GetGraph();
            for (DBGraphVertex::adjacency_iterator node1_iter =
                 node1->begin(pool_graph);
                 node1_iter != node1->end(pool_graph); ++node1_iter) {

                IFMapNode *vn_node =
                    static_cast<IFMapNode *>(node1_iter.operator->());
                if (BuildFloatingIpVnVrf(agent, data, node, vn_node) == true)
                    break;
            }
            break;
        }
    }
    return;
}

// Build one Alias IP entry for a virtual-machine-interface
static void BuildAliasIpList(InterfaceTable *intf_table,
                             VmInterfaceConfigData *data,
                             IFMapNode *node) {
    Agent *agent = intf_table->agent();
    ConfigManager *cfg_manager= agent->config_manager();
    if (cfg_manager->SkipNode(node)) {
        return;
    }

    // Find VRF for the alias-ip. Following path in graphs leads to VRF
    // virtual-machine-port <-> alias-ip <-> alias-ip-pool
    // <-> virtual-network <-> routing-instance
    IFMapAgentTable *aip_table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *aip_graph = aip_table->GetGraph();

    // Iterate thru links for alias-ip looking for alias-ip-pool node
    for (DBGraphVertex::adjacency_iterator aip_iter = node->begin(aip_graph);
         aip_iter != node->end(aip_graph); ++aip_iter) {
        IFMapNode *pool_node = static_cast<IFMapNode *>(aip_iter.operator->());
        if (cfg_manager->SkipNode
            (pool_node, agent->cfg()->cfg_aliasip_pool_table())) {
            continue;
        }

        // Iterate thru links for alias-ip-pool looking for virtual-network
        IFMapAgentTable *pool_table =
            static_cast<IFMapAgentTable *> (pool_node->table());
        DBGraph *pool_graph = pool_table->GetGraph();
        for (DBGraphVertex::adjacency_iterator pool_iter =
             pool_node->begin(pool_graph);
             pool_iter != pool_node->end(pool_graph); ++pool_iter) {

            IFMapNode *vn_node =
                static_cast<IFMapNode *>(pool_iter.operator->());
            if (cfg_manager->SkipNode
                (vn_node, agent->cfg()->cfg_vn_table())) {
                continue;
            }

            VirtualNetwork *cfg = static_cast <VirtualNetwork *>
                (vn_node->GetObject());
            assert(cfg);
            autogen::IdPermsType id_perms = cfg->id_perms();
            boost::uuids::uuid vn_uuid;
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                       vn_uuid);

            IFMapAgentTable *vn_table =
                static_cast<IFMapAgentTable *> (vn_node->table());
            DBGraph *vn_graph = vn_table->GetGraph();
            // Iterate thru links for virtual-network looking for
            // routing-instance
            for (DBGraphVertex::adjacency_iterator vn_iter =
                 vn_node->begin(vn_graph);
                 vn_iter != vn_node->end(vn_graph); ++vn_iter) {

                IFMapNode *vrf_node =
                    static_cast<IFMapNode *>(vn_iter.operator->());
                if (cfg_manager->SkipNode
                    (vrf_node, agent->cfg()->cfg_vrf_table())){
                    continue;
                }
                // Checking whether it is default vrf of not
                RoutingInstance *ri =
                    static_cast<RoutingInstance *>(vrf_node->GetObject());
                if(!(ri->is_default())) {
                    continue;
                }
                AliasIp *aip = static_cast<AliasIp *>(node->GetObject());
                assert(aip != NULL);

                boost::system::error_code ec;
                IpAddress addr = IpAddress::from_string(aip->address(), ec);
                if (ec.value() != 0) {
                    OPER_TRACE_ENTRY(Trace, intf_table,
                                     "Error decoding Alias IP address " +
                                     aip->address());
                } else {
                    data->alias_ip_list_.list_.insert
                        (VmInterface::AliasIp(addr, vrf_node->name(), vn_uuid));
                    if (addr.is_v4()) {
                        data->alias_ip_list_.v4_count_++;
                    } else {
                        data->alias_ip_list_.v6_count_++;
                    }
                }
                break;
            }
            break;
        }
        break;
    }
    return;
}

// Build list of static-routes on virtual-machine-interface
static void BuildStaticRouteList(VmInterfaceConfigData *data, IFMapNode *node) {
    InterfaceRouteTable *entry =
        static_cast<InterfaceRouteTable*>(node->GetObject());
    assert(entry);

    for (std::vector<RouteType>::const_iterator it = entry->routes().begin();
         it != entry->routes().end(); it++) {
        int plen;
        boost::system::error_code ec;
        IpAddress ip;
        bool add = false;

        Ip4Address ip4;
        ec = Ip4PrefixParse(it->prefix, &ip4, &plen);
        if (ec.value() == 0) {
            ip = ip4;
            add = true;
        } else {
            Ip6Address ip6;
            ec = Inet6PrefixParse(it->prefix, &ip6, &plen);
            if (ec.value() == 0) {
                ip = ip6;
                add = true;
            } else {
                LOG(DEBUG, "Error decoding v4/v6 Static Route address " <<
                    it->prefix);
            }
        }

        IpAddress gw = IpAddress::from_string(it->next_hop, ec);
        if (ec) {
            gw = IpAddress::from_string("0.0.0.0", ec);
        }

        if (add) {
            data->static_route_list_.list_.insert
                (VmInterface::StaticRoute
                 (ip, plen, gw, it->community_attributes.community_attribute));
        }
    }
}

static void BuildResolveRoute(VmInterfaceConfigData *data, IFMapNode *node) {
    Subnet *entry =
        static_cast<Subnet *>(node->GetObject());
    assert(entry);
    Ip4Address ip;
    boost::system::error_code ec;
    ip = Ip4Address::from_string(entry->ip_prefix().ip_prefix, ec);
    if (ec.value() == 0) {
        data->subnet_ = ip;
        data->subnet_plen_ = entry->ip_prefix().ip_prefix_len;
    }
}

// Check if VMI is a sub-interface. Sub-interface will have
// sub_interface_vlan_tag property set to non-zero
static bool IsVlanSubInterface(VirtualMachineInterface *cfg) {
    if (cfg->IsPropertySet(VirtualMachineInterface::PROPERTIES) == false)
        return false;

    if (cfg->properties().sub_interface_vlan_tag == 0)
        return false;

    return true;
}

// Get VLAN if linked to physical interface and router
static void BuildInterfaceConfigurationData(Agent *agent,
                                            IFMapNode *node,
                                            uint16_t *rx_vlan_id,
                                            uint16_t *tx_vlan_id,
                                            IFMapNode **li_node,
                                            IFMapNode **phy_interface,
                                            IFMapNode **phy_device) {
    if (*li_node == NULL) {
        *li_node = node;
    }

    if (*phy_interface == NULL) {
        *phy_interface = agent->config_manager()->
            FindAdjacentIFMapNode(node, "physical-interface");
        // Update li_node if phy_interface is set
        if (*phy_interface) {
            *li_node = node;
        }
    }
    if (!(*phy_interface)) {
        *rx_vlan_id = VmInterface::kInvalidVlanId;
        *tx_vlan_id = VmInterface::kInvalidVlanId;
        return;
    }

    if (*phy_device == NULL) {
        *phy_device =
            agent->config_manager()->
            FindAdjacentIFMapNode(*phy_interface, "physical-router");
    }
    if (!(*phy_device)) {
        *rx_vlan_id = VmInterface::kInvalidVlanId;
        *tx_vlan_id = VmInterface::kInvalidVlanId;
        return;
    }

    autogen::LogicalInterface *port =
        static_cast <autogen::LogicalInterface *>(node->GetObject());
    if (port->IsPropertySet(autogen::LogicalInterface::VLAN_TAG)) {
        *rx_vlan_id = port->vlan_tag();
        *tx_vlan_id = port->vlan_tag();
    }
}

static void BuildInterfaceConfigurationDataFromVpg(Agent *agent,
                                            IFMapNode *node,
                                            IFMapNode **vpg_node,
                                            IFMapNode **phy_interface,
                                            IFMapNode **phy_device) {
    if (*vpg_node == NULL) {
        *vpg_node = node;
    }

    IFMapNode *vpg_pi_node = agent->config_manager()->
        FindAdjacentIFMapNode(node, "virtual-port-group-physical-interface");
    if (vpg_pi_node == NULL) {
        return;
    }

    if (*phy_interface == NULL) {
        *phy_interface = agent->config_manager()->
            FindAdjacentIFMapNode(vpg_pi_node, "physical-interface");
        // Update vpg_node if phy_interface is present
        if (*phy_interface) {
            *vpg_node = node;
        }
    }
    if (*phy_interface == NULL) {
            return;
    }

    if (*phy_device == NULL) {
        *phy_device =
            agent->config_manager()->
            FindAdjacentIFMapNode(*phy_interface, "physical-router");
    }
    if (*phy_device == NULL) {
            return;
    }
}

static void BuildAllowedAddressPairRouteList(VirtualMachineInterface *cfg,
                                             VmInterfaceConfigData *data) {
    for (std::vector<AllowedAddressPair>::const_iterator it =
         cfg->allowed_address_pairs().begin();
         it != cfg->allowed_address_pairs().end(); ++it) {
        boost::system::error_code ec;
        int plen = it->ip.ip_prefix_len;
        IpAddress ip = Ip4Address::from_string(it->ip.ip_prefix, ec);
        if (ec.value() != 0) {
            ip = Ip6Address::from_string(it->ip.ip_prefix, ec);
        }
        if (ec.value() != 0) {
            continue;
        }

        MacAddress mac = MacAddress::FromString(it->mac, &ec);
        if (ec.value() != 0) {
            mac.Zero();
        }

        if (ip.is_unspecified() && mac == MacAddress::kZeroMac) {
            continue;
        }

        bool ecmp = false;
        if (it->address_mode == "active-active") {
            ecmp = true;
        }

        VmInterface::AllowedAddressPair entry(ip, plen, ecmp, mac);
        data->allowed_address_pair_list_.list_.insert(entry);
    }
}

// Build VM Interface VRF or one Service Vlan entry for VM Interface
static void BuildVrfAndServiceVlanInfo(Agent *agent,
                                       VmInterfaceConfigData *data,
                                       IFMapNode *node) {

    ConfigManager *cfg_manager= agent->config_manager();
    VirtualMachineInterfaceRoutingInstance *entry =
        static_cast<VirtualMachineInterfaceRoutingInstance*>(node->GetObject());
    assert(entry);

    // Ignore node if direction is not yet set. An update will come later
    const PolicyBasedForwardingRuleType &rule = entry->data();
    if (rule.direction == "" && !agent->isMockMode()) {
        return;
    }


    // Find VRF by looking for link
    // virtual-machine-interface-routing-instance <-> routing-instance
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();

    // Iterate thru links looking for routing-instance node
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {

        IFMapNode *vrf_node = static_cast<IFMapNode *>(iter.operator->());
        if (cfg_manager->SkipNode(vrf_node, agent->cfg()->cfg_vrf_table())) {
            continue;
        }

        if (rule.vlan_tag == 0 && rule.protocol == "" &&
            rule.service_chain_address == "") {
            data->vrf_name_ = vrf_node->name();
            const autogen::RoutingInstance *ri =
                static_cast<autogen::RoutingInstance *>(vrf_node->GetObject());
            if (ri->fabric_snat()) {
                AddFabricFloatingIp(agent, data, Ip4Address(0));
            }
        } else {
            if (!rule.service_chain_address.size() &&
                !rule.ipv6_service_chain_address.size()) {
                LOG(DEBUG, "Service VLAN IP address not specified for "
                    << node->name());
                break;
            }
            boost::system::error_code ec;
            Ip4Address addr;
            bool ip_set = true, ip6_set = true;
            if (rule.service_chain_address.size()) {
                addr = Ip4Address::from_string(rule.service_chain_address, ec);
                if (ec.value() != 0) {
                    ip_set = false;
                    LOG(DEBUG, "Error decoding Service VLAN IP address " <<
                        rule.service_chain_address);
                }
            }
            Ip6Address addr6;
            if (rule.ipv6_service_chain_address.size()) {
                addr6 = Ip6Address::from_string(rule.ipv6_service_chain_address,
                                                ec);
                if (ec.value() != 0) {
                    ip6_set = false;
                    LOG(DEBUG, "Error decoding Service VLAN IP address " <<
                        rule.ipv6_service_chain_address);
                }
            }
            if (!ip_set && !ip6_set) {
                break;
            }

            if (rule.vlan_tag > 4093) {
                LOG(DEBUG, "Invalid VLAN Tag " << rule.vlan_tag);
                break;
            }

            LOG(DEBUG, "Add Service VLAN entry <" << rule.vlan_tag << " : " <<
                rule.service_chain_address << " : " << vrf_node->name());

            MacAddress smac(agent->vrrp_mac());
            MacAddress dmac = MacAddress::FromString(Agent::BcastMac());
            if (rule.src_mac != Agent::NullString()) {
                smac = MacAddress::FromString(rule.src_mac);
            }
            if (rule.src_mac != Agent::NullString()) {
                dmac = MacAddress::FromString(rule.dst_mac);
            }
            data->service_vlan_list_.list_.insert
                (VmInterface::ServiceVlan(rule.vlan_tag, vrf_node->name(), addr,
                                          addr6, smac, dmac));
        }
        break;
    }

    return;
}

// Build proxy-arp flag on VMI
// In future, we expect a proxy-arp flag on VMI. In the meanwhile, we want
// to enable proxy-arp on following,
// 1. Left and right interface of transparent service-chain
// 2. Left and right interface of in-network service-chain
// 3. Left interface of in-network-nat service-chain
//
// The common attribute for all these interface are,
// - They have vrf-assign rules
// - They have service-interface-type attribute
//
// Note: Right interface of in-network-nat will not have vrf-assign
static void BuildProxyArpFlags(Agent *agent, VmInterfaceConfigData *data,
                               VirtualMachineInterface *cfg) {
    if (cfg->vrf_assign_table().size() == 0)
        return;

    // Proxy-mode valid only on left or right interface os SI
    if (cfg->properties().service_interface_type != "left" &&
        cfg->properties().service_interface_type != "right") {
        return;
    }

    data->proxy_arp_mode_ = VmInterface::PROXY_ARP_UNRESTRICTED;
}

static bool ValidateFatFlowCfg(const boost::uuids::uuid &u, const ProtocolType *pt)
{
    if (pt->source_prefix.ip_prefix.length() > 0) {
        if (pt->source_prefix.ip_prefix_len < 8) {
            LOG(ERROR, "FatFlowCfg validation failed for VMI uuid:" << u
                        << " Protocol:" << pt->protocol << " Port:" << pt->port
                        << " Plen:" << pt->source_prefix.ip_prefix_len
                        << " src mask cannot be < 8\n");
            return false;
        }
        if (pt->source_aggregate_prefix_length < pt->source_prefix.ip_prefix_len) {
            LOG(ERROR, "FatFlowCfg validation failed for VMI uuid:" << u
                        << " Protocol:" << pt->protocol << " Port:" << pt->port
                        << " src aggr plen is less than src mask\n");
            return false;
        }
    }
    if (pt->destination_prefix.ip_prefix.length() > 0) {
        if (pt->destination_prefix.ip_prefix_len < 8) {
            LOG(ERROR, "FatFlowCfg validation failed for VMI uuid:" << u
                       << " Protocol:" << pt->protocol << " Port:" << pt->port
                       << " Plen:" << pt->destination_prefix.ip_prefix_len
                       << " dst mask cannot be < 8\n");
            return false;
        }
        if (pt->destination_aggregate_prefix_length <
                pt->destination_prefix.ip_prefix_len) {
            LOG(ERROR, "FatFlowCfg validation failed for VMI uuid:" << u
                       << " Protocol:" << pt->protocol << " Port:" << pt->port
                       << " dst aggr plen is less than dst mask\n");
            return false;
        }
    }
    if ((pt->source_prefix.ip_prefix.length() > 0) &&
        (pt->destination_prefix.ip_prefix.length() > 0)) {
        IpAddress ip_src = IpAddress::from_string(pt->source_prefix.ip_prefix);
        IpAddress ip_dst = IpAddress::from_string(pt->destination_prefix.ip_prefix);
        if ((ip_src.is_v4() && ip_dst.is_v6()) ||
            (ip_src.is_v6() && ip_dst.is_v4())) {
            LOG(ERROR, "FatFlowCfg validation failed for VMI uuid:" << u << " Protocol:" << pt->protocol
                        << " Port:" << pt->port << " src and dst addr family mismatch\n");
            return false;
        }
    }
    return true;
}

static void BuildFatFlowTable(Agent *agent, const boost::uuids::uuid &u,
                              VmInterfaceConfigData *data, IFMapNode *node) {
    VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
                                       (node->GetObject());

    for (FatFlowProtocols::const_iterator it = cfg->fat_flow_protocols().begin();
            it != cfg->fat_flow_protocols().end(); it++) {
        if (!ValidateFatFlowCfg(u, &(*it))) {
            continue;
        }
        VmInterface::FatFlowEntry e = VmInterface::FatFlowEntry::MakeFatFlowEntry(
                                                  it->protocol, it->port,
                                                  it->ignore_address,
                                                  it->source_prefix.ip_prefix,
                                                  it->source_prefix.ip_prefix_len,
                                                  it->source_aggregate_prefix_length,
                                                  it->destination_prefix.ip_prefix,
                                                  it->destination_prefix.ip_prefix_len,
                                                  it->destination_aggregate_prefix_length);
        data->fat_flow_list_.Insert(&e);
    }
}

static void BuildInstanceIp(Agent *agent, VmInterfaceConfigData *data,
                            IFMapNode *node) {
    InstanceIp *ip = static_cast<InstanceIp *>(node->GetObject());
    boost::system::error_code err;
    IpAddress addr = IpAddress::from_string(ip->address(), err);
    bool is_primary = false;

    if (err.value() != 0) {
        return;
    }

    if (ip->secondary() != true && ip->service_instance_ip() != true &&
        ip->service_health_check_ip() != true) {
        is_primary = true;
        if (addr.is_v4()) {
            if (addr == Ip4Address(0)) {
                return;
            }

            if (data->addr_ == Ip4Address(0) ||
                data->addr_ > addr.to_v4()) {
                data->addr_ = addr.to_v4();
                if (ip->mode() == "active-active") {
                    data->ecmp_ = true;
                } else {
                    data->ecmp_ = false;
                }
            }
        } else if (addr.is_v6()) {
            if (addr == Ip6Address()) {
                return;
            }
            if (data->ip6_addr_ == Ip6Address() ||
                data->ip6_addr_ > addr.to_v6()) {
                data->ip6_addr_ = addr.to_v6();
            }
            if (ip->mode() == "active-active") {
                data->ecmp6_ = true;
            } else {
                data->ecmp6_ = false;
            }
        }
    }

    if (ip->service_instance_ip()) {
        if (addr.is_v4()) {
            data->service_ip_ = addr.to_v4();
            data->service_ip_ecmp_ = false;
            if (ip->mode() == "active-active") {
                data->service_ip_ecmp_ = true;
            }
        } else if (addr.is_v6()) {
            data->service_ip6_ = addr.to_v6();
            data->service_ip_ecmp6_ = false;
            if (ip->mode() == "active-active") {
                data->service_ip_ecmp6_ = true;
            }
        }
    }

    bool ecmp = false;
    if (ip->mode() == "active-active") {
        ecmp = true;
    }

    IpAddress tracking_ip = Ip4Address(0);
    if (ip->secondary() || ip->service_instance_ip()) {
        tracking_ip = IpAddress::from_string(
                ip->secondary_ip_tracking_ip().ip_prefix, err);
        if (err.value() != 0) {
            tracking_ip = Ip4Address(0);
        }

        if (tracking_ip == addr) {
            tracking_ip = Ip4Address(0);
        }
    }

    if (ip->service_health_check_ip()) {
        // if instance ip is service health check ip along with adding
        // it to instance ip list to allow route export and save it
        // under service_health_check_ip_ to allow usage for health
        // check service
        if (addr.is_v4()) {
            // TODO: currently only v4 health check is supported
            data->service_health_check_ip_ = addr;
        }
    }

    if (addr.is_v4()) {
        data->instance_ipv4_list_.list_.insert(
                VmInterface::InstanceIp(addr, Address::kMaxV4PrefixLen, ecmp,
                                        is_primary, ip->service_instance_ip(),
                                        ip->service_health_check_ip(),
                                        ip->local_ip(), tracking_ip));
    } else {
        data->instance_ipv6_list_.list_.insert(
                VmInterface::InstanceIp(addr, Address::kMaxV6PrefixLen, ecmp,
                                        is_primary, ip->service_instance_ip(),
                                        ip->service_health_check_ip(),
                                        ip->local_ip(), tracking_ip));
    }
}

static void BuildTagList(VmInterface::TagEntryList *tag_list, IFMapNode *node) {

    Tag *tag_cfg = static_cast<Tag *>(node->GetObject());
    assert(tag_cfg);

    uuid tag_uuid = nil_uuid();
    autogen::IdPermsType id_perms = tag_cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
               tag_uuid);
    uint32_t tag_type = TagEntry::GetTypeVal(tag_cfg->type_name(),
                                             tag_cfg->id());
    tag_list->list_.insert(VmInterface::TagEntry(tag_type, tag_uuid));
}

static void BuildSgList(VmInterfaceConfigData *data, IFMapNode *node) {
    SecurityGroup *sg_cfg = static_cast<SecurityGroup *>
        (node->GetObject());
    assert(sg_cfg);
    autogen::IdPermsType id_perms = sg_cfg->id_perms();
    uint32_t sg_id = sg_cfg->id();
    if (sg_id != SgTable::kInvalidSgId) {
        uuid sg_uuid = nil_uuid();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   sg_uuid);
        data->sg_list_.list_.insert
            (VmInterface::SecurityGroupEntry(sg_uuid));
    }
}

static bool BuildBridgeDomainVrfTable(Agent *agent, IFMapNode *vn_node) {

    ConfigManager *cfg_manager= agent->config_manager();
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(vn_node->table());
    DBGraph *graph = table->GetGraph();

    // Iterate thru links for virtual-network looking for routing-instance
    for (DBGraphVertex::adjacency_iterator iter = vn_node->begin(graph);
            iter != vn_node->end(graph); ++iter) {

        IFMapNode *vrf_node = static_cast<IFMapNode *>(iter.operator->());
        if (cfg_manager->SkipNode(vrf_node, agent->cfg()->cfg_vrf_table())) {
            continue;
        }

        // We are interested only in default-vrf
        RoutingInstance *ri = static_cast<RoutingInstance *>
            (vrf_node->GetObject());
        if(ri->is_default()) {
            return true;
        }
    }

    return false;
}

static bool BuildBridgeDomainVnTable(Agent *agent,
                                     IFMapNode *bridge_domain_node) {

    ConfigManager *cfg_manager= agent->config_manager();
    IFMapAgentTable *table =
        static_cast<IFMapAgentTable *>(bridge_domain_node->table());
    DBGraph *graph = table->GetGraph();

    // Iterate thru links for virtual-network fron bridge domain
    for (DBGraphVertex::adjacency_iterator iter =
         bridge_domain_node->begin(graph);
         iter != bridge_domain_node->end(graph); ++iter) {

        IFMapNode *vn_node = static_cast<IFMapNode *>(iter.operator->());
        if (cfg_manager->SkipNode(vn_node, agent->cfg()->cfg_vn_table())) {
            continue;
        }

        if (BuildBridgeDomainVrfTable(agent, vn_node) == true) {
            return true;
        }
    }
    return false;
}

static void UpdateVmiSiMode(Agent *agent, VmInterfaceConfigData *data,
                            IFMapNode *node) {

    ServiceInstance *entry = static_cast<ServiceInstance*>(node->GetObject());
    assert(entry);

    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(adj_node)) {
            continue;
        }
        if (adj_node->table() != agent->cfg()->cfg_service_template_table()) {
            continue;
        }
        ServiceTemplate *entry =  static_cast<ServiceTemplate*>(adj_node->GetObject());
        if (entry == NULL)
            return;

        ServiceTemplateType svc_template_props = entry->properties();
        string service_mode = svc_template_props.service_mode;

        if (service_mode == "in-network") {
            data->service_mode_ = VmInterface::ROUTED_MODE;
        } else if (service_mode == "transparent") {
            data->service_mode_ = VmInterface::BRIDGE_MODE;
        } else if (service_mode == "in-network-nat") {
            data->service_mode_ = VmInterface::ROUTED_NAT_MODE;
        } else {
            data->service_mode_ = VmInterface::SERVICE_MODE_ERROR;
        }
    }
}

static void BuildVmiSiMode(Agent *agent, VmInterfaceConfigData *data,
                            IFMapNode *node) {

    PortTuple *entry = static_cast<PortTuple*>(node->GetObject());
    assert(entry);

    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (adj_node->table() != agent->cfg()->cfg_service_instance_table()) {
            continue;
        }
        UpdateVmiSiMode(agent, data, adj_node);
    }
}

static bool BuildLogicalRouterData(Agent *agent,
                                   VmInterfaceConfigData *data,
                                   IFMapNode *node) {
    VirtualMachineInterface *vmi =
        static_cast<autogen::VirtualMachineInterface *>
        (node->GetObject());
    if (!vmi->IsPropertySet(VirtualMachineInterface::DEVICE_OWNER)) {
        return false;
    }
    if (vmi->device_owner() != VMI_NETWORK_ROUTER_INTERFACE) {
        return false;
    }
    return true;
}

static void BuildSiOtherVmi(Agent *agent, VmInterfaceConfigData *data,
                            IFMapNode *node, const string &s_intf_type) {
    PortTuple *entry = static_cast<PortTuple*>(node->GetObject());
    assert(entry);

    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(adj_node)) {
            continue;
        }
        if (adj_node->table() != agent->cfg()->cfg_vm_interface_table()) {
            continue;
        }
        VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
            (adj_node->GetObject());
        if (!cfg->IsPropertySet(VirtualMachineInterface::PROPERTIES)) {
            continue;
        }

        string interface_to_find = "right";
        if (s_intf_type == "right") {
            interface_to_find = "left";
        }
        const string &cfg_intf_type = cfg->properties().service_interface_type;
        if (cfg_intf_type != interface_to_find) {
            continue;
        }

        data->is_left_si_ = (interface_to_find == "right") ? true : false;
        data->si_other_end_vmi_ = nil_uuid();
        autogen::IdPermsType id_perms = cfg->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   data->si_other_end_vmi_);
        /* No further iterations required for setting data->si_other_end_vmi */
        break;
    }
}

// Build VM Interface bridge domain link
static void BuildBridgeDomainTable(Agent *agent,
                                   VmInterfaceConfigData *data,
                                   IFMapNode *node) {

    ConfigManager *cfg_manager= agent->config_manager();
    VirtualMachineInterfaceBridgeDomain *entry =
        static_cast<VirtualMachineInterfaceBridgeDomain*>(node->GetObject());
    assert(entry);

    const BridgeDomainMembershipType &vlan = entry->data();
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {

        IFMapNode *bridge_domain_node =
            static_cast<IFMapNode *>(iter.operator->());
        if (cfg_manager->SkipNode
            (bridge_domain_node, agent->cfg()->cfg_bridge_domain_table())) {
            continue;
        }

        //Verify that bridge domain has link to VN and VRF
        //then insert in config node list
        if (BuildBridgeDomainVnTable(agent, bridge_domain_node) == false) {
            continue;
        }
        autogen::BridgeDomain *bd_cfg = static_cast<autogen::BridgeDomain *>
            (bridge_domain_node->GetObject());
        if (bd_cfg->isid() == 0) {
            continue;
        }
        autogen::IdPermsType id_perms = bd_cfg->id_perms();
        uuid bd_uuid = nil_uuid();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   bd_uuid);
        data->bridge_domain_list_.list_.insert(
                VmInterface::BridgeDomain(bd_uuid, vlan.vlan_tag));

        if (bd_cfg->mac_learning_enabled()) {
            data->learning_enabled_ = true;
        }
        break;
    }
    return;
}

static void CompareVnVm(const uuid &vmi_uuid, VmInterfaceConfigData *data,
                        const PortSubscribeEntry *entry) {
    /* VN uuid is mandatory for port adds from REST API only for port type of
     * PortSubscribeEntry::VMPORT. Hence VN match check should be done only for
     * port type of PortSubscribeEntry::VMPORT
     */
    if (entry && entry->type() == PortSubscribeEntry::VMPORT &&
        (entry->MatchVn(data->vn_uuid_) == false)) {
        IFMAP_ERROR(InterfaceConfiguration,
                    "Virtual-network UUID mismatch for interface:",
                    UuidToString(vmi_uuid),
                    "configuration VN uuid",
                    UuidToString(data->vn_uuid_),
                    "compute VN uuid",
                    UuidToString(entry->vn_uuid()));
    }

    if (entry && (entry->MatchVm(data->vm_uuid_) == false)) {
        IFMAP_ERROR(InterfaceConfiguration,
                    "Virtual-machine UUID mismatch for interface:",
                    UuidToString(vmi_uuid),
                    "configuration VM UUID is",
                    UuidToString(data->vm_uuid_),
                    "compute VM uuid is",
                    UuidToString(entry->vm_uuid()));
    }
}

static void BuildVn(VmInterfaceConfigData *data,
                    IFMapNode *node,
                    const boost::uuids::uuid &u,
                    VmInterface::TagEntryList *tag_list) {
    const Agent *agent = data->agent();
    VirtualNetwork *vn =
        static_cast<VirtualNetwork *>(node->GetObject());
    assert(vn);
    autogen::IdPermsType id_perms = vn->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong,
               id_perms.uuid.uuid_lslong, data->vn_uuid_);

    if (node->name() == agent->fabric_vn_name()) {
        data->proxy_arp_mode_ = VmInterface::PROXY_ARP_UNRESTRICTED;
    }

    /* Copy fat-flow configured at VN level */
    for (FatFlowProtocols::const_iterator it = vn->fat_flow_protocols().begin();
            it != vn->fat_flow_protocols().end(); it++) {
        if (!ValidateFatFlowCfg(u, &(*it))) {
             continue;
        }

        VmInterface::FatFlowEntry e = VmInterface::FatFlowEntry::MakeFatFlowEntry(
                                                  it->protocol, it->port,
                                                  it->ignore_address,
                                                  it->source_prefix.ip_prefix,
                                                  it->source_prefix.ip_prefix_len,
                                                  it->source_aggregate_prefix_length,
                                                  it->destination_prefix.ip_prefix,
                                                  it->destination_prefix.ip_prefix_len,
                                                  it->destination_aggregate_prefix_length);
        data->fat_flow_list_.Insert(&e);
    }
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
            node->begin(table->GetGraph());
            iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->CanUseNode(adj_node,
                                                agent->cfg()->cfg_vn_table())) {
            if (adj_node->name() == agent->fabric_vn_name()) {
                data->proxy_arp_mode_ = VmInterface::PROXY_ARP_UNRESTRICTED;
            }
        }

        if (agent->config_manager()->SkipNode(adj_node,
                                              agent->cfg()->cfg_tag_table())) {
            continue;
        }
        BuildTagList(tag_list, adj_node);
    }
}

static void FillHbsInfo(VmInterfaceConfigData *data,
                         IFMapNode *vn_node) {
    //Reset the hbs interface 
    data->hbs_intf_type_ =  VmInterface::HBS_INTF_INVALID;

    if (vn_node == NULL) {
        return;
    }

    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(vn_node->table());
    DBGraph *graph = table->GetGraph();

    //Get the HBS information from project
    for (DBGraphVertex::adjacency_iterator iter = vn_node->begin(graph);
         iter != vn_node->end(graph); ++iter) {
        if (iter->IsDeleted()) {
            continue;
        }
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        //Check if HBS service is enabled for this project
        if (strcmp(adj_node->table()->Typename(),
                   "host-based-service-virtual-network") == 0) {
            autogen::HostBasedServiceVirtualNetwork *hbsvn =
                static_cast<HostBasedServiceVirtualNetwork *>(adj_node->GetObject());
            ServiceVirtualNetworkType type = hbsvn->data();
            if (strcmp(type.virtual_network_type.c_str(),
                       "right") == 0) {
                data->hbs_intf_type_ = VmInterface::HBS_INTF_RIGHT;
            } else if (strcmp(type.virtual_network_type.c_str(),
                              "left") == 0) {
                data->hbs_intf_type_ = VmInterface::HBS_INTF_LEFT;
            } else if (strcmp(type.virtual_network_type.c_str(),
                              "management") == 0) {
                data->hbs_intf_type_ = VmInterface::HBS_INTF_MGMT;
            }
        }
    }
}

static void BuildProject(VmInterfaceConfigData *data,
                         IFMapNode *node,
                         const boost::uuids::uuid &u,
                         VmInterface::TagEntryList *tag_list) {
    const Agent *agent = data->agent();
    Project *pr = static_cast<Project *>(node->GetObject());
    assert(pr);

    //We parse thru edge table while getting tag attached to project
    //This is done because project to tag has 2 links
    //1> With metadata project-scoped-tag this is used to create tag
    //   specific to project
    //2> Another with metadata project-tag which signifies that tag
    //   is attached to project
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::edge_iterator iter =
            node->edge_list_begin(table->GetGraph());
            iter != node->edge_list_end(table->GetGraph()); ++iter) {

        IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.target());
        if (link->metadata() != "project-tag") {
            continue;
        }

        if (agent->config_manager()->SkipNode(adj_node,
                                              agent->cfg()->cfg_tag_table())) {
            continue;
        }
        BuildTagList(tag_list, adj_node);
    }
}

static void BuildQosConfig(VmInterfaceConfigData *data, IFMapNode *node) {
    autogen::QosConfig *qc =
        static_cast<autogen::QosConfig *>(node->GetObject());
    assert(qc);
    autogen::IdPermsType id_perms = qc->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong,
               id_perms.uuid.uuid_lslong, data->qos_config_uuid_);
}

static void BuildVm(VmInterfaceConfigData *data,
                    IFMapNode *node,
                    const boost::uuids::uuid &u,
                    VmInterface::TagEntryList *tag_list) {
    const Agent *agent = data->agent();
    VirtualMachine *vm = static_cast<VirtualMachine *>(node->GetObject());
    assert(vm);

    autogen::IdPermsType id_perms = vm->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong,
               id_perms.uuid.uuid_lslong, data->vm_uuid_);


    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
            node->begin(table->GetGraph());
            iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent->config_manager()->SkipNode(adj_node,
                                              agent->cfg()->cfg_tag_table())) {
            continue;
        }
        BuildTagList(tag_list, adj_node);
    }
}

// Get DHCP configuration
static void ReadDhcpOptions(VirtualMachineInterface *cfg,
                            VmInterfaceConfigData &data) {
    data.oper_dhcp_options_.set_options(cfg->dhcp_option_list());
    data.oper_dhcp_options_.set_host_routes(cfg->host_routes());
}

// Get interface mirror configuration.
static void ReadAnalyzerNameAndCreate(Agent *agent,
                                      VirtualMachineInterface *cfg,
                                      VmInterfaceConfigData &data) {
    if (!cfg) {
        return;
    }
    MirrorActionType mirror_to = cfg->properties().interface_mirror.mirror_to;
    if (mirror_to.analyzer_name.empty())
        return;
    // Check for nic assisted mirroring support.
    if (!mirror_to.nic_assisted_mirroring) {
        boost::system::error_code ec;
        IpAddress dip = IpAddress::from_string(mirror_to.analyzer_ip_address,
                                              ec);
        if (ec.value() != 0) {
            return;
        }
        uint16_t dport;
        if (mirror_to.udp_port) {
            dport = mirror_to.udp_port;
        } else {
            dport = ContrailPorts::AnalyzerUdpPort();
        }
         MirrorEntryData::MirrorEntryFlags mirror_flag =
             MirrorTable::DecodeMirrorFlag(mirror_to.nh_mode,
                                           mirror_to.juniper_header);
        // not using the vrf coming in; by setting this to empty, -1 will be
        // configured so that current VRF will be used (for leaked routes).
        if (mirror_flag == MirrorEntryData::DynamicNH_With_JuniperHdr) {
            agent->mirror_table()->AddMirrorEntry
                (mirror_to.analyzer_name, std::string(),
                 agent->GetMirrorSourceIp(dip),
                 agent->mirror_port(), dip, dport);
        } else if (mirror_flag ==
                   MirrorEntryData::DynamicNH_Without_JuniperHdr) {
            agent->mirror_table()->AddMirrorEntry(mirror_to.analyzer_name,
                    mirror_to.routing_instance, agent->GetMirrorSourceIp(dip),
                    agent->mirror_port(), dip, dport, 0, mirror_flag,
                    MacAddress::FromString(mirror_to.analyzer_mac_address));
        } else if (mirror_flag ==
                   MirrorEntryData::StaticNH_Without_JuniperHdr) {
            IpAddress vtep_dip = IpAddress::from_string
                (mirror_to.static_nh_header.vtep_dst_ip_address, ec);
            if (ec.value() != 0) {
                return;
            }
            agent->mirror_table()->AddMirrorEntry(mirror_to.analyzer_name,
                    mirror_to.routing_instance, agent->GetMirrorSourceIp(dip),
                    agent->mirror_port(), vtep_dip, dport,
                    mirror_to.static_nh_header.vni, mirror_flag,
                    MacAddress::FromString
                    (mirror_to.static_nh_header.vtep_dst_mac_address));
        }
        else {
            LOG(ERROR, "Mirror nh mode not supported");
        }
    } else {
        agent->mirror_table()->AddMirrorEntry(
                mirror_to.analyzer_name,
                mirror_to.nic_assisted_mirroring_vlan);
    }
    data.analyzer_name_ =  mirror_to.analyzer_name;
    string traffic_direction =
        cfg->properties().interface_mirror.traffic_direction;
    if (traffic_direction.compare("egress") == 0) {
        data.mirror_direction_ = Interface::MIRROR_TX;
    } else if (traffic_direction.compare("ingress") == 0) {
        data.mirror_direction_ = Interface::MIRROR_RX;
    } else {
        data.mirror_direction_ = Interface::MIRROR_RX_TX;
    }
}

static void BuildVrfAssignRule(VirtualMachineInterface *cfg,
                               VmInterfaceConfigData *data) {
    uint32_t id = 1;
    for (std::vector<VrfAssignRuleType>::const_iterator iter =
         cfg->vrf_assign_table().begin(); iter != cfg->vrf_assign_table().end();
         ++iter) {
        VmInterface::VrfAssignRule entry(id++, iter->match_condition,
                                         iter->routing_instance,
                                         iter->ignore_acl);
        data->vrf_assign_rule_list_.list_.insert(entry);
    }
}

static IFMapNode *FindTarget(IFMapAgentTable *table, IFMapNode *node,
                             const std::string &node_type) {
    for (DBGraphVertex::adjacency_iterator it = node->begin(table->GetGraph());
         it != node->end(table->GetGraph()); ++it) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(it.operator->());
        if (adj_node->table()->Typename() == node_type)
            return adj_node;
    }
    return NULL;
}

static void ReadDhcpEnable(Agent *agent, VmInterfaceConfigData *data,
                           IFMapNode *node) {
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator it = node->begin(table->GetGraph());
         it != node->end(table->GetGraph()); ++it) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(it.operator->());
        IFMapNode *ipam_node = NULL;
        if (adj_node->table() == agent->cfg()->cfg_vn_network_ipam_table() &&
            (ipam_node = FindTarget(table, adj_node, "network-ipam"))) {
            VirtualNetworkNetworkIpam *ipam =
                static_cast<VirtualNetworkNetworkIpam *>(adj_node->GetObject());
            assert(ipam);
            const VnSubnetsType &subnets = ipam->data();
            boost::system::error_code ec;
            for (unsigned int i = 0; i < subnets.ipam_subnets.size(); ++i) {
                if (IsIp4SubnetMember(data->addr_,
                        Ip4Address::from_string(
                            subnets.ipam_subnets[i].subnet.ip_prefix, ec),
                        subnets.ipam_subnets[i].subnet.ip_prefix_len)) {
                    data->dhcp_enable_ = subnets.ipam_subnets[i].enable_dhcp;
                    return;
                }
            }
        }
    }
}

// Builds parent for VMI (not to be confused with parent ifmap-node)
// Possible values are,
// - logical-interface or virtual-port-group : Incase of baremetals
//   logical-interface would be examined before virtual-port-group
// - virtual-machine-interface : We support virtual-machine-interface
//   sub-interfaces. In this case, another virtual-machine-interface itself
//   can be a parent
static PhysicalRouter *BuildParentInfo(Agent *agent,
                                       VirtualMachineInterface *cfg,
                                       IFMapNode *node,
                                       IFMapNode *logical_node,
                                       IFMapNode *vpg_node,
                                       VmInterfaceConfigData *data,
                                       IFMapNode *parent_vmi_node,
                                       IFMapNode **phy_interface,
                                       IFMapNode **phy_device) {
    if (logical_node) {
        if (*phy_interface) {
            data->physical_interface_ = (*phy_interface)->name();
        }
        agent->interface_table()->
           LogicalInterfaceIFNodeToUuid(logical_node, data->logical_interface_);
        if ((*phy_device) == NULL) {
            return NULL;
        }
        return static_cast<PhysicalRouter *>((*phy_device)->GetObject());
    }

    if (vpg_node) {
        if (*phy_interface) {
            data->physical_interface_ = (*phy_interface)->name();
        }
        if (IsVlanSubInterface(cfg) == true) {
            data->rx_vlan_id_ = cfg->properties().sub_interface_vlan_tag;
            data->tx_vlan_id_ = cfg->properties().sub_interface_vlan_tag;
        }
        if ((*phy_device) == NULL) {
            return NULL;
        }
        return static_cast<PhysicalRouter *>((*phy_device)->GetObject());
    }

    // Check if this is VLAN sub-interface VMI
    if (IsVlanSubInterface(cfg) == false) {
        return NULL;
    }

    if (!parent_vmi_node)
        return NULL;

    // process Parent VMI for sub-interface
    VirtualMachineInterface *parent_cfg =
        static_cast <VirtualMachineInterface *> (parent_vmi_node->GetObject());
    assert(parent_cfg);
    if (IsVlanSubInterface(parent_cfg)) {
        return NULL;
    }
    autogen::IdPermsType id_perms = parent_cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
               data->parent_vmi_);
    data->rx_vlan_id_ = cfg->properties().sub_interface_vlan_tag;
    data->tx_vlan_id_ = cfg->properties().sub_interface_vlan_tag;
    return NULL;
}

static void BuildEcmpHashingIncludeFields(VirtualMachineInterface *cfg,
                                          IFMapNode *vn_node,
                                          VmInterfaceConfigData *data) {
    data->ecmp_load_balance_.set_use_global_vrouter(false);
    if (cfg->IsPropertySet
        (VirtualMachineInterface::ECMP_HASHING_INCLUDE_FIELDS) &&
        (cfg->ecmp_hashing_include_fields().hashing_configured)) {
        data->ecmp_load_balance_.UpdateFields(cfg->
                                              ecmp_hashing_include_fields());
    } else {
        //Extract from VN
        if (!vn_node) {
            data->ecmp_load_balance_.reset();
            data->ecmp_load_balance_.set_use_global_vrouter(true);
            return;
        }
        VirtualNetwork *vn_cfg =
            static_cast <VirtualNetwork *> (vn_node->GetObject());
        if ((vn_cfg->IsPropertySet
            (VirtualNetwork::ECMP_HASHING_INCLUDE_FIELDS) == false) ||
            (vn_cfg->ecmp_hashing_include_fields().hashing_configured == false)) {
            data->ecmp_load_balance_.set_use_global_vrouter(true);
            data->ecmp_load_balance_.reset();
            return;
        }
        data->ecmp_load_balance_.UpdateFields(vn_cfg->
                                              ecmp_hashing_include_fields());
    }

}

// Get IGMP configuration
static void ReadIgmpConfig(Agent *agent, const IFMapNode *vn_node,
                            const VirtualMachineInterface *cfg,
                            VmInterfaceConfigData *data) {

    const VirtualNetwork *vn = NULL;
    bool igmp_enabled = false;

    if (vn_node) {
        vn = static_cast<const VirtualNetwork *>(vn_node->GetObject());
    }

    if (cfg) igmp_enabled = cfg->igmp_enable();

    if (vn && !igmp_enabled) igmp_enabled = vn->igmp_enable();

    if (!igmp_enabled) {
        igmp_enabled =
                    agent->oper_db()->global_system_config()->cfg_igmp_enable();
    }

    data->cfg_igmp_enable_ = cfg->igmp_enable();
    data->igmp_enabled_ = igmp_enabled;

    return;
}

// max_flows is read from vmi properties preferentially , else vn properties
static void ReadMaxFlowsConfig(const IFMapNode *vn_node,
                            const VirtualMachineInterface *cfg,
                            VmInterfaceConfigData *data) {

    const VirtualNetwork *vn = NULL;
    if (vn_node) {
        vn = static_cast<const VirtualNetwork *>(vn_node->GetObject());
    }

    if (cfg->IsPropertySet(VirtualMachineInterface::PROPERTIES)) {
        data->max_flows_ = cfg->properties().max_flows;
    }
    if (vn && (data->max_flows_ == 0)) {
        autogen::VirtualNetworkType properties = vn->properties();
        data->max_flows_ = properties.max_flows;

    }
}

static void BuildAttributes(Agent *agent, IFMapNode *node,
                            VirtualMachineInterface *cfg,
                            VmInterfaceConfigData *data) {
    //Extract the local preference
    if (cfg->IsPropertySet(VirtualMachineInterface::PROPERTIES)) {
        data->local_preference_ = cfg->properties().local_preference;
    }

    ReadAnalyzerNameAndCreate(agent, cfg, *data);

    // Fill DHCP option data
    ReadDhcpOptions(cfg, *data);

    //Fill config data items
    data->cfg_name_ = node->name();
    data->admin_state_ = cfg->id_perms().enable;

    BuildVrfAssignRule(cfg, data);
    BuildAllowedAddressPairRouteList(cfg, data);

    if (cfg->mac_addresses().size()) {
        data->vm_mac_ = cfg->mac_addresses().at(0);
    }
    data->disable_policy_ = cfg->disable_policy();
    BuildProxyArpFlags(agent, data, cfg);
}

static void UpdateAttributes(Agent *agent, VmInterfaceConfigData *data) {
    // Compute fabric_port_ and need_linklocal_ip_ flags
    data->fabric_port_ = false;
    data->need_linklocal_ip_ = true;
    if (data->vrf_name_ == agent->fabric_vrf_name() ||
        data->vrf_name_ == agent->linklocal_vrf_name()) {
        data->fabric_port_ = true;
        data->need_linklocal_ip_ = false;
    }

    if (agent->isXenMode()) {
        data->need_linklocal_ip_ = false;
    }
}

static void ComputeTypeInfo(Agent *agent, VmInterfaceConfigData *data,
                            const PortSubscribeEntry *entry,
                            PhysicalRouter *prouter,
                            IFMapNode *node, IFMapNode *logical_node,
                            IFMapNode *logical_router) {
    if (entry != NULL) {
        // Have got InstancePortAdd message. Treat it as VM_ON_TAP by default
        // TODO: Need to identify more cases here
        data->device_type_ = VmInterface::VM_ON_TAP;
        data->vmi_type_ = VmInterface::INSTANCE;
        return;
    }

    VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
                   (node->GetObject());
    const std::vector<KeyValuePair> &bindings  = cfg->bindings();
    bool vnic_type_direct = false;
    bool vif_type_hw_veb = false;
    for (std::vector<KeyValuePair>::const_iterator it = bindings.begin();
         it != bindings.end(); ++it) {
        KeyValuePair kvp = *it;
        if ((kvp.key == "vnic_type") && (kvp.value == "direct")) {
            vnic_type_direct = true;
        } else if ((kvp.key == "vif_type") && (kvp.value == "hw_veb")) {
            vif_type_hw_veb = true;
        }
    }

    if (vnic_type_direct && vif_type_hw_veb) {
        data->device_type_ = VmInterface::VM_SRIOV;
        data->vmi_type_ = VmInterface::SRIOV;
        return;
    }

    data->device_type_ = VmInterface::DEVICE_TYPE_INVALID;
    data->vmi_type_ = VmInterface::VMI_TYPE_INVALID;
    // Does it have physical-interface
    if (data->physical_interface_.empty() == false) {
        // no physical-router connected. Should be transient case
        if (prouter == NULL) {
            // HACK : TSN/ToR agent only supports barements. So, set as
            // baremetal anyway
            if (agent->tsn_enabled() || agent->tor_agent_enabled()) {
                data->device_type_ = VmInterface::TOR;
                data->vmi_type_ = VmInterface::BAREMETAL;
            }
            return;
        }

        // VMI is either Baremetal or Gateway interface
        if (prouter->display_name() == agent->agent_name()) {
            // VMI connected to local vrouter. Treat it as GATEWAY / Remote VM.
            if (agent->server_gateway_mode() ||
                agent->pbb_gateway_mode()) {
                data->device_type_ = VmInterface::REMOTE_VM_VLAN_ON_VMI;
                data->vmi_type_ = VmInterface::REMOTE_VM;
            } else {
                data->device_type_ = VmInterface::LOCAL_DEVICE;
                data->vmi_type_ = VmInterface::GATEWAY;
            }
            if (logical_node) {
                autogen::LogicalInterface *port =
                    static_cast <autogen::LogicalInterface *>
                    (logical_node->GetObject());
                data->rx_vlan_id_ = port->vlan_tag();
                data->tx_vlan_id_ = port->vlan_tag();
            }
            return;
        } else {
            // prouter does not match. Treat as baremetal
            data->device_type_ = VmInterface::TOR;
            data->vmi_type_ = VmInterface::BAREMETAL;
            return;
        }
        return;
    }

    // Physical router not specified. Check if this is VMI sub-interface
    if (data->parent_vmi_.is_nil() == false) {
        data->device_type_ = VmInterface::VM_VLAN_ON_VMI;
        data->vmi_type_ = VmInterface::INSTANCE;
        return;
    }

    // Logical Router attached node
    if (logical_router) {
        /*
         * since logical-router type "snat-routing" is handled
         * via service-instances, vmi update should happen only
         * for logical-router type "vxlan-routing".
         */
        autogen::LogicalRouter *lr_obj =
            static_cast <autogen::LogicalRouter *>
            (logical_router->GetObject());
        if (lr_obj->type() == "vxlan-routing") {
            data->device_type_ = VmInterface::VMI_ON_LR;
            data->vmi_type_ = VmInterface::ROUTER;
        }
        return;
    }

    return;
}

void CopyTagList(VmInterfaceConfigData *data,
                 VmInterface::TagEntryList &tag_list, bool *label_added) {
    bool dont_copy_label = *label_added;

    VmInterface::TagEntrySet::iterator tag_it;
    for (tag_it = tag_list.list_.begin(); tag_it != tag_list.list_.end();
         tag_it++) {
        if (tag_it->type_ == TagTable::LABEL) {
            *label_added = true;
            if (dont_copy_label) {
                continue;
            }
        }
        data->tag_list_.list_.insert(*tag_it);
    }
}

void CopyTag(VmInterfaceConfigData *data ,
             VmInterface::TagEntryList &vmi_list,
             VmInterface::TagEntryList &vm_list,
             VmInterface::TagEntryList &vn_list,
             VmInterface::TagEntryList &project_list) {
    bool label_added = false;
    //Order of below should be maintained
    //This is becasue any tag present at VMI takes
    //precedence over VM, VN and project and since
    //tag type is key (except for label) duplicate
    //insertion doesnt happen
    CopyTagList(data, vmi_list, &label_added);
    CopyTagList(data, vm_list, &label_added);
    CopyTagList(data, vn_list, &label_added);
    CopyTagList(data, project_list, &label_added);
}

bool InterfaceTable::VmiProcessConfig(IFMapNode *node, DBRequest &req,
                                      const boost::uuids::uuid &u) {
    // Get interface UUID
    VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
        (node->GetObject());

    boost::uuids::uuid vmi_uuid = u;
    assert(cfg);
    // Handle object delete
    if (node->IsDeleted()) {
        return false;
    }

    assert(!u.is_nil());
    // Update interface configuration
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    VmInterfaceConfigData *data = new VmInterfaceConfigData(agent(), NULL);
    data->SetIFMapNode(node);

    BuildAttributes(agent_, node, cfg, data);

    // Graph walk to get interface configuration
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    IFMapNode *vn_node = NULL;
    IFMapNode *li_node = NULL;
    IFMapNode *vpg_node = NULL;
    IFMapNode *lr_node = NULL;
    IFMapNode *phy_interface = NULL;
    IFMapNode *phy_device = NULL;
    IFMapNode *parent_vmi_node = NULL;
    uint16_t rx_vlan_id = VmInterface::kInvalidVlanId;
    uint16_t tx_vlan_id = VmInterface::kInvalidVlanId;
    VmInterface::TagEntryList vmi_list;
    VmInterface::TagEntryList vm_list;
    VmInterface::TagEntryList vn_list;
    VmInterface::TagEntryList project_list;
    string service_intf_type = "";

    if (cfg->IsPropertySet(VirtualMachineInterface::PROPERTIES)) {
        service_intf_type = cfg->properties().service_interface_type;
        data->service_intf_type_ = cfg->properties().service_interface_type;
        /* Overwrite service_intf_type if it is not left or right interface */
        if (service_intf_type != "left" && service_intf_type != "right") {
            service_intf_type = "";
        }
    }

    data->vmi_cfg_uuid_ = vmi_uuid;
    std::list<IFMapNode *> bgp_as_a_service_node_list;
    std::list<IFMapNode *> bgp_router_node_list;
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph());
         iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        /* SkipNode() will be true, if node is registed with dependency_manager.
         * Eventhough bgp-router is registered with dependency_manager,
         * bgp_routers associated to a vmi is passed to bgp-as-a-sercice
         * process_config() for validation. */
        if (agent_->config_manager()->SkipNode(adj_node)) {
            if (strcmp(adj_node->table()->Typename(),
                        BGP_ROUTER_CONFIG_NAME) != 0) {
                continue;
            }
        }

        if (adj_node->table() == agent_->cfg()->cfg_sg_table()) {
            BuildSgList(data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_tag_table()) {
            BuildTagList(&vmi_list, adj_node);
        }

        if (adj_node->table() == agent()->cfg()->cfg_slo_table()) {
            uuid slo_uuid = nil_uuid();
            autogen::SecurityLoggingObject *slo =
                static_cast<autogen::SecurityLoggingObject *>(adj_node->
                                                              GetObject());
            autogen::IdPermsType id_perms = slo->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                       slo_uuid);
            data->slo_list_.push_back(slo_uuid);
        }

        if (adj_node->table() == agent_->cfg()->cfg_vn_table()) {
            vn_node = adj_node;
            BuildVn(data, adj_node, u, &vn_list);
        }

        if (adj_node->table() == agent_->cfg()->cfg_qos_table()) {
            BuildQosConfig(data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_vm_table()) {
            BuildVm(data, adj_node, u, &vm_list);
        }

        if (adj_node->table() == agent_->cfg()->cfg_project_table()) {
            BuildProject(data, adj_node, u, &project_list);
        }

        if (adj_node->table() == agent_->cfg()->cfg_instanceip_table()) {
            BuildInstanceIp(agent_, data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_floatingip_table()) {
            BuildFloatingIpList(agent_, data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_aliasip_table()) {
            BuildAliasIpList(this, data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_vm_port_vrf_table()) {
            BuildVrfAndServiceVlanInfo(agent_, data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_route_table()) {
            BuildStaticRouteList(data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_subnet_table()) {
            BuildResolveRoute(data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_logical_port_table()) {
            BuildInterfaceConfigurationData(agent(), adj_node,
                                            &rx_vlan_id, &tx_vlan_id,
                                            &li_node, &phy_interface,
                                            &phy_device);
        }

        if (adj_node->table() == agent_->cfg()->cfg_vm_interface_table()) {
            parent_vmi_node = adj_node;
        }

        if (strcmp(adj_node->table()->Typename(), BGP_AS_SERVICE_CONFIG_NAME) == 0) {
            bgp_as_a_service_node_list.push_back(adj_node);
        }

        if (strcmp(adj_node->table()->Typename(), BGP_ROUTER_CONFIG_NAME) == 0) {
            bgp_router_node_list.push_back(adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_vm_port_bridge_domain_table()) {
            BuildBridgeDomainTable(agent_, data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_port_tuple_table()) {
            if (!service_intf_type.empty()) {
                BuildSiOtherVmi(agent_, data, adj_node, service_intf_type);
            }
            BuildVmiSiMode(agent_, data, adj_node);
        }

        if (strcmp(adj_node->table()->Typename(), LOGICAL_ROUTER_NAME) == 0) {
            if (BuildLogicalRouterData(agent_, data, node)) {
                lr_node = adj_node;
            }
        }

        if (strcmp(adj_node->table()->Typename(),
                VIRTUAL_PORT_GROUP_CONFIG_NAME) == 0) {
            BuildInterfaceConfigurationDataFromVpg(agent(), adj_node,
                                                   &vpg_node,
                                                   &phy_interface,
                                                   &phy_device);
        }
    }

    //Fill HBS data
    FillHbsInfo(data, vn_node);

    // Fill IGMP data
    ReadIgmpConfig(agent(), vn_node, cfg, data);

    // Read flow control parameter on vmi
    ReadMaxFlowsConfig(vn_node, cfg, data);

    if (parent_vmi_node && data->vm_uuid_ == nil_uuid()) {
        IFMapAgentTable *vmi_table = static_cast<IFMapAgentTable *>
                                    (parent_vmi_node->table());
        DBGraph *vmi_graph = vmi_table->GetGraph();
        //iterate through links for paremt VMI for VM
        for (DBGraphVertex::adjacency_iterator vmi_iter = parent_vmi_node->begin(vmi_graph);
             vmi_iter != parent_vmi_node->end(vmi_graph); ++vmi_iter) {

            IFMapNode *vm_node = static_cast<IFMapNode *>(vmi_iter.operator->());
            if (agent_->config_manager()->SkipNode(vm_node, agent_->cfg()->cfg_vm_table())) {
                continue;
            }
            BuildVm(data, vm_node, u, &vm_list);
            break;
        }
    }


    agent_->oper_db()->bgp_as_a_service()->ProcessConfig
        (data->vrf_name_, bgp_router_node_list, bgp_as_a_service_node_list, u);
    UpdateAttributes(agent_, data);
    BuildFatFlowTable(agent_, u, data, node);

    // Get DHCP enable flag from subnet
    if (vn_node && data->addr_.to_ulong()) {
        ReadDhcpEnable(agent_, data, vn_node);
    }

    PhysicalRouter *prouter = NULL;
    // Build parent for the virtual-machine-interface
    prouter = BuildParentInfo(agent_, cfg, node, li_node, vpg_node,
                              data, parent_vmi_node, &phy_interface,
                              &phy_device);
    BuildEcmpHashingIncludeFields(cfg, vn_node, data);

    // Compare and log any mismatch in vm/vn between config and port-subscribe
    PortSubscribeEntryPtr subscribe_entry;
    PortIpcHandler *pih =  agent_->port_ipc_handler();
    if (pih) {
        subscribe_entry =
            pih->port_subscribe_table()->Get(u, data->vm_uuid_, data->vn_uuid_);
        CompareVnVm(u, data, subscribe_entry.get());
        pih->port_subscribe_table()->HandleVmiIfnodeAdd(u, data);
    }

    // Compute device-type and vmi-type for the interface
    ComputeTypeInfo(agent_, data, subscribe_entry.get(), prouter, node,
                    li_node, lr_node);

    if (cfg->display_name() == agent_->vhost_interface_name()) {
        data->CopyVhostData(agent());
        agent_->set_vhost_disable_policy(cfg->disable_policy());
        vmi_uuid = nil_uuid();
    }

    InterfaceKey *key = NULL;
    if (cfg->display_name() == agent_->vhost_interface_name()) {
        key = new VmInterfaceKey(AgentKey::RESYNC, vmi_uuid, cfg->display_name());
    } else if (data->device_type_ == VmInterface::VM_ON_TAP ||
                    data->device_type_ == VmInterface::DEVICE_TYPE_INVALID) {
        key = new VmInterfaceKey(AgentKey::RESYNC, u, "");
    } else {
        key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, vmi_uuid,
                cfg->display_name());
    }

    if (data->device_type_ != VmInterface::DEVICE_TYPE_INVALID) {
        AddVmiToVmiType(u, data->device_type_);
    }

    CopyTag(data, vmi_list, vm_list, vn_list, project_list);

    if (data->device_type_ == VmInterface::REMOTE_VM_VLAN_ON_VMI &&
        (rx_vlan_id == VmInterface::kInvalidVlanId ||
         tx_vlan_id == VmInterface::kInvalidVlanId)) {
         req.key.reset(key);
         req.data.reset(data);
        return false;
    }

    if (data->vm_mac_ == MacAddress::ZeroMac().ToString()) {
        req.key.reset(key);
        req.data.reset(data);
        return false;
    }

    req.key.reset(key);
    req.data.reset(data);

    boost::uuids::uuid dev = nil_uuid();
    if (prouter) {
        autogen::IdPermsType id_perms = prouter->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, dev);
    }
    UpdatePhysicalDeviceVnEntry(u, dev, data->vn_uuid_, vn_node);
    vmi_ifnode_to_req_++;

    return true;
}

static bool DeleteVmi(InterfaceTable *table, const uuid &u, DBRequest *req) {
    int type = table->GetVmiToVmiType(u);
    if (type <= (int)VmInterface::VMI_TYPE_INVALID)
        return false;

    table->DelVmiToVmiType(u);
    // Process delete based on VmiType
    if (type == VmInterface::INSTANCE) {
        // INSTANCE type are not added by config. We only do RESYNC
        req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req->key.reset(new VmInterfaceKey(AgentKey::RESYNC, u, ""));
        req->data.reset(new VmInterfaceConfigData(NULL, NULL));
        table->Enqueue(req);
        return false;
    } else {
        VmInterface::Delete(table, u, VmInterface::CONFIG);
        return false;
    }
}

bool InterfaceTable::VmiIFNodeToReq(IFMapNode *node, DBRequest &req,
                                    const boost::uuids::uuid &u) {
    // Handle object delete
    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        agent_->oper_db()->bgp_as_a_service()->DeleteVmInterface(u);
        DelPhysicalDeviceVnEntry(u);
        PortIpcHandler *pih =  agent_->port_ipc_handler();
        if (pih) {
            pih->port_subscribe_table()->HandleVmiIfnodeDelete(u);
        }
        return DeleteVmi(this, u, &req);
    }

    IFMapDependencyManager *dep = agent()->oper_db()->dependency_manager();
    IFMapNodeState *state = dep->IFMapNodeGet(node);
    IFMapDependencyManager::IFMapNodePtr vm_node_ref;
    if (!state) {
        vm_node_ref = dep->SetState(node);
        state = dep->IFMapNodeGet(node);
    }

    if (state->uuid().is_nil())
        state->set_uuid(u);

    agent()->config_manager()->AddVmiNode(node);
    return false;
}

bool InterfaceTable::VmiIFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {

    VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
        (node->GetObject());
    autogen::IdPermsType id_perms = cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}
