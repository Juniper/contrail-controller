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

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <cfg/cfg_listener.h>
#include <cmn/agent.h>
#include <oper/operdb_init.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/route_common.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/interface_common.h>
#include <oper/vrf_assign.h>
#include <oper/vxlan.h>
#include <oper/oper_dhcp_options.h>
#include <oper/inet_unicast_route.h>

#include <vnc_cfg_types.h>
#include <oper/agent_sandesh.h>
#include <oper/sg.h>
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include <filter/acl.h>

using namespace std;
using namespace boost::uuids;
using namespace autogen;

VmInterface::VmInterface(const boost::uuids::uuid &uuid) :
    Interface(Interface::VM_INTERFACE, uuid, "", NULL), vm_(NULL),
    vn_(NULL), ip_addr_(0), mdata_addr_(0), subnet_bcast_addr_(0), ip6_addr_(),
    vm_mac_(""), policy_enabled_(false), mirror_entry_(NULL),
    mirror_direction_(MIRROR_RX_TX), cfg_name_(""), fabric_port_(true),
    need_linklocal_ip_(false), dhcp_enable_(true),
    do_dhcp_relay_(false), vm_name_(),
    vm_project_uuid_(nil_uuid()), vxlan_id_(0), layer2_forwarding_(true),
    layer3_forwarding_(true), mac_set_(false), ecmp_(false),
    tx_vlan_id_(kInvalidVlanId), rx_vlan_id_(kInvalidVlanId), parent_(NULL),
    local_preference_(VmInterface::INVALID), oper_dhcp_options_(),
    sg_list_(), floating_ip_list_(), service_vlan_list_(), static_route_list_(),
    allowed_address_pair_list_(), vrf_assign_rule_list_(),
    vrf_assign_acl_(NULL), vm_ip_gw_addr_(0), vm_ip6_gw_addr_(),
    sub_type_(VmInterface::NONE), configurer_(0), ifmap_node_(NULL),
    subnet_(0), subnet_plen_(0) {
    ipv4_active_ = false;
    ipv6_active_ = false;
    l2_active_ = false;
}

VmInterface::VmInterface(const boost::uuids::uuid &uuid,
                         const std::string &name,
                         const Ip4Address &addr, const std::string &mac,
                         const std::string &vm_name,
                         const boost::uuids::uuid &vm_project_uuid,
                         uint16_t tx_vlan_id, uint16_t rx_vlan_id,
                         Interface *parent, const Ip6Address &a6) :
    Interface(Interface::VM_INTERFACE, uuid, name, NULL), vm_(NULL),
    vn_(NULL), ip_addr_(addr), mdata_addr_(0), subnet_bcast_addr_(0),
    ip6_addr_(a6), vm_mac_(mac), policy_enabled_(false), mirror_entry_(NULL),
    mirror_direction_(MIRROR_RX_TX), cfg_name_(""), fabric_port_(true),
    need_linklocal_ip_(false), dhcp_enable_(true),
    do_dhcp_relay_(false), vm_name_(vm_name),
    vm_project_uuid_(vm_project_uuid), vxlan_id_(0),
    layer2_forwarding_(true), layer3_forwarding_(true), mac_set_(false),
    ecmp_(false), tx_vlan_id_(tx_vlan_id), rx_vlan_id_(rx_vlan_id),
    parent_(parent), local_preference_(VmInterface::INVALID), oper_dhcp_options_(),
    sg_list_(), floating_ip_list_(), service_vlan_list_(), static_route_list_(),
    allowed_address_pair_list_(), vrf_assign_rule_list_(),
    vrf_assign_acl_(NULL), sub_type_(VmInterface::NONE), configurer_(0),
    ifmap_node_(NULL), subnet_(0), subnet_plen_(0) {
    ipv4_active_ = false;
    ipv6_active_ = false;
    l2_active_ = false;
}

VmInterface::~VmInterface() {
}

bool VmInterface::CmpInterface(const DBEntry &rhs) const {
    const VmInterface &intf=static_cast<const VmInterface &>(rhs);
    return uuid_ < intf.uuid_;
}
/////////////////////////////////////////////////////////////////////////////
// Template function to audit two lists. This is used to synchronize the
// operational and config list for Floating-IP, Service-Vlans, Static Routes
// and SG List
/////////////////////////////////////////////////////////////////////////////
template<class List, class Iterator>
bool AuditList(List &list, Iterator old_first, Iterator old_last,
               Iterator new_first, Iterator new_last) {
    bool ret = false;
    Iterator old_iterator = old_first;
    Iterator new_iterator = new_first;
    while (old_iterator != old_last && new_iterator != new_last) {
        if (old_iterator->IsLess(new_iterator.operator->())) {
            Iterator bkp = old_iterator++;
            list.Remove(bkp);
            ret = true;
        } else if (new_iterator->IsLess(old_iterator.operator->())) {
            Iterator bkp = new_iterator++;
            list.Insert(bkp.operator->());
            ret = true;
        } else {
            Iterator old_bkp = old_iterator++;
            Iterator new_bkp = new_iterator++;
            list.Update(old_bkp.operator->(), new_bkp.operator->());
            ret = true;
        }
    }

    while (old_iterator != old_last) {
        Iterator bkp = old_iterator++;
        list.Remove(bkp);
            ret = true;
    }

    while (new_iterator != new_last) {
        Iterator bkp = new_iterator++;
        list.Insert(bkp.operator->());
            ret = true;
    }

    return ret;
}

// Build one Floating IP entry for a virtual-machine-interface
static void BuildFloatingIpList(Agent *agent, VmInterfaceConfigData *data,
                                IFMapNode *node) {
    CfgListener *cfg_listener = agent->cfg_listener();
    if (cfg_listener->SkipNode(node)) {
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
        if (cfg_listener->SkipNode
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
            if (cfg_listener->SkipNode
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
                if (cfg_listener->SkipNode
                    (vrf_node, agent->cfg()->cfg_vrf_table())){
                    continue;
                }
                // Checking whether it is default vrf of not
                size_t found = vn_node->name().find_last_of(':');
                std::string vrf_name = "";
                if (found != string::npos) {
                    vrf_name = vn_node->name() + vn_node->name().substr(found);
                }
                if (vrf_node->name().compare(vrf_name) != 0) {
                    continue;
                }
                FloatingIp *fip = static_cast<FloatingIp *>(node->GetObject());
                assert(fip != NULL);
                LOG(DEBUG, "Add FloatingIP <" << fip->address() << ":" <<
                    vrf_node->name() << "> to interface " << node->name());

                boost::system::error_code ec;
                IpAddress addr = IpAddress::from_string(fip->address(), ec);
                if (ec.value() != 0) {
                    LOG(DEBUG, "Error decoding Floating IP address " 
                        << fip->address());
                } else {
                    data->floating_ip_list_.list_.insert
                        (VmInterface::FloatingIp(addr, vrf_node->name(),
                                                 vn_uuid));
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
                (VmInterface::StaticRoute(data->vrf_name_, ip, plen, gw));
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

static void BuildAllowedAddressPairRouteList(VirtualMachineInterface *cfg,
                                             VmInterfaceConfigData *data) {
    for (std::vector<AllowedAddressPair>::const_iterator it =
         cfg->allowed_address_pairs().begin();
         it != cfg->allowed_address_pairs().end(); ++it) {
        boost::system::error_code ec;
        int plen = it->ip.ip_prefix_len;
        Ip4Address ip = Ip4Address::from_string(it->ip.ip_prefix, ec);

        bool ecmp = false;
        if (it->address_mode == "active-active") {
            ecmp = true;
        }
        if (ec.value() == 0) {
            VmInterface::AllowedAddressPair entry(data->vrf_name_, ip, plen,
                                                  ecmp);
            data->allowed_address_pair_list_.list_.insert(entry);
        } else {
            LOG(DEBUG, "Error decoding Static Route IP address " << ip);
        }
    }
}

// Build VM Interface VRF or one Service Vlan entry for VM Interface
static void BuildVrfAndServiceVlanInfo(Agent *agent,
                                       VmInterfaceConfigData *data,
                                       IFMapNode *node) {

    CfgListener *cfg_listener = agent->cfg_listener();
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
        if (cfg_listener->SkipNode
            (vrf_node, agent->cfg()->cfg_vrf_table())) {
            continue;
        }

        if (rule.vlan_tag == 0 && rule.protocol == "" 
            && rule.service_chain_address == "") {
            LOG(DEBUG, "VRF for interface " << data->cfg_name_ << " set to <" 
                << vrf_node->name() << ">");
            data->vrf_name_ = vrf_node->name();
        } else {
            boost::system::error_code ec;
            Ip4Address addr = Ip4Address::from_string
                (rule.service_chain_address, ec);
            if (ec.value() != 0) {
                LOG(DEBUG, "Error decoding Service VLAN IP address "
                    << rule.service_chain_address);
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
                                          32, smac, dmac));
        }
        break;
    }

    return;
}

static void ReadInstanceIp(VmInterfaceConfigData *data, IFMapNode *node) {
    InstanceIp *ip = static_cast<InstanceIp *>(node->GetObject());
    boost::system::error_code err;
    LOG(DEBUG, "InstanceIp config for " << data->cfg_name_ << " "
        << ip->address());
    IpAddress addr = IpAddress::from_string(ip->address(), err);
    if (addr.is_v4()) {
        data->addr_ = addr.to_v4();
    } else if (addr.is_v6()) {
        data->ip6_addr_ = addr.to_v6();
    }
    if (ip->mode() == "active-active") {
        data->ecmp_ = true;
    } else {
        data->ecmp_ = false;
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
        agent->mirror_table()->AddMirrorEntry
            (mirror_to.analyzer_name, std::string(), agent->router_id(),
             agent->mirror_port(), dip.to_v4(), dport);
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

//TBD Use link instead of device_owner
VmInterface::SubType GetVmInterfaceSubType(Agent *agent,
                                           const std::string &device_owner) {
    if (device_owner.compare("compute:nova") == 0 || agent->test_mode() ||
        agent->tsn_enabled() == false)
        return VmInterface::NOVA;
    else
        return VmInterface::TOR;
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

// Virtual Machine Interface is added or deleted into oper DB from Nova 
// messages. The Config notify is used only to change interface.
bool InterfaceTable::VmiIFNodeToReq(IFMapNode *node, DBRequest &req) {
    // Get interface UUID
    VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
        (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);

    VmInterface::SubType interface_sub_type =
        GetVmInterfaceSubType(agent_, cfg->device_owner());

    // Skip config interface delete notification
    if (node->IsDeleted()) {
        if (interface_sub_type == VmInterface::NOVA) {
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, u, ""));
            req.data.reset(new VmInterfaceConfigData(NULL));
            return true;
        } else {
            VmInterface::Delete(this, u, VmInterface::CONFIG);
            return false;
        }
    }

    CfgIntTable *cfg_table = agent_->interface_config_table();
    CfgIntKey cfg_key(u);
    CfgIntEntry *cfg_entry =
        static_cast <CfgIntEntry *>(cfg_table->Find(&cfg_key));

    // Update interface configuration
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    InterfaceKey *key = NULL; 
    if (interface_sub_type == VmInterface::NOVA) {
        key = new VmInterfaceKey(AgentKey::RESYNC, u, "");
    } else {
        key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, u,
                                 cfg->display_name());
    }

    VmInterfaceConfigData *data = new VmInterfaceConfigData(NULL);
    data->ifmap_node_ = node;
    //Extract the local preference
    if (cfg->IsPropertySet(VirtualMachineInterface::PROPERTIES)) {
        autogen::VirtualMachineInterfacePropertiesType prop = cfg->properties();
        data->local_preference_ = VmInterface::LOW;
        if (prop.local_preference == VmInterface::HIGH) {
            data->local_preference_ = VmInterface::HIGH;
        }
    }

    ReadAnalyzerNameAndCreate(agent_, cfg, *data);

    // Fill DHCP option data
    ReadDhcpOptions(cfg, *data);

    //Fill config data items
    data->cfg_name_ = node->name();
    data->admin_state_ = id_perms.enable;

    BuildVrfAssignRule(cfg, data);
    BuildAllowedAddressPairRouteList(cfg, data);

    SgUuidList sg_list(0);
    IFMapNode *vn_node = NULL;

    // Walk Interface Graph to get VM, VN and FloatingIPList
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph()); 
         iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent_->cfg_listener()->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() == agent_->cfg()->cfg_sg_table()) {
            SecurityGroup *sg_cfg = static_cast<SecurityGroup *>
                    (adj_node->GetObject());
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

        if (adj_node->table() == agent_->cfg()->cfg_vn_table()) {
            vn_node = adj_node;
            VirtualNetwork *vn = static_cast<VirtualNetwork *>
                (adj_node->GetObject());
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

        if (adj_node->table() == agent_->cfg()->cfg_vm_table()) {
            VirtualMachine *vm = static_cast<VirtualMachine *>
                (adj_node->GetObject());
            assert(vm);

            //VM link to virtual-router tells kind of virtual-router.
            //If virtual-router is not of type TOR/TSN, then NOVA is the only
            //place from where interface gets created. So if subtype is unknown
            //and there was a nova entry found then treat it as NOVA else
            //ignore the request. Subsequent addition of VM or nova should
            //re-invoke this routine.
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
                            UuidToString(cfg_entry->GetVnUuid()));
            }
        }

        if (adj_node->table() == agent_->cfg()->cfg_instanceip_table()) {
            ReadInstanceIp(data, adj_node);
        }

        if (adj_node->table() == agent_->cfg()->cfg_floatingip_table()) {
            BuildFloatingIpList(agent_, data, adj_node);
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
    }

    //Read parent interface name if any
    IFMapNode *logical_node = agent_->cfg_listener()->
                              FindAdjacentIFMapNode(agent_, node, 
                                                     "logical-interface");
    if (logical_node) {
        IFMapNode *physical_node = agent_->cfg_listener()->
            FindAdjacentIFMapNode(agent_, logical_node, "physical-interface");
        //Add physical interface
        if (physical_node) {
            autogen::PhysicalInterface *physical_interface =
                static_cast <autogen::PhysicalInterface *>(
                        physical_node->GetObject());
            data->parent_ = physical_interface->display_name();
        }
    }

    if (!data->subnet_.is_unspecified() &&
        data->parent_ != agent_->NullString()) {
        interface_sub_type = VmInterface::GATEWAY;
        data->rx_vlan_id_ = 0;
        data->tx_vlan_id_ = 0;
        delete key;
        //Add request for a VMI of type gateway
        key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, u,
                                 cfg->display_name());
    }

    data->sub_type_ = interface_sub_type;

    if (cfg->mac_addresses().size()) {
        data->vm_mac_ = cfg->mac_addresses().at(0);
    }

    // Get DHCP enable flag from subnet
    if (vn_node && data->addr_.to_ulong()) {
        ReadDhcpEnable(agent_, data, vn_node);
    }

    data->fabric_port_ = false;
    data->need_linklocal_ip_ = true;
    if (data->vrf_name_ == agent_->fabric_vrf_name() ||
        data->vrf_name_ == agent_->linklocal_vrf_name()) {
        data->fabric_port_ = true;
        data->need_linklocal_ip_ = false;
    } 

    if (agent_->isXenMode()) {
        data->need_linklocal_ip_ = false;
    }

    req.key.reset(key);
    req.data.reset(data);
    return true;
}

// Handle virtual-machine-interface-routing-instance config node
// Find the interface-node and enqueue RESYNC of service-vlans to interface
void InterfaceTable::VmInterfaceVrfSync(IFMapNode *node) {
    if (agent_->cfg_listener()->SkipNode(node)) {
        return;
    }
    // Walk the node to get neighbouring interface 
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph()); 
         iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent_->cfg_listener()->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() == agent_->cfg()->cfg_vm_interface_table()) {
            DBRequest req;
            if (IFNodeToReq(adj_node, req) == true) {
                LOG(DEBUG, "Service VLAN SYNC for Port " << adj_node->name());
                Enqueue(&req);
            }
        }
    }
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
    Ip4Address old_addr = ip_addr_;
    Ip6Address old_v6_addr = ip6_addr_;
    int old_vxlan_id = vxlan_id_;
    bool old_need_linklocal_ip = need_linklocal_ip_;
    bool sg_changed = false;
    bool ecmp_changed = false;
    bool local_pref_changed = false;
    Ip4Address old_subnet = subnet_;
    uint8_t  old_subnet_plen = subnet_plen_;

    if (data) {
        ret = data->OnResync(table, this, &sg_changed, &ecmp_changed,
                             &local_pref_changed);
    }

    ipv4_active_ = IsIpv4Active();
    ipv6_active_ = IsIpv6Active();
    l2_active_ = IsL2Active();
    if (ipv4_active_ != old_ipv4_active) {
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
                old_addr, old_vxlan_id, old_need_linklocal_ip, sg_changed,
                old_ipv6_active, old_v6_addr, ecmp_changed,
                local_pref_changed, old_subnet, old_subnet_plen);

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

void VmInterface::UpdateL3(bool old_ipv4_active, VrfEntry *old_vrf,
                           const Ip4Address &old_addr, int old_vxlan_id,
                           bool force_update, bool policy_change,
                           bool old_ipv6_active,
                           const Ip6Address &old_v6_addr,
                           const Ip4Address &old_subnet,
                           const uint8_t old_subnet_plen) {
    UpdateSecurityGroup();
    UpdateL3NextHop(old_ipv4_active, old_ipv6_active);
    UpdateL3TunnelId(force_update, policy_change);
    if (ipv4_active_) {
        UpdateIpv4InterfaceRoute(old_ipv4_active, force_update, policy_change,
                                 old_vrf, old_addr);
        UpdateMetadataRoute(old_ipv4_active, old_vrf);
        UpdateFloatingIp(force_update, policy_change);
        UpdateServiceVlan(force_update, policy_change);
        UpdateAllowedAddressPair(force_update, policy_change);
        UpdateVrfAssignRule();
        UpdateResolveRoute(old_ipv4_active, force_update, policy_change, 
                           old_vrf, old_subnet, old_subnet_plen);
    }
    if (ipv6_active_) {
        UpdateIpv6InterfaceRoute(old_ipv6_active, force_update, policy_change,
                                 old_vrf, old_v6_addr);
    }
    UpdateStaticRoute(force_update, policy_change);
}

void VmInterface::DeleteL3(bool old_ipv4_active, VrfEntry *old_vrf,
                           const Ip4Address &old_addr,
                           bool old_need_linklocal_ip, bool old_ipv6_active,
                           const Ip6Address &old_v6_addr,
                           const Ip4Address &old_subnet,
                           const uint8_t old_subnet_plen) {
    if (old_ipv4_active) {
        DeleteIpv4InterfaceRoute(old_vrf, old_addr);
    }
    if (old_ipv6_active) {
        DeleteIpv6InterfaceRoute(old_vrf, old_v6_addr);
    }
    DeleteMetadataRoute(old_ipv4_active, old_vrf, old_need_linklocal_ip);
    DeleteFloatingIp();
    DeleteServiceVlan();
    DeleteStaticRoute();
    DeleteAllowedAddressPair();
    DeleteSecurityGroup();
    DeleteL3TunnelId();
    DeleteVrfAssignRule();
    DeleteL3NextHop(old_ipv4_active, old_ipv6_active);
    DeleteResolveRoute(old_vrf, old_subnet, old_subnet_plen);
}

void VmInterface::UpdateVxLan() {
    int new_vxlan_id = vn_.get() ? vn_->GetVxLanId() : 0;
    if (l2_active_ && ((vxlan_id_ == 0) ||
                       (vxlan_id_ != new_vxlan_id))) {
        vxlan_id_ = new_vxlan_id;
    }
}

void VmInterface::UpdateL2(bool old_l2_active, VrfEntry *old_vrf, int old_vxlan_id,
                           bool force_update, bool policy_change) {
    UpdateVxLan();
    UpdateL2NextHop(old_l2_active);
    //Update label only if new entry is to be created, so
    //no force update on same.
    UpdateL2TunnelId(false, policy_change);
    UpdateL2InterfaceRoute(old_l2_active, force_update);
}

void VmInterface::UpdateL2(bool force_update) {
    UpdateL2(l2_active_, vrf_.get(), vxlan_id_, force_update, false);
}

void VmInterface::DeleteL2(bool old_l2_active, VrfEntry *old_vrf) {
    DeleteL2TunnelId();
    DeleteL2InterfaceRoute(old_l2_active, old_vrf);
    DeleteL2NextHop(old_l2_active);
}

// Apply the latest configuration
void VmInterface::ApplyConfig(bool old_ipv4_active, bool old_l2_active, bool old_policy,
                              VrfEntry *old_vrf, const Ip4Address &old_addr,
                              int old_vxlan_id, bool old_need_linklocal_ip,
                              bool sg_changed, bool old_ipv6_active,
                              const Ip6Address &old_v6_addr, bool ecmp_mode_changed,
                              bool local_pref_changed,
                              const Ip4Address &old_subnet,
                              uint8_t old_subnet_plen) {
    //Need not apply config for TOR VMI as it is more of an inidicative
    //interface. No route addition or NH addition happens for this interface.
    if (sub_type_ == VmInterface::TOR &&
        (old_subnet.is_unspecified() && old_subnet_plen == 0)) {
        return;
    }

    bool force_update = false;
    if (sg_changed || ecmp_mode_changed | local_pref_changed) {
        force_update = true;
    }

    bool policy_change = (policy_enabled_ != old_policy);

    if (ipv4_active_ == true || l2_active_ == true) {
        UpdateMulticastNextHop(old_ipv4_active, old_l2_active);
    } else {
        DeleteMulticastNextHop();
    }

    //Irrespective of interface state, if ipv4 forwarding mode is enabled
    //enable L3 services on this interface
    if (layer3_forwarding_) {
        UpdateL3Services(dhcp_enable_, true);
    } else {
        UpdateL3Services(false, false);
    }

    // Add/Del/Update L3 
    if ((ipv4_active_ || ipv6_active_) && layer3_forwarding_) {
        UpdateL3(old_ipv4_active, old_vrf, old_addr, old_vxlan_id, force_update,
                 policy_change, old_ipv6_active, old_v6_addr,
                 old_subnet, old_subnet_plen);
    } else if ((old_ipv4_active || old_ipv6_active)) {
        DeleteL3(old_ipv4_active, old_vrf, old_addr, old_need_linklocal_ip, 
                 old_ipv6_active, old_v6_addr,
                 old_subnet, old_subnet_plen);
    }

    // Add/Del/Update L2 
    if (l2_active_ && layer2_forwarding_) {
        UpdateL2(old_l2_active, old_vrf, old_vxlan_id, 
                 force_update, policy_change);
    } else if (old_l2_active) {
        DeleteL2(old_l2_active, old_vrf);
    }

    if (old_l2_active != l2_active_) {
        if (l2_active_) {
            SendTrace(ACTIVATED_L2);
        } else {
            SendTrace(DEACTIVATED_L2);
        }
    }

    if (old_ipv4_active != ipv4_active_) {
        if (ipv4_active_) {
            SendTrace(ACTIVATED_IPV4);
        } else {
            SendTrace(DEACTIVATED_IPV4);
        }
    }

    if (old_ipv6_active != ipv6_active_) {
        if (ipv6_active_) {
            SendTrace(ACTIVATED_IPV6);
        } else {
            SendTrace(DEACTIVATED_IPV6);
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
    }

    // Retain the old if new IP could not be got
    if (addr.to_ulong() == 0) {
        addr = ip_addr_;
    }

    if (ip_addr_ != addr) {
        ip_addr_ = addr;
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceConfigData routines
/////////////////////////////////////////////////////////////////////////////
VmInterface *VmInterfaceConfigData::OnAdd(const InterfaceTable *table,
                                          const VmInterfaceKey *key) const {
    VmInterface *vmi =
        new VmInterface(key->uuid_, key->name_, addr_, vm_mac_, vm_name_,
                        vm_uuid_, VmInterface::kInvalidVlanId,
                        VmInterface::kInvalidVlanId, NULL, ip6_addr_);
    vmi->SetConfigurer(VmInterface::CONFIG);
    return vmi;
}

bool VmInterfaceConfigData::OnDelete(const InterfaceTable *table,
                                     VmInterface *vmi) const {
    if (vmi->IsConfigurerSet(VmInterface::CONFIG) == false)
        return true;

    vmi->ResetConfigurer(VmInterface::CONFIG);
    VmInterfaceConfigData data(NULL);
    vmi->Resync(table, &data);
    if (ifmap_node_ != NULL)
        table->operdb()->dependency_manager()->ResetObject(ifmap_node_);
    return true;
}

bool VmInterfaceConfigData::OnResync(const InterfaceTable *table,
                                     VmInterface *vmi, bool *sg_changed,
                                     bool *ecmp_changed,
                                     bool *local_pref_changed) const {
    return vmi->CopyConfig(table, this, sg_changed, ecmp_changed,
                           local_pref_changed);
}

// Copies configuration from DB-Request data. The actual applying of 
// configuration, like adding/deleting routes must be done with ApplyConfig()
bool VmInterface::CopyConfig(const InterfaceTable *table,
                             const VmInterfaceConfigData *data,
                             bool *sg_changed,
                             bool *ecmp_changed, bool *local_pref_changed) {
    bool ret = false;
    if (table) {
        VmEntry *vm = table->FindVmRef(data->vm_uuid_);
        if (vm_.get() != vm) {
            vm_ = vm;
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

        int vxlan_id = vn ? vn->GetVxLanId() : 0;
        if (vxlan_id_ != vxlan_id) {
            vxlan_id_ = vxlan_id;
            ret = true;
        }
    }

    if (local_preference_ != data->local_preference_) {
        local_preference_ = data->local_preference_;
        *local_pref_changed = true;
        ret = true;
    }

    bool val = layer3_forwarding_ ? data->need_linklocal_ip_ : false;
    if (need_linklocal_ip_ != val) {
        need_linklocal_ip_ = val;
        ret = true;
    }

    // CopyIpAddress uses fabric_port_. So, set it before CopyIpAddresss
    val = layer3_forwarding_ ? data->fabric_port_ : false;
    if (fabric_port_ != val) {
        fabric_port_ = val;
        ret = true;
    }

    Ip4Address ipaddr = layer3_forwarding_ ? data->addr_ : Ip4Address(0);
    if (CopyIpAddress(ipaddr)) {
        ret = true;
    }
    if (CopyIp6Address(data->ip6_addr_)) {
        ret = true;
    }

    if (dhcp_enable_ != data->dhcp_enable_) {
        dhcp_enable_ = data->dhcp_enable_;
        ret = true;
    }

    bool mac_set = true;
    boost::system::error_code ec;
    MacAddress addr(vm_mac_, &ec);
    if (ec.value() != 0) {
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

    if (data->addr_ != Ip4Address(0) && ecmp_ != data->ecmp_) {
        ecmp_ = data->ecmp_;
        *ecmp_changed = true;
    }

    if (sub_type_ != data->sub_type_) {
        sub_type_= data->sub_type_;
        ret = true;
    }

    if (sub_type_ == GATEWAY) {
        if (rx_vlan_id_ != data->rx_vlan_id_) {
            rx_vlan_id_ = data->rx_vlan_id_;
        }
        if (tx_vlan_id_ != data->tx_vlan_id_) {
            tx_vlan_id_ = data->tx_vlan_id_;
        }
    }

    if (data->parent_ != Agent::NullString()) {
        PhysicalInterfaceKey key(data->parent_);
        parent_ = static_cast<Interface *>
            (table->agent()->interface_table()->FindActiveEntry(&key));
    }

    if (ifmap_node_ != data->ifmap_node_) {
        if (ifmap_node_ != NULL)
            table->operdb()->dependency_manager()->ResetObject(ifmap_node_);
        ifmap_node_ = data->ifmap_node_;
        if (ifmap_node_)
            table->operdb()->dependency_manager()->SetObject(ifmap_node_, this);
    }

    if (table) {
        if (os_index_ == kInvalidIndex) {
            GetOsParams(table->agent());
            if (os_index_ != kInvalidIndex)
                ret = true;
        }
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceNovaData routines
/////////////////////////////////////////////////////////////////////////////
VmInterfaceNovaData::VmInterfaceNovaData() :
    VmInterfaceData(NULL, NOVA),
    ipv4_addr_(),
    ipv6_addr_(),
    mac_addr_(),
    vm_name_(),
    vm_uuid_(),
    parent_(),
    tx_vlan_id_(),
    rx_vlan_id_() {
}

VmInterfaceNovaData::VmInterfaceNovaData(const Ip4Address &ipv4_addr,
                                         const Ip6Address &ipv6_addr,
                                         const std::string &mac_addr,
                                         const std::string vm_name,
                                         boost::uuids::uuid vm_uuid,
                                         const std::string &parent,
                                         uint16_t tx_vlan_id,
                                         uint16_t rx_vlan_id) :
    VmInterfaceData(NULL, NOVA),
    ipv4_addr_(ipv4_addr),
    ipv6_addr_(ipv6_addr),
    mac_addr_(mac_addr),
    vm_name_(vm_name),
    vm_uuid_(vm_uuid),
    parent_(parent),
    tx_vlan_id_(tx_vlan_id),
    rx_vlan_id_(rx_vlan_id) {
}

VmInterfaceNovaData::~VmInterfaceNovaData() {
}

VmInterface *VmInterfaceNovaData::OnAdd(const InterfaceTable *table,
                                        const VmInterfaceKey *key) const {
    Interface *parent = NULL;
    if (tx_vlan_id_ != VmInterface::kInvalidVlanId &&
        rx_vlan_id_ != VmInterface::kInvalidVlanId &&
        parent_ != Agent::NullString()) {
        PhysicalInterfaceKey key_1(parent_);
        parent = static_cast<Interface *>
            (table->agent()->interface_table()->FindActiveEntry(&key_1));
        assert(parent != NULL);
    }
    VmInterface *vmi =
        new VmInterface(key->uuid_, key->name_, ipv4_addr_, mac_addr_, vm_name_,
                        vm_uuid_, tx_vlan_id_, rx_vlan_id_, parent, ipv6_addr_);
    vmi->SetConfigurer(VmInterface::EXTERNAL);
    return vmi;
}

bool VmInterfaceNovaData::OnDelete(const InterfaceTable *table,
                                   VmInterface *vmi) const {
    if (vmi->IsConfigurerSet(VmInterface::EXTERNAL) == false)
        return true;

    vmi->ResetConfigurer(VmInterface::CONFIG);
    VmInterfaceConfigData data(NULL);
    vmi->Resync(table, &data);
    vmi->ResetConfigurer(VmInterface::EXTERNAL);
    return true;
}

bool VmInterfaceNovaData::OnResync(const InterfaceTable *table,
                                   VmInterface *vmi, bool *sg_changed,
                                   bool *ecmp_changed,
                                   bool *local_pref_changed) const {
    bool ret = false;

    if (vmi->tx_vlan_id_ != tx_vlan_id_) {
        vmi->tx_vlan_id_ = tx_vlan_id_;
        ret = true;
    }

    if (vmi->rx_vlan_id_ != rx_vlan_id_) {
        vmi->rx_vlan_id_ = rx_vlan_id_;
        ret = true;
    }
    vmi->SetConfigurer(VmInterface::EXTERNAL);

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceMirrorData routines
/////////////////////////////////////////////////////////////////////////////
bool VmInterfaceMirrorData::OnResync(const InterfaceTable *table,
                                     VmInterface *vmi, bool *sg_changed,
                                     bool *ecmp_changed,
                                     bool *local_pref_changed) const {
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
                                        VmInterface *vmi, bool *sg_changed,
                                        bool *ecmp_changed,
                                        bool *local_pref_changed) const {
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
                                          VmInterface *vmi, bool *sg_changed,
                                          bool *ecmp_changed,
                                          bool *local_pref_changed) const {
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

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VM Port Entry utility routines
/////////////////////////////////////////////////////////////////////////////
// Does the VMInterface need a physical device to be present
bool VmInterface::NeedDevice() const {
    bool ret = true;

    if (sub_type_ == TOR)
        ret = false;

    if (subnet_.is_unspecified() == false) {
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

    return mac_set_;
}

bool VmInterface::IsIpv4Active() const {
    if (!layer3_forwarding()) {
        return false;
    }

    if (subnet_.is_unspecified() && ip_addr_.to_ulong() == 0) {
        return false;
    }

    if (subnet_.is_unspecified() == false && parent_ == NULL) {
        return false;
    }

    if (os_oper_state_ == false)
        return false;

    return IsActive();
}

bool VmInterface::IsIpv6Active() const {
    if (!layer3_forwarding() || (ip6_addr_.is_unspecified())) {
        return false;
    }

    if (os_oper_state_ == false)
        return false;

    return IsActive();
}

bool VmInterface::IsL3Active() const {
    if (!layer3_forwarding() || (ip6_addr_.is_unspecified())) {
        return false;
    }

    if (os_oper_state_ == false)
        return false;

    return IsActive();
}

bool VmInterface::IsL2Active() const {
    if (!layer2_forwarding()) {
        return false;
    }

    if (os_oper_state_ == false)
        return false;

    return IsActive();
}

bool VmInterface::WaitForTraffic() const {
    if (IsActive() == false) {
        return false;
    }

    //Get the instance ip route and its corresponding traffic seen status
    InetUnicastRouteKey rt_key(peer_.get(), vrf_->GetName(), ip_addr_, 32);
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

// Compute if policy is to be enabled on the interface
bool VmInterface::PolicyEnabled() const {
    // Policy not supported for fabric ports
    if (fabric_port_) {
        return false;
    }

    if (vn_.get() && vn_->IsAclSet()) {
        return true;
    }

    // Floating-IP list and SG List can have entries in del_pending state
    // Look for entries in non-del-pending state
    FloatingIpSet::iterator fip_it = floating_ip_list_.list_.begin();
    while (fip_it != floating_ip_list_.list_.end()) {
        if (fip_it->del_pending_ == false) {
            return true;
        }
        fip_it++;
    }

    SecurityGroupEntrySet::iterator sg_it = sg_list_.list_.begin();
    while (sg_it != sg_list_.list_.end()) {
        if (sg_it->del_pending_ == false) {
            return true;
        }
        sg_it++;
    }

    VrfAssignRuleSet::iterator vrf_it = vrf_assign_rule_list_.list_.begin();
    while (vrf_it != vrf_assign_rule_list_.list_.end()) {
        if (vrf_it->del_pending_ == false) {
            return true;
        }
        vrf_it++;
    }
    return false;
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
void VmInterface::AllocL3MplsLabel(bool force_update, bool policy_change) {
    if (fabric_port_)
        return;

    bool new_entry = false;
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    if (label_ == MplsTable::kInvalidLabel) {
        label_ = agent->mpls_table()->AllocLabel();
        new_entry = true;
    }

    if (force_update || policy_change || new_entry)
        MplsLabel::CreateVPortLabel(agent, label_, GetUuid(), policy_enabled_,
                                    InterfaceNHFlags::INET4);
}

// Delete MPLS Label for Layer3 routes
void VmInterface::DeleteL3MplsLabel() {
    if (label_ == MplsTable::kInvalidLabel) {
        return;
    }

    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    MplsLabel::Delete(agent, label_);
    label_ = MplsTable::kInvalidLabel;
}

// Allocate MPLS Label for Layer2 routes
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
        MplsLabel::CreateVPortLabel(agent, l2_label_, GetUuid(), false,
                                    InterfaceNHFlags::LAYER2);
}

// Delete MPLS Label for Layer2 routes
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
    AllocL3MplsLabel(force_update, policy_change);
}

void VmInterface::DeleteL3TunnelId() {
    if (!ipv4_active_ && !ipv6_active_) {
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

//Check if interface transitioned from active layer2 forwarding to inactive state
bool VmInterface::L2Deactivated(bool old_l2_active) {
    if (old_l2_active == true && l2_active_ == false) {
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

void VmInterface::UpdateMulticastNextHop(bool old_ipv4_active,
                                         bool old_l2_active) {
    if (Ipv4Activated(old_ipv4_active) || L2Activated(old_l2_active)) {
        InterfaceNH::CreateMulticastVmInterfaceNH(GetUuid(),
                                                  MacAddress::FromString(vm_mac_),
                                                  vrf_->GetName());
    }
}

void VmInterface::UpdateL2NextHop(bool old_l2_active) {
    if (L2Activated(old_l2_active)) {
        InterfaceNH::CreateL2VmInterfaceNH(GetUuid(),
                                           MacAddress::FromString(vm_mac_),
                                           vrf_->GetName());
    }
}

void VmInterface::UpdateL3NextHop(bool old_ipv4_active, bool old_ipv6_active) {
    if (old_ipv4_active || old_ipv6_active) {
        return;
    }
    if (Ipv4Activated(old_ipv4_active) || Ipv6Activated(old_ipv6_active)) {
        InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
        Agent *agent = table->agent();

        InterfaceNH::CreateL3VmInterfaceNH(GetUuid(),
                                           MacAddress::FromString(vm_mac_), vrf_->GetName());
        InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              GetUuid(), ""), true,
                                              InterfaceNHFlags::INET4);
        flow_key_nh_ = static_cast<const NextHop *>(
                agent->nexthop_table()->FindActiveEntry(&key));
        assert(flow_key_nh_);
    }
}

void VmInterface::DeleteL2NextHop(bool old_l2_active) {
    if (L2Deactivated(old_l2_active)) {
        InterfaceNH::DeleteL2InterfaceNH(GetUuid());
    }
}

void VmInterface::DeleteL3NextHop(bool old_ipv4_active, bool old_ipv6_active) {
    if (Ipv4Deactivated(old_ipv4_active) || Ipv6Deactivated(old_ipv6_active)) {
        if (!ipv4_active_ && !ipv6_active_) {
            InterfaceNH::DeleteL3InterfaceNH(GetUuid());
            flow_key_nh_ = NULL;
        }
    }
}

void VmInterface::DeleteMulticastNextHop() {
    InterfaceNH::DeleteMulticastVmInterfaceNH(GetUuid());
}

Ip4Address VmInterface::GetGateway() const {
    Ip4Address ip(0);
    if (vn_.get() == NULL) {
        return ip;
    }

    const VnIpam *ipam = NULL;
    if (subnet_.is_unspecified()) {
        ipam = vn_->GetIpam(ip_addr_);
    } else {
        ipam = vn_->GetIpam(subnet_);
    }

    if (ipam) {
        ip = ipam->default_gw.to_v4();
    }
    return ip;
}

// Add/Update route. Delete old route if VRF or address changed
void VmInterface::UpdateIpv4InterfaceRoute(bool old_ipv4_active, bool force_update,
                                         bool policy_change,
                                         VrfEntry * old_vrf,
                                         const Ip4Address &old_addr) {
    Ip4Address ip = GetGateway();

    // If interface was already active earlier and there is no force_update or
    // policy_change, return
    if (old_ipv4_active == true && force_update == false
        && policy_change == false && old_addr == ip_addr_ &&
        vm_ip_gw_addr_ == ip) {
        return;
    }

    // We need to have valid IP and VRF to add route
    if (ip_addr_.to_ulong() != 0 && vrf_.get() != NULL) {
        // Add route if old was inactive or force_update is set
        if (old_ipv4_active == false || force_update == true ||
            old_addr != ip_addr_ || vm_ip_gw_addr_ != ip) {
            vm_ip_gw_addr_ = ip;
            AddRoute(vrf_->GetName(), ip_addr_, 32, vn_->GetName(),
                     policy_enabled_, ecmp_, vm_ip_gw_addr_);
        } else if (policy_change == true) {
            // If old-l3-active and there is change in policy, invoke RESYNC of
            // route to account for change in NH policy
            InetUnicastAgentRouteTable::ReEvaluatePaths(vrf_->GetName(),
                                                        ip_addr_, 32);
        }
    }

    // If there is change in VRF or IP address, delete old route
    if (old_vrf != vrf_.get() || ip_addr_ != old_addr) {
        DeleteIpv4InterfaceRoute(old_vrf, old_addr);
    }
}

// Add/Update route. Delete old route if VRF or address changed
void VmInterface::UpdateIpv6InterfaceRoute(bool old_ipv6_active, bool force_update,
                                           bool policy_change,
                                           VrfEntry * old_vrf,
                                           const Ip6Address &old_addr) {
    const VnIpam *ipam = vn_->GetIpam(ip6_addr_);
    Ip6Address ip6;
    if (ipam) {
        ip6 = ipam->default_gw.to_v6();
    }

    // If interface was already active earlier and there is no force_update or
    // policy_change, return
    if (old_ipv6_active == true && force_update == false
        && policy_change == false && vm_ip6_gw_addr_ == ip6) {
        return;
    }

    // We need to have valid IP and VRF to add route
    if (!ip6_addr_.is_unspecified() && vrf_.get() != NULL) {
        // Add route if old was inactive or force_update is set
        if (old_ipv6_active == false || force_update == true ||
            old_addr != ip6_addr_ || vm_ip6_gw_addr_ != ip6) {
            vm_ip6_gw_addr_ = ip6;
            SecurityGroupList sg_id_list;
            PathPreference path_preference;
            CopySgIdList(&sg_id_list);
            //TODO: change subnet_gw_ip to Ip6Address
            InetUnicastAgentRouteTable::AddLocalVmRoute
                (peer_.get(), vrf_->GetName(), ip6_addr_, 128, GetUuid(),
                 vn_->GetName(), label_, sg_id_list, false, path_preference,
                 vm_ip6_gw_addr_);
        } else if (policy_change == true) {
            // If old-l3-active and there is change in policy, invoke RESYNC of
            // route to account for change in NH policy
            InetUnicastAgentRouteTable::ReEvaluatePaths(vrf_->GetName(),
                                                        ip6_addr_, 128);
        }
    }

    // If there is change in VRF or IP address, delete old route
    if (old_vrf != vrf_.get() || ip6_addr_ != old_addr) {
        DeleteIpv6InterfaceRoute(old_vrf, old_addr);
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

void VmInterface::DeleteIpv6InterfaceRoute(VrfEntry *old_vrf, 
                                           const Ip6Address &old_addr) {
    if ((old_vrf == NULL) || (old_addr.is_unspecified()))
        return;

    InetUnicastAgentRouteTable::Delete(peer_.get(), old_vrf->GetName(),
                                       old_addr, 128);
}

// Add meta-data route if linklocal_ip is needed
void VmInterface::UpdateMetadataRoute(bool old_ipv4_active, VrfEntry *old_vrf) {
    if (ipv4_active_ == false || old_ipv4_active == true)
        return;

    if (!need_linklocal_ip_) {
        return;
    }

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();
    table->VmPortToMetaDataIp(id(), vrf_->vrf_id(), &mdata_addr_);
    PathPreference path_preference;
    InetUnicastAgentRouteTable::AddLocalVmRoute
        (agent->link_local_peer(), agent->fabric_vrf_name(), mdata_addr_,
         32, GetUuid(), vn_->GetName(), label_, SecurityGroupList(), true,
         path_preference, Ip4Address(0));
}

// Delete meta-data route
void VmInterface::DeleteMetadataRoute(bool old_active, VrfEntry *old_vrf,
                                      bool old_need_linklocal_ip) {
    if (!old_need_linklocal_ip) {
        return;
    }

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();
    InetUnicastAgentRouteTable::Delete(agent->link_local_peer(),
                                       agent->fabric_vrf_name(),
                                       mdata_addr_, 32);
}

void VmInterface::UpdateFipFamilyCount(const FloatingIp &fip) {
    if (fip.floating_ip_.is_v4()) {
        floating_ip_list_.v4_count_--;
        assert(floating_ip_list_.v4_count_ >= 0);
    } else {
        floating_ip_list_.v6_count_--;
        assert(floating_ip_list_.v6_count_ >= 0);
    }
}

void VmInterface::UpdateFloatingIp(bool force_update, bool policy_change) {
    FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
    while (it != floating_ip_list_.list_.end()) {
        FloatingIpSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this);
            floating_ip_list_.list_.erase(prev);
            UpdateFipFamilyCount(*prev);
        } else {
            prev->Activate(this, force_update||policy_change);
        }
    }
}

void VmInterface::DeleteFloatingIp() {
    FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
    while (it != floating_ip_list_.list_.end()) {
        FloatingIpSet::iterator prev = it++;
        prev->DeActivate(this);
        if (prev->del_pending_) {
            floating_ip_list_.list_.erase(prev);
            UpdateFipFamilyCount(*prev);
        }
    }
}

void VmInterface::UpdateServiceVlan(bool force_update, bool policy_change) {
    ServiceVlanSet::iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        ServiceVlanSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this);
            service_vlan_list_.list_.erase(prev);
        } else {
            prev->Activate(this, force_update);
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

void VmInterface::UpdateStaticRoute(bool force_update, bool policy_change) {
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
            prev->Activate(this, force_update, policy_change);
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

void VmInterface::UpdateAllowedAddressPair(bool force_update, bool policy_change) {
    AllowedAddressPairSet::iterator it =
       allowed_address_pair_list_.list_.begin();
    while (it != allowed_address_pair_list_.list_.end()) {
        AllowedAddressPairSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this);
            allowed_address_pair_list_.list_.erase(prev);
        } else {
            prev->Activate(this, force_update, policy_change);
        }
    }
}

void VmInterface::DeleteAllowedAddressPair() {
    AllowedAddressPairSet::iterator it =
        allowed_address_pair_list_.list_.begin();
    while (it != allowed_address_pair_list_.list_.end()) {
        AllowedAddressPairSet::iterator prev = it++;
        prev->DeActivate(this);
        if (prev->del_pending_) {
            allowed_address_pair_list_.list_.erase(prev);
        }
    }
}

static bool CompareAddressType(const AddressType &lhs,
                               const AddressType &rhs) {
    if (lhs.subnet.ip_prefix != rhs.subnet.ip_prefix) {
        return false;
    }

    if (lhs.subnet.ip_prefix_len != rhs.subnet.ip_prefix_len) {
        return false;
    }

    if (lhs.virtual_network != rhs.virtual_network) {
        return false;
    }

    if (lhs.security_group != rhs.security_group) {
        return false;
    }
    return true;
}

static bool ComparePortType(const PortType &lhs,
                            const PortType &rhs) {
    if (lhs.start_port != rhs.start_port) {
        return false;
    }

    if (lhs.end_port != rhs.end_port) {
        return false;
    }
    return true;
}

static bool CompareMatchConditionType(const MatchConditionType &lhs,
                                      const MatchConditionType &rhs) {
    if (lhs.protocol != rhs.protocol) {
        return lhs.protocol < rhs.protocol;
    }

    if (!CompareAddressType(lhs.src_address, rhs.src_address)) {
        return false;
    }

    if (!ComparePortType(lhs.src_port, rhs.src_port)) {
        return false;
    }

    if (!CompareAddressType(lhs.dst_address, rhs.dst_address)) {
        return false;
    }

    if (!ComparePortType(lhs.dst_port, rhs.dst_port)) {
        return false;
    }

    return true;
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
    AclData *data = new AclData(acl_spec);
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
        vrf_assign_rule_list_.list_.erase(prev);
    }

    vrf_assign_acl_ = NULL;
    DBRequest req;
    AclKey *key = new AclKey(uuid_);
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(key);
    req.data.reset(NULL);
    agent->acl_table()->Process(req);
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

void VmInterface::UpdateL2TunnelId(bool force_update, bool policy_change) {
    if (IsVxlanMode() == false) {
        AllocL2MplsLabel(force_update, policy_change);
    }
}

void VmInterface::DeleteL2TunnelId() {
    DeleteL2MplsLabel();
}

void VmInterface::UpdateL2InterfaceRoute(bool old_l2_active, bool force_update) {
    if (l2_active_ == false)
        return;

    if (old_l2_active && force_update == false)
        return;

    const string &vrf_name = vrf_.get()->GetName();

    assert(peer_.get());
    Layer2AgentRouteTable::AddLocalVmRoute(peer_.get(), GetUuid(),
                                           vn_->GetName(), vrf_name, l2_label_,
                                           vxlan_id_, MacAddress::FromString(vm_mac()),
                                           ip_addr(), 0, 32);
}

void VmInterface::DeleteL2InterfaceRoute(bool old_l2_active, VrfEntry *old_vrf) {
    if (old_l2_active == false)
        return;

    if ((vxlan_id_ != 0) &&
        (TunnelType::ComputeType(TunnelType::AllType()) == TunnelType::VXLAN)) {
        VxLanId::Delete(vxlan_id_);
        vxlan_id_ = 0;
    }
    Layer2AgentRouteTable::Delete(peer_.get(), old_vrf->GetName(),
                                  vxlan_id_, MacAddress::FromString(vm_mac_));
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

//Add a route for VM port
//If ECMP route, add new composite NH and mpls label for same
void VmInterface::AddRoute(const std::string &vrf_name, const IpAddress &addr,
                           uint32_t plen, const std::string &dest_vn,
                           bool policy, bool ecmp, const IpAddress &gw_ip) {
    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);
    PathPreference path_preference;
    path_preference.set_ecmp(ecmp);
    if (local_preference_ != INVALID) {
        path_preference.set_static_preference(true);
    }
    if (ecmp || local_preference_ == HIGH) {
        path_preference.set_preference(PathPreference::HIGH);
    }

    InetUnicastAgentRouteTable::AddLocalVmRoute(peer_.get(), vrf_name, addr,
                                                 plen, GetUuid(),
                                                 dest_vn, label_,
                                                 sg_id_list, false,
                                                 path_preference, gw_ip);

    InetUnicastRouteKey *rt_key =
                new InetUnicastRouteKey(peer_.get(), vrf_name, addr, plen);
    rt_key->sub_op_ = AgentKey::RESYNC;

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(rt_key);
    req.data.reset(new PathPreferenceData(path_preference));
    AgentRouteTable *table =
    Agent::GetInstance()->vrf_table()->GetInet4UnicastRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }

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

void VmInterface::UpdateL3Services(bool dhcp, bool dns) {
    dhcp_enabled_ = dhcp;
    dns_enabled_ = dns;
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
            if (!ipv6 && vn_ipam[index].IsSubnetMember(ip_addr())) {
                break;
            }
            if (ipv6 && vn_ipam[index].IsSubnetMember(ip6_addr())) {
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
        if (!ipv6 && vn()->GetIpamData(ip_addr(), &ipam_name, &ipam_type)) {
            *options = ipam_type.dhcp_option_list.dhcp_option;
            return true;
        }
        if (ipv6 && vn()->GetIpamData(ip6_addr(), &ipam_name, &ipam_type)) {
            *options = ipam_type.dhcp_option_list.dhcp_option;
            return true;
        }
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////
// FloatingIp routines
/////////////////////////////////////////////////////////////////////////////

VmInterface::FloatingIp::FloatingIp() : 
    ListEntry(), floating_ip_(), vn_(NULL),
    vrf_(NULL), vrf_name_(""), vn_uuid_() {
}

VmInterface::FloatingIp::FloatingIp(const FloatingIp &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_),
    floating_ip_(rhs.floating_ip_), vn_(rhs.vn_), vrf_(rhs.vrf_),
    vrf_name_(rhs.vrf_name_), vn_uuid_(rhs.vn_uuid_) {
}

VmInterface::FloatingIp::FloatingIp(const IpAddress &addr,
                                    const std::string &vrf,
                                    const boost::uuids::uuid &vn_uuid) :
    ListEntry(), floating_ip_(addr), vn_(NULL), vrf_(NULL), vrf_name_(vrf),
    vn_uuid_(vn_uuid) {
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

void VmInterface::FloatingIp::Activate(VmInterface *interface,
                                       bool force_update) const {
    // Add route if not installed or if force requested
    if (installed_ && force_update == false)
        return;

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

    if (floating_ip_.is_v4()) {
        interface->AddRoute(vrf_.get()->GetName(), floating_ip_.to_v4(), 32,
                        vn_->GetName(), true, interface->ecmp(), Ip4Address(0));
        if (table->update_floatingip_cb().empty() == false) {
            table->update_floatingip_cb()(interface, vn_.get(),
                                          floating_ip_.to_v4(), false);
        }
    } else if (floating_ip_.is_v6()) {
        interface->AddRoute(vrf_.get()->GetName(), floating_ip_.to_v6(), 128,
                            vn_->GetName(), true, false, Ip6Address());
        //TODO:: callback for DNS handling
    }

    installed_ = true;
}

void VmInterface::FloatingIp::DeActivate(VmInterface *interface) const {
    if (installed_ == false)
        return;

    if (floating_ip_.is_v4()) {
        interface->DeleteRoute(vrf_.get()->GetName(), floating_ip_, 32);
        InterfaceTable *table =
            static_cast<InterfaceTable *>(interface->get_table());
        if (table->update_floatingip_cb().empty() == false) {
            table->update_floatingip_cb()(interface, vn_.get(),
                                          floating_ip_.to_v4(), true);
        }
    } else if (floating_ip_.is_v6()) {
        interface->DeleteRoute(vrf_.get()->GetName(), floating_ip_, 128);
        //TODO:: callback for DNS handling
    }
    vrf_ = NULL;
    installed_ = false;
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
    // Nothing to do 
}

void VmInterface::FloatingIpList::Remove(FloatingIpSet::iterator &it) {
    it->set_del_pending(true);
}

/////////////////////////////////////////////////////////////////////////////
// StaticRoute routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::StaticRoute::StaticRoute() :
    ListEntry(), vrf_(""), addr_(), plen_(0), gw_() {
}

VmInterface::StaticRoute::StaticRoute(const StaticRoute &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_), vrf_(rhs.vrf_),
    addr_(rhs.addr_), plen_(rhs.plen_), gw_(rhs.gw_) {
}

VmInterface::StaticRoute::StaticRoute(const std::string &vrf,
                                      const IpAddress &addr,
                                      uint32_t plen, const IpAddress &gw) :
    ListEntry(), vrf_(vrf), addr_(addr), plen_(plen), gw_(gw) {
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

    return plen_ < rhs->plen_;
}

void VmInterface::StaticRoute::Activate(VmInterface *interface,
                                        bool force_update,
                                        bool policy_change) const {
    bool ecmp = false;
    if (installed_ && force_update == false && policy_change == false)
        return;

    if (vrf_ != interface->vrf()->GetName()) {
        vrf_ = interface->vrf()->GetName();
    }

    if (installed_ == true && policy_change) {
        InetUnicastAgentRouteTable::ReEvaluatePaths(vrf_, addr_, plen_);
    } else if (installed_ == false || force_update) {
        if (addr_.is_v4()) {
            ecmp = interface->ecmp();
        }
        Ip4Address gw_ip(0);
        if (gw_.is_v4() && addr_.is_v4() && gw_.to_v4() != gw_ip) {
            SecurityGroupList sg_id_list;
            interface->CopySgIdList(&sg_id_list);
            InetUnicastAgentRouteTable::AddGatewayRoute(interface->peer_.get(),
                    vrf_, addr_.to_v4(),
                    plen_, gw_.to_v4(), interface->vn_->GetName(),
                    interface->vrf_->table_label(),
                    sg_id_list);
        } else {
            interface->AddRoute(vrf_, addr_, plen_,
                                interface->vn_->GetName(),
                                interface->policy_enabled(),
                                ecmp, IpAddress());
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
}

void VmInterface::StaticRouteList::Remove(StaticRouteSet::iterator &it) {
    it->set_del_pending(true);
}

///////////////////////////////////////////////////////////////////////////////
//Allowed addresss pair route
///////////////////////////////////////////////////////////////////////////////
VmInterface::AllowedAddressPair::AllowedAddressPair() :
    ListEntry(), vrf_(""), addr_(0), plen_(0), ecmp_(false), gw_ip_(0) {
}

VmInterface::AllowedAddressPair::AllowedAddressPair(
    const AllowedAddressPair &rhs) : ListEntry(rhs.installed_,
    rhs.del_pending_), vrf_(rhs.vrf_), addr_(rhs.addr_), plen_(rhs.plen_),
    ecmp_(rhs.ecmp_), gw_ip_(rhs.gw_ip_) {
}

VmInterface::AllowedAddressPair::AllowedAddressPair(const std::string &vrf,
                                                    const Ip4Address &addr,
                                                    uint32_t plen, bool ecmp) :
    ListEntry(), vrf_(vrf), addr_(addr), plen_(plen), ecmp_(ecmp) {
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

    return plen_ < rhs->plen_;
}

void VmInterface::AllowedAddressPair::Activate(VmInterface *interface,
                                               bool force_update,
                                               bool policy_change) const {
    const VnIpam *ipam = interface->vn_->GetIpam(addr_);
    Ip4Address ip(0);
    if (ipam) {
        ip = ipam->default_gw.to_v4();
    }

    if (installed_ && force_update == false && policy_change == false &&
        gw_ip_ == ip) {
        return;
    }

    if (vrf_ != interface->vrf()->GetName()) {
        vrf_ = interface->vrf()->GetName();
    }

    if (installed_ == true && policy_change) {
        InetUnicastAgentRouteTable::ReEvaluatePaths(vrf_, addr_, plen_);
    } else if (installed_ == false || force_update || gw_ip_ != ip) {
        gw_ip_ = ip;
        interface->AddRoute(vrf_, addr_, plen_, interface->vn_->GetName(),
                            interface->policy_enabled(),
                            ecmp_, gw_ip_);
    }

    installed_ = true;
}

void VmInterface::AllowedAddressPair::DeActivate(VmInterface *interface) const {
    if (installed_ == false)
        return;
    interface->DeleteRoute(vrf_, addr_, plen_);
    installed_ = false;
}

void VmInterface::AllowedAddressPairList::Insert(const AllowedAddressPair *rhs) {
    list_.insert(*rhs);
}

void VmInterface::AllowedAddressPairList::Update(const AllowedAddressPair *lhs,
                                          const AllowedAddressPair *rhs) {
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
    ListEntry(), tag_(0), vrf_name_(""), addr_(0), plen_(32), smac_(), dmac_(),
    vrf_(NULL), label_(MplsTable::kInvalidLabel) {
}

VmInterface::ServiceVlan::ServiceVlan(const ServiceVlan &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_), tag_(rhs.tag_),
    vrf_name_(rhs.vrf_name_), addr_(rhs.addr_), plen_(rhs.plen_),
    smac_(rhs.smac_), dmac_(rhs.dmac_), vrf_(rhs.vrf_), label_(rhs.label_) {
}

VmInterface::ServiceVlan::ServiceVlan(uint16_t tag, const std::string &vrf_name,
                                      const Ip4Address &addr, uint8_t plen,
                                      const MacAddress &smac,
                                      const MacAddress &dmac) :
    ListEntry(), tag_(tag), vrf_name_(vrf_name), addr_(addr), plen_(plen),
    smac_(smac), dmac_(dmac), vrf_(NULL), label_(MplsTable::kInvalidLabel)
    {
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
                                        bool force_update) const {
    InterfaceTable *table =
        static_cast<InterfaceTable *>(interface->get_table());
    VrfEntry *vrf = table->FindVrfRef(vrf_name_);
    assert(vrf);

    if (label_ == MplsTable::kInvalidLabel) {
        VlanNH::Create(interface->GetUuid(), tag_, vrf_name_, smac_, dmac_);
        label_ = table->agent()->mpls_table()->AllocLabel();
        MplsLabel::CreateVlanNh(table->agent(), label_,
                                interface->GetUuid(), tag_);
        VrfAssignTable::CreateVlanReq(interface->GetUuid(), vrf_name_, tag_);
    }

    if (vrf_.get() != vrf) {
        interface->ServiceVlanRouteDel(*this);
        vrf_ = vrf;
        installed_ = false;
    }

    if (installed_ && force_update == false)
        return;

    interface->ServiceVlanRouteAdd(*this);
    installed_ = true;
}

void VmInterface::ServiceVlan::DeActivate(VmInterface *interface) const {
    if (label_ != MplsTable::kInvalidLabel) {
        VrfAssignTable::DeleteVlanReq(interface->GetUuid(), tag_);
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
}

void VmInterface::ServiceVlanList::Remove(ServiceVlanSet::iterator &it) {
    it->set_del_pending(true);
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

void VmInterface::ServiceVlanRouteAdd(const ServiceVlan &entry) {
    if (vrf_.get() == NULL ||
        vn_.get() == NULL) {
        return;
    }

    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);
    PathPreference path_preference;
    path_preference.set_ecmp(ecmp());
    if (ecmp()) {
        path_preference.set_preference(PathPreference::HIGH);
    }
    InetUnicastAgentRouteTable::AddVlanNHRoute
        (peer_.get(), entry.vrf_->GetName(), entry.addr_, 32,
         GetUuid(), entry.tag_, entry.label_, vn()->GetName(), sg_id_list,
         path_preference);

    entry.installed_ = true;
    return;
}

void VmInterface::ServiceVlanRouteDel(const ServiceVlan &entry) {
    if (entry.installed_ == false) {
        return;
    }
    
    InetUnicastAgentRouteTable::Delete
        (peer_.get(), entry.vrf_->GetName(), entry.addr_, 32);

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

////////////////////////////////////////////////////////////////////////////
// VRF assign rule routines
////////////////////////////////////////////////////////////////////////////
VmInterface::VrfAssignRule::VrfAssignRule():
    ListEntry(), id_(0), vrf_name_(" "), vrf_(NULL), ignore_acl_(false) {
}

VmInterface::VrfAssignRule::VrfAssignRule(const VrfAssignRule &rhs):
    ListEntry(rhs.installed_, rhs.del_pending_), id_(rhs.id_),
    vrf_name_(rhs.vrf_name_), vrf_(rhs.vrf_), ignore_acl_(rhs.ignore_acl_),
    match_condition_(rhs.match_condition_) {
}

VmInterface::VrfAssignRule::VrfAssignRule(uint32_t id,
    const autogen::MatchConditionType &match_condition, 
    const std::string &vrf_name,
    bool ignore_acl):
    ListEntry(), id_(id), vrf_name_(vrf_name), ignore_acl_(ignore_acl), 
    match_condition_(match_condition) {
}

VmInterface::VrfAssignRule::~VrfAssignRule() {
}

bool VmInterface::VrfAssignRule::operator() (const VrfAssignRule &lhs,
                                             const VrfAssignRule &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::VrfAssignRule::IsLess(const VrfAssignRule *rhs) const {
    if (id_ != rhs->id_) {
        return id_ < rhs->id_;
    }
    if (vrf_name_ != rhs->vrf_name_) {
        return vrf_name_ < rhs->vrf_name_;
    }
    if (ignore_acl_ != rhs->ignore_acl_) {
        return ignore_acl_ < rhs->ignore_acl_;
    }

    return CompareMatchConditionType(match_condition_, rhs->match_condition_);
}

void VmInterface::VrfAssignRuleList::Insert(const VrfAssignRule *rhs) {
    list_.insert(*rhs);
}

void VmInterface::VrfAssignRuleList::Update(const VrfAssignRule *lhs,
                                            const VrfAssignRule *rhs) {
}

void VmInterface::VrfAssignRuleList::Remove(VrfAssignRuleSet::iterator &it) {
    it->set_del_pending(true);
}

/////////////////////////////////////////////////////////////////////////////
// Confg triggers for FloatingIP notification to operational DB
/////////////////////////////////////////////////////////////////////////////

// Find the vm-port linked to the floating-ip and resync it
void VmInterface::FloatingIpSync(InterfaceTable *table, IFMapNode *node) {
    if (table->agent()->cfg_listener()->SkipNode
        (node, table->agent()->cfg()->cfg_floatingip_table())) {
        return;
    }

    DBGraph *graph =
        static_cast<IFMapAgentTable *> (node->table())->GetGraph();;
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *if_node = static_cast<IFMapNode *>(iter.operator->());
        if (table->agent()->cfg_listener()->SkipNode
            (if_node, table->agent()->cfg()->cfg_vm_interface_table())) {
            continue;
        }

        DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
        if (table->IFNodeToReq(if_node, req)) {
            LOG(DEBUG, "FloatingIP SYNC for VM Port " << if_node->name());
            table->Enqueue(&req);
        }
    }

    return;
}

// Find all adjacent Floating-IP nodes and resync the corresponding
// interfaces
void VmInterface::FloatingIpPoolSync(InterfaceTable *table, IFMapNode *node) {
    CfgListener *cfg_listener = table->agent()->cfg_listener();
    if (cfg_listener->SkipNode(node)) {
        return;
    }

    DBGraph *graph =
        static_cast<IFMapAgentTable *> (node->table())->GetGraph();;
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *fip_node = static_cast<IFMapNode *>(iter.operator->());
        FloatingIpSync(table, fip_node);
    }

    return;
}

void VmInterface::InstanceIpSync(InterfaceTable *table, IFMapNode *node) {
    CfgListener *cfg_listener = table->agent()->cfg_listener();
    if (cfg_listener->SkipNode(node)) {
        return;
    }

    DBGraph *graph =
        static_cast<IFMapAgentTable *> (node->table())->GetGraph();;
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (table->agent()->cfg_listener()->SkipNode(adj)) {
            continue;
        }

        if (adj->table() ==
            table->agent()->cfg()->cfg_vm_interface_table()) {
            DBRequest req;
            if (table->IFNodeToReq(adj, req)) {
                table->Enqueue(&req);
            }
        }
    }

}

void VmInterface::PhysicalPortSync(InterfaceTable *table, IFMapNode *node) {
    CfgListener *cfg_listener = table->agent()->cfg_listener();
    if (cfg_listener->SkipNode(node)) {
        return;
    }

    DBGraph *graph =
        static_cast<IFMapAgentTable *> (node->table())->GetGraph();;
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (table->agent()->cfg_listener()->SkipNode(adj)) {
            continue;
        }

        if (adj->table() ==
            table->agent()->cfg()->cfg_logical_port_table()) {
            LogicalPortSync(table, adj);
        }
    }
}



void VmInterface::LogicalPortSync(InterfaceTable *table, IFMapNode *node) {
    CfgListener *cfg_listener = table->agent()->cfg_listener();
    if (cfg_listener->SkipNode(node)) {
        return;
    }

    DBGraph *graph =
        static_cast<IFMapAgentTable *> (node->table())->GetGraph();;
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (table->agent()->cfg_listener()->SkipNode(adj)) {
            continue;
        }

        if (adj->table() ==
            table->agent()->cfg()->cfg_vm_interface_table()) {
            DBRequest req;
            if (table->IFNodeToReq(adj, req)) {
                table->Enqueue(&req);
            }
        }
    }
}


void VmInterface::SubnetSync(InterfaceTable *table, IFMapNode *node) {
    CfgListener *cfg_listener = table->agent()->cfg_listener();
    if (cfg_listener->SkipNode(node)) {
        return;
    }

    DBGraph *graph =
        static_cast<IFMapAgentTable *> (node->table())->GetGraph();;
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (table->agent()->cfg_listener()->SkipNode(adj)) {
            continue;
        }

        if (adj->table() ==
            table->agent()->cfg()->cfg_vm_interface_table()) {
            DBRequest req;
            if (table->IFNodeToReq(adj, req)) {
                table->Enqueue(&req);
            }
        }
    }
}

void VmInterface::FloatingIpVnSync(InterfaceTable *table, IFMapNode *node) {
    CfgListener *cfg_listener = table->agent()->cfg_listener();
    if (cfg_listener->SkipNode(node)) {
        return;
    }

    DBGraph *graph =
        static_cast<IFMapAgentTable *> (node->table())->GetGraph();;
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *pool_node = static_cast<IFMapNode *>(iter.operator->());
        FloatingIpPoolSync(table, pool_node);
    }

    return;
}

void VmInterface::FloatingIpVrfSync(InterfaceTable *table, IFMapNode *node) {
    CfgListener *cfg_listener = table->agent()->cfg_listener();
    if (cfg_listener->SkipNode(node)) {
        return;
    }

    DBGraph *graph =
        static_cast<IFMapAgentTable *> (node->table())->GetGraph();;
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *vn_node = static_cast<IFMapNode *>(iter.operator->());
        FloatingIpVnSync(table, vn_node);
    }

    return;
}

void VmInterface::VnSync(InterfaceTable *table, IFMapNode *node) {
    CfgListener *cfg_listener = table->agent()->cfg_listener();
    if (cfg_listener->SkipNode(node)) {
        return;
    }
    // Walk the node to get neighbouring interface 
    DBGraph *graph =
        static_cast<IFMapAgentTable *> (node->table())->GetGraph();;
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph); 
         iter != node->end(graph); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (cfg_listener->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() ==
            table->agent()->cfg()->cfg_vm_interface_table()) {
            DBRequest req;
            if (table->IFNodeToReq(adj_node, req) == true) {
                LOG(DEBUG, "VN change sync for Port " << adj_node->name());
                table->Enqueue(&req);
            }
        }
    }
}

const string VmInterface::GetAnalyzer() const {
    if (mirror_entry()) {
        return mirror_entry()->GetAnalyzerName();
    } else {
        return std::string();
    }
}

void VmInterface::SendTrace(Trace event) {
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

    intf_info.set_ip_address(ip_addr_.to_string());
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
    OPER_TRACE(Interface, intf_info);
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
                          const Ip6Address &ip6) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid,
                                     os_name));

    req.data.reset(new VmInterfaceNovaData(addr, ip6, mac, vm_name,
                                           nil_uuid(), parent, tx_vlan_id,
                                           rx_vlan_id));
    table->Enqueue(&req);
}

// Delete a VM-Interface
void VmInterface::Delete(InterfaceTable *table, const uuid &intf_uuid,
                         VmInterface::Configurer configurer) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""));

    if (configurer == VmInterface::CONFIG) {
        req.data.reset(new VmInterfaceConfigData(NULL));
    } else if (configurer == VmInterface::EXTERNAL) {
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

    if (ip6_addr_ != addr) {
        ip6_addr_ = addr;
        ret = true;
    }

    return ret;
}

