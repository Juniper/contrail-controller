/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

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
    vn_(NULL), ip_addr_(0), mdata_addr_(0), subnet_bcast_addr_(0),
    vm_mac_(""), policy_enabled_(false), mirror_entry_(NULL),
    mirror_direction_(MIRROR_RX_TX), cfg_name_(""), fabric_port_(true),
    need_linklocal_ip_(false), dhcp_enable_(true),
    do_dhcp_relay_(false), vm_name_(),
    vm_project_uuid_(nil_uuid()), vxlan_id_(0), layer2_forwarding_(true),
    ipv4_forwarding_(true), mac_set_(false), ecmp_(false),
    vlan_id_(kInvalidVlanId), parent_(NULL), oper_dhcp_options_(),
    sg_list_(), floating_ip_list_(), service_vlan_list_(),
    static_route_list_(), allowed_address_pair_list_(),
    vrf_assign_rule_list_(), vrf_assign_acl_(NULL), vm_ip_gw_addr_(0) {
    ipv4_active_ = false;
    l2_active_ = false;
}

VmInterface::VmInterface(const boost::uuids::uuid &uuid,
                         const std::string &name,
                         const Ip4Address &addr, const std::string &mac,
                         const std::string &vm_name,
                         const boost::uuids::uuid &vm_project_uuid,
                         uint16_t vlan_id, Interface *parent) :
    Interface(Interface::VM_INTERFACE, uuid, name, NULL), vm_(NULL),
    vn_(NULL), ip_addr_(addr), mdata_addr_(0), subnet_bcast_addr_(0),
    vm_mac_(mac), policy_enabled_(false), mirror_entry_(NULL),
    mirror_direction_(MIRROR_RX_TX), cfg_name_(""), fabric_port_(true),
    need_linklocal_ip_(false), dhcp_enable_(true),
    do_dhcp_relay_(false), vm_name_(vm_name),
    vm_project_uuid_(vm_project_uuid), vxlan_id_(0),
    layer2_forwarding_(true), ipv4_forwarding_(true), mac_set_(false),
    ecmp_(false), vlan_id_(vlan_id), parent_(parent), oper_dhcp_options_(),
    sg_list_(), floating_ip_list_(), service_vlan_list_(),
    static_route_list_(), allowed_address_pair_list_(),
    vrf_assign_rule_list_(), vrf_assign_acl_(NULL) {
    ipv4_active_ = false;
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
                Ip4Address addr = Ip4Address::from_string(fip->address(), ec);
                if (ec.value() != 0) {
                    LOG(DEBUG, "Error decoding Floating IP address "
                        << fip->address());
                } else {
                    data->floating_ip_list_.list_.insert
                        (VmInterface::FloatingIp(addr, vrf_node->name(),
                                                 vn_uuid));
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
        Ip4Address ip(0);
        boost::system::error_code ec;
        ec = Ip4PrefixParse(it->prefix, &ip, &plen);
        if (ec.value() == 0) {
            data->static_route_list_.list_.insert
                (VmInterface::StaticRoute(data->vrf_name_, ip, plen));
        } else {
            LOG(DEBUG, "Error decoding Static Route IP address " << ip);
        }
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
            MacAddress dmac(Agent::BcastMac());
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
    data->addr_ = Ip4Address::from_string(ip->address(), err);
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

// Virtual Machine Interface is added or deleted into oper DB from Nova
// messages. The Config notify is used only to change interface.
bool InterfaceTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    // Get interface UUID
    VirtualMachineInterface *cfg = static_cast <VirtualMachineInterface *>
        (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);

    CfgIntTable *cfg_table = agent_->interface_config_table();
    CfgIntKey cfg_key(u);
    CfgIntEntry *nova_entry = static_cast <CfgIntEntry *>
        (cfg_table->Find(&cfg_key));
    // If interface is not yet added to Config tree, return.
    // This API is invoked again when the interface is added to config tree.
    if (!nova_entry) {
        return false;
    }

    // Skip, if Nova has deleted the interface
    if (nova_entry->IsDeleted()) {
        return false;
    }

    // Skip config interface delete notification
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, u, ""));
        req.data.reset(new VmInterfaceConfigData());
        return true;
    }

    // Update interface configuration
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    InterfaceKey *key = new VmInterfaceKey(AgentKey::RESYNC, u, "");

    VmInterfaceConfigData *data;
    data = new VmInterfaceConfigData();
    ReadAnalyzerNameAndCreate(agent_, cfg, *data);

    // Fill DHCP option data
    ReadDhcpOptions(cfg, *data);

    //Fill config data items
    data->cfg_name_= node->name();
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
            if (nova_entry->GetVnUuid() != data->vn_uuid_) {
                IFMAP_ERROR(InterfaceConfiguration,
                            "Virtual-network UUID mismatch for interface:",
                            UuidToString(u),
                            "configuration VN uuid",
                            UuidToString(data->vn_uuid_),
                            "compute VN uuid",
                            UuidToString(nova_entry->GetVnUuid()));
            }
        }

        if (adj_node->table() == agent_->cfg()->cfg_vm_table()) {
            VirtualMachine *vm = static_cast<VirtualMachine *>
                (adj_node->GetObject());
            assert(vm);
            autogen::IdPermsType id_perms = vm->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong,
                       id_perms.uuid.uuid_lslong, data->vm_uuid_);
            if (nova_entry->GetVmUuid() != data->vm_uuid_) {
                IFMAP_ERROR(InterfaceConfiguration,
                            "Virtual-machine UUID mismatch for interface:",
                            UuidToString(u),
                            "configuration VM UUID is",
                            UuidToString(data->vm_uuid_),
                            "compute VM uuid is",
                            UuidToString(nova_entry->GetVnUuid()));
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
    const VmInterfaceAddData *vm_data =
        static_cast<const VmInterfaceAddData *>(data);
    // Add is only supported with ADD_DEL_CHANGE key and data
    assert(vm_data->type_ == VmInterfaceData::ADD_DEL_CHANGE);

    const VmInterfaceAddData *add_data =
        static_cast<const VmInterfaceAddData *>(data);

    Interface *parent = NULL;
    if (add_data->vlan_id_ != VmInterface::kInvalidVlanId &&
        add_data->parent_ != Agent::NullString()) {
        PhysicalInterfaceKey key(add_data->parent_);
        parent = static_cast<Interface *>
            (table->agent()->interface_table()->FindActiveEntry(&key));
        assert(parent != NULL);
    }

    return new VmInterface(uuid_, name_, add_data->ip_addr_, add_data->vm_mac_,
                           add_data->vm_name_, add_data->vm_project_uuid_,
                           add_data->vlan_id_, parent);
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
    return false;
}

// Handle RESYNC DB Request. Handles multiple sub-types,
// - CONFIG : RESYNC from config message
// - IP_ADDR: RESYNC due to learning IP from DHCP
// - MIRROR : RESYNC due to change in mirror config
bool VmInterface::Resync(VmInterfaceData *data) {
    bool ret = false;

    // Copy old values used to update config below
    bool old_ipv4_active = ipv4_active_;
    bool old_l2_active = l2_active_;
    bool old_policy = policy_enabled_;
    VrfEntryRef old_vrf = vrf_;
    Ip4Address old_addr = ip_addr_;
    int old_vxlan_id = vxlan_id_;
    bool old_need_linklocal_ip = need_linklocal_ip_;
    bool sg_changed = false;
    bool ecmp_changed = false;

    if (data) {
        if (data->type_ == VmInterfaceData::CONFIG) {
            VmInterfaceConfigData *cfg = static_cast<VmInterfaceConfigData *>
                (data);
            ret = CopyConfig(cfg, &sg_changed, &ecmp_changed);
        } else if (data->type_ == VmInterfaceData::IP_ADDR) {
            VmInterfaceIpAddressData *addr =
                static_cast<VmInterfaceIpAddressData *> (data);
            ret = ResyncIpAddress(addr);
        } else if (data->type_ == VmInterfaceData::MIRROR) {
            VmInterfaceMirrorData *mirror = static_cast<VmInterfaceMirrorData *>
                (data);
            ret = ResyncMirror(mirror);
        } else if (data->type_ == VmInterfaceData::OS_OPER_STATE) {
            VmInterfaceOsOperStateData *oper_state =
                static_cast<VmInterfaceOsOperStateData *> (data);
            ret = ResyncOsOperState(oper_state);
        } else {
            assert(0);
        }
    }

    ipv4_active_ = IsL3Active();
    l2_active_ = IsL2Active();
    if (ipv4_active_ != old_ipv4_active) {
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
                ecmp_changed);

    return ret;
}

void VmInterface::Add() {
    peer_.reset(new LocalVmPortPeer(LOCAL_VM_PORT_PEER_NAME, id_));
}

void VmInterface::Delete() {
    VmInterfaceConfigData data;
    Resync(&data);

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    table->DeleteDhcpSnoopEntry(name_);
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

// Copies configuration from DB-Request data. The actual applying of
// configuration, like adding/deleting routes must be done with ApplyConfig()
bool VmInterface::CopyConfig(VmInterfaceConfigData *data, bool *sg_changed,
                             bool *ecmp_changed) {
    bool ret = false;
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());

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
    if (os_index_ == kInvalidIndex) {
        GetOsParams(table->agent());
        if (os_index_ != kInvalidIndex)
            ret = true;
    }

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

    bool val = vn ? vn_->layer2_forwarding() : false;
    if (layer2_forwarding_ != val) {
        layer2_forwarding_ = val;
        ret = true;
    }

    val = vn ? vn_->Ipv4Forwarding() : false;
    if (ipv4_forwarding_ != val) {
        ipv4_forwarding_ = val;
        ret = true;
    }

    val = ipv4_forwarding_ ? data->need_linklocal_ip_ : false;
    if (need_linklocal_ip_ != val) {
        need_linklocal_ip_ = val;
        ret = true;
    }

    // CopyIpAddress uses fabric_port_. So, set it before CopyIpAddresss
    val = ipv4_forwarding_ ? data->fabric_port_ : false;
    if (fabric_port_ != val) {
        fabric_port_ = val;
        ret = true;
    }

    Ip4Address ipaddr = ipv4_forwarding_ ? data->addr_ : Ip4Address(0);
    if (CopyIpAddress(ipaddr)) {
        ret = true;
    }

    if (dhcp_enable_ != data->dhcp_enable_) {
        dhcp_enable_ = data->dhcp_enable_;
        ret = true;
    }

    bool mac_set = true;
    struct ether_addr *addrp = ether_aton(vm_mac_.c_str());
    if (addrp == NULL) {
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

    // Copy DHCP options; ret is not modified as there is no dependent action
    oper_dhcp_options_ = data->oper_dhcp_options_;

    // Audit operational and config floating-ip list
    FloatingIpSet &old_fip_list = floating_ip_list_.list_;
    FloatingIpSet &new_fip_list = data->floating_ip_list_.list_;
    if (AuditList<FloatingIpList, FloatingIpSet::iterator>
        (floating_ip_list_, old_fip_list.begin(), old_fip_list.end(),
         new_fip_list.begin(), new_fip_list.end())) {
        ret = true;
    }


    // Audit operational and config Service VLAN list
    ServiceVlanSet &old_service_list = service_vlan_list_.list_;
    ServiceVlanSet &new_service_list = data->service_vlan_list_.list_;
    if (AuditList<ServiceVlanList, ServiceVlanSet::iterator>
        (service_vlan_list_, old_service_list.begin(), old_service_list.end(),
         new_service_list.begin(), new_service_list.end())) {
        ret = true;
    }

    // Audit operational and config Static Route list
    StaticRouteSet &old_route_list = static_route_list_.list_;
    StaticRouteSet &new_route_list = data->static_route_list_.list_;
    if (AuditList<StaticRouteList, StaticRouteSet::iterator>
        (static_route_list_, old_route_list.begin(), old_route_list.end(),
         new_route_list.begin(), new_route_list.end())) {
        ret = true;
    }

    // Audit operational and config allowed address pair
    AllowedAddressPairSet &old_aap_list = allowed_address_pair_list_.list_;
    AllowedAddressPairSet &new_aap_list = data->allowed_address_pair_list_.list_;
    if (AuditList<AllowedAddressPairList, AllowedAddressPairSet::iterator>
       (allowed_address_pair_list_, old_aap_list.begin(), old_aap_list.end(),
        new_aap_list.begin(), new_aap_list.end())) {
        ret = true;
    }

    // Audit operational and config Security Group list
    SecurityGroupEntrySet &old_sg_list = sg_list_.list_;
    SecurityGroupEntrySet &new_sg_list = data->sg_list_.list_;
    *sg_changed =
	    AuditList<SecurityGroupEntryList, SecurityGroupEntrySet::iterator>
	    (sg_list_, old_sg_list.begin(), old_sg_list.end(),
	     new_sg_list.begin(), new_sg_list.end());
    if (*sg_changed) {
        ret = true;
    }

    VrfAssignRuleSet &old_vrf_assign_list = vrf_assign_rule_list_.list_;
    VrfAssignRuleSet &new_vrf_assign_list = data->vrf_assign_rule_list_.list_;
    if (AuditList<VrfAssignRuleList, VrfAssignRuleSet::iterator>
        (vrf_assign_rule_list_, old_vrf_assign_list.begin(),
         old_vrf_assign_list.end(), new_vrf_assign_list.begin(),
         new_vrf_assign_list.end())) {
        ret = true;
     }

    if (ecmp_ != data->ecmp_) {
        ecmp_ = data->ecmp_;
        *ecmp_changed = true;
    }
    return ret;
}

void VmInterface::UpdateL3(bool old_ipv4_active, VrfEntry *old_vrf,
                           const Ip4Address &old_addr, int old_vxlan_id,
                           bool force_update, bool policy_change) {
    UpdateSecurityGroup();
    UpdateL3NextHop(old_ipv4_active);
    UpdateL3TunnelId(force_update, policy_change);
    UpdateL3InterfaceRoute(old_ipv4_active, force_update, policy_change,
                           old_vrf, old_addr);
    UpdateMetadataRoute(old_ipv4_active, old_vrf);
    UpdateFloatingIp(force_update, policy_change);
    UpdateServiceVlan(force_update, policy_change);
    UpdateStaticRoute(force_update, policy_change);
    UpdateAllowedAddressPair(force_update, policy_change);
    UpdateVrfAssignRule();
}

void VmInterface::DeleteL3(bool old_ipv4_active, VrfEntry *old_vrf,
                           const Ip4Address &old_addr,
                           bool old_need_linklocal_ip) {
    DeleteL3InterfaceRoute(old_ipv4_active, old_vrf, old_addr);
    DeleteMetadataRoute(old_ipv4_active, old_vrf, old_need_linklocal_ip);
    DeleteFloatingIp();
    DeleteServiceVlan();
    DeleteStaticRoute();
    DeleteAllowedAddressPair();
    DeleteSecurityGroup();
    DeleteL3TunnelId();
    DeleteVrfAssignRule();
    DeleteL3NextHop(old_ipv4_active);
}

void VmInterface::UpdateVxLan() {
    if (l2_active_ && (vxlan_id_ == 0)) {
        vxlan_id_ = vn_.get() ? vn_->GetVxLanId() : 0;
    }
}

void VmInterface::UpdateL2(bool old_l2_active, VrfEntry *old_vrf, int old_vxlan_id,
                           bool force_update, bool policy_change) {
    UpdateVxLan();
    UpdateL2NextHop(old_l2_active);
    UpdateL2TunnelId(force_update, policy_change);
    UpdateL2InterfaceRoute(old_l2_active, force_update);
}

void VmInterface::UpdateL2() {
    UpdateL2(l2_active_, vrf_.get(), vxlan_id_, false, false);
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
                              bool sg_changed, bool ecmp_mode_changed) {
    bool force_update = false;
    if (sg_changed || ecmp_mode_changed) {
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
    if (ipv4_forwarding_) {
        UpdateL3Services(dhcp_enable_, true);
    } else {
        UpdateL3Services(false, false);
    }

    // Add/Del/Update L3
    if (ipv4_active_ && ipv4_forwarding_) {
        UpdateL3(old_ipv4_active, old_vrf, old_addr, old_vxlan_id, force_update,
                 policy_change);
    } else if (old_ipv4_active) {
        DeleteL3(old_ipv4_active, old_vrf, old_addr, old_need_linklocal_ip);
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
}

// Handle RESYNC message from mirror
bool VmInterface::ResyncMirror(VmInterfaceMirrorData *data) {
    bool ret = false;

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    MirrorEntry *mirror_entry = NULL;

    if (data->mirror_enable_ == true) {
        mirror_entry = table->FindMirrorRef(data->analyzer_name_);
    }

    if (mirror_entry_ != mirror_entry) {
        mirror_entry_ = mirror_entry;
        ret = true;
    }

    return ret;
}

// Update for VM IP address only
// For interfaces in IP Fabric VRF, we send DHCP requests to external servers
// if config doesnt provide an address. This address is updated here.
bool VmInterface::ResyncIpAddress(const VmInterfaceIpAddressData *data) {
    bool ret = false;

    if (os_index_ == kInvalidIndex) {
        InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
        GetOsParams(table->agent());
        if (os_index_ != kInvalidIndex)
            ret = true;
    }

    if (!ipv4_forwarding_) {
        return ret;
    }

    bool old_ipv4_active = ipv4_active_;
    Ip4Address old_addr = ip_addr_;

    Ip4Address addr = Ip4Address(0);
    if (CopyIpAddress(addr)) {
        ret = true;
    }

    ipv4_active_ = IsL3Active();
    ApplyConfig(old_ipv4_active, l2_active_, policy_enabled_, vrf_.get(), old_addr,
                vxlan_id_, need_linklocal_ip_, false, false);
    return ret;
}

// Resync oper-state for the interface
bool VmInterface::ResyncOsOperState(const VmInterfaceOsOperStateData *data) {
    bool ret = false;

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());

    uint32_t old_os_index = os_index_;
    bool old_ipv4_active = ipv4_active_;

    GetOsParams(table->agent());
    if (os_index_ != old_os_index)
        ret = true;

    ipv4_active_ = IsL3Active();
    if (ipv4_active_ != old_ipv4_active)
        ret = true;

    ApplyConfig(old_ipv4_active, l2_active_, policy_enabled_, vrf_.get(),
                ip_addr_, vxlan_id_, need_linklocal_ip_, false, false);
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VM Port Entry utility routines
/////////////////////////////////////////////////////////////////////////////

void VmInterface::GetOsParams(Agent *agent) {
    if (vlan_id_ == VmInterface::kInvalidVlanId) {
        Interface::GetOsParams(agent);
        return;
    }

    os_index_ = Interface::kInvalidIndex;
    mac_ = agent->vrrp_mac();
    os_oper_state_ = true;
}

// A VM Interface is L3 active under following conditions,
// - If interface is deleted, it is inactive
// - VM, VN, VRF are set
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

    if ((vn_.get() == NULL) || (vm_.get() == NULL) || (vrf_.get() == NULL)) { 
        return false;
    }

    if (!vn_.get()->admin_state()) {
        return false;
    }

    if (vlan_id_ != VmInterface::kInvalidVlanId) {
       return true;
    }


    if (os_index_ == kInvalidIndex)
        return false;

    return mac_set_;
}

bool VmInterface::IsL3Active() const {
    if (!ipv4_forwarding() || (ip_addr_.to_ulong() == 0)) {
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
    Inet4UnicastRouteKey rt_key(peer_.get(), vrf_->GetName(), ip_addr_, 32);
    const Inet4UnicastRouteEntry *rt =
        static_cast<const Inet4UnicastRouteEntry *>(
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
    if (label_ == MplsTable::kInvalidLabel) {
        Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
        label_ = agent->mpls_table()->AllocLabel();
        new_entry = true;
    }

    if (force_update || policy_change || new_entry)
        MplsLabel::CreateVPortLabel(label_, GetUuid(), policy_enabled_,
                                    InterfaceNHFlags::INET4);
}

// Delete MPLS Label for Layer3 routes
void VmInterface::DeleteL3MplsLabel() {
    if (label_ == MplsTable::kInvalidLabel) {
        return;
    }

    MplsLabel::Delete(label_);
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

    if (force_update || policy_change || new_entry)
        MplsLabel::CreateVPortLabel(l2_label_, GetUuid(), false,
                                    InterfaceNHFlags::LAYER2);
}

// Delete MPLS Label for Layer2 routes
void VmInterface::DeleteL2MplsLabel() {
    if (l2_label_ == MplsTable::kInvalidLabel) {
        return;
    }

    MplsLabel::Delete(l2_label_);
    l2_label_ = MplsTable::kInvalidLabel;
}

void VmInterface::UpdateL3TunnelId(bool force_update, bool policy_change) {
    //Currently only MPLS encap ind no VXLAN is supported for L3.
    //Unconditionally create a label
    AllocL3MplsLabel(force_update, policy_change);
}

void VmInterface::DeleteL3TunnelId() {
    DeleteL3MplsLabel();
}

//Check if interface transitioned from inactive to active layer 2 forwarding
bool VmInterface::L2Activated(bool old_l2_active) {
    if (old_l2_active == false && l2_active_ == true) {
        return true;
    }
    return false;
}

//Check if interface transitioned from inactive to active IP forwarding
bool VmInterface::L3Activated(bool old_ipv4_active) {
    if (old_ipv4_active == false && ipv4_active_ == true) {
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
bool VmInterface::L3Deactivated(bool old_ipv4_active) {
    if (old_ipv4_active == true && ipv4_active_ == false) {
        return true;
    }
    return false;
}

void VmInterface::UpdateMulticastNextHop(bool old_ipv4_active,
                                         bool old_l2_active) {
    if (L3Activated(old_ipv4_active) || L2Activated(old_l2_active)) {
        struct ether_addr *addrp = ether_aton(vm_mac_.c_str());
        InterfaceNH::CreateMulticastVmInterfaceNH(GetUuid(),
                                                  MacAddress(addrp),
                                                  vrf_->GetName());
    }
}

void VmInterface::UpdateL2NextHop(bool old_l2_active) {
    if (L2Activated(old_l2_active)) {
        struct ether_addr *addrp = ether_aton(vm_mac_.c_str());
        InterfaceNH::CreateL2VmInterfaceNH(GetUuid(), MacAddress(addrp), vrf_->GetName());
    }
}

void VmInterface::UpdateL3NextHop(bool old_ipv4_active) {
    if (L3Activated(old_ipv4_active)) {
        InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
        Agent *agent = table->agent();

        struct ether_addr *addrp = ether_aton(vm_mac_.c_str());
        InterfaceNH::CreateL3VmInterfaceNH(GetUuid(), MacAddress(addrp), vrf_->GetName());
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

void VmInterface::DeleteL3NextHop(bool old_l3_active) {
    if (L3Deactivated(old_l3_active)) {
        InterfaceNH::DeleteL3InterfaceNH(GetUuid());
        flow_key_nh_ = NULL;
    }
}

void VmInterface::DeleteMulticastNextHop() {
    InterfaceNH::DeleteMulticastVmInterfaceNH(GetUuid());
}

// Add/Update route. Delete old route if VRF or address changed
void VmInterface::UpdateL3InterfaceRoute(bool old_ipv4_active, bool force_update,
                                         bool policy_change,
                                         VrfEntry * old_vrf,
                                         const Ip4Address &old_addr) {
    const VnIpam *ipam = vn_->GetIpam(ip_addr_);
    Ip4Address ip(0);
    if (ipam) {
        ip = ipam->default_gw;
    }

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
            Inet4UnicastAgentRouteTable::ReEvaluatePaths(vrf_->GetName(),
                                                        ip_addr_, 32);
        }
    }

    // If there is change in VRF or IP address, delete old route
    if (old_vrf != vrf_.get() || ip_addr_ != old_addr) {
        DeleteL3InterfaceRoute(old_ipv4_active, old_vrf, old_addr);
    }
}

void VmInterface::DeleteL3InterfaceRoute(bool old_ipv4_active, VrfEntry *old_vrf,
                                         const Ip4Address &old_addr) {
    if ((old_vrf == NULL) || (old_addr.to_ulong() == 0))
        return;

    DeleteRoute(old_vrf->GetName(), old_addr, 32);
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
    Inet4UnicastAgentRouteTable::AddLocalVmRoute
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
    Inet4UnicastAgentRouteTable::Delete(agent->link_local_peer(),
                                        agent->fabric_vrf_name(),
                                        mdata_addr_, 32);
}

void VmInterface::UpdateFloatingIp(bool force_update, bool policy_change) {
    FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
    while (it != floating_ip_list_.list_.end()) {
        FloatingIpSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this);
            floating_ip_list_.list_.erase(prev);
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
    } else {
        // If we are using VXLAN, then free label if allocated
        DeleteL2MplsLabel();
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

    struct ether_addr *addrp = ether_aton(vm_mac().c_str());
    const string &vrf_name = vrf_.get()->GetName();


    assert(peer_.get());
    Layer2AgentRouteTable::AddLocalVmRoute(peer_.get(), GetUuid(),
                                           vn_->GetName(), vrf_name, l2_label_,
                                           vxlan_id_, MacAddress(addrp), ip_addr(), 32);
}

void VmInterface::DeleteL2InterfaceRoute(bool old_l2_active, VrfEntry *old_vrf) {
    if (old_l2_active == false)
        return;

    if ((vxlan_id_ != 0) &&
        (TunnelType::ComputeType(TunnelType::AllType()) == TunnelType::VXLAN)) {
        VxLanId::Delete(vxlan_id_);
        vxlan_id_ = 0;
    }
    struct ether_addr *addrp = ether_aton(vm_mac_.c_str());
    Layer2AgentRouteTable::Delete(peer_.get(), old_vrf->GetName(),
                                  MacAddress(addrp));
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
void VmInterface::AddRoute(const std::string &vrf_name, const Ip4Address &addr,
                           uint32_t plen, const std::string &dest_vn,
                           bool policy, bool ecmp, const Ip4Address &gw_ip) {
    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);
    PathPreference path_preference;
    path_preference.set_ecmp(ecmp);
    if (ecmp) {
        path_preference.set_preference(PathPreference::HIGH);
    }

    Inet4UnicastAgentRouteTable::AddLocalVmRoute(peer_.get(), vrf_name, addr,
                                                 plen, GetUuid(),
                                                 dest_vn, label_,
                                                 sg_id_list, false,
                                                 path_preference, gw_ip);

    return;
}

void VmInterface::DeleteRoute(const std::string &vrf_name,
                              const Ip4Address &addr, uint32_t plen) {
    Inet4UnicastAgentRouteTable::Delete(peer_.get(), vrf_name, addr, plen);
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
                  std::vector<autogen::DhcpOptionType> *options) const {
    if (vn()) {
        const std::vector<VnIpam> &vn_ipam = vn()->GetVnIpam();
        uint32_t index;
        for (index = 0; index < vn_ipam.size(); ++index) {
            if (vn_ipam[index].IsSubnetMember(ip_addr())) {
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
                  std::vector<autogen::DhcpOptionType> *options) const {
    if (vn()) {
        std::string ipam_name;
        autogen::IpamType ipam_type;
        if (vn()->GetIpamData(ip_addr(), &ipam_name, &ipam_type)) {
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
    ListEntry(), floating_ip_(), vn_(NULL), vrf_(NULL), vrf_name_(""),
    vn_uuid_() {
}

VmInterface::FloatingIp::FloatingIp(const FloatingIp &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_), floating_ip_(rhs.floating_ip_),
    vn_(rhs.vn_), vrf_(rhs.vrf_), vrf_name_(rhs.vrf_name_),
    vn_uuid_(rhs.vn_uuid_) {
}

VmInterface::FloatingIp::FloatingIp(const Ip4Address &addr,
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

    interface->AddRoute(vrf_.get()->GetName(), floating_ip_, 32, vn_->GetName(),
                        true, interface->ecmp(), Ip4Address(0));
    if (table->update_floatingip_cb().empty() == false) {
        table->update_floatingip_cb()(interface, vn_.get(), floating_ip_, false);
    }

    installed_ = true;
}

void VmInterface::FloatingIp::DeActivate(VmInterface *interface) const {
    if (installed_ == false)
        return;

    interface->DeleteRoute(vrf_.get()->GetName(), floating_ip_, 32);
    InterfaceTable *table =
        static_cast<InterfaceTable *>(interface->get_table());
    if (table->update_floatingip_cb().empty() == false) {
        table->update_floatingip_cb()(interface, vn_.get(), floating_ip_, true);
    }
    vrf_ = NULL;
    installed_ = false;
}

void VmInterface::FloatingIpList::Insert(const FloatingIp *rhs) {
    list_.insert(*rhs);
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
    ListEntry(), vrf_(""), addr_(0), plen_(0) {
}

VmInterface::StaticRoute::StaticRoute(const StaticRoute &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_), vrf_(rhs.vrf_),
    addr_(rhs.addr_), plen_(rhs.plen_) {
}

VmInterface::StaticRoute::StaticRoute(const std::string &vrf,
                                      const Ip4Address &addr,
                                      uint32_t plen) :
    ListEntry(), vrf_(vrf), addr_(addr), plen_(plen) {
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
    if (installed_ && force_update == false && policy_change == false)
        return;

    if (vrf_ != interface->vrf()->GetName()) {
        vrf_ = interface->vrf()->GetName();
    }

    if (installed_ == true && policy_change) {
        Inet4UnicastAgentRouteTable::ReEvaluatePaths(vrf_, addr_, plen_);
    } else if (installed_ == false || force_update) {
        interface->AddRoute(vrf_, addr_, plen_, interface->vn_->GetName(),
                            interface->policy_enabled(),
                            interface->ecmp(), Ip4Address(0));
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
        ip = ipam->default_gw;
    }

    if (installed_ && force_update == false && policy_change == false &&
        gw_ip_ == ip) {
        return;
    }

    if (vrf_ != interface->vrf()->GetName()) {
        vrf_ = interface->vrf()->GetName();
    }

    if (installed_ == true && policy_change) {
        Inet4UnicastAgentRouteTable::ReEvaluatePaths(vrf_, addr_, plen_);
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
        MplsLabel::CreateVlanNh(label_, interface->GetUuid(), tag_);
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
        MplsLabel::Delete(label_);
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
    Inet4UnicastAgentRouteTable::AddVlanNHRoute
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
    
    Inet4UnicastAgentRouteTable::Delete
        (peer_.get(), entry.vrf_->GetName(), entry.addr_, 32);

    entry.installed_ = false;
    return;
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
void VmInterface::Add(InterfaceTable *table, const uuid &intf_uuid,
                      const string &os_name, const Ip4Address &addr,
                      const string &mac, const string &vm_name,
                      const uuid &vm_project_uuid, uint16_t vlan_id,
                      const std::string &parent) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid,
                                     os_name));
    req.data.reset(new VmInterfaceAddData(addr, mac, vm_name, vm_project_uuid,
                                          vlan_id, parent));
    table->Enqueue(&req);
}

// Delete a VM-Interface
void VmInterface::Delete(InterfaceTable *table, const uuid &intf_uuid) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""));
    req.data.reset(NULL);
    table->Enqueue(&req);
}
