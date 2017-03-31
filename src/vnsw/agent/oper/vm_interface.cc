/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <net/ethernet.h>
#include <boost/uuid/uuid_io.hpp>

#include "base/logging.h"
#include "db/db.h"
#include "db/db_entry.h"
#include "db/db_table.h"
#include "ifmap/ifmap_node.h"
#include "net/address_util.h"

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <cmn/agent.h>
#include <init/agent_param.h>
#include <oper/operdb_init.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/config_manager.h>
#include <oper/route_common.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/metadata_ip.h>
#include <oper/interface_common.h>
#include <oper/health_check.h>
#include <oper/vrf_assign.h>
#include <oper/vxlan.h>
#include <oper/oper_dhcp_options.h>
#include <oper/inet_unicast_route.h>
#include <oper/physical_device_vn.h>
#include <oper/ecmp_load_balance.h>
#include <oper/global_vrouter.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/qos_config.h>

#include <vnc_cfg_types.h>
#include <oper/agent_sandesh.h>
#include <oper/sg.h>
#include <oper/bgp_as_service.h>
#include <bgp_schema_types.h>
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include <filter/acl.h>

using namespace std;
using namespace boost::uuids;
using namespace autogen;

VmInterface::VmInterface(const boost::uuids::uuid &uuid) :
    Interface(Interface::VM_INTERFACE, uuid, "", NULL), vm_(NULL, this),
    vn_(NULL), primary_ip_addr_(0), mdata_ip_(NULL), subnet_bcast_addr_(0),
    primary_ip6_addr_(), vm_mac_(MacAddress::kZeroMac), policy_enabled_(false),
    mirror_entry_(NULL), mirror_direction_(MIRROR_RX_TX), cfg_name_(""),
    fabric_port_(true), need_linklocal_ip_(false), drop_new_flows_(false),
    dhcp_enable_(true), do_dhcp_relay_(false), vm_name_(),
    vm_project_uuid_(nil_uuid()), vxlan_id_(0), bridging_(false),
    layer3_forwarding_(true), flood_unknown_unicast_(false),
    mac_set_(false), ecmp_(false), ecmp6_(false), disable_policy_(false),
    tx_vlan_id_(kInvalidVlanId), rx_vlan_id_(kInvalidVlanId), parent_(NULL, this),
    local_preference_(VmInterface::INVALID), oper_dhcp_options_(),
    sg_list_(), floating_ip_list_(), alias_ip_list_(), service_vlan_list_(),
    static_route_list_(), allowed_address_pair_list_(), fat_flow_list_(),
    vrf_assign_rule_list_(), vrf_assign_acl_(NULL), vm_ip_service_addr_(0),
    device_type_(VmInterface::DEVICE_TYPE_INVALID),
    vmi_type_(VmInterface::VMI_TYPE_INVALID),
    configurer_(0), subnet_(0), subnet_plen_(0), ethernet_tag_(0),
    logical_interface_(nil_uuid()), nova_ip_addr_(0), nova_ip6_addr_(),
    dhcp_addr_(0), metadata_ip_map_(), hc_instance_set_(),
    ecmp_load_balance_(), service_health_check_ip_(), is_vn_qos_config_(false) {
    metadata_ip_active_ = false;
    metadata_l2_active_ = false;
    ipv4_active_ = false;
    ipv6_active_ = false;
    l2_active_ = false;
    l3_interface_nh_policy_.reset();
    l2_interface_nh_policy_.reset();
    l3_interface_nh_no_policy_.reset();
    l2_interface_nh_no_policy_.reset();
}

VmInterface::VmInterface(const boost::uuids::uuid &uuid,
                         const std::string &name,
                         const Ip4Address &addr, const MacAddress &mac,
                         const std::string &vm_name,
                         const boost::uuids::uuid &vm_project_uuid,
                         uint16_t tx_vlan_id, uint16_t rx_vlan_id,
                         Interface *parent, const Ip6Address &a6,
                         DeviceType device_type, VmiType vmi_type) :
    Interface(Interface::VM_INTERFACE, uuid, name, NULL), vm_(NULL, this),
    vn_(NULL), primary_ip_addr_(addr), mdata_ip_(NULL), subnet_bcast_addr_(0),
    primary_ip6_addr_(a6), vm_mac_(mac), policy_enabled_(false),
    mirror_entry_(NULL), mirror_direction_(MIRROR_RX_TX), cfg_name_(""),
    fabric_port_(true), need_linklocal_ip_(false), drop_new_flows_(false),
    dhcp_enable_(true), do_dhcp_relay_(false), vm_name_(vm_name),
    vm_project_uuid_(vm_project_uuid), vxlan_id_(0),
    bridging_(false), layer3_forwarding_(true),
    flood_unknown_unicast_(false), mac_set_(false),
    ecmp_(false), ecmp6_(false), disable_policy_(false),
    tx_vlan_id_(tx_vlan_id), rx_vlan_id_(rx_vlan_id), parent_(parent, this),
    local_preference_(VmInterface::INVALID), oper_dhcp_options_(),
    sg_list_(), floating_ip_list_(), alias_ip_list_(), service_vlan_list_(),
    static_route_list_(), allowed_address_pair_list_(), vrf_assign_rule_list_(),
    vrf_assign_acl_(NULL), device_type_(device_type),
    vmi_type_(vmi_type), configurer_(0), subnet_(0),
    subnet_plen_(0), ethernet_tag_(0), logical_interface_(nil_uuid()),
    nova_ip_addr_(0), nova_ip6_addr_(), dhcp_addr_(0), metadata_ip_map_(),
    hc_instance_set_(), service_health_check_ip_(), is_vn_qos_config_(false) {
    metadata_ip_active_ = false;
    metadata_l2_active_ = false;
    ipv4_active_ = false;
    ipv6_active_ = false;
    l2_active_ = false;
}

VmInterface::~VmInterface() {
    mdata_ip_.reset(NULL);
    assert(metadata_ip_map_.empty());
    assert(hc_instance_set_.empty());
}

bool VmInterface::CmpInterface(const DBEntry &rhs) const {
    const VmInterface &intf=static_cast<const VmInterface &>(rhs);
    return uuid_ < intf.uuid_;
}

// Build one Floating IP entry for a virtual-machine-interface
static void BuildFloatingIpList(Agent *agent, VmInterfaceConfigData *data,
                                IFMapNode *node) {
    ConfigManager *cfg_manager= agent->config_manager();
    if (cfg_manager->SkipNode(node)) {
        return;
    }

    // Find VRF for the floating-ip. Following path in graphs leads to VRF
    // virtual-machine-port <-> floating-ip <-> floating-ip-pool 
    // <-> virtual-network <-> routing-instance
    IFMapAgentTable *fip_table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *fip_graph = fip_table->GetGraph();

    // Iterate thru links for floating-ip looking for floating-ip-pool node
    for (DBGraphVertex::adjacency_iterator fip_iter = node->begin(fip_graph);
         fip_iter != node->end(fip_graph); ++fip_iter) {
        IFMapNode *pool_node = static_cast<IFMapNode *>(fip_iter.operator->());
        if (cfg_manager->SkipNode
            (pool_node, agent->cfg()->cfg_floatingip_pool_table())) {
            continue;
        }

        // Iterate thru links for floating-ip-pool looking for virtual-network
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
                RoutingInstance *ri = static_cast<RoutingInstance *>(vrf_node->GetObject());
                if(!(ri->is_default())) {
                    continue;
                }
                FloatingIp *fip = static_cast<FloatingIp *>(node->GetObject());
                assert(fip != NULL);
                LOG(DEBUG, "Add FloatingIP <" << fip->address() << ":" <<
                    vrf_node->name() << "> to interface " << node->name());

                boost::system::error_code ec;
                IpAddress addr = IpAddress::from_string(fip->address(), ec);
                if (ec.value() != 0) {
                    LOG(ERROR, "Error decoding Floating IP address "
                        << fip->address());
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
                            string msg = "Invalid fixed-ip " +
                                fip->fixed_ip_address() + " for FloatingIP " +
                                fip->address();
                            LOG(ERROR, msg);
                            OPER_TRACE_ENTRY(Trace, agent->interface_table(),
                                             msg);
                            break;
                        }
                    }
                    data->floating_ip_list_.list_.insert
                        (VmInterface::FloatingIp(addr, vrf_node->name(),
                                                 vn_uuid, fixed_ip_addr));
                    if (addr.is_v4()) {
                        data->floating_ip_list_.v4_count_++;
                    } else {
                        data->floating_ip_list_.v6_count_++;
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
                RoutingInstance *ri = static_cast<RoutingInstance *>(vrf_node->GetObject());
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
                LOG(DEBUG, "Error decoding v4/v6 Static Route address " << it->prefix);
            }
        }

        IpAddress gw = IpAddress::from_string(it->next_hop, ec);
        if (ec) {
            gw = IpAddress::from_string("0.0.0.0", ec);
        }

        if (add) {
            data->static_route_list_.list_.insert
                (VmInterface::StaticRoute(data->vrf_name_, ip, plen, gw,
                              it->community_attributes.community_attribute));
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

// Get VLAN if linked to physical interface and router
static void BuildInterfaceConfigurationData(Agent *agent,
                                            VmInterfaceConfigData *data,
                                            IFMapNode *node,
                                            uint16_t *rx_vlan_id,
                                            uint16_t *tx_vlan_id,
                                            IFMapNode **phy_interface,
                                            IFMapNode **phy_device) {
    if (*phy_interface == NULL) {
        *phy_interface = agent->config_manager()->
            FindAdjacentIFMapNode(node, "physical-interface");
    }

    if (!(*phy_interface)) {
        *rx_vlan_id = VmInterface::kInvalidVlanId;
        *tx_vlan_id = VmInterface::kInvalidVlanId;
        return;
    }
    if (*phy_device == NULL) {
        *phy_device =
            agent->config_manager()->helper()->
            FindLink("physical-router-physical-interface",
                     *phy_interface);
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

        VmInterface::AllowedAddressPair entry(data->vrf_name_, ip, plen,
                                              ecmp, mac);
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
    if (rule.direction == "") {
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
        if (cfg_manager->SkipNode
            (vrf_node, agent->cfg()->cfg_vrf_table())) {
            continue;
        }

        if (rule.vlan_tag == 0 && rule.protocol == "" 
            && rule.service_chain_address == "") {
            data->vrf_name_ = vrf_node->name();
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
                addr = Ip4Address::from_string
                    (rule.service_chain_address, ec);
                if (ec.value() != 0) {
                    ip_set = false;
                    LOG(DEBUG, "Error decoding Service VLAN IP address "
                        << rule.service_chain_address);
                }
            }
            Ip6Address addr6;
            if (rule.ipv6_service_chain_address.size()) {
                addr6 = Ip6Address::from_string
                    (rule.ipv6_service_chain_address, ec);
                if (ec.value() != 0) {
                    ip6_set = false;
                    LOG(DEBUG, "Error decoding Service VLAN IP address "
                        << rule.ipv6_service_chain_address);
                }
            }
            if (!ip_set && !ip6_set) {
                break;
            }

            if (rule.vlan_tag > 4093) {
                LOG(DEBUG, "Invalid VLAN Tag " << rule.vlan_tag);
                break;
            }

            LOG(DEBUG, "Add Service VLAN entry <" << rule.vlan_tag << " : "
                << rule.service_chain_address << " : " << vrf_node->name());

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

static void BuildFatFlowTable(Agent *agent, VmInterfaceConfigData *data,
                              IFMapNode *node) {
    VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
                                       (node->GetObject());
    for (FatFlowProtocols::const_iterator it = cfg->fat_flow_protocols().begin();
            it != cfg->fat_flow_protocols().end(); it++) {
        uint16_t protocol = Agent::ProtocolStringToInt(it->protocol);
        data->fat_flow_list_.list_.insert(VmInterface::FatFlowEntry(protocol,
                                          it->port));
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
        data->service_health_check_ip_ = addr;
    }

    if (addr.is_v4()) {
        data->instance_ipv4_list_.list_.insert(
                VmInterface::InstanceIp(addr, Address::kMaxV4PrefixLen, ecmp,
                                        is_primary,
                                        ip->service_health_check_ip(),
                                        ip->local_ip(), tracking_ip));
    } else {
        data->instance_ipv6_list_.list_.insert(
                VmInterface::InstanceIp(addr, Address::kMaxV6PrefixLen, ecmp,
                                        is_primary,
                                        ip->service_health_check_ip(),
                                        ip->local_ip(), tracking_ip));
    }
}

static void BuildSgList(VmInterfaceConfigData *data, IFMapNode *node) {
    SecurityGroup *sg_cfg = static_cast<SecurityGroup *>
        (node->GetObject());
    assert(sg_cfg);
    autogen::IdPermsType id_perms = sg_cfg->id_perms();
    uint32_t sg_id = SgTable::kInvalidSgId;
    stringToInteger(sg_cfg->id(), sg_id);
    if (sg_id != SgTable::kInvalidSgId) {
        uuid sg_uuid = nil_uuid();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   sg_uuid);
        data->sg_list_.list_.insert
            (VmInterface::SecurityGroupEntry(sg_uuid));
    }
}

static void BuildVn(VmInterfaceConfigData *data, IFMapNode *node,
                    const boost::uuids::uuid &u, CfgIntEntry *cfg_entry) {
    VirtualNetwork *vn = static_cast<VirtualNetwork *>
        (node->GetObject());
    assert(vn);
    autogen::IdPermsType id_perms = vn->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong,
               id_perms.uuid.uuid_lslong, data->vn_uuid_);
    if (cfg_entry && (cfg_entry->GetVnUuid() != data->vn_uuid_)) {
        IFMAP_ERROR(InterfaceConfiguration, 
                    "Virtual-network UUID mismatch for interface:",
                    UuidToString(u),
                    "configuration VN uuid",
                    UuidToString(data->vn_uuid_),
                    "compute VN uuid",
                    UuidToString(cfg_entry->GetVnUuid()));
    }
}

static void BuildQosConfig(VmInterfaceConfigData *data, IFMapNode *node) {
    autogen::QosConfig *qc = static_cast<autogen::QosConfig *>
        (node->GetObject());
    assert(qc);
    autogen::IdPermsType id_perms = qc->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong,
               id_perms.uuid.uuid_lslong, data->qos_config_uuid_);
}

static void BuildVm(VmInterfaceConfigData *data, IFMapNode *node,
                    const boost::uuids::uuid &u, CfgIntEntry *cfg_entry) {
    VirtualMachine *vm = static_cast<VirtualMachine *>
        (node->GetObject());
    assert(vm);

    autogen::IdPermsType id_perms = vm->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong,
               id_perms.uuid.uuid_lslong, data->vm_uuid_);
    if (cfg_entry && (cfg_entry->GetVmUuid() != data->vm_uuid_)) {
        IFMAP_ERROR(InterfaceConfiguration, 
                    "Virtual-machine UUID mismatch for interface:",
                    UuidToString(u),
                    "configuration VM UUID is",
                    UuidToString(data->vm_uuid_),
                    "compute VM uuid is",
                    UuidToString(cfg_entry->GetVmUuid()));
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
    if (!mirror_to.analyzer_name.empty()) {
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
        } else if (mirror_flag == MirrorEntryData::DynamicNH_Without_JuniperHdr) {
            agent->mirror_table()->AddMirrorEntry(mirror_to.analyzer_name,
                    mirror_to.routing_instance, agent->GetMirrorSourceIp(dip),
                    agent->mirror_port(), dip, dport, 0, mirror_flag,
                    MacAddress::FromString(mirror_to.analyzer_mac_address));
        } else if (mirror_flag == MirrorEntryData::StaticNH_Without_JuniperHdr) {
            IpAddress vtep_dip =
                IpAddress::from_string(mirror_to.static_nh_header.vtep_dst_ip_address, ec);
            if (ec.value() != 0) {
                return;
            }
            agent->mirror_table()->AddMirrorEntry(mirror_to.analyzer_name,
                    mirror_to.routing_instance, agent->GetMirrorSourceIp(dip),
                    agent->mirror_port(), vtep_dip, dport,
                    mirror_to.static_nh_header.vni, mirror_flag,
                    MacAddress::FromString(mirror_to.static_nh_header.vtep_dst_mac_address));
        } else {
            LOG(ERROR, "Mirror nh mode not supported");
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

// Check if VMI is a sub-interface. Sub-interface will have
// sub_interface_vlan_tag property set to non-zero
static bool IsVlanSubInterface(VirtualMachineInterface *cfg) {
    if (cfg->IsPropertySet(VirtualMachineInterface::PROPERTIES) == false)
        return false;

    if (cfg->properties().sub_interface_vlan_tag == 0)
        return false;

    return true;
}

// Builds parent for VMI (not to be confused with parent ifmap-node)
// Possible values are,
// - logical-interface : Incase of baremetals
// - virtual-machine-interface : We support virtual-machine-interface
//   sub-interfaces. In this case, another virtual-machine-interface itself
//   can be a parent
static PhysicalRouter *BuildParentInfo(Agent *agent,
                                       VmInterfaceConfigData *data,
                                       VirtualMachineInterface *cfg,
                                       IFMapNode *node,
                                       IFMapNode *logical_node,
                                       IFMapNode *parent_vmi_node,
                                       IFMapNode **phy_interface,
                                       IFMapNode **phy_device) {
    if (logical_node) {
        if ((*phy_interface) == NULL) {
            *phy_interface = agent->config_manager()->
                FindAdjacentIFMapNode(logical_node, "physical-interface");
        }
        agent->interface_table()->
           LogicalInterfaceIFNodeToUuid(logical_node, data->logical_interface_);
        // Find phyiscal-interface for the VMI
        if (*phy_interface) {
            data->physical_interface_ = (*phy_interface)->name();
            // Find vrouter for the physical interface
            if ((*phy_device) == NULL) {
                *phy_device = agent->config_manager()->helper()->
                    FindLink("physical-router-physical-interface",
                             (*phy_interface));
            }
        }
        if ((*phy_device) == NULL)
            return NULL;
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

static void BuildAttributes(Agent *agent, IFMapNode *node,
                            VirtualMachineInterface *cfg,
                            VmInterfaceConfigData *data) {
    //Extract the local preference
    if (cfg->IsPropertySet(VirtualMachineInterface::PROPERTIES)) {
        autogen::VirtualMachineInterfacePropertiesType prop = cfg->properties();
        //Service instance also would have VirtualMachineInterface
        //properties field set, pick up local preference
        //value only when it has been initialized to proper
        //value, if its 0, ignore the local preference
        if (prop.local_preference) {
            data->local_preference_ = VmInterface::LOW;
            if (prop.local_preference == VmInterface::HIGH) {
                data->local_preference_ = VmInterface::HIGH;
            }
        }
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
                            CfgIntEntry *cfg_entry, PhysicalRouter *prouter,
                            IFMapNode *node, IFMapNode *logical_node) {
    if (cfg_entry != NULL) {
        // Have got InstancePortAdd message. Treat it as VM_ON_TAP by default
        // TODO: Need to identify more cases here
        data->device_type_ = VmInterface::VM_ON_TAP;
        data->vmi_type_ = VmInterface::INSTANCE;
        return;
    }

    VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
                   (node->GetObject());
    const std::vector<KeyValuePair> &bindings  = cfg->bindings();
    for (std::vector<KeyValuePair>::const_iterator it = bindings.begin();
            it != bindings.end(); ++it) {
        KeyValuePair kvp = *it;
        if ((kvp.key == "vnic_type") && (kvp.value == "direct")) {
            data->device_type_ = VmInterface::VM_SRIOV;
            data->vmi_type_ = VmInterface::SRIOV;
            return;
        }
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
            if (agent->server_gateway_mode()) {
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

    return;
}

void VmInterface::SetConfigurer(VmInterface::Configurer type) {
    configurer_ |= (1 << type);
}

void VmInterface::ResetConfigurer(VmInterface::Configurer type) {
    configurer_ &= ~(1 << type);
}

bool VmInterface::IsConfigurerSet(VmInterface::Configurer type) {
    return ((configurer_ & (1 << type)) != 0);
}

bool VmInterface::IsFatFlow(uint8_t protocol, uint16_t port) const {
    if (fat_flow_list_.list_.find(FatFlowEntry(protocol, port)) !=
                fat_flow_list_.list_.end()) {
        return true;
    }
    return false;
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

bool InterfaceTable::VmiIFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) { 

    VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
        (node->GetObject());
    autogen::IdPermsType id_perms = cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

// Virtual Machine Interface is added or deleted into oper DB from Nova 
// messages. The Config notify is used only to change interface.
extern IFMapNode *vn_test_node;
extern IFMapNode *vmi_test_node;

bool InterfaceTable::VmiProcessConfig(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &u) {

    // Get interface UUID
    VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
        (node->GetObject());

    assert(cfg);
    // Handle object delete
    if (node->IsDeleted()) {
        return false;
    }

    assert(!u.is_nil());
    // Get the entry from Interface Config table
    CfgIntTable *cfg_table = agent_->interface_config_table();
    CfgIntKey cfg_key(u);
    CfgIntEntry *cfg_entry =
        static_cast <CfgIntEntry *>(cfg_table->Find(&cfg_key));

    // Update interface configuration
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    VmInterfaceConfigData *data = new VmInterfaceConfigData(agent(), NULL);
    data->SetIFMapNode(node);

    BuildAttributes(agent_, node, cfg, data);

    // Graph walk to get interface configuration
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    IFMapNode *vn_node = NULL;
    IFMapNode *li_node = NULL;
    IFMapNode *phy_interface = NULL;
    IFMapNode *phy_device = NULL;
    IFMapNode *parent_vmi_node = NULL;
    uint16_t rx_vlan_id = VmInterface::kInvalidVlanId;
    uint16_t tx_vlan_id = VmInterface::kInvalidVlanId;
    std::list<IFMapNode *> bgp_as_a_service_node_list;
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph()); 
         iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent_->config_manager()->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() == agent_->cfg()->cfg_sg_table()) {
            BuildSgList(data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_vn_table()) {
            vn_node = adj_node;
            BuildVn(data, adj_node, u, cfg_entry);
        }

        if (adj_node->table() == agent_->cfg()->cfg_qos_table()) {
            BuildQosConfig(data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_vm_table()) {
            BuildVm(data, adj_node, u, cfg_entry);
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
            li_node = adj_node;
            BuildInterfaceConfigurationData(agent(), data, adj_node,
                                            &rx_vlan_id, &tx_vlan_id,
                                            &phy_interface, &phy_device);
        }

        if (adj_node->table() == agent_->cfg()->cfg_vm_interface_table()) {
            parent_vmi_node = adj_node;
        }

        if (adj_node->table() == agent_->cfg()->cfg_logical_port_table()) {
            li_node = adj_node;
        }

        if (adj_node->table() == agent_->cfg()->cfg_vm_interface_table()) {
            parent_vmi_node = adj_node;
        }

        if (strcmp(adj_node->table()->Typename(), BGP_AS_SERVICE_CONFIG_NAME) == 0) {
            bgp_as_a_service_node_list.push_back(adj_node);
        }
    }

    agent_->oper_db()->bgp_as_a_service()->ProcessConfig(data->vrf_name_,
                                           bgp_as_a_service_node_list, u);
    UpdateAttributes(agent_, data);
    BuildFatFlowTable(agent_, data, node);

    // Get DHCP enable flag from subnet
    if (vn_node && data->addr_.to_ulong()) {
        ReadDhcpEnable(agent_, data, vn_node);
    }

    PhysicalRouter *prouter = NULL;
    // Build parent for the virtual-machine-interface
    prouter = BuildParentInfo(agent_, data, cfg, node, li_node,
                              parent_vmi_node, &phy_interface,
                              &phy_device);
    BuildEcmpHashingIncludeFields(cfg, vn_node, data);

    // Compute device-type and vmi-type for the interface
    ComputeTypeInfo(agent_, data, cfg_entry, prouter, node, li_node);

    InterfaceKey *key = NULL; 
    if (data->device_type_ == VmInterface::VM_ON_TAP ||
        data->device_type_ == VmInterface::DEVICE_TYPE_INVALID) {
        key = new VmInterfaceKey(AgentKey::RESYNC, u, "");
    } else {
        key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, u,
                                 cfg->display_name());
    }

    if (data->device_type_ != VmInterface::DEVICE_TYPE_INVALID) {
        AddVmiToVmiType(u, data->device_type_);
    }

    if (data->device_type_ == VmInterface::REMOTE_VM_VLAN_ON_VMI &&
        (rx_vlan_id == VmInterface::kInvalidVlanId ||
         tx_vlan_id == VmInterface::kInvalidVlanId)) {
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

bool InterfaceTable::VmiIFNodeToReq(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &u) {

    // Handle object delete
    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        agent_->oper_db()->bgp_as_a_service()->DeleteVmInterface(u);
        DelPhysicalDeviceVnEntry(u);
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

/////////////////////////////////////////////////////////////////////////////
// Routines to manage VmiToPhysicalDeviceVnTree
/////////////////////////////////////////////////////////////////////////////
VmiToPhysicalDeviceVnData::VmiToPhysicalDeviceVnData
(const boost::uuids::uuid &dev, const boost::uuids::uuid &vn) :
    dev_(dev), vn_(vn) {
}

VmiToPhysicalDeviceVnData::~VmiToPhysicalDeviceVnData() {
}

void InterfaceTable::UpdatePhysicalDeviceVnEntry(const boost::uuids::uuid &vmi,
                                                 boost::uuids::uuid &dev,
                                                 boost::uuids::uuid &vn,
                                                 IFMapNode *vn_node) {
    VmiToPhysicalDeviceVnTree::iterator iter =
        vmi_to_physical_device_vn_tree_.find(vmi);
    if (iter == vmi_to_physical_device_vn_tree_.end()) {
        vmi_to_physical_device_vn_tree_.insert
            (make_pair(vmi,VmiToPhysicalDeviceVnData(nil_uuid(), nil_uuid())));
        iter = vmi_to_physical_device_vn_tree_.find(vmi);
    }

    if (iter->second.dev_ != dev || iter->second.vn_ != vn) {
        agent()->physical_device_vn_table()->DeleteConfigEntry
            (vmi, iter->second.dev_, iter->second.vn_);
    }

    iter->second.dev_ = dev;
    iter->second.vn_ = vn;
    agent()->physical_device_vn_table()->AddConfigEntry(vmi, dev, vn);
}

void InterfaceTable::DelPhysicalDeviceVnEntry(const boost::uuids::uuid &vmi) {
    VmiToPhysicalDeviceVnTree::iterator iter =
        vmi_to_physical_device_vn_tree_.find(vmi);
    if (iter == vmi_to_physical_device_vn_tree_.end())
        return;

    agent()->physical_device_vn_table()->DeleteConfigEntry
        (vmi, iter->second.dev_, iter->second.vn_);
    vmi_to_physical_device_vn_tree_.erase(iter);
}

/////////////////////////////////////////////////////////////////////////////
// VM Port Key routines
/////////////////////////////////////////////////////////////////////////////
VmInterfaceKey::VmInterfaceKey(AgentKey::DBSubOperation sub_op,
                   const boost::uuids::uuid &uuid, const std::string &name) :
    InterfaceKey(sub_op, Interface::VM_INTERFACE, uuid, name, false) {
}

Interface *VmInterfaceKey::AllocEntry(const InterfaceTable *table) const {
    return new VmInterface(uuid_);
}

Interface *VmInterfaceKey::AllocEntry(const InterfaceTable *table,
                                      const InterfaceData *data) const {
    const VmInterfaceData *vm_data =
        static_cast<const VmInterfaceData *>(data);

    VmInterface *vmi = vm_data->OnAdd(table, this);
    return vmi;
}

InterfaceKey *VmInterfaceKey::Clone() const {
    return new VmInterfaceKey(*this);
}

/////////////////////////////////////////////////////////////////////////////
// VM Port Entry routines
/////////////////////////////////////////////////////////////////////////////
string VmInterface::ToString() const {
    return "VM-PORT <" + name() + ">";
}

DBEntryBase::KeyPtr VmInterface::GetDBRequestKey() const {
    InterfaceKey *key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, uuid_,
                                           name_);
    return DBEntryBase::KeyPtr(key);
}

const Peer *VmInterface::peer() const { 
    return peer_.get();
}

bool VmInterface::OnChange(VmInterfaceData *data) {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    return Resync(table, data);
}

// When VMInterface is added from Config (sub-interface, gateway interface etc.)
// the RESYNC is not called and some of the config like VN and VRF are not
// applied on the interface (See Add() API above). Force change to ensure
// RESYNC is called
void VmInterface::PostAdd() {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    DBRequest req;
    IFMapNode *node = ifmap_node();
    if (node == NULL)
        return;

    boost::uuids::uuid u;
    table->IFNodeToUuid(node, u);
    if (table->IFNodeToReq(ifmap_node(), req, u) == true) {
        table->Process(req);
    }
}

// Handle RESYNC DB Request. Handles multiple sub-types,
// - CONFIG : RESYNC from config message
// - IP_ADDR: RESYNC due to learning IP from DHCP
// - MIRROR : RESYNC due to change in mirror config
bool VmInterface::Resync(const InterfaceTable *table,
                         const VmInterfaceData *data) {
    bool ret = false;

    // Copy old values used to update config below
    bool old_ipv4_active = ipv4_active_;
    bool old_ipv6_active = ipv6_active_;
    bool old_l2_active = l2_active_;
    bool old_policy = policy_enabled_;
    VrfEntryRef old_vrf = vrf_;
    Ip4Address old_addr = primary_ip_addr_;
    Ip6Address old_v6_addr = primary_ip6_addr_;
    bool old_need_linklocal_ip = need_linklocal_ip_;
    bool force_update = false;
    Ip4Address old_subnet = subnet_;
    uint8_t  old_subnet_plen = subnet_plen_;
    int old_ethernet_tag = ethernet_tag_;
    bool old_dhcp_enable = dhcp_enable_;
    bool old_layer3_forwarding = layer3_forwarding_;
    Ip4Address old_dhcp_addr = dhcp_addr_;
    bool old_metadata_ip_active = metadata_ip_active_;
    bool old_metadata_l2_active = metadata_l2_active_;
    bool old_bridging = bridging_;

    if (data) {
        ret = data->OnResync(table, this, &force_update);
    }

    metadata_ip_active_ = IsMetaDataIPActive();
    metadata_l2_active_ = IsMetaDataL2Active();
    ipv4_active_ = IsIpv4Active();
    ipv6_active_ = IsIpv6Active();
    l2_active_ = IsL2Active();

    if (metadata_ip_active_ != old_metadata_ip_active) {
        ret = true;
    }

    if (metadata_l2_active_ != old_metadata_l2_active) {
        ret = true;
    }

    if (ipv4_active_ != old_ipv4_active) {
        InterfaceTable *intf_table = static_cast<InterfaceTable *>(get_table());
        if (ipv4_active_)
            intf_table->incr_active_vmi_count();
        else
            intf_table->decr_active_vmi_count();
        ret = true;
    }

    if (ipv6_active_ != old_ipv6_active) {
        ret = true;
    }

    if (l2_active_ != old_l2_active) {
        ret = true;
    }

    policy_enabled_ = PolicyEnabled();
    if (policy_enabled_ != old_policy) {
        ret = true;
    }

    // Apply config based on old and new values
    ApplyConfig(old_ipv4_active, old_l2_active, old_policy, old_vrf.get(), 
                old_addr, old_ethernet_tag, old_need_linklocal_ip,
                old_ipv6_active, old_v6_addr, old_subnet, old_subnet_plen,
                old_dhcp_enable, old_layer3_forwarding, force_update,
                old_dhcp_addr, old_metadata_ip_active, old_bridging);

    return ret;
}

void VmInterface::Add() {
    peer_.reset(new LocalVmPortPeer(LOCAL_VM_PORT_PEER_NAME, id_));
}

bool VmInterface::Delete(const DBRequest *req) {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    const VmInterfaceData *vm_data = static_cast<const VmInterfaceData *>
        (req->data.get());
    vm_data->OnDelete(table, this);
    if (configurer_) {
        return false;
    }
    table->DeleteDhcpSnoopEntry(name_);
    return true;
}

void VmInterface::UpdateL3MetadataIp(VrfEntry *old_vrf, bool force_update,
                                     bool policy_change,
                                     bool old_metadata_ip_active) {
    assert(metadata_ip_active_);
    UpdateL3TunnelId(force_update, policy_change);
    UpdateMetadataRoute(old_metadata_ip_active, old_vrf);
}

void VmInterface::DeleteL3MetadataIp(VrfEntry *old_vrf, bool force_update,
                                     bool policy_change,
                                     bool old_metadata_ip_active,
                                     bool old_need_linklocal_ip) {
    assert(!metadata_ip_active_);
    DeleteL3TunnelId();
    DeleteMetadataRoute(old_metadata_ip_active, old_vrf,
                        old_need_linklocal_ip);
}

void VmInterface::UpdateL3(bool old_ipv4_active, VrfEntry *old_vrf,
                           const Ip4Address &old_addr, int old_ethernet_tag,
                           bool force_update, bool policy_change,
                           bool old_ipv6_active,
                           const Ip6Address &old_v6_addr,
                           const Ip4Address &old_subnet,
                           const uint8_t old_subnet_plen,
                           const Ip4Address &old_dhcp_addr) {
    if (ipv4_active_) {
        if (do_dhcp_relay_) {
            UpdateIpv4InterfaceRoute(old_ipv4_active,
                                     force_update||policy_change,
                                     old_vrf, old_dhcp_addr);
        }
        UpdateIpv4InstanceIp(force_update, policy_change, false,
                             old_ethernet_tag, old_vrf);
        UpdateFloatingIp(force_update, policy_change, false, old_ethernet_tag);
        UpdateAliasIp(force_update, policy_change);
        UpdateResolveRoute(old_ipv4_active, force_update, policy_change, 
                           old_vrf, old_subnet, old_subnet_plen);
    }
    if (ipv6_active_) {
        UpdateIpv6InstanceIp(force_update, policy_change, false, old_ethernet_tag);
    }
    UpdateServiceVlan(force_update, policy_change, old_ipv4_active,
                      old_ipv6_active);
    UpdateAllowedAddressPair(force_update, policy_change, false, false, false);
    UpdateVrfAssignRule();
    UpdateStaticRoute(force_update||policy_change);
}

void VmInterface::DeleteL3(bool old_ipv4_active, VrfEntry *old_vrf,
                           const Ip4Address &old_addr,
                           bool old_need_linklocal_ip, bool old_ipv6_active,
                           const Ip6Address &old_v6_addr,
                           const Ip4Address &old_subnet,
                           const uint8_t old_subnet_plen,
                           int old_ethernet_tag,
                           const Ip4Address &old_dhcp_addr,
                           bool force_update) {
    // trigger delete instance ip v4/v6 unconditionally, it internally
    // handles cases where delete is already processed
    DeleteIpv4InstanceIp(false, old_ethernet_tag, old_vrf, force_update);
    DeleteIpv4InstanceIp(true, old_ethernet_tag, old_vrf, force_update);
    DeleteIpv6InstanceIp(false, old_ethernet_tag, old_vrf, force_update);
    DeleteIpv6InstanceIp(true, old_ethernet_tag, old_vrf, force_update);

    if (old_ipv4_active) {
        if (old_dhcp_addr != Ip4Address(0)) {
            DeleteIpv4InterfaceRoute(old_vrf, old_dhcp_addr);
        }
    }
    // Process following deletes only if any of old ipv4 or ipv6
    // was active
    if ((old_ipv4_active || old_ipv6_active)) {
        DeleteFloatingIp(false, old_ethernet_tag);
        DeleteAliasIp();
        DeleteServiceVlan();
        DeleteStaticRoute();
        DeleteAllowedAddressPair(false);
        DeleteVrfAssignRule();
        DeleteResolveRoute(old_vrf, old_subnet, old_subnet_plen);
    }

}

void VmInterface::UpdateVxLan() {
    int new_vxlan_id = vn_.get() ? vn_->GetVxLanId() : 0;
    if (l2_active_ && ((vxlan_id_ == 0) ||
                       (vxlan_id_ != new_vxlan_id))) {
        vxlan_id_ = new_vxlan_id;
    }
    ethernet_tag_ = IsVxlanMode() ? vxlan_id_ : 0;
}

void VmInterface::AddL2ReceiveRoute(bool old_bridging) {
    if (BridgingActivated(old_bridging)) {
        InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
        Agent *agent = table->agent();
        BridgeAgentRouteTable *l2_table = static_cast<BridgeAgentRouteTable *>(
                vrf_->GetRouteTable(Agent::BRIDGE));

        l2_table->AddBridgeReceiveRoute(peer_.get(), vrf_->GetName(), 0,
                                        GetVifMac(agent), vn_->GetName());
    }
}

void VmInterface::UpdateBridgeRoutes(bool old_bridging, VrfEntry *old_vrf,
                                     int old_ethernet_tag,
                                     bool force_update, bool policy_change,
                                     const Ip4Address &old_v4_addr,
                                     const Ip6Address &old_v6_addr,
                                     bool old_layer3_forwarding) {
    if (device_type() == VmInterface::TOR ||
        device_type() == VmInterface::DEVICE_TYPE_INVALID)
        return;

    UpdateL2InterfaceRoute(old_bridging, force_update, old_vrf, Ip4Address(),
                           Ip6Address(), old_ethernet_tag,
                           old_layer3_forwarding, policy_change,
                           Ip4Address(), Ip6Address(),
                           vm_mac_,
                           Ip4Address(0));
    UpdateIpv4InstanceIp(force_update, policy_change, true, old_ethernet_tag,
                         old_vrf);
    UpdateIpv6InstanceIp(force_update, policy_change, true, old_ethernet_tag);
    UpdateFloatingIp(force_update, policy_change, true, old_ethernet_tag);
    UpdateAllowedAddressPair(force_update, policy_change, true, old_bridging,
                             old_layer3_forwarding);
    //If the interface is Gateway we need to add a receive route,
    //such the packet gets routed. Bridging on gateway
    //interface is not supported
    if ((vmi_type() == GATEWAY || vmi_type() == REMOTE_VM) &&
        BridgingActivated(old_bridging)) {
        AddL2ReceiveRoute(old_bridging);
    }
}

void VmInterface::UpdateL2(bool old_l2_active, bool policy_change) {
    if (device_type() == VmInterface::TOR ||
        device_type() == VmInterface::DEVICE_TYPE_INVALID)
        return;

    UpdateVxLan();
    //Update label only if new entry is to be created, so
    //no force update on same.
    UpdateL2TunnelId(false, policy_change);
}

void VmInterface::DeleteBridgeRoutes(bool old_bridging, VrfEntry *old_vrf,
                                     int old_ethernet_tag,
                                     const Ip4Address &old_v4_addr,
                                     const Ip6Address &old_v6_addr,
                                     bool old_layer3_forwarding,
                                     bool force_update) {
    DeleteIpv4InstanceIp(true, old_ethernet_tag, old_vrf, force_update);
    DeleteIpv6InstanceIp(true, old_ethernet_tag, old_vrf, force_update);
    if (old_bridging) {
        DeleteL2InterfaceRoute(old_bridging, old_vrf, Ip4Address(0),
                               Ip6Address(), old_ethernet_tag,
                               vm_mac_);
        DeleteFloatingIp(true, old_ethernet_tag);
        DeleteL2ReceiveRoute(old_vrf, old_bridging);
        DeleteAllowedAddressPair(true);
    }
}

void VmInterface::DeleteL2(bool old_l2_active) {
    DeleteL2TunnelId();
}

const MacAddress& VmInterface::GetVifMac(const Agent *agent) const {
    if (parent()) {
        if (device_type_ == VM_VLAN_ON_VMI) {
            const VmInterface *vmi =
                static_cast<const VmInterface *>(parent_.get());
            return vmi->GetVifMac(agent);
        }
        return parent()->mac();
    } else {
        return agent->vrrp_mac();
    }
}

bool VmInterface::InstallBridgeRoutes() const {
    return (l2_active_ & is_hc_active_);
}

void VmInterface::ApplyConfigCommon(const VrfEntry *old_vrf,
                                    bool old_l2_active,
                                    bool old_dhcp_enable) {
    //DHCP MAC IP binding
    ApplyMacVmBindingConfig(old_vrf, old_l2_active,  old_dhcp_enable);
    //Security Group update
    if (IsActive()) {
        UpdateSecurityGroup();
        UpdateFatFlow();
    } else {
        DeleteSecurityGroup();
        DeleteFatFlow();
    }
}

/*
 * L2 nexthops:
 * These L2 nexthop are used by multicast and bridge.
 * Presence of multicast forces it to be present in
 * ipv4 mode(l3-only).
 *
 * L3 nexthops:
 * Also creates L3 interface NH, if layer3_forwarding is set.
 * It does not depend on oper state of ip forwarding.
 * Needed as health check can disable oper ip_active and will result in flow key
 * pointing to L2 interface NH. This has to be avoided as interface still points
 * to l3 nh and flow should use same. For this fix it is also required that l3
 * i/f nh is also created on seeing config and not oper state of l3. Reason
 * being if vmi(irrespective of health check) is coming up and transitioning
 * from ipv4_inactive to ip4_active and during this transition a flow is added
 * then flow_key in vmi will return null because l3 config is set and interface
 * nh not created yet.
 */
void VmInterface::UpdateCommonNextHop() {
    UpdateL2NextHop();
    UpdateL3NextHop();
}

void VmInterface::DeleteCommonNextHop() {
    DeleteL2NextHop();
    DeleteL3NextHop();
}

void VmInterface::ApplyMacVmBindingConfig(const VrfEntry *old_vrf,
                                          bool old_active,
                                          bool old_dhcp_enable) {
    if (!IsActive() || old_vrf != vrf()) {
        DeleteMacVmBinding(old_vrf);
    }

    //Update DHCP and DNS flag in Interface Class.
    if (dhcp_enable_) {
        dhcp_enabled_ = true;
        dns_enabled_ = true;
    } else {
        dhcp_enabled_ = false;
        dns_enabled_ = false;
    }

    if (!IsActive())
        return;

    UpdateMacVmBinding();
}

// Apply the latest configuration
void VmInterface::ApplyConfig(bool old_ipv4_active, bool old_l2_active,
                              bool old_policy, VrfEntry *old_vrf,
                              const Ip4Address &old_addr,
                              int old_ethernet_tag, bool old_need_linklocal_ip,
                              bool old_ipv6_active,
                              const Ip6Address &old_v6_addr,
                              const Ip4Address &old_subnet,
                              uint8_t old_subnet_plen,
                              bool old_dhcp_enable,
                              bool old_layer3_forwarding,
                              bool force_update,
                              const Ip4Address &old_dhcp_addr,
                              bool old_metadata_ip_active,
                              bool old_bridging) {

    //For SRIOV we dont generate any things lile l2 routes, l3 routes
    //etc
    if (device_type_ == VmInterface::VM_SRIOV) {
        return;
    }

    ApplyConfigCommon(old_vrf, old_l2_active, old_dhcp_enable);
    //Need not apply config for TOR VMI as it is more of an inidicative
    //interface. No route addition or NH addition happens for this interface.
    //Also, when parent is not updated for a non-Nova interface, device type
    //remains invalid.
    if ((device_type_ == VmInterface::TOR ||
         device_type_ == VmInterface::DEVICE_TYPE_INVALID) &&
        (old_subnet.is_unspecified() && old_subnet_plen == 0)) {
        return;
    }

    bool policy_change = (policy_enabled_ != old_policy);

    if (vrf_ && vmi_type() == GATEWAY) {
        vrf_->CreateTableLabel();
    }

    //Update common prameters
    if (IsActive()) {
        UpdateCommonNextHop();
    }
    // Add/Update L3 Metadata
    if (metadata_ip_active_) {
        UpdateL3MetadataIp(old_vrf, force_update, policy_change,
                           old_metadata_ip_active);
    }

    // Add/Del/Update L3 
    if ((ipv4_active_ || ipv6_active_) && layer3_forwarding_) {
        UpdateL3(old_ipv4_active, old_vrf, old_addr, old_ethernet_tag, force_update,
                 policy_change, old_ipv6_active, old_v6_addr,
                 old_subnet, old_subnet_plen, old_dhcp_addr);
    } else {
        DeleteL3(old_ipv4_active, old_vrf, old_addr, old_need_linklocal_ip,
                 old_ipv6_active, old_v6_addr,
                 old_subnet, old_subnet_plen, old_ethernet_tag, old_dhcp_addr,
                 (force_update || policy_change));
    }

    // Del L3 Metadata after deleting L3
    if (!metadata_ip_active_ && old_metadata_ip_active) {
        DeleteL3MetadataIp(old_vrf, force_update, policy_change,
                           old_metadata_ip_active, old_need_linklocal_ip);
    }

    // Add/Update L2 
    if (l2_active_) {
        UpdateL2(old_l2_active, policy_change);
    }

    if (InstallBridgeRoutes()) {
        UpdateBridgeRoutes(old_l2_active, old_vrf, old_ethernet_tag,
                           force_update, policy_change, old_addr, old_v6_addr,
                           old_layer3_forwarding);
    } else {
        DeleteBridgeRoutes(old_l2_active, old_vrf, old_ethernet_tag,
                           old_addr, old_v6_addr, old_layer3_forwarding,
                           (force_update || policy_change));
    }

    // Delete L2
    if (!l2_active_ && old_l2_active) {
        DeleteL2(old_l2_active);
    }

    UpdateFlowKeyNextHop();

    // Remove floating-ip entries marked for deletion
    CleanupFloatingIpList();

    // Remove Alias-ip entries marked for deletion
    CleanupAliasIpList();

    if (!IsActive()) {
        DeleteCommonNextHop();
    }
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    if (old_l2_active != l2_active_) {
        if (l2_active_) {
            SendTrace(table, ACTIVATED_L2);
        } else {
            SendTrace(table, DEACTIVATED_L2);
        }
    }

    if (old_ipv4_active != ipv4_active_) {
        if (ipv4_active_) {
            SendTrace(table, ACTIVATED_IPV4);
        } else {
            SendTrace(table, DEACTIVATED_IPV4);
        }
    }

    if (old_ipv6_active != ipv6_active_) {
        if (ipv6_active_) {
            SendTrace(table, ACTIVATED_IPV6);
        } else {
            SendTrace(table, DEACTIVATED_IPV6);
        }
    }
}

bool VmInterface::CopyIpAddress(Ip4Address &addr) {
    bool ret = false;
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());

    // Support DHCP relay for fabric-ports if IP address is not configured
    do_dhcp_relay_ = (fabric_port_ && addr.to_ulong() == 0 && vrf() &&
                      vrf()->GetName() == table->agent()->fabric_vrf_name());

    if (do_dhcp_relay_) {
        // Set config_seen flag on DHCP SNoop entry
        table->DhcpSnoopSetConfigSeen(name_);
        // IP Address not know. Get DHCP Snoop entry.
        // Also sets the config_seen_ flag for DHCP Snoop entry
        addr = table->GetDhcpSnoopEntry(name_);
        dhcp_addr_ = addr;
    }

    // Retain the old if new IP could not be got
    if (addr.to_ulong() == 0) {
        addr = primary_ip_addr_;
    }

    if (primary_ip_addr_ != addr) {
        primary_ip_addr_ = addr;
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceConfigData routines
/////////////////////////////////////////////////////////////////////////////
VmInterfaceConfigData::VmInterfaceConfigData(Agent *agent, IFMapNode *node) :
    VmInterfaceData(agent, node, CONFIG, Interface::TRANSPORT_INVALID),
    addr_(0), ip6_addr_(), vm_mac_(""),
    cfg_name_(""), vm_uuid_(), vm_name_(), vn_uuid_(), vrf_name_(""),
    fabric_port_(true), need_linklocal_ip_(false), bridging_(true),
    layer3_forwarding_(true), mirror_enable_(false), ecmp_(false),
    ecmp6_(false), dhcp_enable_(true), admin_state_(true),
    disable_policy_(false), analyzer_name_(""),
    local_preference_(VmInterface::INVALID), oper_dhcp_options_(),
    mirror_direction_(Interface::UNKNOWN), sg_list_(),
    floating_ip_list_(), alias_ip_list_(), service_vlan_list_(),
    static_route_list_(), allowed_address_pair_list_(),
    device_type_(VmInterface::DEVICE_TYPE_INVALID),
    vmi_type_(VmInterface::VMI_TYPE_INVALID),
    physical_interface_(""), parent_vmi_(), subnet_(0), subnet_plen_(0),
    rx_vlan_id_(VmInterface::kInvalidVlanId),
    tx_vlan_id_(VmInterface::kInvalidVlanId),
    logical_interface_(nil_uuid()), ecmp_load_balance_(),
    service_health_check_ip_(), service_ip_(0),
    service_ip_ecmp_(false), service_ip6_(), service_ip_ecmp6_(false), 
    qos_config_uuid_(){
}

VmInterface *VmInterfaceConfigData::OnAdd(const InterfaceTable *table,
                                          const VmInterfaceKey *key) const {

    Interface *parent = NULL;
    if (device_type_ == VmInterface::REMOTE_VM_VLAN_ON_VMI) {
        if (rx_vlan_id_ == VmInterface::kInvalidVlanId ||
            tx_vlan_id_ == VmInterface::kInvalidVlanId)
            return NULL;

        if (physical_interface_ != Agent::NullString()) {
            PhysicalInterfaceKey key_1(physical_interface_);
            parent = static_cast<Interface *>
                (table->agent()->interface_table()->FindActiveEntry(&key_1));
        }

        if (parent == NULL)
            return NULL;
    }

    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString(vm_mac_, &ec);
    if (ec.value() != 0) {
        mac.Zero();
    }

    VmInterface *vmi =
        new VmInterface(key->uuid_, key->name_, addr_, mac, vm_name_,
                        nil_uuid(), tx_vlan_id_, rx_vlan_id_, parent,
                        ip6_addr_, device_type_, vmi_type_);
    vmi->SetConfigurer(VmInterface::CONFIG);
    return vmi;
}

bool VmInterfaceConfigData::OnDelete(const InterfaceTable *table,
                                     VmInterface *vmi) const {
    if (vmi->IsConfigurerSet(VmInterface::CONFIG) == false)
        return true;

    VmInterfaceConfigData data(NULL, NULL);
    vmi->Resync(table, &data);
    vmi->ResetConfigurer(VmInterface::CONFIG);
    return true;
}

bool VmInterfaceConfigData::OnResync(const InterfaceTable *table,
                                     VmInterface *vmi,
                                     bool *force_update) const {
    bool sg_changed = false;
    bool ecmp_changed = false;
    bool local_pref_changed = false;
    bool ecmp_load_balance_changed = false;
    bool static_route_config_changed = false;
    bool ret = false;

    ret = vmi->CopyConfig(table, this, &sg_changed, &ecmp_changed,
                          &local_pref_changed, &ecmp_load_balance_changed,
                          &static_route_config_changed);
    if (sg_changed || ecmp_changed || local_pref_changed ||
        ecmp_load_balance_changed || static_route_config_changed)
        *force_update = true;

    vmi->SetConfigurer(VmInterface::CONFIG);
    return ret;
}

// Copies configuration from DB-Request data. The actual applying of 
// configuration, like adding/deleting routes must be done with ApplyConfig()
bool VmInterface::CopyConfig(const InterfaceTable *table,
                             const VmInterfaceConfigData *data,
                             bool *sg_changed,
                             bool *ecmp_changed,
                             bool *local_pref_changed,
                             bool *ecmp_load_balance_changed,
                             bool *static_route_config_changed) {
    bool ret = false;
    if (table) {
        VmEntry *vm = table->FindVmRef(data->vm_uuid_);
        if (vm_.get() != vm) {
            vm_ = vm;
            ret = true;
        }

        bool drop_new_flows =
            (vm_.get() != NULL) ? vm_->drop_new_flows() : false;
        if (drop_new_flows_ != drop_new_flows) {
            drop_new_flows_ = drop_new_flows;
            ret = true;
        }

        VrfEntry *vrf = table->FindVrfRef(data->vrf_name_);
        if (vrf_.get() != vrf) {
            vrf_ = vrf;
            ret = true;
        }

        MirrorEntry *mirror = table->FindMirrorRef(data->analyzer_name_);
        if (mirror_entry_.get() != mirror) {
            mirror_entry_ = mirror;
            ret = true;
        }
    }

    MirrorDirection mirror_direction = data->mirror_direction_;
    if (mirror_direction_ != mirror_direction) {
        mirror_direction_ = mirror_direction;
        ret = true;
    }

    string cfg_name = data->cfg_name_;
    if (cfg_name_ != cfg_name) {
        cfg_name_ = cfg_name;
        ret = true;
    }

    // Read ifindex for the interface
    if (table) {
        VnEntry *vn = table->FindVnRef(data->vn_uuid_);
        if (vn_.get() != vn) {
            vn_ = vn;
            ret = true;
        }

        bool val = vn ? vn->layer3_forwarding() : false;
        if (layer3_forwarding_ != val) {
            layer3_forwarding_ = val;
            ret = true;
        }

        val = vn ? vn->bridging() : false;
        if (bridging_ != val) {
            bridging_ = val;
            ret = true;
        }

        int vxlan_id = vn ? vn->GetVxLanId() : 0;
        if (vxlan_id_ != vxlan_id) {
            vxlan_id_ = vxlan_id;
            ret = true;
        }

        bool flood_unknown_unicast =
            vn ? vn->flood_unknown_unicast(): false;
        if (flood_unknown_unicast_ != flood_unknown_unicast) {
            flood_unknown_unicast_ = flood_unknown_unicast;
            ret = true;
        }

        AgentQosConfigTable *qos_table = table->agent()->qos_config_table();
        AgentQosConfigKey qos_key(data->qos_config_uuid_);
        const AgentQosConfig *qos_config =  static_cast<AgentQosConfig *>(
            qos_table->FindActiveEntry(&qos_key));
        bool is_vn_qos_config = false;

        if (qos_config == NULL) {
            if (vn && vn->qos_config()) {
                qos_config = vn->qos_config();
                is_vn_qos_config = true;
            }
        }

        if (qos_config_ != qos_config) {
            qos_config_ = qos_config;
            ret = true;
        }

        if (is_vn_qos_config != is_vn_qos_config_) {
            is_vn_qos_config_ = is_vn_qos_config;
            ret = true;
        }
    }

    if (local_preference_ != data->local_preference_) {
        local_preference_ = data->local_preference_;
        *local_pref_changed = true;
        ret = true;
    }

    if (need_linklocal_ip_ != data->need_linklocal_ip_) {
        need_linklocal_ip_ = data->need_linklocal_ip_;
        ret = true;
    }

    // CopyIpAddress uses fabric_port_. So, set it before CopyIpAddresss
    if (fabric_port_ != data->fabric_port_) {
        fabric_port_ = data->fabric_port_;
        ret = true;
    }

    if (service_health_check_ip_ != data->service_health_check_ip_) {
        service_health_check_ip_ = data->service_health_check_ip_;
        ret = true;
    }

    //If nova gives a instance-ip then retain that
    //ip address as primary ip address
    //Else choose the ip address to be old
    //primary ip address as long as its present in
    //new configuration also
    Ip4Address ipaddr = data->addr_;
    if (nova_ip_addr_ != Ip4Address(0)) {
        ipaddr = nova_ip_addr_;
    }
    if (CopyIpAddress(ipaddr)) {
        ret = true;
    }

    Ip6Address ip6_addr = data->ip6_addr_;
    if (nova_ip6_addr_ != Ip6Address()) {
        ip6_addr = nova_ip6_addr_;
    }

    if (CopyIp6Address(ip6_addr)) {
        ret = true;
    }

    if (dhcp_enable_ != data->dhcp_enable_) {
        dhcp_enable_ = data->dhcp_enable_;
        ret = true;
    }

    if (disable_policy_ != data->disable_policy_) {
        disable_policy_ = data->disable_policy_;
        ret = true;
    }

    bool mac_set = true;
    if (vm_mac_ == MacAddress::kZeroMac) {
        mac_set = false;
    }
    if (mac_set_ != mac_set) {
        mac_set_ = mac_set;
        ret = true;
    }

    if (admin_state_ != data->admin_state_) {
        admin_state_ = data->admin_state_;
        ret = true;
    }

    if (subnet_ != data->subnet_ || subnet_plen_ != data->subnet_plen_) {
        subnet_ = data->subnet_;
        subnet_plen_ = data->subnet_plen_;
    }

    // Copy DHCP options; ret is not modified as there is no dependent action
    oper_dhcp_options_ = data->oper_dhcp_options_;

    // Audit operational and config floating-ip list
    FloatingIpSet &old_fip_list = floating_ip_list_.list_;
    const FloatingIpSet &new_fip_list = data->floating_ip_list_.list_;
    if (AuditList<FloatingIpList, FloatingIpSet::iterator>
        (floating_ip_list_, old_fip_list.begin(), old_fip_list.end(),
         new_fip_list.begin(), new_fip_list.end())) {
        ret = true;
        assert(floating_ip_list_.list_.size() ==
               (floating_ip_list_.v4_count_ + floating_ip_list_.v6_count_));
    }

    // Audit operational and config alias-ip list
    AliasIpSet &old_aip_list = alias_ip_list_.list_;
    const AliasIpSet &new_aip_list = data->alias_ip_list_.list_;
    if (AuditList<AliasIpList, AliasIpSet::iterator>
        (alias_ip_list_, old_aip_list.begin(), old_aip_list.end(),
         new_aip_list.begin(), new_aip_list.end())) {
        ret = true;
        assert(alias_ip_list_.list_.size() ==
               (alias_ip_list_.v4_count_ + alias_ip_list_.v6_count_));
    }

    // Audit operational and config Service VLAN list
    ServiceVlanSet &old_service_list = service_vlan_list_.list_;
    const ServiceVlanSet &new_service_list = data->service_vlan_list_.list_;
    if (AuditList<ServiceVlanList, ServiceVlanSet::iterator>
        (service_vlan_list_, old_service_list.begin(), old_service_list.end(),
         new_service_list.begin(), new_service_list.end())) {
        ret = true;
    }

    // Audit operational and config Static Route list
    StaticRouteSet &old_route_list = static_route_list_.list_;
    const StaticRouteSet &new_route_list = data->static_route_list_.list_;
    if (AuditList<StaticRouteList, StaticRouteSet::iterator>
        (static_route_list_, old_route_list.begin(), old_route_list.end(),
         new_route_list.begin(), new_route_list.end())) {
        *static_route_config_changed = true;
        ret = true;
    }

    // Audit operational and config allowed address pair
    AllowedAddressPairSet &old_aap_list = allowed_address_pair_list_.list_;
    const AllowedAddressPairSet &new_aap_list = data->
        allowed_address_pair_list_.list_;
    if (AuditList<AllowedAddressPairList, AllowedAddressPairSet::iterator>
       (allowed_address_pair_list_, old_aap_list.begin(), old_aap_list.end(),
        new_aap_list.begin(), new_aap_list.end())) {
        ret = true;
    }

    // Audit operational and config Security Group list
    SecurityGroupEntrySet &old_sg_list = sg_list_.list_;
    const SecurityGroupEntrySet &new_sg_list = data->sg_list_.list_;
    *sg_changed =
	    AuditList<SecurityGroupEntryList, SecurityGroupEntrySet::iterator>
	    (sg_list_, old_sg_list.begin(), old_sg_list.end(),
	     new_sg_list.begin(), new_sg_list.end());
    if (*sg_changed) {
        ret = true;
    }

    VrfAssignRuleSet &old_vrf_assign_list = vrf_assign_rule_list_.list_;
    const VrfAssignRuleSet &new_vrf_assign_list = data->
        vrf_assign_rule_list_.list_;
    if (AuditList<VrfAssignRuleList, VrfAssignRuleSet::iterator>
        (vrf_assign_rule_list_, old_vrf_assign_list.begin(),
         old_vrf_assign_list.end(), new_vrf_assign_list.begin(),
         new_vrf_assign_list.end())) {
        ret = true;
     }

    FatFlowEntrySet &old_fat_flow_entry_list = fat_flow_list_.list_;
    const FatFlowEntrySet &new_fat_flow_entry_list =
        data->fat_flow_list_.list_;
    if (AuditList<FatFlowList, FatFlowEntrySet::iterator>
        (fat_flow_list_, old_fat_flow_entry_list.begin(),
         old_fat_flow_entry_list.end(), new_fat_flow_entry_list.begin(),
         new_fat_flow_entry_list.end())) {
        ret = true;
    }

    InstanceIpSet &old_ipv4_list = instance_ipv4_list_.list_;
    InstanceIpSet new_ipv4_list = data->instance_ipv4_list_.list_;
    //Native ip of instance should be advertised even if
    //config is not present, so manually add that entry
    if (nova_ip_addr_ != Ip4Address(0) &&
        data->vrf_name_ != Agent::NullString()) {
        new_ipv4_list.insert(
            VmInterface::InstanceIp(nova_ip_addr_, Address::kMaxV4PrefixLen,
                                    data->ecmp_, true, false, false,
                                    Ip4Address(0)));
    }
    if (AuditList<InstanceIpList, InstanceIpSet::iterator>
        (instance_ipv4_list_, old_ipv4_list.begin(), old_ipv4_list.end(),
         new_ipv4_list.begin(), new_ipv4_list.end())) {
        ret = true;
    }

    InstanceIpSet &old_ipv6_list = instance_ipv6_list_.list_;
    InstanceIpSet new_ipv6_list = data->instance_ipv6_list_.list_;
    if (nova_ip6_addr_ != Ip6Address() &&
            data->vrf_name_ != Agent::NullString()) {
        new_ipv6_list.insert(
            VmInterface::InstanceIp(nova_ip6_addr_, Address::kMaxV6PrefixLen,
                                    data->ecmp6_, true, false, false,
                                    Ip4Address(0)));
    }

    if (AuditList<InstanceIpList, InstanceIpSet::iterator>
        (instance_ipv6_list_, old_ipv6_list.begin(), old_ipv6_list.end(),
         new_ipv6_list.begin(), new_ipv6_list.end())) {
        ret = true;
    }

    if (data->addr_ != Ip4Address(0) && ecmp_ != data->ecmp_) {
        ecmp_ = data->ecmp_;
        *ecmp_changed = true;
    }

    if (!data->ip6_addr_.is_unspecified() && ecmp6_ != data->ecmp6_) {
        ecmp6_ = data->ecmp6_;
        *ecmp_changed = true;
    }

    if (service_ip_ecmp_ != data->service_ip_ecmp_ ||
        service_ip_ != data->service_ip_) {
        service_ip_ecmp_ = data->service_ip_ecmp_;
        service_ip_ = data->service_ip_;
        *ecmp_changed = true;
        ret = true;
    }

    if (service_ip_ecmp6_ != data->service_ip_ecmp6_ ||
        service_ip6_ != data->service_ip6_) {
        service_ip_ecmp6_ = data->service_ip_ecmp6_;
        service_ip6_ = data->service_ip6_;
        *ecmp_changed = true;
        ret = true;
    }

    if (data->device_type_ !=  VmInterface::DEVICE_TYPE_INVALID &&
        device_type_ != data->device_type_) {
        device_type_= data->device_type_;
        ret = true;
    }

    if (device_type_ == LOCAL_DEVICE || device_type_ == REMOTE_VM_VLAN_ON_VMI ||
        device_type_ == VM_VLAN_ON_VMI) {
        if (rx_vlan_id_ != data->rx_vlan_id_) {
            rx_vlan_id_ = data->rx_vlan_id_;
            ret = true;
        }
        if (tx_vlan_id_ != data->tx_vlan_id_) {
            tx_vlan_id_ = data->tx_vlan_id_;
            ret = true;
        }
    }

    if (logical_interface_ != data->logical_interface_) {
        logical_interface_ = data->logical_interface_;
        ret = true;
    }
    Interface *new_parent = NULL;
    if (data->physical_interface_.empty() == false) {
        PhysicalInterfaceKey key(data->physical_interface_);
        new_parent = static_cast<Interface *>
            (table->agent()->interface_table()->FindActiveEntry(&key));
    } else if (data->parent_vmi_.is_nil() == false) {
        VmInterfaceKey key(AgentKey::RESYNC, data->parent_vmi_, "");
        new_parent = static_cast<Interface *>
            (table->agent()->interface_table()->FindActiveEntry(&key));
    } else {
        new_parent = parent_.get();
    }

    if (parent_ != new_parent) {
        parent_ = new_parent;
        ret = true;
    }

    if (table) {
        if (os_index_ == kInvalidIndex) {
            GetOsParams(table->agent());
            if (os_index_ != kInvalidIndex)
                ret = true;
        }
    }

    if (ecmp_load_balance_ != data->ecmp_load_balance_) {
        ecmp_load_balance_.Copy(data->ecmp_load_balance_);
        *ecmp_load_balance_changed = true;
        ret = true;
    }
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceNovaData routines
/////////////////////////////////////////////////////////////////////////////
VmInterfaceNovaData::VmInterfaceNovaData() :
    VmInterfaceData(NULL, NULL, INSTANCE_MSG, Interface::TRANSPORT_INVALID),
    ipv4_addr_(),
    ipv6_addr_(),
    mac_addr_(),
    vm_name_(),
    vm_uuid_(),
    vm_project_uuid_(),
    physical_interface_(),
    tx_vlan_id_(),
    rx_vlan_id_() {
}

VmInterfaceNovaData::VmInterfaceNovaData(const Ip4Address &ipv4_addr,
                                         const Ip6Address &ipv6_addr,
                                         const std::string &mac_addr,
                                         const std::string vm_name,
                                         boost::uuids::uuid vm_uuid,
                                         boost::uuids::uuid vm_project_uuid,
                                         const std::string &physical_interface,
                                         uint16_t tx_vlan_id,
                                         uint16_t rx_vlan_id,
                                         VmInterface::DeviceType device_type,
                                         VmInterface::VmiType vmi_type,
                                         Interface::Transport transport) :
    VmInterfaceData(NULL, NULL, INSTANCE_MSG, transport),
    ipv4_addr_(ipv4_addr),
    ipv6_addr_(ipv6_addr),
    mac_addr_(mac_addr),
    vm_name_(vm_name),
    vm_uuid_(vm_uuid),
    vm_project_uuid_(vm_project_uuid),
    physical_interface_(physical_interface),
    tx_vlan_id_(tx_vlan_id),
    rx_vlan_id_(rx_vlan_id),
    device_type_(device_type),
    vmi_type_(vmi_type) {
}

VmInterfaceNovaData::~VmInterfaceNovaData() {
}

VmInterface *VmInterfaceNovaData::OnAdd(const InterfaceTable *table,
                                        const VmInterfaceKey *key) const {
    Interface *parent = NULL;
    if (tx_vlan_id_ != VmInterface::kInvalidVlanId &&
        rx_vlan_id_ != VmInterface::kInvalidVlanId &&
        physical_interface_ != Agent::NullString()) {
        PhysicalInterfaceKey key_1(physical_interface_);
        parent = static_cast<Interface *>
            (table->agent()->interface_table()->FindActiveEntry(&key_1));
        assert(parent != NULL);
    }

    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString(mac_addr_, &ec);
    if (ec.value() != 0) {
        mac.Zero();
    }

    VmInterface *vmi =
        new VmInterface(key->uuid_, key->name_, ipv4_addr_, mac, vm_name_,
                        vm_project_uuid_, tx_vlan_id_, rx_vlan_id_,
                        parent, ipv6_addr_, device_type_, vmi_type_);
    vmi->SetConfigurer(VmInterface::INSTANCE_MSG);
    vmi->nova_ip_addr_ = ipv4_addr_;
    vmi->nova_ip6_addr_ = ipv6_addr_;

    return vmi;
}

bool VmInterfaceNovaData::OnDelete(const InterfaceTable *table,
                                   VmInterface *vmi) const {
    if (vmi->IsConfigurerSet(VmInterface::INSTANCE_MSG) == false)
        return true;

    VmInterfaceConfigData data(NULL, NULL);
    vmi->Resync(table, &data);
    vmi->ResetConfigurer(VmInterface::CONFIG);
    vmi->ResetConfigurer(VmInterface::INSTANCE_MSG);
    return true;
}

bool VmInterfaceNovaData::OnResync(const InterfaceTable *table,
                                   VmInterface *vmi,
                                   bool *force_update) const {
    bool ret = false;

    if (vmi->vm_project_uuid_ != vm_project_uuid_) {
        vmi->vm_project_uuid_ = vm_project_uuid_;
        ret = true;
    }

    if (vmi->tx_vlan_id_ != tx_vlan_id_) {
        vmi->tx_vlan_id_ = tx_vlan_id_;
        ret = true;
    }

    if (vmi->rx_vlan_id_ != rx_vlan_id_) {
        vmi->rx_vlan_id_ = rx_vlan_id_;
        ret = true;
    }

    if (vmi->nova_ip_addr_ != ipv4_addr_) {
        vmi->nova_ip_addr_ = ipv4_addr_;
        ret = true;
    }

    if (vmi->nova_ip6_addr_ != ipv6_addr_) {
        vmi->nova_ip6_addr_ = ipv6_addr_;
        ret = true;
    }

    vmi->SetConfigurer(VmInterface::INSTANCE_MSG);

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceMirrorData routines
/////////////////////////////////////////////////////////////////////////////
bool VmInterfaceMirrorData::OnResync(const InterfaceTable *table,
                                     VmInterface *vmi,
                                     bool *force_update) const {
    bool ret = false;

    MirrorEntry *mirror_entry = NULL;
    if (mirror_enable_ == true) {
        mirror_entry = table->FindMirrorRef(analyzer_name_);
    }

    if (vmi->mirror_entry_ != mirror_entry) {
        vmi->mirror_entry_ = mirror_entry;
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceIpAddressData routines
/////////////////////////////////////////////////////////////////////////////
// Update for VM IP address only
// For interfaces in IP Fabric VRF, we send DHCP requests to external servers
// if config doesnt provide an address. This address is updated here.
bool VmInterfaceIpAddressData::OnResync(const InterfaceTable *table,
                                        VmInterface *vmi,
                                        bool *force_update) const {
    bool ret = false;

    if (vmi->os_index_ == VmInterface::kInvalidIndex) {
        vmi->GetOsParams(table->agent());
        if (vmi->os_index_ != VmInterface::kInvalidIndex)
            ret = true;
    }

    // Ignore IP address change if L3 Forwarding not enabled
    if (!vmi->layer3_forwarding_) {
        return ret;
    }

    Ip4Address addr = Ip4Address(0);
    if (vmi->CopyIpAddress(addr)) {
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceOsOperStateData routines
/////////////////////////////////////////////////////////////////////////////
// Resync oper-state for the interface
bool VmInterfaceOsOperStateData::OnResync(const InterfaceTable *table,
                                          VmInterface *vmi,
                                          bool *force_update) const {
    bool ret = false;

    uint32_t old_os_index = vmi->os_index_;
    bool old_ipv4_active = vmi->ipv4_active_;
    bool old_ipv6_active = vmi->ipv6_active_;

    vmi->GetOsParams(table->agent());
    if (vmi->os_index_ != old_os_index)
        ret = true;

    vmi->ipv4_active_ = vmi->IsIpv4Active();
    if (vmi->ipv4_active_ != old_ipv4_active)
        ret = true;

    vmi->ipv6_active_ = vmi->IsIpv6Active();
    if (vmi->ipv6_active_ != old_ipv6_active)
        ret = true;
    // Resync the Oper data for SubInterfaces if attached to parent interface.
    if (ret == true)
        vmi->UpdateOperStateOfSubIntf(table);

    return ret;
}

bool VmInterfaceGlobalVrouterData::OnResync(const InterfaceTable *table,
                                            VmInterface *vmi,
                                            bool *force_update) const {
    bool ret = false;

    if (layer3_forwarding_ != vmi->layer3_forwarding_) {
        vmi->layer3_forwarding_ = layer3_forwarding_;
        *force_update = true;
        ret = true;
    }

    if (bridging_ != vmi->bridging_) {
        vmi->bridging_= bridging_;
        *force_update = true;
        ret = true;
    }

    if (vxlan_id_ != vmi->vxlan_id_)
        ret = true;

    if (vmi->ecmp_load_balance().use_global_vrouter()) {
        *force_update = true;
        ret = true;
    }

    return ret;
}

VmInterfaceHealthCheckData::VmInterfaceHealthCheckData() :
    VmInterfaceData(NULL, NULL, HEALTH_CHECK, Interface::TRANSPORT_INVALID) {
}

VmInterfaceHealthCheckData::~VmInterfaceHealthCheckData() {
}

bool VmInterfaceHealthCheckData::OnResync(const InterfaceTable *table,
                                          VmInterface *vmi,
                                          bool *force_update) const {
    return vmi->UpdateIsHealthCheckActive();
}

VmInterfaceNewFlowDropData::VmInterfaceNewFlowDropData(bool drop_new_flows) :
    VmInterfaceData(NULL, NULL, DROP_NEW_FLOWS, Interface::TRANSPORT_INVALID),
    drop_new_flows_(drop_new_flows) {
}

VmInterfaceNewFlowDropData::~VmInterfaceNewFlowDropData() {
}

bool VmInterfaceNewFlowDropData::OnResync(const InterfaceTable *table,
                                          VmInterface *vmi,
                                          bool *force_update) const {
    if (vmi->drop_new_flows_ != drop_new_flows_) {
        vmi->drop_new_flows_ = drop_new_flows_;
        return true;
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////
// VM Port Entry utility routines
/////////////////////////////////////////////////////////////////////////////
// Does the VMInterface need a physical device to be present
bool VmInterface::NeedDevice() const {
    bool ret = true;

    if (device_type_ == TOR)
        ret = false;

    if (device_type_ == VM_VLAN_ON_VMI)
        ret = false;

    if (subnet_.is_unspecified() == false) {
        ret = false;
    }

    if (transport_ != TRANSPORT_ETHERNET) {
        ret = false;
    }

    if (rx_vlan_id_ != VmInterface::kInvalidVlanId) {
        ret = false;
    } else {
        // Sanity check. rx_vlan_id is set, make sure tx_vlan_id is also set
        assert(tx_vlan_id_ == VmInterface::kInvalidVlanId);
    }

    return ret;
}

void VmInterface::GetOsParams(Agent *agent) {
    if (NeedDevice()) {
        Interface::GetOsParams(agent);
        return;
    }

    os_index_ = Interface::kInvalidIndex;
    mac_ = agent->vrrp_mac();
    os_oper_state_ = true;
}

// A VM Interface is L3 active under following conditions,
// - If interface is deleted, it is inactive
// - VN, VRF are set
// - If sub_interface VMIs, parent_ should be set
//   (We dont track parent_ and activate sub-interfaces. So, we only check 
//    parent_ is present and not necessarily active)
// - For non-VMWARE hypervisors,
//   The tap interface must be created. This is verified by os_index_
// - MAC address set for the interface
bool VmInterface::IsActive()  const {
    if (IsDeleted()) {
        return false;
    }

    if (!admin_state_) {
        return false;
    }

    // If sub_interface VMIs, parent_vmi_ should be set
    // (We dont track parent_ and activate sub-interfaces. So, we only check 
    //  paremt_vmi is present and not necessarily active)
    //  Check if parent is  not active set the SubInterface also not active
    const VmInterface *parentIntf = static_cast<const VmInterface *>(parent());
    if (device_type_ == VM_VLAN_ON_VMI) {
        if (parentIntf == NULL)
            return false;
        else if (parentIntf->IsActive() == false) {
            return false;
        }
    }

    if (device_type_ == REMOTE_VM_VLAN_ON_VMI) {
        if (tx_vlan_id_ == VmInterface::kInvalidVlanId ||
            rx_vlan_id_ == VmInterface::kInvalidVlanId ||
            parent_.get() == NULL)
            return false;
    }

    if ((vn_.get() == NULL) || (vrf_.get() == NULL)) {
        return false;
    }

    if (!vn_.get()->admin_state()) {
        return false;
    }

    if (NeedDevice() == false) {
        return true;
    }

    if (os_index_ == kInvalidIndex)
        return false;

    if (os_oper_state_ == false)
        return false;

    return mac_set_;
}

bool VmInterface::IsMetaDataIPActive() const {
    if (!layer3_forwarding()) {
        return false;
    }

    if (primary_ip6_addr_.is_unspecified()) {
        if (subnet_.is_unspecified() && primary_ip_addr_.to_ulong() == 0) {
            return false;
        }

        if (subnet_.is_unspecified() == false && parent_ == NULL) {
            return false;
        }
    }

    return IsActive();
}

bool VmInterface::IsMetaDataL2Active() const {
    if (!bridging()) {
        return false;
    }

    return IsActive();
}

bool VmInterface::IsIpv4Active() const {
    if (!layer3_forwarding()) {
        return false;
    }

    if (subnet_.is_unspecified() && primary_ip_addr_.to_ulong() == 0) {
        return false;
    }

    if (subnet_.is_unspecified() == false && parent_ == NULL) {
        return false;
    }

    if (!is_hc_active_) {
        return false;
    }

    return IsActive();
}

bool VmInterface::IsIpv6Active() const {
    if (!layer3_forwarding() || (primary_ip6_addr_.is_unspecified())) {
        return false;
    }

    if (!is_hc_active_) {
        return false;
    }

    return IsActive();
}

bool VmInterface::IsL2Active() const {
    if (!bridging()) {
        return false;
    }

    if (!is_hc_active_) {
        return false;
    }

    return IsActive();
}

bool VmInterface::WaitForTraffic() const {
    // do not continue if the interface is inactive or if the VRF is deleted
    if (IsActive() == false || vrf_->IsDeleted()) {
        return false;
    }

    //Get the instance ip route and its corresponding traffic seen status
    InetUnicastRouteKey rt_key(peer_.get(), vrf_->GetName(),
                               primary_ip_addr_, 32);
    const InetUnicastRouteEntry *rt =
        static_cast<const InetUnicastRouteEntry *>(
        vrf_->GetInet4UnicastRouteTable()->FindActiveEntry(&rt_key));
     if (!rt) {
         return false;
     }

     if (rt->FindPath(peer_.get()) == false) {
         return false;
     }

     return rt->FindPath(peer_.get())->path_preference().wait_for_traffic();
}

// Policy is disabled only if user explicitly sets disable policy.
// If user changes to disable policy. only policy will be enabled in case of
// link local services & BGP as a service.
bool VmInterface::PolicyEnabled() const {
    if (disable_policy_) {
        return false;
    }
    return true;
}

// VN is in VXLAN mode if,
// - Tunnel type computed is VXLAN and
// - vxlan_id_ set in VN is non-zero
bool VmInterface::IsVxlanMode() const {
    if (TunnelType::ComputeType(TunnelType::AllType()) != TunnelType::VXLAN)
        return false;

    return vxlan_id_ != 0;
}

// Allocate MPLS Label for Layer3 routes
void VmInterface::AllocL3MplsLabel(bool force_update, bool policy_change,
                                   uint32_t new_label) {
    if (fabric_port_)
        return;

    bool new_entry = false;
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    if (label_ == MplsTable::kInvalidLabel) {
        if (new_label == MplsTable::kInvalidLabel) {
            label_ = agent->mpls_table()->AllocLabel();
        } else {
            label_ = new_label;
        }
        new_entry = true;
        UpdateMetaDataIpInfo();
    }

    if (force_update || policy_change || new_entry)
        MplsLabel::CreateVPortLabel(agent, label_, GetUuid(), policy_enabled_,
                                    InterfaceNHFlags::INET4,
                                    vm_mac_);
}

// Delete MPLS Label for Layer3 routes
void VmInterface::DeleteL3MplsLabel() {
    if (label_ == MplsTable::kInvalidLabel) {
        return;
    }

    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    MplsLabel::Delete(agent, label_);
    label_ = MplsTable::kInvalidLabel;
    UpdateMetaDataIpInfo();
}

// Allocate MPLS Label for Bridge entries 
void VmInterface::AllocL2MplsLabel(bool force_update,
                                   bool policy_change) {
    bool new_entry = false;
    if (l2_label_ == MplsTable::kInvalidLabel) {
        Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
        l2_label_ = agent->mpls_table()->AllocLabel();
        new_entry = true;
    }

    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    if (force_update || policy_change || new_entry)
        MplsLabel::CreateVPortLabel(agent, l2_label_, GetUuid(),
                                    policy_enabled_, InterfaceNHFlags::BRIDGE,
                                    vm_mac_);
}

// Delete MPLS Label for Bridge Entries 
void VmInterface::DeleteL2MplsLabel() {
    if (l2_label_ == MplsTable::kInvalidLabel) {
        return;
    }

    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    MplsLabel::Delete(agent, l2_label_);
    l2_label_ = MplsTable::kInvalidLabel;
}

void VmInterface::UpdateL3TunnelId(bool force_update, bool policy_change) {
    //Currently only MPLS encap ind no VXLAN is supported for L3.
    //Unconditionally create a label
    if (policy_change && (label_ != MplsTable::kInvalidLabel)) {
        /* We delete the existing label and add new one whenever policy changes
         * to ensure that leaked routes point to NH with correct policy
         * status. This is to handle the case when after leaking the route, if
         * policy is disabled on VMI, the leaked route was still pointing to
         * policy enabled NH */

        /* Fetch new label before we delete the existing label */
        Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
        uint32_t new_label = agent->mpls_table()->AllocLabel();
        DeleteL3MplsLabel();
        AllocL3MplsLabel(force_update, policy_change, new_label);
    } else {
        AllocL3MplsLabel(force_update, policy_change, MplsTable::kInvalidLabel);
    }
}

void VmInterface::DeleteL3TunnelId() {
    if (!metadata_ip_active_) {
        DeleteL3MplsLabel();
    }
}

//Check if interface transitioned from inactive to active layer 2 forwarding
bool VmInterface::L2Activated(bool old_l2_active) {
    if (old_l2_active == false && l2_active_ == true) {
        return true;
    }
    return false;
}

// check if interface transitioned from inactive to active for bridging
bool VmInterface::BridgingActivated(bool old_bridging) {
    if (old_bridging == false && bridging_ == true) {
        return true;
    }
    return false;
}

//Check if interface transitioned from inactive to active IP forwarding
bool VmInterface::Ipv4Activated(bool old_ipv4_active) {
    if (old_ipv4_active == false && ipv4_active_ == true) {
        return true;
    }
    return false;
}

bool VmInterface::Ipv6Activated(bool old_ipv6_active) {
    if (old_ipv6_active == false && ipv6_active_ == true) {
        return true;
    }
    return false;
}

//Check if interface transitioned from active bridging to inactive state
bool VmInterface::L2Deactivated(bool old_l2_active) {
    if (old_l2_active == true && l2_active_ == false) {
        return true;
    }
    return false;
}

//Check if interface transitioned from active bridging to inactive state
bool VmInterface::BridgingDeactivated(bool old_bridging) {
    if (old_bridging == true && bridging_ == false) {
        return true;
    }
    return false;
}

//Check if interface transitioned from active IP forwarding to inactive state
bool VmInterface::Ipv4Deactivated(bool old_ipv4_active) {
    if (old_ipv4_active == true && ipv4_active_ == false) {
        return true;
    }
    return false;
}

bool VmInterface::Ipv6Deactivated(bool old_ipv6_active) {
    if (old_ipv6_active == true && ipv6_active_ == false) {
        return true;
    }
    return false;
}

void VmInterface::UpdateFlowKeyNextHop() {
    if (!IsActive()) {
        flow_key_nh_.reset();
        return;
    }
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();

    //If Layer3 forwarding is configured irrespective of ipv4/v6 status,
    //flow_key_nh should be l3 based.
    if (layer3_forwarding()) {
        InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              GetUuid(), ""), true,
                                              InterfaceNHFlags::INET4,
                                              vm_mac_);
        flow_key_nh_ = static_cast<const NextHop *>(
                agent->nexthop_table()->FindActiveEntry(&key));
        return;
    }

    //L2 mode is identified if layer3_forwarding is diabled.
    InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                          GetUuid(), ""), true,
                                          InterfaceNHFlags::BRIDGE,
                                          vm_mac_);
    flow_key_nh_ = static_cast<const NextHop *>(
            agent->nexthop_table()->FindActiveEntry(&key));
}

void VmInterface::UpdateMacVmBinding() {
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf_->GetBridgeRouteTable());
    Agent *agent = table->agent();
    table->AddMacVmBindingRoute(agent->mac_vm_binding_peer(),
                                vrf_->GetName(),
                                vm_mac_,
                                this);
}

void VmInterface::UpdateL3NextHop() {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();
    //layer3_forwarding config is not set, so delete as i/f is active.
    if (!layer3_forwarding_) {
        if ((l3_interface_nh_policy_.get() != NULL) ||
            (l3_interface_nh_no_policy_.get() != NULL)) {
            DeleteL3NextHop();
        }
        return;
    }

    InterfaceNH::CreateL3VmInterfaceNH(GetUuid(),
                                       vm_mac_,
                                       vrf_->GetName());
    InterfaceNHKey key1(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                           GetUuid(), ""),
                        true, InterfaceNHFlags::INET4,
                        vm_mac_);
    l3_interface_nh_policy_ =
        static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key1));
    InterfaceNHKey key2(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                           GetUuid(), ""),
                        false, InterfaceNHFlags::INET4,
                        vm_mac_);
    l3_interface_nh_no_policy_ =
        static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key2));
}

void VmInterface::DeleteL3NextHop() {
    InterfaceNH::DeleteL3InterfaceNH(GetUuid(), vm_mac_);
    l3_interface_nh_policy_.reset();
    l3_interface_nh_no_policy_.reset();
}

//Create these NH irrespective of mode, as multicast uses l2 NH.
void VmInterface::UpdateL2NextHop() {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();
    if (l2_interface_nh_policy_.get() == NULL) {
        InterfaceNH::CreateL2VmInterfaceNH(GetUuid(),
                                           vm_mac_,
                                           vrf_->GetName());
        InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              GetUuid(), ""),
                           true, InterfaceNHFlags::BRIDGE, vm_mac_);
        l2_interface_nh_policy_ = static_cast<NextHop *>(agent->
                                  nexthop_table()->FindActiveEntry(&key));
    }
    if (l2_interface_nh_no_policy_.get() == NULL) {
        InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              GetUuid(), ""),
                           false, InterfaceNHFlags::BRIDGE, vm_mac_);
        l2_interface_nh_no_policy_ = static_cast<NextHop *>(agent->
                                     nexthop_table()->FindActiveEntry(&key));
    }
}

void VmInterface::DeleteMacVmBinding(const VrfEntry *old_vrf) {
    if (old_vrf == NULL)
        return;
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (old_vrf->GetBridgeRouteTable());
    if (table == NULL)
        return;
    Agent *agent = table->agent();
    table->DeleteMacVmBindingRoute(agent->mac_vm_binding_peer(),
                                   old_vrf->GetName(), vm_mac_,
                                   this);
}

void VmInterface::DeleteL2NextHop() {
    InterfaceNH::DeleteL2InterfaceNH(GetUuid(),
                                     vm_mac_);
    if (l2_interface_nh_policy_.get() != NULL)
        l2_interface_nh_policy_.reset();
    if (l2_interface_nh_no_policy_.get() != NULL)
        l2_interface_nh_no_policy_.reset();
}

void VmInterface::DeleteL2ReceiveRoute(const VrfEntry *old_vrf,
                                       bool old_bridging) {
    if (old_vrf) {
        InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
        Agent *agent = table->agent();
        BridgeAgentRouteTable::Delete(peer_.get(), old_vrf->GetName(),
                                      GetVifMac(agent), 0);
    }
}

bool VmInterface::UpdateIsHealthCheckActive() {
    bool is_hc_active = true;
    HealthCheckInstanceSet::iterator it = hc_instance_set_.begin();
    while (it != hc_instance_set_.end()) {
        if ((*it)->active() == false) {
            // if any of the health check instance reports not active
            // status mark interface health check status inactive
            is_hc_active = false;
            break;
        }
        it++;
    }

    if (is_hc_active_ != is_hc_active) {
        is_hc_active_ = is_hc_active;
        return true;
    }
    return false;
}

IpAddress VmInterface::GetServiceIp(const IpAddress &vm_ip) const {
    IpAddress ip;
    if (vm_ip.is_v4()) {
        ip = Ip4Address(0);
    } else if (vm_ip.is_v6()) {
        ip = Ip6Address();
    }
    if (vn_.get() == NULL) {
        return ip;
    }

    const VnIpam *ipam = NULL;
    if (subnet_.is_unspecified()) {
        ipam = vn_->GetIpam(vm_ip);
    } else {
        ipam = vn_->GetIpam(subnet_);
    }

    if (ipam) {
        if ((vm_ip.is_v4() && ipam->dns_server.is_v4()) ||
            (vm_ip.is_v6() && ipam->dns_server.is_v6())) {
            return ipam->dns_server;
        }
    }
    return ip;
}

// Add/Update route. Delete old route if VRF or address changed
void VmInterface::UpdateIpv4InterfaceRoute(bool old_ipv4_active,
                                           bool force_update,
                                           VrfEntry * old_vrf,
                                           const Ip4Address &old_addr) {
    Ip4Address ip = GetServiceIp(primary_ip_addr_).to_v4();

    // If interface was already active earlier and there is no force_update or
    // policy_change, return
    if (old_ipv4_active == true && force_update == false
        && old_addr == primary_ip_addr_ &&
        vm_ip_service_addr_ == ip) {
        return;
    }

    // We need to have valid IP and VRF to add route
    if (primary_ip_addr_.to_ulong() != 0 && vrf_.get() != NULL) {
        // Add route if old was inactive or force_update is set
        if (old_ipv4_active == false || force_update == true ||
            old_addr != primary_ip_addr_ || vm_ip_service_addr_ != ip) {
            vm_ip_service_addr_ = ip;
            AddRoute(vrf_->GetName(), primary_ip_addr_, 32, vn_->GetName(),
                     false, ecmp_, false, false, vm_ip_service_addr_,
                     Ip4Address(0), CommunityList(), label_);
        }
    }

    // If there is change in VRF or IP address, delete old route
    if (old_vrf != vrf_.get() || primary_ip_addr_ != old_addr) {
        DeleteIpv4InterfaceRoute(old_vrf, old_addr);
    }
}

void VmInterface::UpdateResolveRoute(bool old_ipv4_active, bool force_update,
                                     bool policy_change, VrfEntry * old_vrf,
                                     const Ip4Address &old_addr,
                                     uint8_t old_plen) {
    if (old_ipv4_active == true && force_update == false
        && policy_change == false && old_addr == subnet_ &&
        subnet_plen_ == old_plen) {
        return;
    }

    if (old_vrf && (old_vrf != vrf_.get() || 
        old_addr != subnet_ ||
        subnet_plen_ != old_plen)) {
        DeleteResolveRoute(old_vrf, old_addr, old_plen);
    }

    if (subnet_.to_ulong() != 0 && vrf_.get() != NULL && vn_.get() != NULL) {
        SecurityGroupList sg_id_list;
        CopySgIdList(&sg_id_list);
        VmInterfaceKey vm_intf_key(AgentKey::ADD_DEL_CHANGE, GetUuid(), "");

        InetUnicastAgentRouteTable::AddResolveRoute(peer_.get(), vrf_->GetName(),
                Address::GetIp4SubnetAddress(subnet_, subnet_plen_),
                subnet_plen_, vm_intf_key, vrf_->table_label(),
                policy_enabled_, vn_->GetName(), sg_id_list);
    }
}

void VmInterface::DeleteResolveRoute(VrfEntry *old_vrf,
                                     const Ip4Address &old_addr,
                                     const uint8_t plen) {
    DeleteRoute(old_vrf->GetName(), old_addr, plen);
}

void VmInterface::DeleteIpv4InterfaceRoute(VrfEntry *old_vrf,
                            const Ip4Address &old_addr) {
    if ((old_vrf == NULL) || (old_addr.to_ulong() == 0))
        return;

    DeleteRoute(old_vrf->GetName(), old_addr, 32);
}

// Add meta-data route if linklocal_ip is needed
void VmInterface::UpdateMetadataRoute(bool old_metadata_ip_active,
                                      VrfEntry *old_vrf) {
    if (metadata_ip_active_ == false || old_metadata_ip_active == true)
        return;

    if (!need_linklocal_ip_) {
        return;
    }

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();
    if (mdata_ip_.get() == NULL) {
        mdata_ip_.reset(new MetaDataIp(agent->metadata_ip_allocator(),
                                       this, id()));
    }
    //mdata_ip_->set_nat_src_ip(Ip4Address(METADATA_IP_ADDR));
    mdata_ip_->set_active(true);
}

// Delete meta-data route
void VmInterface::DeleteMetadataRoute(bool old_active, VrfEntry *old_vrf,
                                      bool old_need_linklocal_ip) {
    if (!old_need_linklocal_ip) {
        return;
    }
    if (mdata_ip_.get() != NULL) {
        mdata_ip_->set_active(false);
    }
}

void VmInterface::CleanupFloatingIpList() {
    FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
    while (it != floating_ip_list_.list_.end()) {
        FloatingIpSet::iterator prev = it++;
        if (prev->del_pending_ == false)
            continue;

        if (prev->floating_ip_.is_v4()) {
            floating_ip_list_.v4_count_--;
            assert(floating_ip_list_.v4_count_ >= 0);
        } else {
            floating_ip_list_.v6_count_--;
            assert(floating_ip_list_.v6_count_ >= 0);
        }
        floating_ip_list_.list_.erase(prev);
    }
}

void VmInterface::UpdateFloatingIp(bool force_update, bool policy_change,
                                   bool l2, uint32_t old_ethernet_tag) {
    FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
    while (it != floating_ip_list_.list_.end()) {
        FloatingIpSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this, l2, old_ethernet_tag);
        } else {
            prev->Activate(this, force_update||policy_change, l2,
                           old_ethernet_tag);
        }
    }
}

void VmInterface::DeleteFloatingIp(bool l2, uint32_t old_ethernet_tag) {
    FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
    while (it != floating_ip_list_.list_.end()) {
        FloatingIpSet::iterator prev = it++;
        prev->DeActivate(this, l2, old_ethernet_tag);
    }
}

void VmInterface::CleanupAliasIpList() {
    AliasIpSet::iterator it = alias_ip_list_.list_.begin();
    while (it != alias_ip_list_.list_.end()) {
        AliasIpSet::iterator prev = it++;
        if (prev->del_pending_ == false)
            continue;

        if (prev->alias_ip_.is_v4()) {
            alias_ip_list_.v4_count_--;
            assert(alias_ip_list_.v4_count_ >= 0);
        } else {
            alias_ip_list_.v6_count_--;
            assert(alias_ip_list_.v6_count_ >= 0);
        }
        alias_ip_list_.list_.erase(prev);
    }
}

void VmInterface::UpdateAliasIp(bool force_update, bool policy_change) {
    AliasIpSet::iterator it = alias_ip_list_.list_.begin();
    while (it != alias_ip_list_.list_.end()) {
        AliasIpSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this);
        } else {
            prev->Activate(this, force_update||policy_change);
        }
    }
}

void VmInterface::DeleteAliasIp() {
    AliasIpSet::iterator it = alias_ip_list_.list_.begin();
    while (it != alias_ip_list_.list_.end()) {
        AliasIpSet::iterator prev = it++;
        prev->DeActivate(this);
    }
}

void VmInterface::UpdateServiceVlan(bool force_update, bool policy_change,
                                    bool old_ipv4_active,
                                    bool old_ipv6_active) {
    ServiceVlanSet::iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        ServiceVlanSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this);
            service_vlan_list_.list_.erase(prev);
        } else {
            prev->Activate(this, force_update, old_ipv4_active,
                           old_ipv6_active);
        }
    }
}

void VmInterface::DeleteServiceVlan() {
    ServiceVlanSet::iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        ServiceVlanSet::iterator prev = it++;
        prev->DeActivate(this);
        if (prev->del_pending_) {
            service_vlan_list_.list_.erase(prev);
        }
    }
} 

void VmInterface::UpdateStaticRoute(bool force_update) {
    StaticRouteSet::iterator it = static_route_list_.list_.begin();
    while (it != static_route_list_.list_.end()) {
        StaticRouteSet::iterator prev = it++;
        /* V4 static routes should be enabled only if ipv4_active_ is true
         * V6 static routes should be enabled only if ipv6_active_ is true
         */
        if ((!ipv4_active_ && prev->addr_.is_v4()) ||
            (!ipv6_active_ && prev->addr_.is_v6())) {
            continue;
        }
        if (prev->del_pending_) {
            prev->DeActivate(this);
            static_route_list_.list_.erase(prev);
        } else {
            prev->Activate(this, force_update);
        }
    }
}

void VmInterface::DeleteStaticRoute() {
    StaticRouteSet::iterator it = static_route_list_.list_.begin();
    while (it != static_route_list_.list_.end()) {
        StaticRouteSet::iterator prev = it++;
        prev->DeActivate(this);
        if (prev->del_pending_) {
            static_route_list_.list_.erase(prev);
        }
    }
}

void VmInterface::UpdateAllowedAddressPair(bool force_update, bool policy_change,
                                           bool l2, bool old_layer2_forwarding,
                                           bool old_layer3_forwarding) {
    AllowedAddressPairSet::iterator it =
        allowed_address_pair_list_.list_.begin();
    while (it != allowed_address_pair_list_.list_.end()) {
        AllowedAddressPairSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->L2DeActivate(this);
            prev->DeActivate(this);
            allowed_address_pair_list_.list_.erase(prev);
        }
    }

    for (it = allowed_address_pair_list_.list_.begin();
         it != allowed_address_pair_list_.list_.end(); it++) {
        /* V4 AAP entries should be enabled only if ipv4_active_ is true
         * V6 AAP entries should be enabled only if ipv6_active_ is true
         */
        if ((!ipv4_active_ && it->addr_.is_v4()) ||
            (!ipv6_active_ && it->addr_.is_v6())) {
            continue;
        }
        if (l2) {
            it->L2Activate(this, force_update, policy_change,
                    old_layer2_forwarding, old_layer3_forwarding);
        } else {
           it->Activate(this, force_update, policy_change);
        }
    }
}

void VmInterface::DeleteAllowedAddressPair(bool l2) {
    AllowedAddressPairSet::iterator it =
        allowed_address_pair_list_.list_.begin();
    while (it != allowed_address_pair_list_.list_.end()) {
        AllowedAddressPairSet::iterator prev = it++;
        if (l2) {
            prev->L2DeActivate(this);
        } else {
            prev->DeActivate(this);
        }

        if (prev->del_pending_) {
            prev->L2DeActivate(this);
            prev->DeActivate(this);
            allowed_address_pair_list_.list_.erase(prev);
        }
    }
}

void VmInterface::UpdateVrfAssignRule() {
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    //Erase all delete marked entry
    VrfAssignRuleSet::iterator it = vrf_assign_rule_list_.list_.begin();
    while (it != vrf_assign_rule_list_.list_.end()) {
        VrfAssignRuleSet::iterator prev = it++;
        if (prev->del_pending_) {
            vrf_assign_rule_list_.list_.erase(prev);
        }
    }

    if (vrf_assign_rule_list_.list_.size() == 0 && 
        vrf_assign_acl_.get() != NULL) {
        DeleteVrfAssignRule();
        return;
    }

    if (vrf_assign_rule_list_.list_.size() == 0) {
        return;
    }

    AclSpec acl_spec;
    acl_spec.acl_id = uuid_;
    //Create the ACL
    it = vrf_assign_rule_list_.list_.begin();
    uint32_t id = 0;
    for (;it != vrf_assign_rule_list_.list_.end();it++) {
        //Go thru all match condition and create ACL entry
        AclEntrySpec ace_spec;
        ace_spec.id = id++;
        if (ace_spec.Populate(&(it->match_condition_)) == false) {
            continue;
        }
        /* Add both v4 and v6 rules regardless of whether interface is
         * ipv4_active_/ipv6_active_
         */
        ActionSpec vrf_translate_spec;
        vrf_translate_spec.ta_type = TrafficAction::VRF_TRANSLATE_ACTION;
        vrf_translate_spec.simple_action = TrafficAction::VRF_TRANSLATE;
        vrf_translate_spec.vrf_translate.set_vrf_name(it->vrf_name_);
        vrf_translate_spec.vrf_translate.set_ignore_acl(it->ignore_acl_);
        ace_spec.action_l.push_back(vrf_translate_spec);
        acl_spec.acl_entry_specs_.push_back(ace_spec);
    }

    DBRequest req;
    AclKey *key = new AclKey(acl_spec.acl_id);
    AclData *data = new AclData(agent, NULL, acl_spec);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);
    agent->acl_table()->Process(req);

    AclKey entry_key(uuid_);
    AclDBEntry *acl = static_cast<AclDBEntry *>(
        agent->acl_table()->FindActiveEntry(&entry_key));
    assert(acl);
    vrf_assign_acl_ = acl;
}

void VmInterface::DeleteVrfAssignRule() {
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    VrfAssignRuleSet::iterator it = vrf_assign_rule_list_.list_.begin();
    while (it != vrf_assign_rule_list_.list_.end()) {
        VrfAssignRuleSet::iterator prev = it++;
        if (prev->del_pending_) {
            vrf_assign_rule_list_.list_.erase(prev);
        }
    }

    if (vrf_assign_acl_ != NULL) {
        vrf_assign_acl_ = NULL;
        DBRequest req;
        AclKey *key = new AclKey(uuid_);
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(key);
        req.data.reset(NULL);
        agent->acl_table()->Process(req);
    }
}

void VmInterface::UpdateSecurityGroup() {
    SecurityGroupEntrySet::iterator it = sg_list_.list_.begin();
    while (it != sg_list_.list_.end()) {
        SecurityGroupEntrySet::iterator prev = it++;
        if (prev->del_pending_) {
            sg_list_.list_.erase(prev);
        } else {
            prev->Activate(this);
        }
    }
}

void VmInterface::DeleteSecurityGroup() {
    SecurityGroupEntrySet::iterator it = sg_list_.list_.begin();
    while (it != sg_list_.list_.end()) {
        SecurityGroupEntrySet::iterator prev = it++;
        if (prev->del_pending_) {
            sg_list_.list_.erase(prev);
        }
    }
}

void VmInterface::UpdateFatFlow() {
    DeleteFatFlow();
}

void VmInterface::DeleteFatFlow() { 
    FatFlowEntrySet::iterator it = fat_flow_list_.list_.begin();
    while (it != fat_flow_list_.list_.end()) {
        FatFlowEntrySet::iterator prev = it++;
        if (prev->del_pending_) {
            fat_flow_list_.list_.erase(prev);
        }
    }
}

void VmInterface::UpdateL2TunnelId(bool force_update, bool policy_change) {
    AllocL2MplsLabel(force_update, policy_change);
}

void VmInterface::DeleteL2TunnelId() {
    DeleteL2MplsLabel();
}

void VmInterface::UpdateL2InterfaceRoute(bool old_bridging, bool force_update,
                                         VrfEntry *old_vrf,
                                         const Ip4Address &old_v4_addr,
                                         const Ip6Address &old_v6_addr,
                                         int old_ethernet_tag,
                                         bool old_layer3_forwarding,
                                         bool policy_changed,
                                         const Ip4Address &new_ip_addr,
                                         const Ip6Address &new_ip6_addr,
                                         const MacAddress &mac,
                                         const IpAddress &dependent_ip) const {
    if (ethernet_tag_ != old_ethernet_tag) {
        force_update = true;
    }

    if (old_layer3_forwarding != layer3_forwarding_) {
        force_update = true;
    }

    if (old_vrf && old_vrf != vrf()) {
        force_update = true;
    }

    //Encap change will result in force update of l2 routes.
    if (force_update) {
        DeleteL2InterfaceRoute(true, old_vrf, old_v4_addr,
                               old_v6_addr, old_ethernet_tag, mac);
    } else {
        if (new_ip_addr != old_v4_addr) {
            force_update = true;
            DeleteL2InterfaceRoute(true, old_vrf, old_v4_addr, Ip6Address(),
                                   old_ethernet_tag, mac);
        }

        if (new_ip6_addr != old_v6_addr) {
            force_update = true;
            DeleteL2InterfaceRoute(true, old_vrf, Ip4Address(), old_v6_addr,
                                   old_ethernet_tag, mac);
        }
    }

    assert(peer_.get());
    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>
        (vrf_->GetEvpnRouteTable());

    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);

    PathPreference path_preference;
    SetPathPreference(&path_preference, false, dependent_ip);

    if (policy_changed == true) {
        //Resync the nexthop
        table->ResyncVmRoute(peer_.get(), vrf_->GetName(),
                             mac, new_ip_addr,
                             ethernet_tag_, NULL);
        table->ResyncVmRoute(peer_.get(), vrf_->GetName(),
                             mac, new_ip6_addr,
                             ethernet_tag_, NULL);
    }

    if (old_bridging && force_update == false)
        return;

    if (new_ip_addr.is_unspecified() || layer3_forwarding_ == true) {
        table->AddLocalVmRoute(peer_.get(), vrf_->GetName(),
                mac, this, new_ip_addr,
                l2_label_, vn_->GetName(), sg_id_list,
                path_preference, ethernet_tag_);
    }

    if (new_ip6_addr.is_unspecified() == false && layer3_forwarding_ == true) {
        table->AddLocalVmRoute(peer_.get(), vrf_->GetName(),
                mac, this, new_ip6_addr,
                l2_label_, vn_->GetName(), sg_id_list,
                path_preference, ethernet_tag_);
    }
}

void VmInterface::DeleteL2InterfaceRoute(bool old_bridging, VrfEntry *old_vrf,
                                         const Ip4Address &old_v4_addr,
                                         const Ip6Address &old_v6_addr,
                                         int old_ethernet_tag,
                                         const MacAddress &mac) const {
    if (old_bridging == false)
        return;

    if (old_vrf == NULL)
        return;

    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>
        (old_vrf->GetEvpnRouteTable());
    if (table == NULL)
        return;
    table->DelLocalVmRoute(peer_.get(), old_vrf->GetName(), mac,
                           this, old_v4_addr,
                           old_ethernet_tag);
    table->DelLocalVmRoute(peer_.get(), old_vrf->GetName(), mac,
                           this, old_v6_addr,
                           old_ethernet_tag);
}

// Copy the SG List for VM Interface. Used to add route for interface
void VmInterface::CopySgIdList(SecurityGroupList *sg_id_list) const {
    SecurityGroupEntrySet::const_iterator it;
    for (it = sg_list_.list_.begin(); it != sg_list_.list_.end(); ++it) {
        if (it->del_pending_)
            continue;
        if (it->sg_.get() == NULL)
            continue;
        sg_id_list->push_back(it->sg_->GetSgId());
    }
}

// Set path-preference information for the route
void VmInterface::SetPathPreference(PathPreference *pref, bool ecmp,
                                    const IpAddress &dependent_ip) const {
    pref->set_ecmp(ecmp);
    if (local_preference_ != INVALID) {
        pref->set_static_preference(true);
    }
    if (local_preference_ == HIGH || ecmp == true) {
        pref->set_preference(PathPreference::HIGH);
    }
    pref->set_dependent_ip(dependent_ip);
    pref->set_vrf(vrf()->GetName());
}

void VmInterface::SetServiceVlanPathPreference(PathPreference *pref,
                                          const IpAddress &service_ip) const {

    bool ecmp_mode = false;
    IpAddress dependent_ip;

    //Logic for setting ecmp and tracking IP on Service chain route
    //Service vlan route can be active when interface is either
    //IPV4 active or IPV6 active, hence we have to consider both
    //IPV6 and IPV4 IP
    //If Service vlan is for Ipv4 route, then priority is as below
    //1> Service IP for v4
    //3> Primary IP for v4
    if (service_ip.is_v4()) {
        if (service_ip_ != Ip4Address(0)) {
            dependent_ip = service_ip_;
            ecmp_mode = service_ip_ecmp_;
        } else {
            dependent_ip = primary_ip_addr_;
            ecmp_mode = ecmp_;
        }
    }

    //If Service vlan is for Ipv6 route, then priority is as below
    //1> Service IP for v6
    //3> Primary IP for v6
    if (service_ip.is_v6()) {
        if (service_ip6_ != Ip6Address()) {
            dependent_ip = service_ip6_;
            ecmp_mode = service_ip_ecmp6_;
        } else {
            dependent_ip = primary_ip6_addr_;
            ecmp_mode = ecmp6_;
        }
    }

    pref->set_ecmp(ecmp_mode);
    if (local_preference_ != INVALID) {
        pref->set_static_preference(true);
    }
    if (local_preference_ == HIGH) {
        pref->set_preference(PathPreference::HIGH);
    } else {
        pref->set_preference(PathPreference::LOW);
    }

    pref->set_dependent_ip(dependent_ip);
    pref->set_vrf(vrf()->GetName());
}

void VmInterface::CopyEcmpLoadBalance(EcmpLoadBalance &ecmp_load_balance) {
    if (ecmp_load_balance_.use_global_vrouter() == false)
        return ecmp_load_balance.Copy(ecmp_load_balance_);
    return ecmp_load_balance.Copy(agent()->oper_db()->global_vrouter()->
                                  ecmp_load_balance());
}

//Add a route for VM port
//If ECMP route, add new composite NH and mpls label for same
void VmInterface::AddRoute(const std::string &vrf_name, const IpAddress &addr,
                           uint32_t plen, const std::string &dest_vn,
                           bool force_policy, bool ecmp, bool is_local,
                           bool is_health_check_service,
                           const IpAddress &service_ip,
                           const IpAddress &dependent_rt,
                           const CommunityList &communities, uint32_t label) {
    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);

    PathPreference path_preference;
    SetPathPreference(&path_preference, ecmp, dependent_rt);

    VnListType vn_list;
    vn_list.insert(dest_vn);
    EcmpLoadBalance ecmp_load_balance;
    CopyEcmpLoadBalance(ecmp_load_balance);
    InetUnicastAgentRouteTable::AddLocalVmRoute(peer_.get(), vrf_name, addr,
                                                 plen, GetUuid(),
                                                 vn_list, label,
                                                 sg_id_list, communities,
                                                 force_policy,
                                                 path_preference, service_ip,
                                                 ecmp_load_balance, is_local,
                                                 is_health_check_service);
    return;
}

void VmInterface::ResolveRoute(const std::string &vrf_name, const Ip4Address &addr,
                               uint32_t plen, const std::string &dest_vn, bool policy) {
    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);
    VmInterfaceKey vm_intf_key(AgentKey::ADD_DEL_CHANGE, GetUuid(), "");

    InetUnicastAgentRouteTable::AddResolveRoute(peer_.get(), vrf_name,
            Address::GetIp4SubnetAddress(addr, plen),
            plen, vm_intf_key, vrf_->table_label(),
            policy, dest_vn, sg_id_list);
}

void VmInterface::DeleteRoute(const std::string &vrf_name,
                              const IpAddress &addr, uint32_t plen) {
    InetUnicastAgentRouteTable::Delete(peer_.get(), vrf_name, addr, plen);
    return;
}

// DHCP options applicable to the Interface
bool VmInterface::GetInterfaceDhcpOptions(
                  std::vector<autogen::DhcpOptionType> *options) const {
    if (oper_dhcp_options().are_dhcp_options_set()) {
        *options = oper_dhcp_options().dhcp_options();
        return true;
    }

    return false;
}

// DHCP options applicable to the Subnet to which the interface belongs
bool VmInterface::GetSubnetDhcpOptions(
                  std::vector<autogen::DhcpOptionType> *options,
                  bool ipv6) const {
    if (vn()) {
        const std::vector<VnIpam> &vn_ipam = vn()->GetVnIpam();
        uint32_t index;
        for (index = 0; index < vn_ipam.size(); ++index) {
            if (!ipv6 && vn_ipam[index].IsSubnetMember(primary_ip_addr())) {
                break;
            }
            if (ipv6 && vn_ipam[index].IsSubnetMember(primary_ip6_addr())) {
                break;
            }
        }
        if (index < vn_ipam.size() &&
            vn_ipam[index].oper_dhcp_options.are_dhcp_options_set()) {
            *options = vn_ipam[index].oper_dhcp_options.dhcp_options();
            return true;
        }
    }

    return false;
}

// DHCP options applicable to the Ipam to which the interface belongs
bool VmInterface::GetIpamDhcpOptions(
                  std::vector<autogen::DhcpOptionType> *options,
                  bool ipv6) const {
    if (vn()) {
        std::string ipam_name;
        autogen::IpamType ipam_type;
        if (!ipv6 &&
            vn()->GetIpamData(primary_ip_addr(), &ipam_name, &ipam_type)) {
            *options = ipam_type.dhcp_option_list.dhcp_option;
            return true;
        }
        if (ipv6 &&
            vn()->GetIpamData(primary_ip6_addr(), &ipam_name, &ipam_type)) {
            *options = ipam_type.dhcp_option_list.dhcp_option;
            return true;
        }
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////
// InstanceIp routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::InstanceIp::InstanceIp() :
    ListEntry(), ip_(), plen_(), ecmp_(false), l2_installed_(false), old_ecmp_(false),
    is_primary_(false), is_service_health_check_ip_(false), is_local_(false),
    old_tracking_ip_(), tracking_ip_() {
}

VmInterface::InstanceIp::InstanceIp(const InstanceIp &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_),
    ip_(rhs.ip_), plen_(rhs.plen_), ecmp_(rhs.ecmp_),
    l2_installed_(rhs.l2_installed_), old_ecmp_(rhs.old_ecmp_),
    is_primary_(rhs.is_primary_),
    is_service_health_check_ip_(rhs.is_service_health_check_ip_),
    is_local_(rhs.is_local_), old_tracking_ip_(rhs.old_tracking_ip_),
    tracking_ip_(rhs.tracking_ip_) {
}

VmInterface::InstanceIp::InstanceIp(const IpAddress &addr, uint8_t plen,
                                    bool ecmp, bool is_primary,
                                    bool is_service_health_check_ip,
                                    bool is_local,
                                    const IpAddress &tracking_ip) :
    ListEntry(), ip_(addr), plen_(plen), ecmp_(ecmp),
    l2_installed_(false), old_ecmp_(false), is_primary_(is_primary),
    is_service_health_check_ip_(is_service_health_check_ip),
    is_local_(is_local), tracking_ip_(tracking_ip) {
}

VmInterface::InstanceIp::~InstanceIp() {
}

bool VmInterface::InstanceIp::operator() (const InstanceIp &lhs,
                                          const InstanceIp &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::InstanceIp::IsLess(const InstanceIp *rhs) const {
    return ip_ < rhs->ip_;
}

void VmInterface::InstanceIp::L3Activate(VmInterface *interface,
                                         bool force_update) const {
    if (old_ecmp_ != ecmp_) {
        force_update = true;
        old_ecmp_ = ecmp_;
    }

    if (old_tracking_ip_ != tracking_ip_) {
        force_update = true;
    }

    // Add route if not installed or if force requested
    if (installed_ && force_update == false) {
        return;
    }

    // Add route only when vn IPAM exists
    if (!interface->vn()->GetIpam(ip_)) {
        return;
    }

    // Set prefix len for instance_ip based on Alloc-unit in VnIPAM
    SetPrefixForAllocUnitIpam(interface);

    if (ip_.is_v4()) {
        interface->AddRoute(interface->vrf()->GetName(), ip_.to_v4(), plen_,
                            interface->vn()->GetName(), is_force_policy(),
                            ecmp_, is_local_, is_service_health_check_ip_,
                            interface->GetServiceIp(ip_), tracking_ip_,
                            CommunityList(), interface->label());
    } else if (ip_.is_v6()) {
        interface->AddRoute(interface->vrf()->GetName(), ip_.to_v6(), plen_,
                            interface->vn()->GetName(), is_force_policy(),
                            ecmp_, is_local_, is_service_health_check_ip_,
                            interface->GetServiceIp(ip_), tracking_ip_,
                            CommunityList(), interface->label());
    }
    installed_ = true;
}

void VmInterface::InstanceIp::L3DeActivate(VmInterface *interface,
                                           VrfEntry *old_vrf) const {
    if (installed_ == false) {
        return;
    }

    if (ip_.is_v4()) {
        interface->DeleteRoute(old_vrf->GetName(), ip_, plen_);
    } else if (ip_.is_v6()) {
        interface->DeleteRoute(old_vrf->GetName(), ip_, plen_);
    }
    installed_ = false;
}

void VmInterface::InstanceIp::L2Activate(VmInterface *interface,
                                         bool force_update,
                                         uint32_t old_ethernet_tag) const {

    Ip4Address ipv4(0);
    Ip6Address ipv6;

    if (IsL3Only()) {
        return;
    }

    if (ip_.is_v4()) {
        // check if L3 route is already installed or not 
        if (installed_ == false) {
            return;
        }
        ipv4 = ip_.to_v4();
    } else {
        // check if L3 route is already installed or not 
        if (installed_ == false) {
            return;
        }
        ipv6 = ip_.to_v6();
    }

    if (tracking_ip_ != old_tracking_ip_) {
        force_update = true;
    }

    if (l2_installed_ == false || force_update) {
        interface->UpdateL2InterfaceRoute(false, force_update,
                               interface->vrf(), ipv4, ipv6,
                               old_ethernet_tag, false,
                               false, ipv4, ipv6,
                               interface->vm_mac(),
                               tracking_ip_);
        l2_installed_ = true;
    }
}

void VmInterface::InstanceIp::L2DeActivate(VmInterface *interface,
                                           VrfEntry *old_vrf,
                                           uint32_t old_ethernet_tag) const {
    if (l2_installed_ == false) {
        return;
    }

    Ip4Address ipv4(0);
    Ip6Address ipv6;

    if (ip_.is_v4()) {
        ipv4 = ip_.to_v4();
    } else {
        ipv6 = ip_.to_v6();
    }

    interface->DeleteL2InterfaceRoute(true, old_vrf, ipv4, ipv6,
                                      old_ethernet_tag,
                                      interface->vm_mac());
    l2_installed_ = false;
}

void VmInterface::InstanceIp::Activate(VmInterface *interface,
                                       bool force_update, bool l2,
                                       int old_ethernet_tag) const {
    if (l2) {
        L2Activate(interface, force_update, old_ethernet_tag);
    } else {
        L3Activate(interface, force_update);
    }
}

void VmInterface::InstanceIp::DeActivate(VmInterface *interface, bool l2,
                                         VrfEntry *old_vrf,
                                         uint32_t old_ethernet_tag) const {
    if (l2) {
        L2DeActivate(interface, old_vrf, old_ethernet_tag);
    } else {
        L3DeActivate(interface, old_vrf);
    }
}

void VmInterface::InstanceIp::SetPrefixForAllocUnitIpam(
                                  VmInterface *interface) const {
    uint32_t alloc_unit = interface->vn()->GetAllocUnitFromIpam(ip_);

    uint8_t alloc_prefix = 0;
    if (alloc_unit > 0) {
        alloc_prefix = log2(alloc_unit);
    }

    if (ip_.is_v4()) {
        plen_ = Address::kMaxV4PrefixLen - alloc_prefix;
    } else if (ip_.is_v6()) {
        plen_ = Address::kMaxV6PrefixLen - alloc_prefix;
    }
}

void VmInterface::InstanceIpList::Insert(const InstanceIp *rhs) {
    list_.insert(*rhs);
}

void VmInterface::InstanceIpList::Update(const InstanceIp *lhs,
                                         const InstanceIp *rhs) {
    if (lhs->ecmp_ != rhs->ecmp_) {
        lhs->ecmp_ = rhs->ecmp_;
    }

    lhs->is_service_health_check_ip_ = rhs->is_service_health_check_ip_;
    lhs->is_local_ = rhs->is_local_;

    lhs->old_tracking_ip_ = lhs->tracking_ip_;
    lhs->tracking_ip_ = rhs->tracking_ip_;

    lhs->set_del_pending(false);
}

void VmInterface::InstanceIpList::Remove(InstanceIpSet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::FatFlowList::Insert(const FatFlowEntry *rhs) {
    list_.insert(*rhs);
}

void VmInterface::FatFlowList::Update(const FatFlowEntry *lhs,
                                      const FatFlowEntry *rhs) {
}

void VmInterface::FatFlowList::Remove(FatFlowEntrySet::iterator &it) {
    it->set_del_pending(true);
}

/////////////////////////////////////////////////////////////////////////////
// FloatingIp routines
/////////////////////////////////////////////////////////////////////////////

VmInterface::FloatingIp::FloatingIp() : 
    ListEntry(), floating_ip_(), vn_(NULL),
    vrf_(NULL, this), vrf_name_(""), vn_uuid_(), l2_installed_(false),
    fixed_ip_(), force_l3_update_(false), force_l2_update_(false) {
}

VmInterface::FloatingIp::FloatingIp(const FloatingIp &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_),
    floating_ip_(rhs.floating_ip_), vn_(rhs.vn_), vrf_(rhs.vrf_, this),
    vrf_name_(rhs.vrf_name_), vn_uuid_(rhs.vn_uuid_),
    l2_installed_(rhs.l2_installed_), fixed_ip_(rhs.fixed_ip_),
    force_l3_update_(rhs.force_l3_update_),
    force_l2_update_(rhs.force_l2_update_) {
}

VmInterface::FloatingIp::FloatingIp(const IpAddress &addr,
                                    const std::string &vrf,
                                    const boost::uuids::uuid &vn_uuid,
                                    const IpAddress &fixed_ip) :
    ListEntry(), floating_ip_(addr), vn_(NULL), vrf_(NULL, this), vrf_name_(vrf),
    vn_uuid_(vn_uuid), l2_installed_(false), fixed_ip_(fixed_ip),
    force_l3_update_(false), force_l2_update_(false){
}

VmInterface::FloatingIp::~FloatingIp() {
}

bool VmInterface::FloatingIp::operator() (const FloatingIp &lhs,
                                          const FloatingIp &rhs) const {
    return lhs.IsLess(&rhs);
}

// Compare key for FloatingIp. Key is <floating_ip_ and vrf_name_> for both
// Config and Operational processing
bool VmInterface::FloatingIp::IsLess(const FloatingIp *rhs) const {
    if (floating_ip_ != rhs->floating_ip_)
        return floating_ip_ < rhs->floating_ip_;

    return (vrf_name_ < rhs->vrf_name_);
}

void VmInterface::FloatingIp::L3Activate(VmInterface *interface,
                                         bool force_update) const {
    // Add route if not installed or if force requested
    if (installed_ && force_update == false && force_l3_update_ == false) {
        return;
    }

    fixed_ip_ = GetFixedIp(interface);

    InterfaceTable *table =
        static_cast<InterfaceTable *>(interface->get_table());

    if (floating_ip_.is_v4()) {
        interface->AddRoute(vrf_.get()->GetName(), floating_ip_.to_v4(),
                        Address::kMaxV4PrefixLen, vn_->GetName(), false,
                        interface->ecmp(), false, false, Ip4Address(0),
                        fixed_ip_, CommunityList(),
                        interface->label());
        if (table->update_floatingip_cb().empty() == false) {
            table->update_floatingip_cb()(interface, vn_.get(),
                                          floating_ip_.to_v4(), false);
        }
    } else if (floating_ip_.is_v6()) {
        interface->AddRoute(vrf_.get()->GetName(), floating_ip_.to_v6(),
                        Address::kMaxV6PrefixLen, vn_->GetName(), false,
                        interface->ecmp6(), false, false, Ip6Address(),
                        fixed_ip_, CommunityList(),
                        interface->label());
        //TODO:: callback for DNS handling
    }

    installed_ = true;
    force_l3_update_ = false;
}

void VmInterface::FloatingIp::L3DeActivate(VmInterface *interface) const {
    if (installed_ == false)
        return;

    if (floating_ip_.is_v4()) {
        interface->DeleteRoute(vrf_.get()->GetName(), floating_ip_,
                               Address::kMaxV4PrefixLen);
        InterfaceTable *table =
            static_cast<InterfaceTable *>(interface->get_table());
        if (table->update_floatingip_cb().empty() == false) {
            table->update_floatingip_cb()(interface, vn_.get(),
                                          floating_ip_.to_v4(), true);
        }
    } else if (floating_ip_.is_v6()) {
        interface->DeleteRoute(vrf_.get()->GetName(), floating_ip_,
                               Address::kMaxV6PrefixLen);
        //TODO:: callback for DNS handling
    }
    installed_ = false;
}

void VmInterface::FloatingIp::L2Activate(VmInterface *interface,
                                         bool force_update,
                                         uint32_t old_ethernet_tag) const {
    // Add route if not installed or if force requested
    if (l2_installed_ && force_update == false &&
            force_l2_update_ == false) {
        return;
    }

    SecurityGroupList sg_id_list;
    interface->CopySgIdList(&sg_id_list);

    PathPreference path_preference;
    interface->SetPathPreference(&path_preference, false, GetFixedIp(interface));

    EvpnAgentRouteTable *evpn_table = static_cast<EvpnAgentRouteTable *>
        (vrf_->GetEvpnRouteTable());
    //Agent *agent = evpn_table->agent();
    if (old_ethernet_tag != interface->ethernet_tag()) {
        L2DeActivate(interface, old_ethernet_tag);
    }
    evpn_table->AddReceiveRoute(interface->peer_.get(), vrf_->GetName(),
                                interface->l2_label(),
                                interface->vm_mac(),
                                floating_ip_, interface->ethernet_tag(),
                                vn_->GetName(), path_preference);
    l2_installed_ = true;
    force_l2_update_ = false;
}

void VmInterface::FloatingIp::L2DeActivate(VmInterface *interface,
                                           uint32_t ethernet_tag) const {
    if (l2_installed_ == false)
        return;

    EvpnAgentRouteTable *evpn_table = static_cast<EvpnAgentRouteTable *>
        (vrf_->GetEvpnRouteTable());
    if (evpn_table) {
        evpn_table->DelLocalVmRoute(interface->peer_.get(), vrf_->GetName(),
                                    interface->vm_mac(),
                                    interface, floating_ip_, ethernet_tag);
    }
    //Reset the interface ethernet_tag
    l2_installed_ = false;
}

void VmInterface::FloatingIp::Activate(VmInterface *interface,
                                       bool force_update, bool l2,
                                       uint32_t old_ethernet_tag) const {
    InterfaceTable *table =
        static_cast<InterfaceTable *>(interface->get_table());

    if (vn_.get() == NULL) {
        vn_ = table->FindVnRef(vn_uuid_);
        assert(vn_.get());
    }

    if (vrf_.get() == NULL) {
        vrf_ = table->FindVrfRef(vrf_name_);
        assert(vrf_.get());
    }

    if (l2)
        L2Activate(interface, force_update, old_ethernet_tag);
    else
        L3Activate(interface, force_update);
}

void VmInterface::FloatingIp::DeActivate(VmInterface *interface, bool l2,
                                         uint32_t old_ethernet_tag) const{
    if (l2)
        L2DeActivate(interface, old_ethernet_tag);
    else
        L3DeActivate(interface);

    if (installed_ == false && l2_installed_ == false)
        vrf_ = NULL;
}

const IpAddress
VmInterface::FloatingIp::GetFixedIp(const VmInterface *interface) const {
    if (fixed_ip_.is_unspecified()) {
        if (floating_ip_.is_v4() == true) {
            return interface->primary_ip_addr();
        } else {
            return interface->primary_ip6_addr();
        }
    }
    return fixed_ip_;
}

void VmInterface::FloatingIpList::Insert(const FloatingIp *rhs) {
    std::pair<FloatingIpSet::iterator, bool> ret = list_.insert(*rhs);
    if (ret.second) {
        if (rhs->floating_ip_.is_v4()) {
            v4_count_++;
        } else {
            v6_count_++;
        }
    }
}

void VmInterface::FloatingIpList::Update(const FloatingIp *lhs,
                                         const FloatingIp *rhs) {
    if (lhs->fixed_ip_ != rhs->fixed_ip_) {
        lhs->fixed_ip_ = rhs->fixed_ip_;
        lhs->force_l3_update_ = true;
        lhs->force_l2_update_ = true;
    }
    lhs->set_del_pending(false);
}

void VmInterface::FloatingIpList::Remove(FloatingIpSet::iterator &it) {
    it->set_del_pending(true);
}

/////////////////////////////////////////////////////////////////////////////
// AliasIp routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::AliasIp::AliasIp() :
    ListEntry(), alias_ip_(), vn_(NULL),
    vrf_(NULL, this), vrf_name_(""), vn_uuid_(), force_update_(false) {
}

VmInterface::AliasIp::AliasIp(const AliasIp &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_),
    alias_ip_(rhs.alias_ip_), vn_(rhs.vn_), vrf_(rhs.vrf_, this),
    vrf_name_(rhs.vrf_name_), vn_uuid_(rhs.vn_uuid_),
    force_update_(rhs.force_update_) {
}

VmInterface::AliasIp::AliasIp(const IpAddress &addr,
                              const std::string &vrf,
                              const boost::uuids::uuid &vn_uuid) :
    ListEntry(), alias_ip_(addr), vn_(NULL), vrf_(NULL, this), vrf_name_(vrf),
    vn_uuid_(vn_uuid), force_update_(false) {
}

VmInterface::AliasIp::~AliasIp() {
}

bool VmInterface::AliasIp::operator() (const AliasIp &lhs,
                                       const AliasIp &rhs) const {
    return lhs.IsLess(&rhs);
}

// Compare key for AliasIp. Key is <alias_ip_ and vrf_name_> for both
// Config and Operational processing
bool VmInterface::AliasIp::IsLess(const AliasIp *rhs) const {
    if (alias_ip_ != rhs->alias_ip_)
        return alias_ip_ < rhs->alias_ip_;

    return (vrf_name_ < rhs->vrf_name_);
}

void VmInterface::AliasIp::Activate(VmInterface *interface,
                                    bool force_update) const {
    InterfaceTable *table =
        static_cast<InterfaceTable *>(interface->get_table());

    if (vn_.get() == NULL) {
        vn_ = table->FindVnRef(vn_uuid_);
        assert(vn_.get());
    }

    if (vrf_.get() == NULL) {
        vrf_ = table->FindVrfRef(vrf_name_);
        assert(vrf_.get());
    }

    // Add route if not installed or if force requested
    if (installed_ && force_update == false && force_update_ == false) {
        return;
    }

    if (alias_ip_.is_v4()) {
        interface->AddRoute(vrf_.get()->GetName(), alias_ip_.to_v4(), 32,
                            vn_->GetName(), false, interface->ecmp(), false,
                            false, Ip4Address(0), Ip4Address(0),
                            CommunityList(), interface->label());
    } else if (alias_ip_.is_v6()) {
        interface->AddRoute(vrf_.get()->GetName(), alias_ip_.to_v6(), 128,
                            vn_->GetName(), false, interface->ecmp6(), false,
                            false, Ip6Address(), Ip6Address(), CommunityList(),
                            interface->label());
    }

    installed_ = true;
    force_update_ = false;
}

void VmInterface::AliasIp::DeActivate(VmInterface *interface) const {
    if (installed_) {
        if (alias_ip_.is_v4()) {
            interface->DeleteRoute(vrf_.get()->GetName(), alias_ip_, 32);
        } else if (alias_ip_.is_v6()) {
            interface->DeleteRoute(vrf_.get()->GetName(), alias_ip_, 128);
        }
        installed_ = false;
    }

    vrf_ = NULL;
}

void VmInterface::AliasIpList::Insert(const AliasIp *rhs) {
    std::pair<AliasIpSet::iterator, bool> ret = list_.insert(*rhs);
    if (ret.second) {
        if (rhs->alias_ip_.is_v4()) {
            v4_count_++;
        } else {
            v6_count_++;
        }
    }
}

void VmInterface::AliasIpList::Update(const AliasIp *lhs,
                                      const AliasIp *rhs) {
    lhs->set_del_pending(false);
}

void VmInterface::AliasIpList::Remove(AliasIpSet::iterator &it) {
    it->set_del_pending(true);
}


/////////////////////////////////////////////////////////////////////////////
// StaticRoute routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::StaticRoute::StaticRoute() :
    ListEntry(), vrf_(""), addr_(), plen_(0), gw_(), communities_() {
}

VmInterface::StaticRoute::StaticRoute(const StaticRoute &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_), vrf_(rhs.vrf_),
    addr_(rhs.addr_), plen_(rhs.plen_), gw_(rhs.gw_),
    communities_(rhs.communities_) {
}

VmInterface::StaticRoute::StaticRoute(const std::string &vrf,
                                      const IpAddress &addr,
                                      uint32_t plen, const IpAddress &gw,
                                      const CommunityList &communities) :
    ListEntry(), vrf_(vrf), addr_(addr), plen_(plen), gw_(gw),
    communities_(communities) {
}

VmInterface::StaticRoute::~StaticRoute() {
}

bool VmInterface::StaticRoute::operator() (const StaticRoute &lhs,
                                           const StaticRoute &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::StaticRoute::IsLess(const StaticRoute *rhs) const {
#if 0
    //Enable once we can add static routes across vrf
    if (vrf_name_ != rhs->vrf_name_)
        return vrf_name_ < rhs->vrf_name_;
#endif

    if (addr_ != rhs->addr_)
        return addr_ < rhs->addr_;

    if (plen_ != rhs->plen_) {
        return plen_ < rhs->plen_;
    }

    return gw_ < rhs->gw_;
}

void VmInterface::StaticRoute::Activate(VmInterface *interface,
                                        bool force_update) const {
    if (installed_ && force_update == false)
        return;

    if (vrf_ != interface->vrf()->GetName()) {
        vrf_ = interface->vrf()->GetName();
    }

    if (installed_ == false || force_update) {
        Ip4Address gw_ip(0);
        if (gw_.is_v4() && addr_.is_v4() && gw_.to_v4() != gw_ip) {
            SecurityGroupList sg_id_list;
            interface->CopySgIdList(&sg_id_list);
            InetUnicastAgentRouteTable::AddGatewayRoute(interface->peer_.get(),
                    vrf_, addr_.to_v4(),
                    plen_, gw_.to_v4(), interface->vn_->GetName(),
                    interface->vrf_->table_label(),
                    sg_id_list, communities_);
        } else {
            IpAddress dependent_ip;
            bool ecmp = false;
            if (addr_.is_v4()) {
                dependent_ip = interface->primary_ip_addr();
                ecmp = interface->ecmp();
            } else if (addr_.is_v6()) {
                dependent_ip = interface->primary_ip6_addr();
                ecmp = interface->ecmp6();
            }
            interface->AddRoute(vrf_, addr_, plen_,
                                interface->vn_->GetName(),
                                false, ecmp, false, false,
                                interface->GetServiceIp(addr_),
                                dependent_ip, communities_,
                                interface->label());
        }
    }

    installed_ = true;
}

void VmInterface::StaticRoute::DeActivate(VmInterface *interface) const {
    if (installed_ == false)
        return;
    interface->DeleteRoute(vrf_, addr_, plen_);
    installed_ = false;
}

void VmInterface::StaticRouteList::Insert(const StaticRoute *rhs) {
    list_.insert(*rhs);
}

void VmInterface::StaticRouteList::Update(const StaticRoute *lhs,
                                          const StaticRoute *rhs) {
    if (lhs->communities_ != rhs->communities_) {
        (const_cast<StaticRoute *>(lhs))->communities_ = rhs->communities_;
    }
    lhs->set_del_pending(false);
}

void VmInterface::StaticRouteList::Remove(StaticRouteSet::iterator &it) {
    it->set_del_pending(true);
}

///////////////////////////////////////////////////////////////////////////////
//Allowed addresss pair route
///////////////////////////////////////////////////////////////////////////////
VmInterface::AllowedAddressPair::AllowedAddressPair() :
    ListEntry(), vrf_(""), addr_(), plen_(0), ecmp_(false), mac_(),
    l2_entry_installed_(false), ecmp_config_changed_(false), ethernet_tag_(0),
    vrf_ref_(NULL, this), service_ip_(), label_(MplsTable::kInvalidLabel),
    policy_enabled_nh_(NULL), policy_disabled_nh_(NULL) {
}

VmInterface::AllowedAddressPair::AllowedAddressPair(
    const AllowedAddressPair &rhs) : ListEntry(rhs.installed_,
    rhs.del_pending_), vrf_(rhs.vrf_), addr_(rhs.addr_), plen_(rhs.plen_),
    ecmp_(rhs.ecmp_), mac_(rhs.mac_),
    l2_entry_installed_(rhs.l2_entry_installed_),
    ecmp_config_changed_(rhs.ecmp_config_changed_),
    ethernet_tag_(rhs.ethernet_tag_), vrf_ref_(rhs.vrf_ref_, this),
    service_ip_(rhs.service_ip_), label_(rhs.label_),
    policy_enabled_nh_(rhs.policy_enabled_nh_),
    policy_disabled_nh_(rhs.policy_disabled_nh_) {
}

VmInterface::AllowedAddressPair::AllowedAddressPair(const std::string &vrf,
                                                    const IpAddress &addr,
                                                    uint32_t plen, bool ecmp,
                                                    const MacAddress &mac) :
    ListEntry(), vrf_(vrf), addr_(addr), plen_(plen), ecmp_(ecmp), mac_(mac),
    l2_entry_installed_(false), ecmp_config_changed_(false), ethernet_tag_(0),
    vrf_ref_(NULL, this), label_(MplsTable::kInvalidLabel),
    policy_enabled_nh_(NULL), policy_disabled_nh_(NULL) {
}

VmInterface::AllowedAddressPair::~AllowedAddressPair() {
}

bool VmInterface::AllowedAddressPair::operator() (const AllowedAddressPair &lhs,
                                                  const AllowedAddressPair &rhs) 
                                                  const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::AllowedAddressPair::IsLess(const AllowedAddressPair *rhs) const {
#if 0
    //Enable once we can add static routes across vrf
    if (vrf_name_ != rhs->vrf_name_)
        return vrf_name_ < rhs->vrf_name_;
#endif

    if (addr_ != rhs->addr_)
        return addr_ < rhs->addr_;

    if (plen_ != rhs->plen_) {
        return plen_ < rhs->plen_;
    }

    return mac_ < rhs->mac_;
}

void VmInterface::AllowedAddressPair::L2Activate(VmInterface *interface,
                                                 bool force_update,
                                                 bool policy_change,
                                                 bool old_layer2_forwarding,
                                                 bool old_layer3_forwarding) const {
    if (mac_ == MacAddress::kZeroMac) {
        return;
    }

    if (l2_entry_installed_ && force_update == false &&
        policy_change == false && ethernet_tag_ == interface->ethernet_tag() &&
        old_layer3_forwarding == interface->layer3_forwarding() &&
        ecmp_config_changed_ == false) {
        return;
    }

    if (vrf_ != interface->vrf()->GetName()) {
        vrf_ = interface->vrf()->GetName();
    }

    vrf_ref_ = interface->vrf();
    if (old_layer3_forwarding != interface->layer3_forwarding() ||
        l2_entry_installed_ == false || ecmp_config_changed_) {
        force_update = true;
    }

    if (ethernet_tag_ != interface->ethernet_tag()) {
        force_update = true;
    }

    if (l2_entry_installed_ == false || force_update || policy_change) {
        IpAddress dependent_rt;
        Ip4Address v4ip(0);
        Ip6Address v6ip;
        if (addr_.is_v4()) {
            dependent_rt = Ip4Address(0);
            v4ip = addr_.to_v4();
        } else if (addr_.is_v6()) {
            dependent_rt = Ip6Address();
            v6ip = addr_.to_v6();
        }

        if (interface->bridging()) {
            interface->UpdateL2InterfaceRoute(old_layer2_forwarding,
                                   force_update, interface->vrf(), v4ip, v6ip,
                                   ethernet_tag_, old_layer3_forwarding,
                                   policy_change, v4ip, v6ip, mac_,
                                   dependent_rt);
        }
        ethernet_tag_ = interface->ethernet_tag();
        //If layer3 forwarding is disabled
        //  * IP + mac allowed address pair should not be published
        //  * Only mac allowed address pair should be published
        //Logic for same is present in UpdateL2InterfaceRoute
        if (interface->layer3_forwarding() || addr_.is_unspecified() == true) {
            l2_entry_installed_ = true;
        } else {
            l2_entry_installed_ = false;
        }
        ecmp_config_changed_ = false;
    }
}

void VmInterface::AllowedAddressPair::L2DeActivate(VmInterface *interface) const{
    if (mac_ == MacAddress::kZeroMac) {
        return;
    }

    if (l2_entry_installed_ == false) {
        return;
    }

    Ip4Address v4ip(0);
    Ip6Address v6ip;
    if (addr_.is_v4()) {
        v4ip = addr_.to_v4();
    } else if (addr_.is_v6()) {
        v6ip = addr_.to_v6();
    }
    interface->DeleteL2InterfaceRoute(true, vrf_ref_.get(), v4ip,
                                      v6ip, ethernet_tag_, mac_);
    l2_entry_installed_ = false;
    vrf_ref_ = NULL;
}

void VmInterface::AllowedAddressPair::CreateLabelAndNH(Agent *agent,
                                                       VmInterface *interface,
                                                       bool policy_change)
                                                       const {
    uint32_t old_label = MplsTable::kInvalidLabel;
    //Allocate a new L3 label with proper layer 2
    //rewrite information
    if (label_ == MplsTable::kInvalidLabel) {
        label_ = agent->mpls_table()->AllocLabel();
    } else if (policy_change) {
        old_label = label_;
        label_ = agent->mpls_table()->AllocLabel();
        MplsLabel::Delete(interface->agent(), old_label);
    }

    InterfaceNH::CreateL3VmInterfaceNH(interface->GetUuid(), mac_,
                                       interface->vrf_->GetName());

    VmInterfaceKey vmi_key(AgentKey::ADD_DEL_CHANGE, interface->GetUuid(),
                           interface->name());
    InterfaceNHKey key(vmi_key.Clone(), false, InterfaceNHFlags::INET4, mac_);
    InterfaceNH *nh = static_cast<InterfaceNH *>(agent->nexthop_table()->
                                                 FindActiveEntry(&key));
    policy_disabled_nh_ = nh;
    //Ensure nexthop to be deleted upon refcount falling to 0
    nh->set_delete_on_zero_refcount(true);

    InterfaceNHKey key1(vmi_key.Clone(), true, InterfaceNHFlags::INET4, mac_);
    nh = static_cast<InterfaceNH *>(agent->
                                    nexthop_table()->FindActiveEntry(&key1));
    //Ensure nexthop to be deleted upon refcount falling to 0
    nh->set_delete_on_zero_refcount(true);
    policy_enabled_nh_ = nh;

    MplsLabel::CreateVPortLabel(agent, label_, interface->GetUuid(),
                                interface->policy_enabled(),
                                InterfaceNHFlags::INET4, mac_);
}

void VmInterface::AllowedAddressPair::Activate(VmInterface *interface,
                                               bool force_update,
                                               bool policy_change) const {
    IpAddress ip = interface->GetServiceIp(addr_);

    if (installed_ && force_update == false && service_ip_ == ip &&
        policy_change == false && ecmp_config_changed_ == false) {
        return;
    }

    Agent *agent = interface->agent();
    if (vrf_ != interface->vrf()->GetName()) {
        vrf_ = interface->vrf()->GetName();
    }

    if (installed_ == false || force_update || service_ip_ != ip ||
        policy_change || ecmp_config_changed_) {
        service_ip_ = ip;
        if (mac_ == MacAddress::kZeroMac ||
            mac_ == interface->vm_mac_) {
            interface->AddRoute(vrf_, addr_, plen_, interface->vn_->GetName(),
                                false, ecmp_, false, false, service_ip_,
                                Ip4Address(0), CommunityList(),
                                interface->label());
        } else {
            CreateLabelAndNH(agent, interface, policy_change);
            interface->AddRoute(vrf_, addr_, plen_, interface->vn_->GetName(),
                                false, ecmp_, false, false, service_ip_,
                                Ip6Address(), CommunityList(), label_);
        }
    }
    installed_ = true;
    ecmp_config_changed_ = false;
}

void VmInterface::AllowedAddressPair::DeActivate(VmInterface *interface) const {
    if (installed_ == false)
        return;
    interface->DeleteRoute(vrf_, addr_, plen_);
    MplsLabel::Delete(interface->agent(), label_);
    label_ = MplsTable::kInvalidLabel;
    policy_enabled_nh_ = NULL;
    policy_disabled_nh_ = NULL;
    installed_ = false;
}

void VmInterface::AllowedAddressPairList::Insert(const AllowedAddressPair *rhs) {
    list_.insert(*rhs);
}

void VmInterface::AllowedAddressPairList::Update(const AllowedAddressPair *lhs,
                                          const AllowedAddressPair *rhs) {
    lhs->set_del_pending(false);
    if (lhs->ecmp_ != rhs->ecmp_) {
        lhs->ecmp_ = rhs->ecmp_;
        lhs->ecmp_config_changed_ = true;
    }
}

void VmInterface::AllowedAddressPairList::Remove(AllowedAddressPairSet::iterator &it) {
    it->set_del_pending(true);
}
/////////////////////////////////////////////////////////////////////////////
// SecurityGroup routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::SecurityGroupEntry::SecurityGroupEntry() : 
    ListEntry(), uuid_(nil_uuid()) {
}

VmInterface::SecurityGroupEntry::SecurityGroupEntry
    (const SecurityGroupEntry &rhs) : 
        ListEntry(rhs.installed_, rhs.del_pending_), uuid_(rhs.uuid_) {
}

VmInterface::SecurityGroupEntry::SecurityGroupEntry(const uuid &u) : 
    ListEntry(), uuid_(u) {
}

VmInterface::SecurityGroupEntry::~SecurityGroupEntry() {
}

bool VmInterface::SecurityGroupEntry::operator ==
    (const SecurityGroupEntry &rhs) const {
    return uuid_ == rhs.uuid_;
}

bool VmInterface::SecurityGroupEntry::operator() 
    (const SecurityGroupEntry &lhs, const SecurityGroupEntry &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::SecurityGroupEntry::IsLess
    (const SecurityGroupEntry *rhs) const {
    return uuid_ < rhs->uuid_;
}

void VmInterface::SecurityGroupEntry::Activate(VmInterface *interface) const {
    if (sg_.get() != NULL)
        return; 

    Agent *agent = static_cast<InterfaceTable *>
        (interface->get_table())->agent();
    SgKey sg_key(uuid_);
    sg_ = static_cast<SgEntry *> 
        (agent->sg_table()->FindActiveEntry(&sg_key));
}

void VmInterface::SecurityGroupEntry::DeActivate(VmInterface *interface) const {
}

void VmInterface::SecurityGroupEntryList::Insert
    (const SecurityGroupEntry *rhs) {
    list_.insert(*rhs);
}

void VmInterface::SecurityGroupEntryList::Update
        (const SecurityGroupEntry *lhs, const SecurityGroupEntry *rhs) {
}

void VmInterface::SecurityGroupEntryList::Remove
        (SecurityGroupEntrySet::iterator &it) {
    it->set_del_pending(true);
}

/////////////////////////////////////////////////////////////////////////////
// ServiceVlan routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::ServiceVlan::ServiceVlan() :
    ListEntry(), tag_(0), vrf_name_(""), addr_(0), addr6_(), smac_(), dmac_(),
    vrf_(NULL, this), label_(MplsTable::kInvalidLabel), v4_rt_installed_(false),
    v6_rt_installed_(false) {
}

VmInterface::ServiceVlan::ServiceVlan(const ServiceVlan &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_), tag_(rhs.tag_),
    vrf_name_(rhs.vrf_name_), addr_(rhs.addr_), addr6_(rhs.addr6_),
    smac_(rhs.smac_), dmac_(rhs.dmac_), vrf_(rhs.vrf_, this),
    label_(rhs.label_), v4_rt_installed_(rhs.v4_rt_installed_),
    v6_rt_installed_(rhs.v6_rt_installed_) {
}

VmInterface::ServiceVlan::ServiceVlan(uint16_t tag, const std::string &vrf_name,
                                      const Ip4Address &addr,
                                      const Ip6Address &addr6,
                                      const MacAddress &smac,
                                      const MacAddress &dmac) :
    ListEntry(), tag_(tag), vrf_name_(vrf_name), addr_(addr), addr6_(addr6),
    smac_(smac), dmac_(dmac), vrf_(NULL, this), label_(MplsTable::kInvalidLabel)
    , v4_rt_installed_(false), v6_rt_installed_(false) {
}

VmInterface::ServiceVlan::~ServiceVlan() {
}

bool VmInterface::ServiceVlan::operator() (const ServiceVlan &lhs,
                                           const ServiceVlan &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::ServiceVlan::IsLess(const ServiceVlan *rhs) const {
    return tag_ < rhs->tag_;
}

void VmInterface::ServiceVlan::Activate(VmInterface *interface,
                                        bool force_update,
                                        bool old_ipv4_active,
                                        bool old_ipv6_active) const {
    InterfaceTable *table =
        static_cast<InterfaceTable *>(interface->get_table());
    VrfEntry *vrf = table->FindVrfRef(vrf_name_);
    if (!vrf) {
        /* If vrf is delete marked VMI will eventually get delete of link which
         * will trigger ServiceVlan::DeActivate */
        return;
    }

    //Change in VRF, delete and readd the entry
    if (vrf_.get() != vrf) {
        DeActivate(interface);
        vrf_ = vrf;
    }

    if (label_ == MplsTable::kInvalidLabel) {
        VlanNH::Create(interface->GetUuid(), tag_, vrf_name_, smac_, dmac_);
        label_ = table->agent()->mpls_table()->AllocLabel();
        MplsLabel::CreateVlanNh(table->agent(), label_,
                                interface->GetUuid(), tag_);
        VrfAssignTable::CreateVlan(interface->GetUuid(), vrf_name_, tag_);
    }

    if (!interface->ipv4_active() && !interface->ipv6_active() &&
        (old_ipv4_active || old_ipv6_active)) {
        V4RouteDelete(interface->peer());
        V6RouteDelete(interface->peer());
    }

    if (!old_ipv4_active && !old_ipv6_active &&
        (interface->ipv4_active() || interface->ipv6_active())) {
        installed_ = false;
    }

    if (installed_ && force_update == false)
        return;

    interface->ServiceVlanRouteAdd(*this, force_update);
    installed_ = true;
}

void VmInterface::ServiceVlan::V4RouteDelete(const Peer *peer) const {
    if (v4_rt_installed_) {
        InetUnicastAgentRouteTable::Delete(peer, vrf_->GetName(), addr_,
                                        Address::kMaxV4PrefixLen);
        v4_rt_installed_ = false;
    }
}

void VmInterface::ServiceVlan::V6RouteDelete(const Peer *peer) const {
    if (v6_rt_installed_) {
        InetUnicastAgentRouteTable::Delete(peer, vrf_->GetName(), addr6_,
                                        Address::kMaxV6PrefixLen);
        v6_rt_installed_ = false;
    }
}

void VmInterface::ServiceVlan::DeActivate(VmInterface *interface) const {
    if (label_ != MplsTable::kInvalidLabel) {
        VrfAssignTable::DeleteVlan(interface->GetUuid(), tag_);
        interface->ServiceVlanRouteDel(*this);
        Agent *agent =
            static_cast<InterfaceTable *>(interface->get_table())->agent();
        MplsLabel::Delete(agent, label_);
        label_ = MplsTable::kInvalidLabel;
        VlanNH::Delete(interface->GetUuid(), tag_);
        vrf_ = NULL;
    }
    installed_ = false;
    return;
}

void VmInterface::ServiceVlanList::Insert(const ServiceVlan *rhs) {
    list_.insert(*rhs);
}

void VmInterface::ServiceVlanList::Update(const ServiceVlan *lhs,
                                          const ServiceVlan *rhs) {
    lhs->vrf_name_ = rhs->vrf_name_;
    lhs->set_del_pending(false);
}

void VmInterface::ServiceVlanList::Remove(ServiceVlanSet::iterator &it) {
    it->set_del_pending(true);
}

const MacAddress&
VmInterface::GetIpMac(const IpAddress &ip, uint8_t plen) const {
    AllowedAddressPairSet::const_iterator it =
        allowed_address_pair_list_.list_.begin();
    while (it != allowed_address_pair_list_.list_.end()) {
        if (it->addr_ == ip && it->plen_ == plen &&
            it->mac_ != MacAddress::kZeroMac) {
            return it->mac_;
        }
        it++;
    }
    return vm_mac_;
}

uint32_t VmInterface::GetServiceVlanLabel(const VrfEntry *vrf) const {
    ServiceVlanSet::const_iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        if (it->vrf_.get() == vrf) {
            return it->label_;
        }
        it++;
    }
    return 0;
}

uint32_t VmInterface::GetServiceVlanTag(const VrfEntry *vrf) const {
    ServiceVlanSet::const_iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        if (it->vrf_.get() == vrf) {
            return it->tag_;
        }
        it++;
    }
    return 0;
}

const VrfEntry* VmInterface::GetServiceVlanVrf(uint16_t vlan_tag) const {
    ServiceVlanSet::const_iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        if (it->tag_ == vlan_tag) {
            return it->vrf_.get();
        }
        it++;
    }
    return NULL;
}

void VmInterface::ServiceVlanRouteAdd(const ServiceVlan &entry,
                                      bool force_update) {
    if (vrf_.get() == NULL ||
        vn_.get() == NULL) {
        return;
    }

    if (!ipv4_active_ && !ipv6_active_) {
        return;
    }

    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);

    // With IRB model, add L2 Receive route for SMAC and DMAC to ensure
    // packets from service vm go thru routing
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf_->GetBridgeRouteTable());
    table->AddBridgeReceiveRoute(peer_.get(), entry.vrf_->GetName(),
                                 0, entry.dmac_, vn()->GetName());
    table->AddBridgeReceiveRoute(peer_.get(), entry.vrf_->GetName(),
                                 0, entry.smac_, vn()->GetName());
    VnListType vn_list;
    vn_list.insert(vn()->GetName());
    if (force_update ||
        (!entry.v4_rt_installed_ && !entry.addr_.is_unspecified())) {
        PathPreference path_preference;
        SetServiceVlanPathPreference(&path_preference, entry.addr_);

        InetUnicastAgentRouteTable::AddVlanNHRoute
            (peer_.get(), entry.vrf_->GetName(), entry.addr_,
             Address::kMaxV4PrefixLen, GetUuid(), entry.tag_, entry.label_,
             vn_list, sg_id_list, path_preference);
        entry.v4_rt_installed_ = true;
    }
    if ((!entry.v6_rt_installed_ && !entry.addr6_.is_unspecified()) ||
        force_update) {
        PathPreference path_preference;
        SetServiceVlanPathPreference(&path_preference, entry.addr6_);

        InetUnicastAgentRouteTable::AddVlanNHRoute
            (peer_.get(), entry.vrf_->GetName(), entry.addr6_,
             Address::kMaxV6PrefixLen, GetUuid(), entry.tag_, entry.label_,
             vn_list, sg_id_list, path_preference);
        entry.v6_rt_installed_ = true;
    }

    entry.installed_ = true;
    return;
}

void VmInterface::ServiceVlanRouteDel(const ServiceVlan &entry) {
    if (entry.installed_ == false) {
        return;
    }

    entry.V4RouteDelete(peer_.get());
    entry.V6RouteDelete(peer_.get());

    // Delete the L2 Recive routes added for smac_ and dmac_
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (entry.vrf_->GetBridgeRouteTable());
    if (table) {
        table->Delete(peer_.get(), entry.vrf_->GetName(), entry.dmac_, 0);
        table->Delete(peer_.get(), entry.vrf_->GetName(), entry.smac_, 0);
    }
    entry.installed_ = false;
    return;
}

bool VmInterface::HasFloatingIp(Address::Family family) const {
    if (family == Address::INET) {
        return floating_ip_list_.v4_count_ > 0;
    } else {
        return floating_ip_list_.v6_count_ > 0;
    }
}

bool VmInterface::HasFloatingIp() const {
    return floating_ip_list_.list_.size() != 0;
}

bool VmInterface::IsFloatingIp(const IpAddress &ip) const {
    VmInterface::FloatingIpSet::const_iterator it =
        floating_ip_list_.list_.begin();
    while(it != floating_ip_list_.list_.end()) {
        if ((*it).floating_ip_ == ip) {
            return true;
        }
        it++;
    }
    return false;
}

VrfEntry *VmInterface::GetAliasIpVrf(const IpAddress &ip) const {
    // Look for matching Alias IP
    VmInterface::AliasIpSet::const_iterator it =
        alias_ip_list_.list_.begin();
    for (; it != alias_ip_list_.list_.end(); ++it) {
        if (it->vrf_.get() == NULL) {
            continue;
        }

        if (ip == it->alias_ip_) {
            return it->vrf_.get();
        }
    }
    return NULL;
}

Ip4Address VmInterface::mdata_ip_addr() const {
    if (mdata_ip_.get() == NULL) {
        return Ip4Address(0);
    }

    return mdata_ip_->GetLinkLocalIp();
}

MetaDataIp *VmInterface::GetMetaDataIp(const Ip4Address &ip) const {
    MetaDataIpMap::const_iterator it = metadata_ip_map_.find(ip);
    if (it != metadata_ip_map_.end()) {
        return it->second;
    }

    return NULL;
}

void VmInterface::InsertMetaDataIpInfo(MetaDataIp *mip) {
    std::pair<MetaDataIpMap::iterator, bool> ret;
    ret = metadata_ip_map_.insert(std::pair<Ip4Address, MetaDataIp*>
                                  (mip->GetLinkLocalIp(), mip));
    assert(ret.second);
}

void VmInterface::DeleteMetaDataIpInfo(MetaDataIp *mip) {
    std::size_t ret = metadata_ip_map_.erase(mip->GetLinkLocalIp());
    assert(ret != 0);
}

void VmInterface::UpdateMetaDataIpInfo() {
    MetaDataIpMap::iterator it = metadata_ip_map_.begin();
    while (it != metadata_ip_map_.end()) {
        it->second->UpdateInterfaceCb();
        it++;
    }
}

void VmInterface::InsertHealthCheckInstance(HealthCheckInstance *hc_inst) {
    std::pair<HealthCheckInstanceSet::iterator, bool> ret;
    ret = hc_instance_set_.insert(hc_inst);
    assert(ret.second);
}

void VmInterface::DeleteHealthCheckInstance(HealthCheckInstance *hc_inst) {
    std::size_t ret = hc_instance_set_.erase(hc_inst);
    assert(ret != 0);
}

const VmInterface::HealthCheckInstanceSet &
VmInterface::hc_instance_set() const {
    return hc_instance_set_;
}

////////////////////////////////////////////////////////////////////////////
// VRF assign rule routines
////////////////////////////////////////////////////////////////////////////
VmInterface::VrfAssignRule::VrfAssignRule():
    ListEntry(), id_(0), vrf_name_(" "), ignore_acl_(false) {
}

VmInterface::VrfAssignRule::VrfAssignRule(const VrfAssignRule &rhs):
    ListEntry(rhs.installed_, rhs.del_pending_), id_(rhs.id_),
    vrf_name_(rhs.vrf_name_), ignore_acl_(rhs.ignore_acl_),
    match_condition_(rhs.match_condition_) {
}

VmInterface::VrfAssignRule::VrfAssignRule(uint32_t id,
    const autogen::MatchConditionType &match_condition, 
    const std::string &vrf_name,
    bool ignore_acl):
    ListEntry(), id_(id), vrf_name_(vrf_name),
    ignore_acl_(ignore_acl), match_condition_(match_condition) {
}

VmInterface::VrfAssignRule::~VrfAssignRule() {
}

bool VmInterface::VrfAssignRule::operator() (const VrfAssignRule &lhs,
                                             const VrfAssignRule &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::VrfAssignRule::IsLess(const VrfAssignRule *rhs) const {
    return id_ < rhs->id_;
}

void VmInterface::VrfAssignRuleList::Insert(const VrfAssignRule *rhs) {
    list_.insert(*rhs);
}

void VmInterface::VrfAssignRuleList::Update(const VrfAssignRule *lhs,
                                            const VrfAssignRule *rhs) {
    lhs->set_del_pending(false);
    lhs->match_condition_ = rhs->match_condition_;
    lhs->ignore_acl_ = rhs->ignore_acl_;
    lhs->vrf_name_ = rhs->vrf_name_;
}

void VmInterface::VrfAssignRuleList::Remove(VrfAssignRuleSet::iterator &it) {
    it->set_del_pending(true);
}

const string VmInterface::GetAnalyzer() const {
    if (mirror_entry()) {
        return mirror_entry()->GetAnalyzerName();
    } else {
        return std::string();
    }
}

void VmInterface::SendTrace(const AgentDBTable *table, Trace event) const {
    InterfaceInfo intf_info;
    intf_info.set_name(name_);
    intf_info.set_index(id_);

    switch(event) {
    case ACTIVATED_IPV4:
        intf_info.set_op("IPV4 Activated");
        break;
    case DEACTIVATED_IPV4:
        intf_info.set_op("IPV4 Deactivated");
        break;
    case ACTIVATED_IPV6:
        intf_info.set_op("IPV6 Activated");
        break;
    case DEACTIVATED_IPV6:
        intf_info.set_op("IPV6 Deactivated");
        break;
    case ACTIVATED_L2:
        intf_info.set_op("L2 Activated");
        break;
    case DEACTIVATED_L2:
        intf_info.set_op("L2 Deactivated");
        break;
    case ADD:
        intf_info.set_op("Add");
        break;
    case DELETE:
        intf_info.set_op("Delete");
        break;

    case FLOATING_IP_CHANGE: {
        intf_info.set_op("Floating IP change");
        std::vector<FloatingIPInfo> fip_list;
        FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
        while (it != floating_ip_list_.list_.end()) {
            const FloatingIp &ip = *it;
            FloatingIPInfo fip;
            fip.set_ip_address(ip.floating_ip_.to_string());
            fip.set_vrf_name(ip.vrf_->GetName());
            fip_list.push_back(fip);
            it++;
        }
        intf_info.set_fip(fip_list);
        break;
    }
    case SERVICE_CHANGE:
        break;
    }

    intf_info.set_ip_address(primary_ip_addr_.to_string());
    if (vm_) {
        intf_info.set_vm(UuidToString(vm_->GetUuid()));
    }
    if (vn_) {
        intf_info.set_vn(vn_->GetName());
    }
    if (vrf_) {
        intf_info.set_vrf(vrf_->GetName());
    }
    intf_info.set_vm_project(UuidToString(vm_project_uuid_));
    OPER_TRACE_ENTRY(Interface,
                     table,
                     intf_info);
}

/////////////////////////////////////////////////////////////////////////////
// VM Interface DB Table utility functions
/////////////////////////////////////////////////////////////////////////////
// Add a VM-Interface
void VmInterface::NovaAdd(InterfaceTable *table, const uuid &intf_uuid,
                          const string &os_name, const Ip4Address &addr,
                          const string &mac, const string &vm_name,
                          const uuid &vm_project_uuid, uint16_t tx_vlan_id,
                          uint16_t rx_vlan_id, const std::string &parent,
                          const Ip6Address &ip6,
                          Interface::Transport transport) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid,
                                     os_name));

    req.data.reset(new VmInterfaceNovaData(addr, ip6, mac, vm_name,
                                           nil_uuid(), vm_project_uuid, parent,
                                           tx_vlan_id, rx_vlan_id,
                                           VmInterface::VM_ON_TAP,
                                           VmInterface::INSTANCE,
                                           transport));
    table->Enqueue(&req);
}

// Delete a VM-Interface
void VmInterface::Delete(InterfaceTable *table, const uuid &intf_uuid,
                         VmInterface::Configurer configurer) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""));

    if (configurer == VmInterface::CONFIG) {
        req.data.reset(new VmInterfaceConfigData(NULL, NULL));
    } else if (configurer == VmInterface::INSTANCE_MSG) {
        req.data.reset(new VmInterfaceNovaData());
    } else {
        assert(0);
    }
    table->Enqueue(&req);
}

bool VmInterface::CopyIp6Address(const Ip6Address &addr) {
    bool ret = false;

    // Retain the old if new IP could not be got
    if (addr.is_unspecified()) {
        return false;
    }

    if (primary_ip6_addr_ != addr) {
        primary_ip6_addr_ = addr;
        ret = true;
    }

    return ret;
}

void VmInterface::UpdateIpv4InstanceIp(bool force_update, bool policy_change,
                                       bool l2, uint32_t old_ethernet_tag,
                                       VrfEntry *old_vrf) {
    if (l2 && old_ethernet_tag != ethernet_tag()) {
        force_update = true;
    }

    InstanceIpSet::iterator it = instance_ipv4_list_.list_.begin();
    while (it != instance_ipv4_list_.list_.end()) {
        InstanceIpSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this, l2, vrf(), old_ethernet_tag);
            if (prev->installed() == false) {
                instance_ipv4_list_.list_.erase(prev);
            }
        } else {
            if (old_vrf && (old_vrf != vrf())) {
                prev->DeActivate(this, l2, old_vrf, old_ethernet_tag);
            }
            prev->Activate(this, force_update||policy_change, l2,
                           old_ethernet_tag);
        }
    }
}

void VmInterface::DeleteIpv4InstanceIp(bool l2, uint32_t old_ethernet_tag,
                                       VrfEntry *old_vrf_entry,
                                       bool force_update) {
    if (l2 && old_ethernet_tag != ethernet_tag()) {
        force_update = true;
    }

    InstanceIpSet::iterator it = instance_ipv4_list_.list_.begin();
    while (it != instance_ipv4_list_.list_.end()) {
        InstanceIpSet::iterator prev = it++;
        if (prev->is_service_health_check_ip_) {
            // for service health check instance ip do not withdraw
            // the route till it is marked del_pending
            // interface itself is not active
            bool interface_active = false;
            if (l2) {
                interface_active = metadata_l2_active_;
            } else {
                interface_active = metadata_ip_active_;
            }
            if (!prev->del_pending_ && interface_active) {
                prev->Activate(this, force_update, l2,
                               old_ethernet_tag);
                continue;
            }
        }
        prev->DeActivate(this, l2, old_vrf_entry, old_ethernet_tag);
        if (prev->del_pending_ && prev->installed() == false) {
            instance_ipv4_list_.list_.erase(prev);
        }
    }
}

void VmInterface::UpdateIpv6InstanceIp(bool force_update, bool policy_change,
                                       bool l2,
                                       uint32_t old_ethernet_tag) {
    if (l2 && old_ethernet_tag != ethernet_tag()) {
        force_update = true;
    }

    InstanceIpSet::iterator it = instance_ipv6_list_.list_.begin();
    while (it != instance_ipv6_list_.list_.end()) {
        InstanceIpSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this, l2, vrf(), old_ethernet_tag);
            if (prev->installed() == false) {
                instance_ipv6_list_.list_.erase(prev);
            }
        } else {
            prev->Activate(this, force_update||policy_change, l2,
                           old_ethernet_tag);
        }
    }
}

void VmInterface::DeleteIpv6InstanceIp(bool l2, uint32_t old_ethernet_tag,
                                       VrfEntry *old_vrf_entry,
                                       bool force_update) {
    if (l2 && old_ethernet_tag != ethernet_tag()) {
        force_update = true;
    }

    InstanceIpSet::iterator it = instance_ipv6_list_.list_.begin();
    while (it != instance_ipv6_list_.list_.end()) {
        InstanceIpSet::iterator prev = it++;
        if (prev->is_service_health_check_ip_) {
            // for service health check instance ip do not withdraw
            // the route till it is marked del_pending
            // interface itself is not active
            bool interface_active = false;
            if (l2) {
                interface_active = metadata_l2_active_;
            } else {
                interface_active = metadata_ip_active_;
            }
            if (!prev->del_pending_ && interface_active) {
                prev->Activate(this, force_update, l2,
                               old_ethernet_tag);
                continue;
            }
        }
        prev->DeActivate(this, l2, old_vrf_entry, old_ethernet_tag);
        if (prev->del_pending_ && prev->installed() == false) {
            instance_ipv6_list_.list_.erase(prev);
        }
    }
}

void VmInterface::BuildIpStringList(Address::Family family,
                                    std::vector<std::string> *vect) const {
    InstanceIpSet list;
    if (family == Address::INET) {
        list = instance_ipv4_list_.list_;
    } else {
        list = instance_ipv6_list_.list_;
    }
    InstanceIpSet::iterator it = list.begin();
    while (it != list.end()) {
        const VmInterface::InstanceIp &rt = *it;
        it++;
        vect->push_back(rt.ip_.to_string());
    }
}

void AddVmiQosConfig::HandleRequest() const {
    QosResponse *resp = new QosResponse();
    resp->set_context(context());

    boost::uuids::uuid vmi_uuid = StringToUuid(std::string(get_vmi_uuid()));
    boost::uuids::uuid qos_config_uuid =
        StringToUuid(std::string(get_qos_config_uuid()));
    DBTable *table = Agent::GetInstance()->interface_table();

    DBRequest req;
    req.oper =  DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, vmi_uuid, ""));
    req.data.reset(new InterfaceQosConfigData(NULL, NULL, qos_config_uuid));
    table->Enqueue(&req);
    resp->set_resp("Success");
    resp->Response();
}
