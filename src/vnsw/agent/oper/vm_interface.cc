/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <netinet/ether.h>
#include <boost/uuid/uuid_io.hpp>

#include "base/logging.h"
#include "db/db.h"
#include "db/db_entry.h"
#include "db/db_table.h"
#include "ifmap/ifmap_node.h"

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <cmn/agent.h>
#include <oper/operdb_init.h>
#include <oper/agent_route.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/interface_common.h>
#include <oper/vrf_assign.h>
#include <oper/vxlan.h>
#include <oper/route_types.h>

#include <vnc_cfg_types.h>
#include <oper/agent_sandesh.h>
#include <oper/sg.h>
#include <ksync/interface_ksync.h>
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"

#include <services/dns_proto.h>

using namespace std;
using namespace boost::uuids;
using namespace autogen;

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

            ether_addr smac = *ether_aton(Agent::VrrpMac().c_str());
            ether_addr dmac = *ether_aton(Agent::BcastMac().c_str());
            if (rule.src_mac != Agent::NullString()) {
                smac = *ether_aton(rule.src_mac.c_str());
            }
            if (rule.src_mac != Agent::NullString()) {
                dmac = *ether_aton(rule.dst_mac.c_str());
            }
            data->service_vlan_list_.list_.insert
                (VmInterface::ServiceVlan(rule.vlan_tag, vrf_node->name(), addr,
                                          smac, dmac));
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
            dport = ContrailPorts::AnalyzerUdpPort;
        }
        agent->GetMirrorTable()->AddMirrorEntry
            (mirror_to.analyzer_name, std::string(), agent->GetRouterId(),
             agent->GetMirrorPort(), dip.to_v4(), dport);
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

    CfgIntTable *cfg_table = agent_->GetIntfCfgTable();
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

    //Fill config data items
    data->cfg_name_= node->name();

    SgUuidList sg_list(0);
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
            uuid sg_uuid = nil_uuid();
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                       sg_uuid);
            data->sg_list_.list_.insert
                (VmInterface::SecurityGroupEntry(sg_uuid));
        }

        if (adj_node->table() == agent_->cfg()->cfg_vn_table()) {
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

    data->fabric_port_ = false;
    data->need_linklocal_ip_ = true;
    if (data->vrf_name_ == agent_->GetDefaultVrf() ||
        data->vrf_name_ == agent_->GetLinkLocalVrfName()) {
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
            (table->agent()->GetInterfaceTable()->FindActiveEntry(&key));
        assert(parent != NULL);
    }

    return new VmInterface(uuid_, name_, add_data->ip_addr_, add_data->vm_mac_,
                           add_data->vm_name_, add_data->vlan_id_, parent);
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
    bool old_active = active_;
    bool old_policy = policy_enabled_;
    VrfEntryRef old_vrf = vrf_;
    Ip4Address old_addr = ip_addr_;
    int old_vxlan_id = vxlan_id_;
    bool old_layer2_forwarding = layer2_forwarding_;
    bool old_ipv4_forwarding = ipv4_forwarding_;
    bool old_fabric_port = fabric_port_;
    bool old_need_linklocal_ip = need_linklocal_ip_;
    bool sg_changed = false;

    if (data) {
        if (data->type_ == VmInterfaceData::CONFIG) {
            VmInterfaceConfigData *cfg = static_cast<VmInterfaceConfigData *>
                (data);
            ret = CopyConfig(cfg, &sg_changed);
        } else if (data->type_ == VmInterfaceData::IP_ADDR) {
            VmInterfaceIpAddressData *addr =
                static_cast<VmInterfaceIpAddressData *> (data);
            ret = ResyncIpAddress(addr);
        } else if (data->type_ == VmInterfaceData::MIRROR) {
            VmInterfaceMirrorData *mirror = static_cast<VmInterfaceMirrorData *>
                (data);
            ret = ResyncMirror(mirror);
        } else {
            assert(0);
        }
    }

    active_ = IsActive();
    if (active_ != old_active) {
        ret = true;
    }

    policy_enabled_ = PolicyEnabled();
    if (policy_enabled_ != old_policy) {
        ret = true;
    }

    // Apply config based on old and new values
    ApplyConfig(old_active, old_policy, old_vrf.get(), old_addr, old_vxlan_id,
                old_layer2_forwarding, old_ipv4_forwarding, old_fabric_port,
                old_need_linklocal_ip, sg_changed);

    return ret;
}

void VmInterface::Delete() {
    bool old_active = active_;
    active_ = false;
    ApplyConfig(old_active, policy_enabled_, vrf_.get(), ip_addr_, vxlan_id_,
                layer2_forwarding_, ipv4_forwarding_, fabric_port_,
                need_linklocal_ip_, false);
    InterfaceNH::DeleteVportReq(GetUuid());
}

bool VmInterface::CopyIpAddress(Ip4Address &addr) {
    bool ret = false;

    dhcp_snoop_ip_ = false;
    if (addr.to_ulong() == 0) {
        dhcp_snoop_ip_ = IsDhcpSnoopIp(name_, &addr);
    }

    if (ip_addr_ != addr) {
        ip_addr_ = addr;
        ret = true;
    }

    return ret;
}

// Copies configuration from DB-Request data. The actual applying of 
// configuration, like adding/deleting routes must be done with ApplyConfig()
bool VmInterface::CopyConfig(VmInterfaceConfigData *data, bool *sg_changed) {
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
        GetOsParams();
        if (os_index_ != kInvalidIndex)
            ret = true;
    }

    VnEntry *vn = table->FindVnRef(data->vn_uuid_);
    if (vn_.get() != vn) {
        vn_ = vn;
        ret = true;
    }

    int vxlan_id = vn ? vn->vxlan_id() : 0;
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

    val = ipv4_forwarding_ ? data->fabric_port_ : false;
    if (fabric_port_ != val) {
        fabric_port_ = val;
        ret = true;
    }

    Ip4Address ipaddr = ipv4_forwarding_ ? data->addr_ : Ip4Address(0);
    if (ipaddr.to_ulong() && CopyIpAddress(ipaddr)) {
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

    return ret;
}

void VmInterface::UpdateL3(bool old_active, VrfEntry *old_vrf,
                           const Ip4Address &old_addr, int old_vxlan_id,
                           bool force_update, bool policy_change) {
    UpdateNextHop(old_active);
    UpdateL3TunnelId(force_update, policy_change);
    UpdateL3InterfaceRoute(old_active, force_update, policy_change,
                           old_vrf, old_addr);
    UpdateMetadataRoute(old_active, old_vrf);
    UpdateFloatingIp(force_update, policy_change);
    UpdateServiceVlan(force_update, policy_change);
    UpdateStaticRoute(force_update, policy_change);
    UpdateSecurityGroup();
}

void VmInterface::DeleteL3(bool old_active, VrfEntry *old_vrf,
                           const Ip4Address &old_addr,
                           bool old_need_linklocal_ip) {
    DeleteL3InterfaceRoute(old_active, old_vrf, old_addr);
    DeleteMetadataRoute(old_active, old_vrf, old_need_linklocal_ip);
    DeleteFloatingIp();
    DeleteServiceVlan();
    DeleteStaticRoute();
    DeleteSecurityGroup();
    DeleteL3TunnelId();
}

void VmInterface::UpdateL2(bool old_active, VrfEntry *old_vrf, int old_vxlan_id,
                           bool force_update, bool policy_change) {
    UpdateL2TunnelId(force_update, policy_change);
    UpdateL2InterfaceRoute(old_active, force_update);
}

void VmInterface::UpdateL2() {
    UpdateL2(active_, vrf_.get(), vxlan_id_, false, false);
}

void VmInterface::DeleteL2(bool old_active, VrfEntry *old_vrf) {
    DeleteL2TunnelId();
    DeleteL2InterfaceRoute(old_active, old_vrf);
}

// Apply the latest configuration
void VmInterface::ApplyConfig(bool old_active, bool old_policy, 
                              VrfEntry *old_vrf, const Ip4Address &old_addr, 
                              int old_vxlan_id, bool old_layer2_forwarding,
                              bool old_ipv4_forwarding, bool old_fabric_port,
                              bool old_need_linklocal_ip, bool sg_changed) {
    // Update services flag based on active state
    UpdateServices(active_);

    bool force_update = sg_changed;
    bool policy_change = (policy_enabled_ != old_policy);

    // Add/Del/Update L3 
    if (active_ && ipv4_forwarding_ && fabric_port_ == false) {
        UpdateL3(old_active, old_vrf, old_addr, old_vxlan_id, force_update,
                 policy_change);
    } else if (old_active && old_ipv4_forwarding && old_fabric_port == false) {
        DeleteL3(old_active, old_vrf, old_addr, old_need_linklocal_ip);
    }

    // Add/Del/Update L2 
    if (active_ && layer2_forwarding_ && fabric_port_ == false) {
        UpdateL2(old_active, old_vrf, old_vxlan_id, force_update, policy_change);
    } else if (old_active && old_layer2_forwarding && 
               old_fabric_port == false) {
        DeleteL2(old_active, old_vrf);
    }

    if (old_active != active_) {
        if (active_) {
            SendTrace(ACTIVATED);
        } else {
            SendTrace(DEACTIVATED);
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
        GetOsParams();
        if (os_index_ != kInvalidIndex)
            ret = true;
    }

    if (!ipv4_forwarding_) {
        return ret;
    }

    bool old_active = active_;
    Ip4Address old_addr = ip_addr_;

    Ip4Address ipaddr = data->ip_addr_;
    if (CopyIpAddress(ipaddr)) {
        ret = true;
    }

    active_ = IsActive();
    ApplyConfig(old_active, policy_enabled_, vrf_.get(), old_addr,
                vxlan_id_, layer2_forwarding_, ipv4_forwarding_, fabric_port_,
                need_linklocal_ip_, false);
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VM Port Entry utility routines
/////////////////////////////////////////////////////////////////////////////

// Get DHCP IP address. DHCP IP is used only if IP address not specified in 
// config. We can get DHCP IP in two ways,
// - By snooping dhcp packets
// - To support agent restart, the snooped address are stored in InterfaceKSnap
//   table. Query the table to find DHCP Snooped address
bool VmInterface::IsDhcpSnoopIp(std::string &name, Ip4Address *ip) const {
    if (dhcp_snoop_ip_) {
        *ip = ip_addr_;
        return true;
    }

    uint32_t addr;
    InterfaceKSnap *intf = InterfaceKSnap::GetInstance();
    if (intf) {
        if (intf->FindInterfaceKSnapData(name, addr)) {
            *ip = Ip4Address(addr);
            return true;
        }
    }

    return false;
}

// A VM Interface is active under following conditions,
// - If interface is deleted, it is inactive
// - VM, VN, VRF and IP-Address are set
// - For non-VMWARE hypervisors,
//   The tap interface must be created. This is verified by os_index_
// - MAC address set for the interface
bool VmInterface::IsActive() {
    if (IsDeleted()) {
        return false;
    }

    if ((vn_.get() == NULL) || (vm_.get() == NULL) || (vrf_.get() == NULL) || 
        (ip_addr_.to_ulong() == 0)) {
        return false;
    }

    if (os_index_ == kInvalidIndex)
        return false;

    return mac_set_;
}

// Compute if policy is to be enabled on the interface
bool VmInterface::PolicyEnabled() {
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
    bool new_entry = false;
    if (label_ == MplsTable::kInvalidLabel) {
        Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
        label_ = agent->GetMplsTable()->AllocLabel();
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
        l2_label_ = agent->GetMplsTable()->AllocLabel();
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
    if (IsVxlanMode() == false) {
        AllocL3MplsLabel(force_update, policy_change);
    } else {
        // If we are using VXLAN, then free label if allocated
        DeleteL3MplsLabel();
    }
}

void VmInterface::DeleteL3TunnelId() {
    DeleteL3MplsLabel();
}

void VmInterface::UpdateNextHop(bool old_active) {
    if (active_ == false || old_active == true)
        return;

    struct ether_addr *addrp = ether_aton(vm_mac_.c_str());
    if (addrp == NULL) {
        return;
    }

    InterfaceNH::CreateVport(GetUuid(), *addrp, vrf_->GetName());
}

// Add/Update route. Delete old route if VRF or address changed
void VmInterface::UpdateL3InterfaceRoute(bool old_active, bool force_update,
                                         bool policy_change,
                                         VrfEntry * old_vrf,
                                         const Ip4Address &old_addr) {
    // If interface was already active earlier and there is no force_update or
    // policy_change, return
    if (old_active == true && force_update == false
        && policy_change == false) {
        return;
    }

    // We need to have valid IP and VRF to add route
    if (ip_addr_.to_ulong() != 0 && vrf_.get() != NULL) {
        // Add route if old was inactive or force_update is set
        if (old_active == false || force_update == true) {
            AddRoute(vrf_->GetName(), ip_addr_, 32, policy_enabled_);
        } else if (policy_change == true) {
            // If old-active and there is change in policy, invoke RESYNC of
            // route to account for change in NH policy
            Inet4UnicastAgentRouteTable::RouteResyncReq(vrf_->GetName(),
                                                        ip_addr_, 32);
        }
    }

    // If there is change in VRF or IP address, delete old route
    if (old_vrf != vrf_.get() || ip_addr_ != old_addr) {
        DeleteL3InterfaceRoute(old_active, old_vrf, old_addr);
    }
}

void VmInterface::DeleteL3InterfaceRoute(bool old_active, VrfEntry *old_vrf,
                                         const Ip4Address &old_addr) {
    if ((old_vrf == NULL) || (old_addr.to_ulong() == 0))
        return;

    DeleteRoute(old_vrf->GetName(), old_addr, 32);
}

void VmInterface::UpdateInterfaceNH(bool force_update, bool policy_change) {
    struct ether_addr *mac = ether_aton(vm_mac_.c_str());
    if (mac == NULL) {
        LOG(ERROR, "Invalid mac address " << vm_mac_ << " on port " 
            << cfg_name_);
        return;
    }

    InterfaceNH::CreateVport(GetUuid(), *mac, vrf_->GetName());
}

void VmInterface::DeleteInterfaceNH() {
    InterfaceNH::DeleteVportReq(GetUuid());
}

// Add meta-data route if linklocal_ip is needed
void VmInterface::UpdateMetadataRoute(bool old_active, VrfEntry *old_vrf) {
    if (active_ == false || old_active == true)
        return;

    if (!need_linklocal_ip_) {
        return;
    }

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();
    table->VmPortToMetaDataIp(id(), vrf_->GetVrfId(), &mdata_addr_);
    Inet4UnicastAgentRouteTable::AddLocalVmRoute
        (agent->GetMdataPeer(), agent->GetDefaultVrf(), mdata_addr_,
         32, GetUuid(), vn_->GetName(), label_, true);
}

// Delete meta-data route
void VmInterface::DeleteMetadataRoute(bool old_active, VrfEntry *old_vrf,
                                      bool old_need_linklocal_ip) {
    if (!old_need_linklocal_ip) {
        return;
    }

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();
    Inet4UnicastAgentRouteTable::Delete(agent->GetMdataPeer(),
                                        agent->GetDefaultVrf(),
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
        floating_ip_list_.list_.erase(prev);
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
        service_vlan_list_.list_.erase(prev);
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
        static_route_list_.list_.erase(prev);
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
        sg_list_.list_.erase(prev);
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

void VmInterface::UpdateL2InterfaceRoute(bool old_active, bool force_update) {
    if (active_ == false)
        return;

    if (old_active && force_update == false)
        return;

    struct ether_addr *addrp = ether_aton(vm_mac().c_str());
    const string &vrf_name = vrf_.get()->GetName();

    int label = l2_label_;
    int bmap = TunnelType::ComputeType(TunnelType::MplsType()); 
    if ((l2_label_ == MplsTable::kInvalidLabel) && (vxlan_id_ != 0)) {
        label = vxlan_id_;
        bmap = 1 << TunnelType::VXLAN;
    }

    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    Layer2AgentRouteTable::AddLocalVmRoute(agent->GetLocalVmPeer(), GetUuid(),
                                           vn_->GetName(), vrf_name, label,
                                           bmap, *addrp, ip_addr(), 32);
}

void VmInterface::DeleteL2InterfaceRoute(bool old_active, VrfEntry *old_vrf) {
    if (old_active == false)
        return;

    if ((vxlan_id_ != 0) && 
        (TunnelType::ComputeType(TunnelType::AllType()) == TunnelType::VXLAN)) {
        VxLanId::DeleteReq(vxlan_id_);
        vxlan_id_ = 0;
    }
    struct ether_addr *addrp = ether_aton(vm_mac_.c_str());
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    Layer2AgentRouteTable::Delete(agent->GetLocalVmPeer(), old_vrf->GetName(),
                                  *addrp);
}

// Copy the SG List for VM Interface. Used to add route for interface
void VmInterface::CopySgIdList(SecurityGroupList *sg_id_list) const {
    SecurityGroupEntrySet::const_iterator it;
    for (it = sg_list_.list_.begin(); it != sg_list_.list_.end(); ++it) {
        sg_id_list->push_back(it->sg_->GetSgId());
    }
}

//Add a route for VM port
//If ECMP route, add new composite NH and mpls label for same
void VmInterface::AddRoute(const std::string &vrf_name, const Ip4Address &addr,
                           uint32_t plen, bool policy) {
    ComponentNHData component_nh_data(label_, GetUuid(), 
                                      InterfaceNHFlags::INET4);

    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    VrfEntry *vrf_entry = agent->GetVrfTable()->FindVrfFromName(vrf_name);
    uint32_t nh_count = vrf_entry->GetNHCount(addr);

    if (vrf_entry->FindNH(addr, component_nh_data) == true) {
        //Route already current interface as one of its nexthop
        nh_count = nh_count - 1;
    }

    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);
    if (nh_count == 0) {
        //Default add VM receive route
        Inet4UnicastAgentRouteTable::AddLocalVmRoute
            (agent->GetLocalVmPeer(), vrf_name, addr, plen, GetUuid(),
             vn_->GetName(), label_, sg_id_list);
    } else if (nh_count > 1) {
        //Update composite NH pointed by MPLS label
        CompositeNH::AppendComponentNH(vrf_name, addr, true,
                                       component_nh_data);
        //Update new interface to route pointed composite NH
        CompositeNH::AppendComponentNH(vrf_name, addr, false, 
                                       component_nh_data);
    } else if (nh_count == 1 && 
               vrf_entry->FindNH(addr, component_nh_data) == false) {
        //Interface NH to ECMP NH transition
        //Allocate a new MPLS label
        uint32_t new_label =  agent->GetMplsTable()->AllocLabel();
        //Update label data
        vrf_entry->UpdateLabel(addr, new_label);
        //Create list of component NH
        ComponentNHData::ComponentNHDataList component_nh_list = 
            *(vrf_entry->GetNHList(addr));
        component_nh_list.push_back(component_nh_data);

        //Create local composite NH
        DBRequest nh_req;
        NextHopKey *key = new CompositeNHKey(vrf_name, addr, true);
        nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        nh_req.key.reset(key);
        NextHopData *data = 
            new CompositeNHData(component_nh_list, CompositeNHData::REPLACE);
        nh_req.data.reset(data);
        agent->GetNextHopTable()->Process(nh_req);

        //Make route point to composite NH
        Inet4UnicastAgentRouteTable::AddLocalEcmpRoute
            (agent->GetLocalVmPeer(), vrf_name, addr, plen, component_nh_list,
             new_label, vn_->GetName(), sg_id_list);

        //Make MPLS label point to composite NH
        MplsLabel::CreateEcmpLabel(new_label, vrf_name, addr);
        //Update new interface to route pointed composite NH
        CompositeNH::AppendComponentNH(vrf_name, addr, false,
                                       component_nh_data);
    }

    //Append interface to VRF nh list
    vrf_entry->AddNH(addr, &component_nh_data);
    return;
}

void VmInterface::DeleteRoute(const std::string &vrf_name,
                              const Ip4Address &addr, uint32_t plen) {
    ComponentNHData component_nh_data(label_, GetUuid(), 
                                      InterfaceNHFlags::INET4);
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    VrfEntry *vrf_entry = agent->GetVrfTable()->FindVrfFromName(vrf_name);
    if (vrf_entry->FindNH(addr, component_nh_data) == false) {
        //NH not present in route
        return;
    }

    //ECMP NH to Interface NH
    std::vector<ComponentNHData> comp_nh_list =
        *(vrf_entry->GetNHList(addr));

    if (vrf_entry->GetNHCount(addr) == 1) {
        Inet4UnicastAgentRouteTable::Delete(agent->GetLocalVmPeer(), vrf_name,
                                            addr, plen);
    } else if (vrf_entry->GetNHCount(addr) == 2) {
        uint32_t label = vrf_entry->GetLabel(addr);
        uint32_t index = 0;
        //Get UUID of interface still present in composit NH
        if (comp_nh_list[0] == component_nh_data) {
            //NH key of index 0 element is same, as current interface
            //candidate interface is at index 1
            index = 1;
        }
        const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>
            (agent->GetNextHopTable()->FindActiveEntry
             (comp_nh_list[index].nh_key_));
        const VmInterface *vm_port = static_cast<const VmInterface *>
                                        (intf_nh->GetInterface());
        //Enqueue route change request
        SecurityGroupList sg_id_list;
        CopySgIdList(&sg_id_list);
        Inet4UnicastAgentRouteTable::AddLocalVmRoute
            (agent->GetLocalVmPeer(), vrf_name, addr, plen, vm_port->GetUuid(),
             vm_port->vn()->GetName(), vm_port->label(), sg_id_list);

        //Enqueue MPLS label delete request
        MplsLabel::Delete(label);
    } else if (vrf_entry->GetNHCount(addr) > 2) {
        CompositeNH::DeleteComponentNH(vrf_name, addr, false,
                                       component_nh_data);
        CompositeNH::DeleteComponentNH(vrf_name, addr, true,
                                       component_nh_data);
    }
    vrf_entry->DeleteNH(addr, &component_nh_data);
    return;
}

void VmInterface::UpdateServices(bool val) {
    dhcp_enabled_ = val;
    dns_enabled_ = val;
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

    interface->AddRoute(vrf_.get()->GetName(), floating_ip_, 32, true);
    Agent *agent = static_cast<InterfaceTable *>
        (interface->get_table())->agent();
    DnsProto *dns = agent->GetDnsProto();
    if (dns) {
        dns->UpdateDnsEntry(interface, vn_.get(), floating_ip_, false);
    }

    installed_ = true;
}

void VmInterface::FloatingIp::DeActivate(VmInterface *interface) const {
    if (installed_ == false)
        return;

    interface->DeleteRoute(vrf_.get()->GetName(), floating_ip_, 32);
    Agent *agent = static_cast<InterfaceTable *>
        (interface->get_table())->agent();
    DnsProto *dns = agent->GetDnsProto();
    if (dns) {
        dns->UpdateDnsEntry(interface, vn_.get(), floating_ip_, true);
    }
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
        Inet4UnicastAgentRouteTable::RouteResyncReq(vrf_, addr_, plen_);
    } else if (installed_ == false || force_update) {
        interface->AddRoute(vrf_, addr_, plen_, interface->policy_enabled());
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
        (agent->GetSgTable()->FindActiveEntry(&sg_key));
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
    ListEntry(), tag_(0), vrf_name_(""), addr_(0), smac_(), dmac_(),
    vrf_(NULL), label_(MplsTable::kInvalidLabel) {
}

VmInterface::ServiceVlan::ServiceVlan(const ServiceVlan &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_), tag_(rhs.tag_),
    vrf_name_(rhs.vrf_name_), addr_(rhs.addr_), smac_(rhs.smac_),
    dmac_(rhs.dmac_), vrf_(rhs.vrf_), label_(rhs.label_) {
}

VmInterface::ServiceVlan::ServiceVlan(uint16_t tag, const std::string &vrf_name,
                                      const Ip4Address &addr,
                                      const struct ether_addr &smac,
                                      const struct ether_addr &dmac) :
    ListEntry(), tag_(tag), vrf_name_(vrf_name), addr_(addr), smac_(smac),
    dmac_(dmac), vrf_(NULL), label_(MplsTable::kInvalidLabel) {
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
        label_ = table->agent()->GetMplsTable()->AllocLabel();
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
    if (del_pending_ && label_ != MplsTable::kInvalidLabel) {
        VrfAssignTable::DeleteVlanReq(interface->GetUuid(), tag_);
        interface->ServiceVlanRouteDel(*this);
        MplsLabel::Delete(label_);
        label_ = MplsTable::kInvalidLabel;
        VlanNH::Delete(interface->GetUuid(), tag_);
        return;
    }

    if (installed_ == false)
        return;

    interface->ServiceVlanRouteDel(*this);
    installed_ = false;
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
    return label_;
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

    ComponentNHData component_nh_data(entry.label_, GetUuid(), entry.tag_,
                                      false);
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    VrfEntry *vrf_entry = 
        agent->GetVrfTable()->FindVrfFromName(entry.vrf_->GetName());
    if (vrf_entry->FindNH(entry.addr_, component_nh_data) == true) {
        //Route already current interface as one of its nexthop
        return;
    }

    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);

    if (vrf_entry->GetNHCount(entry.addr_) == 0) {
        Inet4UnicastAgentRouteTable::AddVlanNHRoute
            (agent->GetLocalVmPeer(), entry.vrf_->GetName(), entry.addr_, 32,
             GetUuid(), entry.tag_, entry.label_, vn()->GetName(), sg_id_list);
    } else if (vrf_entry->GetNHCount(entry.addr_) > 1) {
        //Update both local composite NH and BGP injected composite NH
        CompositeNH::AppendComponentNH(entry.vrf_->GetName(), entry.addr_,
                                       true, component_nh_data);
        CompositeNH::AppendComponentNH(entry.vrf_->GetName(), entry.addr_,
                                       false, component_nh_data);
    } else if (vrf_entry->GetNHCount(entry.addr_) == 1) {
        //Interface NH to ECMP NH transition
        //Allocate a new MPLS label
        uint32_t new_label =  agent->GetMplsTable()->AllocLabel();
        //Update label data
        vrf_entry->UpdateLabel(entry.addr_, new_label);
        //Create list of component NH
        ComponentNHData::ComponentNHDataList component_nh_list = 
            *(vrf_entry->GetNHList(entry.addr_));
        component_nh_list.push_back(component_nh_data);

        //Create local composite NH
        DBRequest nh_req;
        NextHopKey *key = new CompositeNHKey(entry.vrf_->GetName(),
                                             entry.addr_, true);
        nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        nh_req.key.reset(key);
        NextHopData *data = 
            new CompositeNHData(component_nh_list, CompositeNHData::REPLACE);
        nh_req.data.reset(data);
        agent->GetNextHopTable()->Process(nh_req);

        //Make route point to composite NH
        Inet4UnicastAgentRouteTable::AddLocalEcmpRoute
            (agent->GetLocalVmPeer(), entry.vrf_->GetName(), entry.addr_, 32,
             component_nh_list, new_label, vn()->GetName(), sg_id_list);
        //Make MPLS label point to composite NH
        MplsLabel::CreateEcmpLabel(new_label, entry.vrf_->GetName(), 
                                   entry.addr_);
        //Update new interface to route pointed composite NH
        CompositeNH::AppendComponentNH(entry.vrf_->GetName(), entry.addr_,
                                       false, component_nh_data);
    }

    //Append interface to VRF nh list
    vrf_entry->AddNH(entry.addr_, &component_nh_data);
    entry.installed_ = true;
    return;
}

void VmInterface::ServiceVlanRouteDel(const ServiceVlan &entry) {
    if (entry.installed_ == false) {
        return;
    }
    
    ComponentNHData component_nh_data(entry.label_, GetUuid(), entry.tag_,
                                      false);
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    VrfEntry *vrf_entry = 
        agent->GetVrfTable()->FindVrfFromName(entry.vrf_->GetName());
    if (vrf_entry->FindNH(entry.addr_, component_nh_data) == false) {
        //NH not present in route
        return;
    }

    //ECMP NH to Interface NH
    std::vector<ComponentNHData> comp_nh_list =
        *(vrf_entry->GetNHList(entry.addr_));

    if (vrf_entry->GetNHCount(entry.addr_) == 1) {
        Inet4UnicastAgentRouteTable::Delete
            (agent->GetLocalVmPeer(), entry.vrf_->GetName(), entry.addr_, 32);
    } else if (vrf_entry->GetNHCount(entry.addr_) == 2) {
        uint32_t label = vrf_entry->GetLabel(entry.addr_);
        uint32_t index = 0;
        //Get UUID of interface still present in composit NH
        if (comp_nh_list[0] == component_nh_data) {
            //NH key of index 0 element is same, as current interface
            //candidate interface is at index 1
            index = 1;
        }
        const VlanNH *vlan_nh = static_cast<const VlanNH *>
            (agent->GetNextHopTable()->FindActiveEntry
             (comp_nh_list[index].nh_key_));
        const VmInterface *vm_port = static_cast<const VmInterface *>
                                        (vlan_nh->GetInterface());
        //Enqueue route change request
        SecurityGroupList sg_id_list;
        CopySgIdList(&sg_id_list);
        Inet4UnicastAgentRouteTable::AddVlanNHRoute
            (agent->GetLocalVmPeer(), entry.vrf_->GetName(), entry.addr_, 32,
             vm_port->GetUuid(), vlan_nh->GetVlanTag(),
             comp_nh_list[index].label_, vm_port->vn()->GetName(), sg_id_list);

        //Delete MPLS label
        MplsLabel::Delete(label);
    } else if (vrf_entry->GetNHCount(entry.addr_) > 2) {
        //Delete interface from both local composite NH and BGP
        //injected composite NH
        CompositeNH::DeleteComponentNH(entry.vrf_->GetName(), entry.addr_,
                                       false, component_nh_data);
        CompositeNH::DeleteComponentNH(entry.vrf_->GetName(), entry.addr_,
                                       true, component_nh_data);
    }
    entry.installed_ = false;
    vrf_entry->DeleteNH(entry.addr_, &component_nh_data);
    return;
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
    case ACTIVATED:
        intf_info.set_op("Activated");
        break;
    case DEACTIVATED:
        intf_info.set_op("Deactivated");
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
    OPER_TRACE(Interface, intf_info);
}

/////////////////////////////////////////////////////////////////////////////
// VM Interface DB Table utility functions
/////////////////////////////////////////////////////////////////////////////
// Add a VM-Interface
void VmInterface::Add(InterfaceTable *table, const uuid &intf_uuid,
                      const string &os_name, const Ip4Address &addr,
                      const string &mac, const string &vm_name,
                      uint16_t vlan_id, const std::string &parent) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid,
                                     os_name));
    req.data.reset(new VmInterfaceAddData(addr, mac, vm_name, vlan_id, parent));
    table->Enqueue(&req);
}

// Delete a VM-Interface
void VmInterface::Delete(InterfaceTable *table, const uuid &intf_uuid) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""));
    req.data.reset(NULL);
    table->Enqueue(&req);
}
