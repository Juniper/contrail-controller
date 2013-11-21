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
#include <oper/agent_route.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/interface.h>
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

bool Interface::test_mode_;
InterfaceTable *InterfaceTable::interface_table_;

/////////////////////////////////////////////////////////////////////////////
// Interface Table routines
/////////////////////////////////////////////////////////////////////////////
std::auto_ptr<DBEntry> InterfaceTable::AllocEntry(const DBRequestKey *k) const {
    const InterfaceKey *key = static_cast<const InterfaceKey *>(k);
    
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(key->AllocEntry()));
}

static void GetOsParams(const string &name, bool test_mode, 
                        struct ether_addr &mac, size_t &ifindex) {
    if (test_mode) {
        static int dummy_ifindex = 0;
        ifindex = ++dummy_ifindex;
        bzero(&mac, sizeof(mac));
        return;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name.c_str(), IF_NAMESIZE);
    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(fd >= 0);
    if (ioctl(fd, SIOCGIFHWADDR, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
            "> quering mac-address for interface <" << name << ">");
        ifindex = Interface::kInvalidIndex;
        bzero(&mac, sizeof(mac));
        return;
    }
    close(fd);

    memcpy(mac.ether_addr_octet, ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN);
    ifindex = if_nametoindex(name.c_str());
}

DBEntry *InterfaceTable::Add(const DBRequest *req) {
    InterfaceKey *key = static_cast<InterfaceKey *>(req->key.get());
    InterfaceData *data = static_cast<InterfaceData *>(req->data.get());

    LOG(DEBUG, "Intf add for " << key->name_);

    Interface *intf = key->AllocEntry(data);
    if (intf) {
        intf->id_ = index_table_.Insert(intf);

        // Get the os-ifindex and mac of interface
        GetOsParams(intf->name_, intf->test_mode_, intf->mac_, intf->os_index_);

        intf->SendTrace(Interface::ADD);
    }

    return intf;
}

// Default change handler. No change expected
bool InterfaceTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    InterfaceKey *key = static_cast<InterfaceKey *>(req->key.get());
    InterfaceData *data = static_cast<InterfaceData *>(req->data.get());
    switch (key->type_) {
        case Interface::VMPORT: {
            VmPortInterfaceData *vm_data = static_cast<VmPortInterfaceData *>(data);
            VmPortInterface *vmport_intf = static_cast<VmPortInterface *>(entry);
            MirrorEntry *mirror_entry = FindMirrorRef(vm_data->analyzer_name_);
            if (vmport_intf->GetMirrorEntry() != mirror_entry) {
                vmport_intf->SetMirrorEntry(mirror_entry);
                return true;
            }
            LOG(DEBUG, "Analyzer name: " << vm_data->analyzer_name_);
            LOG(DEBUG, "Direction: " << vm_data->mirror_direction_);
            if (vmport_intf->GetMirrorDirection() != vm_data->mirror_direction_) {
                vmport_intf->SetMirrorDirection(vm_data->mirror_direction_);
                return true;
            }

            break;
        }
	case Interface::VHOST: {
    	VirtualHostInterface *intf = static_cast<VirtualHostInterface *>(entry);
    	if (intf) {
       		// Get the os-ifindex and mac of interface
       		GetOsParams(intf->name_, intf->test_mode_, intf->mac_, intf->os_index_);
			ret = true;
		}
		break;
	}
        default:
            break;
    }
    return ret;
}

// RESYNC supported only for VMPORT
bool InterfaceTable::Resync(DBEntry *entry, DBRequest *req) {
    InterfaceKey *key = static_cast<InterfaceKey *>(req->key.get());
    assert(key->type_ == Interface::VMPORT);
    VmPortInterfaceData *data = static_cast<VmPortInterfaceData *>
                                (req->data.get());
    VmPortInterface *intf = static_cast<VmPortInterface *>(entry);
    if (data && data->ip_addr_update_only_)
        return intf->OnIpAddrResync(req);
    else
        return intf->OnResync(req);
}

void InterfaceTable::Delete(DBEntry *entry, const DBRequest *req) {
    InterfaceKey *key = static_cast<InterfaceKey *>(req->key.get());
    Interface *intf = static_cast<Interface *>(entry);
    if (key->type_ == Interface::VMPORT) {
        VmPortInterface *vmport_intf = static_cast<VmPortInterface *>(entry);
        if (vmport_intf->GetActiveState()) {
            vmport_intf->DeActivate(vmport_intf->GetVrf()->GetName(),
                                    vmport_intf->GetIpAddr());
        }

        // Cleanup Service VLAN config if any
        VmPortInterfaceData data;
        vmport_intf->OnResyncServiceVlan(&data);
    }
    intf->SendTrace(Interface::DELETE);
}

VrfEntry *InterfaceTable::FindVrfRef(const string &name) const {
    VrfKey key(name);
    return static_cast<VrfEntry *>(Agent::GetInstance()->GetVrfTable()->FindActiveEntry(&key));
}

VmEntry *InterfaceTable::FindVmRef(const uuid &uuid) const {
    VmKey key(uuid);
    return static_cast<VmEntry *>(Agent::GetInstance()->GetVmTable()->FindActiveEntry(&key));
}

VnEntry *InterfaceTable::FindVnRef(const uuid &uuid) const {
    VnKey key(uuid);
    return static_cast<VnEntry *>(Agent::GetInstance()->GetVnTable()->FindActiveEntry(&key));
}

MirrorEntry *InterfaceTable::FindMirrorRef(const string &name) const {
    MirrorEntryKey key(name);
    return static_cast<MirrorEntry *>(Agent::GetInstance()->GetMirrorTable()->FindActiveEntry(&key));
}

DBTableBase *InterfaceTable::CreateTable(DB *db, const std::string &name) {
    interface_table_ = new InterfaceTable(db, name);
    interface_table_->Init();
    return interface_table_;
};

// Build one Floating IP entry for a virtual-machine-interface
void BuildFloatingIpList(VmPortInterfaceData *data, IFMapNode *node) {
    CfgListener *cfg_listener = Agent::GetInstance()->cfg_listener();
    if (cfg_listener->CanUseNode(node) == false) {
        return;
    }

    LOG(DEBUG, "Building Floating IP for VN-Port " << node->name());
    // Find VRF for the floating-ip. Following path in graphs leads to VRF
    // virtual-machine-port <-> floating-ip <-> floating-ip-pool 
    // <-> virtual-network <-> routing-instance
    IFMapAgentTable *fip_table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *fip_graph = fip_table->GetGraph();

    // Iterate thru links for floating-ip looking for floating-ip-pool node
    for (DBGraphVertex::adjacency_iterator fip_iter = node->begin(fip_graph);
         fip_iter != node->end(fip_graph); ++fip_iter) {
        if (fip_iter->IsDeleted()) {
            continue;
        }

        IFMapNode *pool_node = static_cast<IFMapNode *>(fip_iter.operator->());
        if (cfg_listener->CanUseNode
            (pool_node, Agent::GetInstance()->cfg()->cfg_floatingip_pool_table())
            == false) {
            continue;
        }

        LOG(DEBUG, "Building Floating IP for Pool " << pool_node->name());
        // Find Virtual-Network node
        IFMapAgentTable *pool_table = 
            static_cast<IFMapAgentTable *> (pool_node->table());
        DBGraph *pool_graph = pool_table->GetGraph();
        // Iterate thru links for floating-ip-pool looking for virtual-network
        for (DBGraphVertex::adjacency_iterator pool_iter = 
             pool_node->begin(pool_graph);
             pool_iter != pool_node->end(pool_graph); ++pool_iter) {
            if (pool_iter->IsDeleted()) {
                continue;
            }

            IFMapNode *vn_node = 
                static_cast<IFMapNode *>(pool_iter.operator->());
            if (cfg_listener->CanUseNode(vn_node, Agent::GetInstance()->cfg()->cfg_vn_table()) 
                == false) {
                continue;
            }

            LOG(DEBUG, "Building Floating IP for VN " << vn_node->name());
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
                if (vn_iter->IsDeleted()) {
                    continue;
                }

                IFMapNode *vrf_node = 
                    static_cast<IFMapNode *>(vn_iter.operator->());
                if (cfg_listener->CanUseNode
                    (vrf_node, Agent::GetInstance()->cfg()->cfg_vrf_table())
                    == false){
                    continue;
                }

                FloatingIp *ip = static_cast<FloatingIp *>(node->GetObject());
                assert(ip != NULL);
                LOG(DEBUG, "Add FloatingIP <" << ip->address() << ":" <<
                    vrf_node->name() << "> to interface " << node->name());

                boost::system::error_code ec;
                Ip4Address addr = Ip4Address::from_string(ip->address(), ec);
                if (ec.value() != 0) {
                    LOG(DEBUG, "Error decoding IP address " << ip->address());
                } else {
                    data->floating_iplist_.insert
                        (CfgFloatingIp(addr, vrf_node->name(), vn_uuid));
                }
                break;
            }
            break;
        }
        break;
    }
    return;
}

void BuildStaticRoute(VmPortInterfaceData *data, IFMapNode *node) {
    InterfaceRouteTable *entry = 
        static_cast<InterfaceRouteTable*>(node->GetObject());
    assert(entry);

    std::vector<RouteType>::const_iterator it = entry->routes().begin();
    for (std::vector<RouteType>::const_iterator it = entry->routes().begin();
         it != entry->routes().end(); it++) {
        int plen;
        Ip4Address ip(0);
        boost::system::error_code ec;
        ec = Ip4PrefixParse(it->prefix, &ip, &plen);
        if (ec.value() == 0) {
            CfgStaticRoute static_route(data->vrf_name_, ip, plen);
            data->static_route_list_.insert(static_route);
        }
    }
}

// Build one Service Vlan entry for virtual-machine-interface
void BuildVmPortVrfInfo(VmPortInterfaceData *data, IFMapNode *node) {

    LOG(DEBUG, "Building VRF / Service-chain info from " << node->name());

    VirtualMachineInterfaceRoutingInstance *entry = 
        static_cast<VirtualMachineInterfaceRoutingInstance*>(node->GetObject());
    assert(entry);

    // Validate the virtural-machine-interface-routing-instance attibutes
    const PolicyBasedForwardingRuleType &rule = entry->data();

    // Ignore node if direction is not yet set. An update will come later
    if (rule.direction == "") {
        return;
    }

    // Find VRF by looking for  link
    // virtual-machine-interface-routing-instance <-> routing-instance 
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();

    // Iterate thru links looking for routing-instance node
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        if (iter->IsDeleted()) {
            continue;
        }

        IFMapNode *vrf_node = static_cast<IFMapNode *>(iter.operator->());
    
        if (Agent::GetInstance()->cfg_listener()->CanUseNode
            (vrf_node, Agent::GetInstance()->cfg()->cfg_vrf_table()) == false) {
            continue;
        }

        if (rule.vlan_tag == 0 && rule.protocol == "" 
            && rule.service_chain_address == "") {
            LOG(DEBUG, "VRF set to <" << vrf_node->name() << ">");
            data->vrf_name_ = vrf_node->name();
        } else {
            boost::system::error_code ec;
            Ip4Address addr = Ip4Address::from_string
                (rule.service_chain_address, ec);
            if (ec.value() != 0) {
                LOG(DEBUG, "Error decoding IP address " 
                    << rule.service_chain_address);
                break;
            }

            if (rule.vlan_tag > 4093) {
                LOG(DEBUG, "Invalid VLAN Tag " << rule.vlan_tag);
                break;
            }

            LOG(DEBUG, "Add Service VLAN entry <" << rule.vlan_tag << " : "
                << rule.service_chain_address << " : " << vrf_node->name());

            ether_addr smac = *ether_aton("00:01:00:5E:00:00");
            ether_addr dmac = *ether_aton("FF:FF:FF:FF:FF:FF");
            if (rule.src_mac != Agent::GetInstance()->NullString()) {
                smac = *ether_aton(rule.src_mac.c_str());
            }
            if (rule.src_mac != Agent::GetInstance()->NullString()) {
                dmac = *ether_aton(rule.dst_mac.c_str());
            }
            data->service_vlan_list_[rule.vlan_tag] = 
                CfgServiceVlan(rule.vlan_tag, addr, vrf_node->name(),
                               smac, dmac);
        }
        break;
    }

    return;
}

static void ReadInstanceIp(VmPortInterfaceData *data, IFMapNode *node) {
    InstanceIp *ip = static_cast<InstanceIp *>(node->GetObject());
    boost::system::error_code err;
    LOG(DEBUG, "InstanceIp config for " << data->cfg_name_ << " "
        << ip->address());
    data->addr_ = Ip4Address::from_string(ip->address(), err);
}


static void ReadAnalyzerNameAndCreate(VirtualMachineInterface *cfg, VmPortInterfaceData &data) {
    if (!cfg) {
        return;
    }
    MirrorActionType mirror_to = cfg->properties().interface_mirror.mirror_to;
    std::string traffic_direction = cfg->properties().interface_mirror.traffic_direction;
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
        Agent::GetInstance()->GetMirrorTable()->AddMirrorEntry(mirror_to.analyzer_name,
                                                               std::string(),
                                                               Agent::GetInstance()->GetRouterId(),
                                                               Agent::GetInstance()->GetMirrorPort(),
                                                               dip.to_v4(),
                                                               dport);
        data.analyzer_name_ =  mirror_to.analyzer_name;
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

    CfgIntTable *cfg_table = Agent::GetInstance()->GetIntfCfgTable();
    CfgIntKey cfg_key(u);
    CfgIntEntry *nova_entry = static_cast <CfgIntEntry *>
        (cfg_table->Find(&cfg_key));
    // If Nova has not yet added interface into Nova Config tree, return.
    // This API is invoked again when the Nova adds interface to Nova config 
    // tree.
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
        InterfaceKey *key = new VmPortInterfaceKey(AgentKey::RESYNC, u, "");
        req.key.reset(key);
        req.data.reset(NULL);
        return true;
    }

    // Update interface configuration
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    InterfaceKey *key = new VmPortInterfaceKey(AgentKey::RESYNC, u, "");

    VmPortInterfaceData *data;
    data = new VmPortInterfaceData();
    data->VmPortInit();
    ReadAnalyzerNameAndCreate(cfg, *data);

    //Fill config data items
    data->cfg_name_= node->name();

    SgUuidList sg_list(0);
    // Walk Interface Graph to get VM, VN and FloatingIPList
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph()); 
         iter != node->end(table->GetGraph()); ++iter) {
        if (iter->IsDeleted()) {
            continue;
        }
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->CanUseNode(adj_node)
            == false) {
            continue;
        }

        if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_sg_table()) {
            SecurityGroup *sg_cfg = static_cast<SecurityGroup *>
                    (adj_node->GetObject());
            assert(sg_cfg);
            autogen::IdPermsType id_perms = sg_cfg->id_perms();
            uuid sg_uuid = nil_uuid();
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                       sg_uuid);
            sg_list.push_back(sg_uuid);
        }

        if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_vn_table()) {
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

        if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_vm_table()) {
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

        if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_instanceip_table()) {
            ReadInstanceIp(data, adj_node);
        }

        if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_floatingip_table()) {
            BuildFloatingIpList(data, adj_node);
        }

        if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_vm_port_vrf_table()) {
            BuildVmPortVrfInfo(data, adj_node);
        }

        if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_route_table()) {
            BuildStaticRoute(data, adj_node);
        }
    }

    data->SetFabricPort(false);
    data->SetNeedLinkLocalIp(true);
    data->SetSgUuidList(sg_list);
    if (data->vrf_name_ == Agent::GetInstance()->GetDefaultVrf() ||
        data->vrf_name_ == Agent::GetInstance()->GetLinkLocalVrfName()) {
        data->SetFabricPort(true);
        data->SetNeedLinkLocalIp(false);
    } 

    if (Agent::GetInstance()->isXenMode()) {
        data->SetNeedLinkLocalIp(false);
    }

    req.key.reset(key);
    req.data.reset(data);
    return true;
}

// Handle virtual-machine-interface-routing-instance config node
// Find the interface-node and enqueue RESYNC of service-vlans to interface
void InterfaceTable::VmInterfaceVrfSync(IFMapNode *node) {
    if (Agent::GetInstance()->cfg_listener()->CanUseNode(node) == false){
        return;
    }
    // Walk the node to get neighbouring interface 
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph()); 
         iter != node->end(table->GetGraph()); ++iter) {
        if (iter->IsDeleted()) {
            continue;
        }

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->CanUseNode(adj_node) 
            == false) {
            continue;
        }

        if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_vm_interface_table()) {
            DBRequest req;
            InterfaceTable *interface_table = static_cast<InterfaceTable *>
                (Agent::GetInstance()->GetInterfaceTable());

            if (interface_table->IFNodeToReq(adj_node, req) == true) {
                LOG(DEBUG, "Service VLAN SYNC for Port " << adj_node->name());
                Agent::GetInstance()->GetInterfaceTable()->Enqueue(&req);
            }
        }
    }
}

Interface *InterfaceTable::FindInterfaceFromMetadataIp(const Ip4Address &ip) {
    uint32_t addr = ip.to_ulong();
    if ((addr & 0xFFFF0000) != (METADATA_IP_ADDR & 0xFFFF0000))
        return NULL;
    return index_table_.At(addr & 0xFF);
}

bool InterfaceTable::FindVmUuidFromMetadataIp(const Ip4Address &ip,
                                              std::string *vm_ip,
                                              std::string *vm_uuid) {
    Interface *itf = FindInterfaceFromMetadataIp(ip);
    if (itf && itf->GetType() == Interface::VMPORT) {
        const VmPortInterface *vitf = static_cast<const VmPortInterface *>(itf);
        *vm_ip = vitf->GetIpAddr().to_string();
        if (vitf->GetVmEntry()) {
            *vm_uuid = UuidToString(vitf->GetVmEntry()->GetUuid());
            return true;
        }
    }
    return false;
}

void InterfaceTable::VmPortToMetaDataIp(uint16_t ifindex, uint32_t vrfid,
                                        Ip4Address *addr) {
    uint32_t ip = METADATA_IP_ADDR & 0xFFFF0000;
    ip += ((vrfid & 0xFF) << 8) + (ifindex & 0xFF);
    *addr = Ip4Address(ip);
}

/////////////////////////////////////////////////////////////////////////////
// Interface Base Entry routines
/////////////////////////////////////////////////////////////////////////////
Interface::Interface(Type type, const uuid &uuid, const string &name,
                     VrfEntry *vrf) :
    type_(type), uuid_(uuid), name_(name),
    vrf_(vrf), label_(MplsTable::kInvalidLabel), 
    l2_label_(MplsTable::kInvalidLabel), active_(true),
    id_(kInvalidIndex), dhcp_service_enabled_(true),
    dns_service_enabled_(true), mac_(), os_index_(kInvalidIndex) { 
}

Interface::~Interface() { 
    if (id_ != kInvalidIndex) {
        InterfaceTable::GetInstance()->FreeInterfaceId(id_);
    }
}

void Interface::SetKey(const DBRequestKey *key) { 
    const InterfaceKey *k = static_cast<const InterfaceKey *>(key);
    type_ = k->type_;
    uuid_ = k->uuid_;
}

DBEntryBase::KeyPtr VmPortInterface::GetDBRequestKey() const {
    InterfaceKey *key = new VmPortInterfaceKey(uuid_, name_);
    return DBEntryBase::KeyPtr(key);
}

DBEntryBase::KeyPtr EthInterface::GetDBRequestKey() const {
    InterfaceKey *key = new EthInterfaceKey(uuid_, name_);
    return DBEntryBase::KeyPtr(key);
}

DBEntryBase::KeyPtr HostInterface::GetDBRequestKey() const {
    InterfaceKey *key = new HostInterfaceKey(uuid_, name_);
    return DBEntryBase::KeyPtr(key);
}

DBEntryBase::KeyPtr VirtualHostInterface::GetDBRequestKey() const {
    InterfaceKey *key = new VirtualHostInterfaceKey(uuid_, name_);
    return DBEntryBase::KeyPtr(key);
}

uint32_t Interface::GetVrfId() const {
    if (vrf_ == NULL) {
        return VrfEntry::kInvalidIndex;
    }

    return vrf_->GetVrfId();
}

AgentDBTable *Interface::DBToTable() const {
    return InterfaceTable::GetInstance();
}

/////////////////////////////////////////////////////////////////////////////
// Host Interface routines
/////////////////////////////////////////////////////////////////////////////

// Enqueue DBRequest to create a Host Interface
void HostInterface::CreateReq(InterfaceTable *table, const string &ifname) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    InterfaceKey *key = new HostInterfaceKey(nil_uuid(), ifname);
    req.key.reset(key);

    HostInterfaceData *data = new HostInterfaceData();
    data->HostInit();
    req.data.reset(data);
    table->Enqueue(&req);
}

// Enqueue DBRequest to delete a Host Interface
void HostInterface::DeleteReq(InterfaceTable *table,
                              const string &ifname) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    InterfaceKey *key = new HostInterfaceKey(nil_uuid(), ifname);
    req.key.reset(key);
    req.data.reset(NULL);
    table->Enqueue(&req);
}

/////////////////////////////////////////////////////////////////////////////
// Virtual Host Interface routines
/////////////////////////////////////////////////////////////////////////////
// Enqueue DBRequest to create a Host Interface
void VirtualHostInterface::CreateReq(InterfaceTable *table,
                                     const string &ifname,
                                     const string &vrf_name, SubType sub_type) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    InterfaceKey *key = new VirtualHostInterfaceKey(nil_uuid(), ifname);
    req.key.reset(key);

    VirtualHostInterfaceData *data = new VirtualHostInterfaceData(sub_type);
    data->VirtualHostInit(vrf_name);
    req.data.reset(data);
    table->Enqueue(&req);
}

// Enqueue DBRequest to delete a Host Interface
void VirtualHostInterface::DeleteReq(InterfaceTable *table, 
                                     const string &ifname) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    InterfaceKey *key = new VirtualHostInterfaceKey(nil_uuid(), ifname);
    req.key.reset(key);
    req.data.reset(NULL);
    table->Enqueue(&req);
}

/////////////////////////////////////////////////////////////////////////////
// Ethernet Interface routines
/////////////////////////////////////////////////////////////////////////////
// Enqueue DBRequest to create a Host Interface
void EthInterface::CreateReq(InterfaceTable *table, const string &ifname,
                             const string &vrf_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    InterfaceKey *key = new EthInterfaceKey(nil_uuid(), ifname);
    req.key.reset(key);

    HostInterfaceData *data = new HostInterfaceData();
    data->EthInit(vrf_name);
    req.data.reset(data);
    table->Enqueue(&req);
}

// Enqueue DBRequest to delete a Host Interface
void EthInterface::DeleteReq(InterfaceTable *table, const string &ifname) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    InterfaceKey *key = new EthInterfaceKey(nil_uuid(), ifname);
    req.key.reset(key);
    req.data.reset(NULL);
    table->Enqueue(&req);
}

/////////////////////////////////////////////////////////////////////////////
// VM Port Entry routines
/////////////////////////////////////////////////////////////////////////////
string VmPortInterface::ToString() const {
    return "VM-PORT";
}

const string &VmPortInterface::GetCfgName() const {
    return cfg_name_;
}

void VmPortInterface::SgIdList(SecurityGroupList &sg_id_list) const {
    SgList::const_iterator sg_it;
    for (sg_it = GetSecurityGroupList().begin();
         sg_it != GetSecurityGroupList().end();
         ++sg_it) {
        sg_id_list.push_back((*sg_it)->GetSgId());
    }
}

void VmPortInterface::AllocMPLSLabels() {
    if (fabric_port_ == false) {
        if (label_ == MplsTable::kInvalidLabel) {
            label_ = Agent::GetInstance()->GetMplsTable()->AllocLabel();
        }
        MplsLabel::CreateVPortLabelReq(label_, GetUuid(), policy_enabled_,
                                       InterfaceNHFlags::INET4);
    }
}

void VmPortInterface::AllocL2Labels(int old_vxlan_id) {
    uint32_t bmap = TunnelType::ComputeType(TunnelType::AllType());
    if (fabric_port_ == false) {
        //Either VXLAN or MPLS
        if ((old_vxlan_id != vxlan_id_) &&
            (bmap == TunnelType::VXLAN)) {
            VxLanId::DeleteReq(old_vxlan_id);
            vxlan_id_ = 0;
        }
        if ((vxlan_id_ == 0) || 
            (bmap != TunnelType::VXLAN)) {
            if (l2_label_ == MplsTable::kInvalidLabel) {
                l2_label_ = Agent::GetInstance()->GetMplsTable()->AllocLabel();
            }
            MplsLabel::CreateVPortLabelReq(l2_label_, GetUuid(), false,
                                           InterfaceNHFlags::LAYER2);
        } else {
            if (l2_label_ != MplsTable::kInvalidLabel) {
                MplsLabel::DeleteReq(l2_label_);
                l2_label_ = MplsTable::kInvalidLabel;
            }
            VxLanId::CreateReq(vxlan_id_, vrf_->GetName());
        }
    }
}

void VmPortInterface::AddL2Route(const std::string vrf_name,
                                 struct ether_addr mac,
                                 const Ip4Address &ip,
                                 bool policy) {
    int label = l2_label_;
    int bmap = TunnelType::ComputeType(TunnelType::MplsType()); 
    if ((l2_label_ == MplsTable::kInvalidLabel) && (vxlan_id_ != 0)) {
        label = vxlan_id_;
        bmap = 1 << TunnelType::VXLAN;
    }
    Layer2AgentRouteTable::AddLocalVmRoute(
                                        Agent::GetInstance()->GetLocalVmPeer(),
                                        GetUuid(), vn_->GetName(),
                                        vrf_name, label, bmap,
                                        mac, ip, 32);
}

void VmPortInterface::DeleteL2Route(const std::string vrf_name,
                                    struct ether_addr mac) {
    Layer2AgentRouteTable::DeleteReq(Agent::GetInstance()->GetLocalVmPeer(), 
                                     vrf_name, mac);
}

//Add a route for VM port
//If ECMP route, add new composite NH and mpls label for same
void VmPortInterface::AddRoute(const std::string vrf_name, Ip4Address addr, 
                               uint32_t plen, bool policy) {
    ComponentNHData component_nh_data(label_, GetUuid(), 
                                      InterfaceNHFlags::INET4);
    VrfEntry *vrf_entry = 
        Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
    uint32_t nh_count = vrf_entry->GetNHCount(addr);

    if (vrf_entry->FindNH(addr, component_nh_data) == true) {
        //Route already current interface as one of its nexthop
        nh_count = nh_count - 1;
    }

    if (nh_count == 0) {
        //Default add VM receive route
        SecurityGroupList sg_id_list;
        SgIdList(sg_id_list);
        Inet4UnicastAgentRouteTable::AddLocalVmRoute(Agent::GetInstance()->GetLocalVmPeer(), 
                                                     vrf_name, 
                                                     addr, plen, GetUuid(), vn_->GetName(),
                                                     label_, sg_id_list);
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
        uint32_t new_label =  Agent::GetInstance()->GetMplsTable()->AllocLabel();
        //Update label data
        vrf_entry->UpdateLabel(addr, new_label);
        //Create list of component NH
        ComponentNHData::ComponentNHDataList component_nh_list = 
            *(vrf_entry->GetNHList(addr));
        component_nh_list.push_back(component_nh_data);

        //Make route point to composite NH
        Inet4UnicastAgentRouteTable::AddRemoteVmRoute(Agent::GetInstance()->GetLocalVmPeer(),
                                                      vrf_name, addr, plen,
                                                      component_nh_list, 
                                                      new_label,
                                                      vn_->GetName(), true);
        //Make MPLS label point to composite NH
        MplsLabel::CreateEcmpLabelReq(new_label, vrf_name, addr);
        //Update new interface to route pointed composite NH
        CompositeNH::AppendComponentNH(vrf_name, addr, false,
                                       component_nh_data);
    }

    //Append interface to VRF nh list
    vrf_entry->AddNH(addr, &component_nh_data);
    return;
}

void VmPortInterface::DeleteRoute(const std::string vrf_name, Ip4Address addr, 
                                  uint32_t plen) {
    ComponentNHData component_nh_data(label_, GetUuid(), 
                                      InterfaceNHFlags::INET4);
    VrfEntry *vrf_entry = Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
    if (vrf_entry->FindNH(addr, component_nh_data) == false) {
        //NH not present in route
        return;
    }

    //ECMP NH to Interface NH
    std::vector<ComponentNHData> comp_nh_list =
        *(vrf_entry->GetNHList(addr));

    if (vrf_entry->GetNHCount(addr) == 1) {
        Inet4UnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->GetLocalVmPeer(),
                                     vrf_name, addr, plen);
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
            (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(comp_nh_list[index].nh_key_));
        const VmPortInterface *vm_port = static_cast<const VmPortInterface *>
                                        (intf_nh->GetInterface());
        //Enqueue route change request
        SecurityGroupList sg_id_list;
        SgIdList(sg_id_list);
        Inet4UnicastAgentRouteTable::AddLocalVmRoute(Agent::GetInstance()->GetLocalVmPeer(), 
                                                     vrf_name, 
                                                     addr, plen, vm_port->GetUuid(), 
                                                     vm_port->GetVnEntry()->GetName(),
                                                     vm_port->GetLabel(), sg_id_list);

        //Enqueue MPLS label delete request
        MplsLabel::DeleteReq(label);
    } else if (vrf_entry->GetNHCount(addr) > 2) {
        CompositeNH::DeleteComponentNH(vrf_name, addr, false, component_nh_data);
        CompositeNH::DeleteComponentNH(vrf_name, addr, true, component_nh_data);
    }
    vrf_entry->DeleteNH(addr, &component_nh_data);
    return;
}

void VmPortInterface::Activate() {
    SetActiveState(true);
    struct ether_addr *addrp = ether_aton(vm_mac_.c_str());
    if (addrp == NULL) {
        LOG(ERROR, "Invalid mac address " << vm_mac_
            << " on port " << cfg_name_);
        return;
    }

    // Create InterfaceNH before MPLS is created
    InterfaceNH::CreateVportReq(GetUuid(), *addrp, vrf_->GetName());

    // Allocate MPLS Label for non-fabric interfaces
    AllocMPLSLabels();
    // Add route for the interface-ip
    AddRoute(vrf_->GetName(), addr_, 32, policy_enabled_);
    // Add route for the interface-ip
    if (AgentRouteTableAPIS::GetInstance()->GetLayer2Status()) {
        AllocL2Labels(vxlan_id_);
        AddL2Route(vrf_->GetName(), *addrp, addr_, false);
    }
    // Add route for Floating-IP
    FloatingIpList::iterator it = floating_iplist_.begin();
    while (it != floating_iplist_.end()) {
        const FloatingIp &ip = *it;
        assert(ip.vrf_.get() != NULL);
        AddRoute(ip.vrf_.get()->GetName(), ip.floating_ip_, 32, true);
        DnsProto *dns = Agent::GetInstance()->GetDnsProto();
        dns && dns->UpdateDnsEntry(this, ip.vn_.get(), ip.floating_ip_, false);
        it++;
    }

    StaticRouteList::iterator static_rt_it = static_route_list_.begin();
    while (static_rt_it != static_route_list_.end()) {
        const StaticRoute &rt = *static_rt_it;
        AddRoute(rt.vrf_, rt.addr_, rt.plen_, true);
        static_rt_it++;
    }
    
    // Allocate Link Local IP if ncessary
    if (alloc_linklocal_ip_) {
        Agent::GetInstance()->GetInterfaceTable()->VmPortToMetaDataIp(GetInterfaceId(),
                                                                      vrf_->GetVrfId(),
                                                                      &mdata_addr_);
        Inet4UnicastAgentRouteTable::AddLocalVmRoute(
            Agent::GetInstance()->GetMdataPeer(), Agent::GetInstance()->GetDefaultVrf(),
            mdata_addr_, 32, GetUuid(), vn_->GetName(), label_, true);
    }

    SendTrace(ACTIVATED);
}

void VmPortInterface::DeActivate(const string &vrf_name, const Ip4Address &ip) {
    SetActiveState(false);

    if (alloc_linklocal_ip_) {
        // Delete the route for meta-data service
        Inet4UnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->GetMdataPeer(), 
                                               Agent::GetInstance()->GetDefaultVrf(), 
                                               mdata_addr_, 32);
    }
    DeleteRoute(vrf_name, ip, 32);
    struct ether_addr *addrp = ether_aton(vm_mac_.c_str());
    //if (AgentRouteTableAPIS::GetInstance()->GetLayer2Status()) {
        DeleteL2Route(vrf_name, *addrp);
    //}

    FloatingIpList::iterator it = floating_iplist_.begin();
    while (it != floating_iplist_.end()) {
        const FloatingIp &ip = *it;
        assert(ip.vrf_.get() != NULL);
        DeleteRoute(ip.vrf_.get()->GetName(), ip.floating_ip_, 32);
        DnsProto *dns = Agent::GetInstance()->GetDnsProto();
        dns && dns->UpdateDnsEntry(this, ip.vn_.get(), ip.floating_ip_, true);
        it++;
    }
    //Clear list of floating IP
    floating_iplist_.clear();

    ServiceVlanList::iterator service_vlan_it = 
        service_vlan_list_.begin();
    while (service_vlan_it != service_vlan_list_.end()) {
        ServiceVlan &entry = service_vlan_it->second;
        ServiceVlanDel(entry);
        service_vlan_it++;
    }
    //Clear list of service vlan entry
    service_vlan_list_.clear();

    StaticRouteList::iterator static_rt_it = static_route_list_.begin();
    while (static_rt_it != static_route_list_.end()) {
        const StaticRoute &rt = *static_rt_it;
        DeleteRoute(rt.vrf_, rt.addr_, rt.plen_);
        static_rt_it++;
    }
    static_route_list_.clear();
    

    if (label_ != MplsTable::kInvalidLabel) {
        MplsLabel::DeleteReq(label_);
        label_ = MplsTable::kInvalidLabel;
    }

    if (l2_label_ != MplsTable::kInvalidLabel) {
        MplsLabel::DeleteReq(l2_label_);
        l2_label_ = MplsTable::kInvalidLabel;
    }

    if ((vxlan_id_ != 0) && 
        (TunnelType::ComputeType(TunnelType::AllType()) 
         == TunnelType::VXLAN)) {
        VxLanId::DeleteReq(vxlan_id_);
        vxlan_id_ = 0;
    }
    InterfaceNH::DeleteVportReq(GetUuid());
    SendTrace(DEACTIVATED);
}

static int CmpStaticRoute(const VmPortInterface::StaticRoute &rt,
                          const CfgStaticRoute &cfg_rt) {
    if (rt.plen_ < cfg_rt.plen_) {
        return -1;
    }

    if (rt.plen_ > cfg_rt.plen_) {
        return 1;
    }

    if (rt.addr_ < cfg_rt.addr_) {
        return -1;
    }

    if (rt.addr_ > cfg_rt.addr_) {
        return 1;
    }

#if 0
    //Enable once we can add static routes across vrf
    if (rt.vrf_ < cfg_rt.vrf_) {
        return -1;
    }

    if (rt.vrf_ > cfg_rt.vrf_) {
        return 1;
    }
#endif
    return 0;
}

static int CmpFloatingIp(const VmPortInterface::FloatingIp &ip,
                         const CfgFloatingIp &cfg_ip) {
    if (ip.floating_ip_ < cfg_ip.addr_) {
        return -1;
    }

    if (ip.floating_ip_ > cfg_ip.addr_) {
        return 1;
    }

    VrfEntry *vrf = ip.vrf_.get();
    if (vrf == NULL) {
        return -1;
    }

    if (vrf->GetName() < cfg_ip.vrf_) {
        return -1;
    }

    if (vrf->GetName() > cfg_ip.vrf_) {
        return 1;
    }

    return 0;
}

// Update the FloatingIP list
bool VmPortInterface::OnResyncFloatingIp(VmPortInterfaceData *data,
                                         bool new_active) {
    bool ret = false;
    FloatingIpList::iterator it = floating_iplist_.begin();
    CfgFloatingIpList::iterator cfg_it = data->floating_iplist_.begin();
    bool install_route = (active_ && new_active);
    DnsProto *dns = Agent::GetInstance()->GetDnsProto();

    while (it != floating_iplist_.end() && 
           cfg_it != data->floating_iplist_.end()) {
        const FloatingIp &ip = *it;
        const CfgFloatingIp &cfg_ip = *cfg_it;
        int cmp;

        cmp = CmpFloatingIp(ip, cfg_ip);
        if (cmp == 0) {
            it++;
            cfg_it++;
            continue;
        }

        if (cmp < 0) {
            FloatingIpList::iterator current = it++;
            assert(ip.vrf_.get());
            DeleteRoute(ip.vrf_.get()->GetName(), ip.floating_ip_, 32);
            dns && dns->UpdateDnsEntry(this, ip.vn_.get(),
                                       ip.floating_ip_, true);
            floating_iplist_.erase(current);
            ret = true;
            continue;
        }

        if (cmp > 0) {
            VnEntry *vn = Agent::GetInstance()->GetInterfaceTable()->FindVnRef(cfg_ip.vn_uuid_);
            assert(vn);
            VrfEntry *vrf = Agent::GetInstance()->GetInterfaceTable()->FindVrfRef(cfg_ip.vrf_);
            assert(vrf);
            if (install_route) {
                AddRoute(cfg_ip.vrf_, cfg_ip.addr_, 32, true);
                dns && dns->UpdateDnsEntry(this, vn, cfg_ip.addr_, false);
            }
            floating_iplist_.insert(FloatingIp(cfg_ip.addr_, vrf, vn, install_route));
            ret = true;
            cfg_it++;
            continue;
        }
    }

    while (it != floating_iplist_.end()) {
        const FloatingIp &ip = *it;
        FloatingIpList::iterator current = it++;
        DeleteRoute(ip.vrf_.get()->GetName(), ip.floating_ip_, 32);
        dns && dns->UpdateDnsEntry(this, ip.vn_.get(), ip.floating_ip_, true);
        floating_iplist_.erase(current);
        ret = true;
    }

    while (cfg_it != data->floating_iplist_.end()) {
        const CfgFloatingIp &cfg_ip = *cfg_it;
        VnEntry *vn = Agent::GetInstance()->GetInterfaceTable()->FindVnRef(cfg_ip.vn_uuid_);
        assert(vn);
        VrfEntry *vrf = Agent::GetInstance()->GetInterfaceTable()->FindVrfRef(cfg_ip.vrf_);
        assert(vrf);
        if (install_route) {
            AddRoute(cfg_ip.vrf_, cfg_ip.addr_, 32, true);
            dns && dns->UpdateDnsEntry(this, vn, cfg_ip.addr_, false);
        }
        floating_iplist_.insert(FloatingIp(cfg_ip.addr_, vrf, vn, install_route));
        ret = true;
        cfg_it++;
    }

    if (ret == true) {
        SendTrace(FLOATING_IP_CHANGE);
    }

    return ret;
}

void VmPortInterface::ServiceVlanRouteAdd(VmPortInterface::ServiceVlan &entry) {
    if (entry.installed_ == true) {
        return;
    }

    if (vrf_.get() == NULL ||
        vn_.get() == NULL) {
        return;
    }

    ComponentNHData component_nh_data(entry.label_, GetUuid(), entry.tag_,
                                      false);
    VrfEntry *vrf_entry = 
        Agent::GetInstance()->GetVrfTable()->FindVrfFromName(entry.vrf_->GetName());
    if (vrf_entry->FindNH(entry.addr_, component_nh_data) == true) {
        //Route already current interface as one of its nexthop
        return;
    }

    if (vrf_entry->GetNHCount(entry.addr_) == 0) {
        SecurityGroupList sg_id_list;
        SgIdList(sg_id_list);
        Inet4UnicastAgentRouteTable::AddVlanNHRoute(Agent::GetInstance()->GetLocalVmPeer(),
                                          entry.vrf_->GetName(), entry.addr_,
                                          32, GetUuid(), entry.tag_,
                                          entry.label_,
                                          GetVnEntry()->GetName(), sg_id_list);
    } else if (vrf_entry->GetNHCount(entry.addr_) > 1) {
        //Update both local composite NH and BGP injected composite NH
        CompositeNH::AppendComponentNH(entry.vrf_->GetName(), entry.addr_, true,
                                       component_nh_data);
        CompositeNH::AppendComponentNH(entry.vrf_->GetName(), entry.addr_, false,
                                       component_nh_data);
    } else if (vrf_entry->GetNHCount(entry.addr_) == 1) {
        //Interface NH to ECMP NH transition
        //Allocate a new MPLS label
        uint32_t new_label =  Agent::GetInstance()->GetMplsTable()->AllocLabel();
        //Update label data
        vrf_entry->UpdateLabel(entry.addr_, new_label);
        //Create list of component NH
        ComponentNHData::ComponentNHDataList component_nh_list = 
            *(vrf_entry->GetNHList(entry.addr_));
        component_nh_list.push_back(component_nh_data);

        //Make route point to composite NH
        Inet4UnicastAgentRouteTable::AddRemoteVmRoute(Agent::GetInstance()->GetLocalVmPeer(),
                                            entry.vrf_->GetName(), entry.addr_,
                                            32, component_nh_list, new_label,
                                            GetVnEntry()->GetName(),
                                            true);
        //Make MPLS label point to composite NH
        MplsLabel::CreateEcmpLabelReq(new_label, entry.vrf_->GetName(), 
                                      entry.addr_);
        //Update new interface to route pointed composite NH
        CompositeNH::AppendComponentNH(entry.vrf_->GetName(), entry.addr_, false,
                                       component_nh_data);
    }

    //Append interface to VRF nh list
    vrf_entry->AddNH(entry.addr_, &component_nh_data);
    entry.installed_ = true;
    return;
}

void VmPortInterface::ServiceVlanRouteDel(VmPortInterface::ServiceVlan &entry) {
    if (entry.installed_ == false) {
        return;
    }
    
    ComponentNHData component_nh_data(entry.label_, GetUuid(), entry.tag_,
                                      false);
    VrfEntry *vrf_entry = 
        Agent::GetInstance()->GetVrfTable()->FindVrfFromName(entry.vrf_->GetName());
    if (vrf_entry->FindNH(entry.addr_, component_nh_data) == false) {
        //NH not present in route
        return;
    }

    //ECMP NH to Interface NH
    std::vector<ComponentNHData> comp_nh_list =
        *(vrf_entry->GetNHList(entry.addr_));

    if (vrf_entry->GetNHCount(entry.addr_) == 1) {
        Inet4UnicastAgentRouteTable::DeleteReq(Agent::GetInstance()->GetLocalVmPeer(),
                                     entry.vrf_->GetName(), entry.addr_, 32);
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
            (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(comp_nh_list[index].nh_key_));
        const VmPortInterface *vm_port = static_cast<const VmPortInterface *>
                                        (vlan_nh->GetInterface());
        //Enqueue route change request
        SecurityGroupList sg_id_list;
        SgIdList(sg_id_list);
        Inet4UnicastAgentRouteTable::AddVlanNHRoute(Agent::GetInstance()->GetLocalVmPeer(),
                                                    entry.vrf_->GetName(), entry.addr_,
                                                    32, vm_port->GetUuid(), 
                                                    vlan_nh->GetVlanTag(),
                                                    comp_nh_list[index].label_,
                                                    vm_port->GetVnEntry()->GetName(),
                                                    sg_id_list);

        //Enqueue MPLS label delete request
        MplsLabel::DeleteReq(label);
    } else if (vrf_entry->GetNHCount(entry.addr_) > 2) {
        //Delete interface from both local composite NH and BGP
        //injected composite NH
        CompositeNH::DeleteComponentNH(entry.vrf_->GetName(), entry.addr_, false,
                                       component_nh_data);
        CompositeNH::DeleteComponentNH(entry.vrf_->GetName(), entry.addr_, true, 
                                       component_nh_data);
    }
    entry.installed_ = false;
    vrf_entry->DeleteNH(entry.addr_, &component_nh_data);
    return;
}

void VmPortInterface::ServiceVlanAdd(VmPortInterface::ServiceVlan &entry) {
    VlanNH::CreateReq(GetUuid(), entry.tag_, entry.vrf_.get()->GetName(),
                      entry.smac_, entry.dmac_);
    entry.label_ = Agent::GetInstance()->GetMplsTable()->AllocLabel();
    MplsLabel::CreateVlanNhReq(entry.label_, GetUuid(), entry.tag_);
    ServiceVlanRouteAdd(entry);
    VrfAssignTable::CreateVlanReq(GetUuid(),
                                  entry.vrf_.get()->GetName(), entry.tag_);
    return;
}

void VmPortInterface::ServiceVlanDel(VmPortInterface::ServiceVlan &entry)  {
    VrfAssignTable::DeleteVlanReq(GetUuid(), entry.tag_);
    ServiceVlanRouteDel(entry);
    MplsLabel::DeleteReq(entry.label_);
    VlanNH::DeleteReq(GetUuid(), entry.tag_);
}

bool VmPortInterface::SgExists(const uuid &id, const SgList &sg_l)
{
    SgList::const_iterator it;
    for (it = sg_l.begin(); it != sg_l.end(); ++it) {
        if ((*it)->GetSgUuid() == id) {
            return true;
        }
    }
    return false;
}

// TODO: Optimize this by using sorted list
bool VmPortInterface::OnResyncSecurityGroupList(VmPortInterfaceData *data,
                                                bool new_active) {
    bool changed = false;
    SgUuidList &idlist = data->sg_uuid_l_;

    if (data->sg_uuid_l_.size() == sg_entry_l_.size()) {
        for (SgUuidList::iterator it = idlist.begin(); it != idlist.end(); ++it) {
            if(!(SgExists((*it), sg_entry_l_))) {
                changed = true;
                break;
            }
        }
    } else {
        changed = true;
    }

    if (changed == false) {
        return false;
    }

    sg_entry_l_.clear();
    sg_uuid_l_.clear();
    for (SgUuidList::iterator it = idlist.begin(); it != idlist.end(); ++it) {
        // Find the ID in the SGList of the entry, add it and return 'true'.
        SgKey sg_key(*it);
        SgEntry *sg_entry = static_cast<SgEntry *>(Agent::GetInstance()->GetSgTable()->FindActiveEntry(&sg_key));
        sg_uuid_l_.push_back(*it);
        if (sg_entry && sg_entry->GetAcl()) {
            sg_entry_l_.push_back(SgEntryRef(sg_entry));
        }
    }
    return true;
}

// Update the Service-VLAN list
bool VmPortInterface::OnResyncServiceVlan(VmPortInterfaceData *data) {
    bool ret = false;
    ServiceVlanList::iterator it = service_vlan_list_.begin();
    CfgServiceVlanList::const_iterator cfg_it = 
        data->service_vlan_list_.begin();

    while (it != service_vlan_list_.end() && 
           cfg_it != data->service_vlan_list_.end()) {
        ServiceVlan &vlan = it->second;
        const CfgServiceVlan &cfg_vlan = cfg_it->second;

        if (it->first == cfg_it->first) {
            it++;
            cfg_it++;
            VrfEntry *vrf = 
                Agent::GetInstance()->GetInterfaceTable()->FindVrfRef(cfg_vlan.vrf_);
            if (vlan.vrf_.get() != vrf) {
                ServiceVlanRouteDel(vlan);
                vlan.vrf_ = vrf;
                ServiceVlanRouteAdd(vlan);
                ret = true;
            }
            continue;
        }

        if (it->first < cfg_it->first) {
            ServiceVlanList::iterator current = it++;
            ServiceVlanDel(vlan);
            service_vlan_list_.erase(current);
            ret = true;
            continue;
        }

        if (it->first > cfg_it->first) {
            VrfEntry *vrf = 
                Agent::GetInstance()->GetInterfaceTable()->FindVrfRef(cfg_vlan.vrf_);
            assert(vrf);
            ServiceVlan entry = ServiceVlan(vrf, cfg_vlan.addr_, cfg_vlan.tag_,
                                            cfg_vlan.smac_, cfg_vlan.dmac_);
            ServiceVlanAdd(entry);
            service_vlan_list_[entry.tag_] = entry;
            ret = true;
            cfg_it++;
            continue;
        }
    }

    while (it != service_vlan_list_.end()) {
        ServiceVlan &vlan = it->second;
        ServiceVlanList::iterator current = it++;
        ServiceVlanDel(vlan);
        service_vlan_list_.erase(current);
        ret = true;
    }

    while (cfg_it != data->service_vlan_list_.end()) {
        const CfgServiceVlan &cfg_vlan = cfg_it->second;
        VrfEntry *vrf = Agent::GetInstance()->GetInterfaceTable()->FindVrfRef(cfg_vlan.vrf_);
        assert(vrf);
        ServiceVlan entry = ServiceVlan(vrf, cfg_vlan.addr_, cfg_vlan.tag_,
                                        cfg_vlan.smac_, cfg_vlan.dmac_);
        ServiceVlanAdd(entry);
        service_vlan_list_[entry.tag_] = entry;
        ret = true;
        cfg_it++;
    }

    return ret;
}

bool VmPortInterface::OnResyncStaticRoute(VmPortInterfaceData *data, 
                                          bool new_active) {
    bool ret = false;
    StaticRouteList::iterator it = static_route_list_.begin();
    CfgStaticRouteList::iterator cfg_it = data->static_route_list_.begin();
    bool install_route = (active_ && new_active);

    if (install_route == false) {
        return ret;
    }
    while (it != static_route_list_.end() && 
           cfg_it != data->static_route_list_.end()) {
        const StaticRoute &rt = *it;
        const CfgStaticRoute &cfg_rt = *cfg_it;
        
        int cmp = 0;
        cmp = CmpStaticRoute(rt, cfg_rt);
        if (cmp == 0) {
            it++;
            cfg_it++;
            continue;
        }

        if (cmp < 0) {
            StaticRouteList::iterator current = it++;
            DeleteRoute(rt.vrf_, rt.addr_, rt.plen_);
            static_route_list_.erase(current);
            ret = true;
            continue;
        }

        if (cmp > 0) {
            if (install_route) {
                AddRoute(data->vrf_name_, cfg_rt.addr_, 
                         cfg_rt.plen_, policy_enabled_);
            }
            static_route_list_.insert(StaticRoute(data->vrf_name_,
                                                  cfg_rt.addr_,
                                                  cfg_rt.plen_));
            ret = true;
            cfg_it++;
            continue;
        }
    }

    while (it != static_route_list_.end()) {
        const StaticRoute &rt = *it;
        StaticRouteList::iterator current = it++;
        DeleteRoute(rt.vrf_, rt.addr_, rt.plen_);
        static_route_list_.erase(current);
        ret = true;
    }

    while (cfg_it != data->static_route_list_.end()) {
        const CfgStaticRoute &cfg_rt = *cfg_it;
        if (install_route) {
            AddRoute(data->vrf_name_, cfg_rt.addr_, 
                     cfg_rt.plen_, policy_enabled_);
        }
        static_route_list_.insert(StaticRoute(data->vrf_name_, 
                                              cfg_rt.addr_,
                                              cfg_rt.plen_));
        ret = true;
        cfg_it++;
    }

    return ret;
}

// Update all Interface routes, floating-ip routes and service-vlan routes
void VmPortInterface::UpdateAllRoutes() {
    FloatingIpList::iterator fip_it = floating_iplist_.begin();
    while (fip_it != floating_iplist_.end()) {
        AddRoute(fip_it->vrf_.get()->GetName(), fip_it->floating_ip_, 32, true);
        fip_it++;
    }
    SendTrace(FLOATING_IP_CHANGE);

    ServiceVlanList::iterator svlan_it = service_vlan_list_.begin();
    while (svlan_it != service_vlan_list_.end()) {
        ServiceVlanRouteAdd(svlan_it->second);
        svlan_it++;
    };

    AddRoute(vrf_->GetName(), addr_, 32, policy_enabled_);
}

// Update VM and VN references. Interface can potentially change 
// active/inactive state
bool VmPortInterface::OnResync(const DBRequest *req) {
    bool ret = false;
    VmPortInterfaceData *data = static_cast<VmPortInterfaceData *>
        (req->data.get());

    // Config can change the fabric_port_ and - alloc_linklocal_ip_ flags
    if (data) {
        alloc_linklocal_ip_ = data->need_linklocal_ip_;
        fabric_port_ = data->fabric_port_;
    }

    // Do policy and VRF related processing first
    bool policy = false;
    VmEntry *vm = NULL;
    VnEntry *vn = NULL;
    VrfEntry *vrf = NULL;
    MirrorEntry *mirror = NULL;
    MirrorDirection mirror_direction = Interface::UNKNOWN;
    VrfEntryRef old_vrf = GetVrf();
    Ip4Address old_addr = addr_;
    string cfg_name = "";

    if (data) {
        vm = InterfaceTable::GetInstance()->FindVmRef(data->vm_uuid_);
        vn = InterfaceTable::GetInstance()->FindVnRef(data->vn_uuid_);
        vrf = InterfaceTable::GetInstance()->FindVrfRef(data->vrf_name_);
        mirror = InterfaceTable::GetInstance()->FindMirrorRef(data->analyzer_name_);
        mirror_direction = data->mirror_direction_;
        cfg_name = data->cfg_name_;
        uint32_t ipaddr = 0;
        if (data->addr_.to_ulong()) {
            dhcp_snoop_ip_ = false;
            if (addr_ != data->addr_) {
                ret = SetIpAddress(data->addr_);
            }
        } else if (IsDhcpSnoopIp(name_, ipaddr)) {
            dhcp_snoop_ip_ = true;
            Ip4Address addr(ipaddr);
            if (addr_ != addr) {
                ret = SetIpAddress(addr);
            }
        } else if (addr_ != data->addr_) {
            ret = SetIpAddress(data->addr_);
        }
    }

    if (vn && vn->IsAclSet()) {
        policy = true;
    }

    if (vn_.get() != vn) {
        vn_ = vn;
        ret = true;
    }

    if (vrf_.get() != vrf) {
        vrf_ = vrf;
        ret = true;
    }

    if (vm_.get() != vm) {
        vm_ = vm;
        ret = true;
    }

    if (mirror_entry_.get() != mirror) {
        mirror_entry_ = mirror;
        ret = true;
    }

    if (mirror_direction_ != mirror_direction) {
        mirror_direction_ = mirror_direction;
        ret = true;
    }

    bool vxlan_id_changed = false;
    int old_vxlan_id = vxlan_id_;
    if (vn && (vxlan_id_ != vn_->GetVxLanId())) {
        vxlan_id_ = vn_->GetVxLanId();
        vxlan_id_changed = true;
        ret = true;
    }

    if (cfg_name_ != cfg_name) {
        cfg_name_ = cfg_name;
        ret = true;
    }

    if (os_index_ == kInvalidIndex) {
        GetOsParams(name_, test_mode_, mac_, os_index_);
    }

    bool active = IsActive(vn, vrf, vm);

    if (data != NULL) {
        if (OnResyncFloatingIp(data, active)) {
            ret = true;
        }

        if (OnResyncServiceVlan(data)) {
            ret = true;
        }

        if (OnResyncStaticRoute(data, active)) {
            ret = true;
        }

        if (OnResyncSecurityGroupList(data, active)) {
            // There is change in SG-List. Update all interface route,
            // FIP Route and Service VLAN Routes to have new SG List
            if (active_ && active) {
                UpdateAllRoutes();
            }
            ret = true;
        }

    }

    if (sg_entry_l_.size()) {
        policy = true;
    }

    if (floating_iplist_.size()) {
        policy = true;
    }

    bool policy_changed = false;
    if (policy != policy_enabled_) {
        policy_enabled_ = policy;
        policy_changed = true;
        ret = true;
    }

    if (active_ != active) {
        //Log message for transition
        if (active) {
            Activate();
        } else {
            DeActivate(old_vrf->GetName(), old_addr);
        }
        ret = true;
    } else if (active) {
        if (policy_changed) {
            // Change in policy. Update Route and MPLS to se NH with policy
            MplsLabel::CreateVPortLabelReq(label_, GetUuid(), policy,
                                           InterfaceNHFlags::INET4);
            //Update path pointed to by BGP to account for NH change
            Inet4UnicastAgentRouteTable::RouteResyncReq(vrf_->GetName(), addr_, 32);
            //Resync all static routes
            StaticRouteList::iterator static_rt_it = static_route_list_.begin();
            while (static_rt_it != static_route_list_.end()) {
                const StaticRoute &rt = *static_rt_it;
                Inet4UnicastAgentRouteTable::RouteResyncReq(rt.vrf_, rt.addr_, 
                                                            rt.plen_);
                static_rt_it++;
            }
        }
        if (AgentRouteTableAPIS::GetInstance()->GetLayer2Status() && 
            vxlan_id_changed) {
            AllocL2Labels(old_vxlan_id);
            struct ether_addr *addrp = ether_aton(vm_mac_.c_str());
            AddL2Route(vrf_->GetName(), *addrp, addr_, false);
        }
    }

    return ret;
}

// Update for VM IP address only
// For interfaces in IP Fabric VRF, we send DHCP requests to external servers
// if config doesnt provide an address. This address is updated here.
bool VmPortInterface::OnIpAddrResync(const DBRequest *req) {
    bool ret = false;
    VmPortInterfaceData *data = static_cast<VmPortInterfaceData *>
        (req->data.get());

    VmEntry *vm = vm_.get();
    VnEntry *vn = vn_.get();
    VrfEntry *vrf = vrf_.get();
    VrfEntryRef old_vrf = GetVrf();
    Ip4Address old_addr = addr_;

    if ((dhcp_snoop_ip_ || !addr_.to_ulong()) &&
        addr_ != data->addr_) {
        dhcp_snoop_ip_ = true;
        ret = SetIpAddress(data->addr_);
    }

    if (os_index_ == kInvalidIndex) {
        GetOsParams(name_, test_mode_, mac_, os_index_);
    }

    bool active = IsActive(vn, vrf, vm);
    if (active_ != active) {
        if (active) {
            Activate();
        } else {
            DeActivate(old_vrf->GetName(), old_addr);
        }
        ret = true;
    } else if (active) {
        if (old_addr != addr_) {
            if (old_addr.to_ulong())
                DeleteRoute(old_vrf->GetName(), old_addr, 32);
            if (addr_.to_ulong())
                AddRoute(vrf_->GetName(), addr_, 32, policy_enabled_);
        }
    }

    return ret;
}

// Nova VM-Port message
void VmPortInterface::NovaMsg(const uuid &intf_uuid, const string &os_name,
                              const Ip4Address &addr, const string &mac,
                              const string &vm_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    InterfaceKey *key = new VmPortInterfaceKey(intf_uuid, os_name);
    req.key.reset(key);

    VmPortInterfaceData *nova_data = new VmPortInterfaceData(addr, mac, vm_name);
    InterfaceData *data = static_cast<InterfaceData *>(nova_data);
    data->VmPortInit();
    req.data.reset(nova_data);
    InterfaceTable::GetInstance()->Enqueue(&req);
}

void VmPortInterface::NovaDel(const uuid &intf_uuid) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    InterfaceKey *key = new VmPortInterfaceKey(intf_uuid, "");
    req.key.reset(key);
    req.data.reset(NULL);
    InterfaceTable::GetInstance()->Enqueue(&req);
}

bool VmPortInterface::IsDhcpSnoopIp(std::string &name, uint32_t &addr) const {
    if (dhcp_snoop_ip_) {
        addr = addr_.to_ulong();
        return true;
    }
    InterfaceKSnap *itf = InterfaceKSnap::GetInstance();
    if (itf)
        return itf->FindInterfaceKSnapData(name, addr);
    return false;
}

inline bool VmPortInterface::IsActive(VnEntry *vn, VrfEntry *vrf, VmEntry *vm) {
    return ((vn != NULL) && (vm != NULL) && (vrf != NULL) && 
            (os_index_ != kInvalidIndex) && (addr_.to_ulong() != 0));
}

inline bool VmPortInterface::SetIpAddress(Ip4Address &addr) {
    addr_ = addr;
    LOG(DEBUG, "Set interface address to " << addr_.to_string());
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Confg triggers for FloatingIP notification to operational DB
/////////////////////////////////////////////////////////////////////////////

// Find the vm-port linked to the floating-ip and resync it
void VmPortInterface::FloatingIpSync(IFMapNode *node) {
    LOG(DEBUG, "FloatingIP SYNC for Floating-IP " << node->name());
    IFMapAgentTable *table = static_cast<IFMapAgentTable *> (node->table());
    DBGraph *graph = table->GetGraph();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *if_node = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->CanUseNode
            (if_node, Agent::GetInstance()->cfg()->cfg_vm_interface_table()) == false) {
            continue;
        }

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        if (Agent::GetInstance()->GetInterfaceTable()->IFNodeToReq(if_node, req)) {
            LOG(DEBUG, "FloatingIP SYNC for VM Port " << if_node->name());
            Agent::GetInstance()->GetInterfaceTable()->Enqueue(&req);
        }
    }

    return;
}

// Find all adjacent Floating-IP nodes and resync the corresponding
// interfaces
void VmPortInterface::FloatingIpPoolSync(IFMapNode *node) {
    if (node->IsDeleted()) {
        return;
    }

    LOG(DEBUG, "FloatingIP SYNC for Pool " << node->name());
    IFMapAgentTable *table = static_cast<IFMapAgentTable *> (node->table());
    DBGraph *graph = table->GetGraph();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *fip_node = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->CanUseNode
            (fip_node, Agent::GetInstance()->cfg()->cfg_floatingip_table()) == false) {
            continue;
        }

        VmPortInterface::FloatingIpSync(fip_node);
    }

    return;
}

void VmPortInterface::InstanceIpSync(IFMapNode *node) {
    if (node->IsDeleted()) {
        return;
    }
    LOG(DEBUG, "InstanceIp SYNC for " << node->name());

    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->CanUseNode(adj) == false) {
            continue;
        }

        if (adj->table() == Agent::GetInstance()->cfg()->cfg_vm_interface_table()) {
            InterfaceTable *interface_table = 
                    static_cast<InterfaceTable *>(Agent::GetInstance()->GetInterfaceTable());
            DBRequest req;
            if (interface_table->IFNodeToReq(adj, req)) {
                interface_table->Enqueue(&req);
            }
        }
    }

}

void VmPortInterface::FloatingIpVnSync(IFMapNode *node) {
    if (node->IsDeleted()) {
        return;
    }
    LOG(DEBUG, "FloatingIP SYNC for VN " << node->name());
    IFMapAgentTable *table = static_cast<IFMapAgentTable *> (node->table());
    DBGraph *graph = table->GetGraph();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *pool_node = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->CanUseNode
            (pool_node, Agent::GetInstance()->cfg()->cfg_floatingip_pool_table())
            == false) {
            continue;
        }

        VmPortInterface::FloatingIpPoolSync(pool_node);
    }

    return;
}

void VmPortInterface::FloatingIpVrfSync(IFMapNode *node) {
    LOG(DEBUG, "FloatingIP SYNC for VRF " << node->name());
    IFMapAgentTable *table = static_cast<IFMapAgentTable *> (node->table());
    DBGraph *graph = table->GetGraph();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *vn_node = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->CanUseNode
            (vn_node, Agent::GetInstance()->cfg()->cfg_vn_table()) == false) {
            continue;
        }

        VmPortInterface::FloatingIpVnSync(vn_node);
    }

    return;
}

const string VmPortInterface::GetAnalyzer() const {
    if (GetMirrorEntry()) {
        return GetMirrorEntry()->GetAnalyzerName();
    } else {
        return std::string();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
void Interface::SetItfSandeshData(ItfSandeshData &data) const {
    data.set_index(id_);
    data.set_name(name_);
    data.set_uuid(UuidToString(uuid_));

    if (vrf_)
        data.set_vrf_name(vrf_->GetName());
    else
        data.set_vrf_name("--ERROR--");

    if (active_) {
        data.set_active("Active");
    } else {
        data.set_active("Inactive");
    }

    if (dhcp_service_enabled_) {
        data.set_dhcp_service("Enable");
    } else {
        data.set_dhcp_service("Disable");
    }

    if (dns_service_enabled_) {
        data.set_dns_service("Enable");
    } else {
        data.set_dns_service("Disable");
    }
    data.set_label(label_);
    data.set_l2_label(l2_label_);

    switch (type_) {
        case Interface::ETH:
            data.set_type("eth");
            break;
        case Interface::VMPORT: {
            data.set_type("vport");
            const VmPortInterface *vitf = static_cast<const VmPortInterface *>(this);
            if (vitf->GetVnEntry())
                data.set_vn_name(vitf->GetVnEntry()->GetName());
            if (vitf->GetVmEntry())
                data.set_vm_uuid(UuidToString(vitf->GetVmEntry()->GetUuid()));
            data.set_ip_addr(vitf->GetIpAddr().to_string());
            data.set_mac_addr(vitf->GetVmMacAddr());
            data.set_mdata_ip_addr(vitf->GetMdataIpAddr().to_string());
            data.set_vxlan_id(vitf->GetVxLanId());
            if (vitf->IsPolicyEnabled()) {
                data.set_policy("Enable");
            } else {
                data.set_policy("Disable");
            }

            if (active_ == false) {
                string reason = "Inactive< ";
                if (vitf->GetVnEntry() == NULL) {
                    reason += "vn-null ";
                }

                if (vitf->GetVmEntry() == NULL) {
                    reason += "vm-null ";
                }

                if (vitf->GetVrf() == NULL) {
                    reason += "vrf-null ";
                }

                if (vitf->GetOsIfindex() == Interface::kInvalidIndex) {
                    reason += "no-dev ";
                }

                if (vitf->GetIpAddr().to_ulong() == 0) {
                    reason += "no-ip-addr ";
                }
                reason += " >";

                data.set_active(reason);
            }
            std::vector<FloatingIpSandeshList> fip_list;
            VmPortInterface::FloatingIpList::const_iterator it = 
                vitf->GetFloatingIpList().begin();
            while (it != vitf->GetFloatingIpList().end()) {
                const VmPortInterface::FloatingIp &ip = *it;
                FloatingIpSandeshList entry;
                
                entry.set_ip_addr(ip.floating_ip_.to_string());
                if (ip.vrf_.get()) {
                    entry.set_vrf_name(ip.vrf_.get()->GetName());
                } else {
                    entry.set_vrf_name("--ERROR--");
                }

                if (ip.installed_) {
                    entry.set_installed("Y");
                } else {
                    entry.set_installed("N");
                }
                fip_list.push_back(entry);
                it++;
            }
            data.set_fip_list(fip_list);

            // Add Service VLAN list
            std::vector<ServiceVlanSandeshList> vlan_list;
            VmPortInterface::ServiceVlanList::const_iterator vlan_it = 
                vitf->GetServiceVlanList().begin();
            while (vlan_it != vitf->GetServiceVlanList().end()) {
                const VmPortInterface::ServiceVlan &vlan = vlan_it->second;
                ServiceVlanSandeshList entry;
                
                entry.set_tag(vlan.tag_);
                if (vlan.vrf_.get()) {
                    entry.set_vrf_name(vlan.vrf_.get()->GetName());
                } else {
                    entry.set_vrf_name("--ERROR--");
                }
                entry.set_ip_addr(vlan.addr_.to_string());
                entry.set_label(vlan.label_);

                if (vlan.installed_) {
                    entry.set_installed("Y");
                } else {
                    entry.set_installed("N");
                }
                vlan_list.push_back(entry);
                vlan_it++;
            }
            if (vitf->IsFabricPort()) {
                data.set_fabric_port("FabricPort");
            } else {
                data.set_fabric_port("NotFabricPort");
            }
            if (vitf->NeedLinkLocalIp()) {
                data.set_alloc_linklocal_ip("LL-Enable");
            } else {
                data.set_alloc_linklocal_ip("LL-Disable");
            }
            data.set_service_vlan_list(vlan_list);
            data.set_analyzer_name(vitf->GetAnalyzer());
            data.set_config_name(vitf->GetCfgName());
            SgUuidList::const_iterator sgit;
            std::vector<VmIntfSgUuid> intf_sg_uuid_l;
            const SgUuidList &sg_uuid_l = vitf->GetSecurityGroupUuidList();
            for (sgit = sg_uuid_l.begin(); sgit != sg_uuid_l.end(); 
                 ++sgit) {
                VmIntfSgUuid sg_id;
                sg_id.set_sg_uuid(UuidToString(*sgit));
                intf_sg_uuid_l.push_back(sg_id);
            }
            data.set_sg_uuid_list(intf_sg_uuid_l);
            data.set_vm_name(vitf->GetVmName());
            break;
        }
        case Interface::VHOST:
            data.set_type("vhost");
            break;
        case Interface::HOST:
            data.set_type("host");
            break;
        default:
            data.set_type("invalid");
            break;
    }
    data.set_os_ifindex(os_index_);
}

bool Interface::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    ItfResp *resp = static_cast<ItfResp *>(sresp);

    if (GetName().find(name) != std::string::npos) {
        ItfSandeshData data;
        SetItfSandeshData(data);
        std::vector<ItfSandeshData> &list =
                const_cast<std::vector<ItfSandeshData>&>(resp->get_itf_list());
        list.push_back(data);

        return true;
    }

    return false;
}

void ItfReq::HandleRequest() const {
    AgentIntfSandesh *sand = new AgentIntfSandesh(context(), get_name());
    sand->DoSandesh();
}

void Interface::SendTrace(Trace event) {
    InterfaceInfo intf_info;
    intf_info.set_name(name_);
    intf_info.set_index(id_);

    switch(event) {
    case ADD:
        intf_info.set_op("Add");
        break;
    case DELETE:
        intf_info.set_op("Delete");
        break;
    default:
        intf_info.set_op("Unknown");
        break;
    }
    OPER_TRACE(Interface, intf_info);
}

void VmPortInterface::SendTrace(Trace event) {
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
        FloatingIpList::iterator it = floating_iplist_.begin();
        while (it != floating_iplist_.end()) {
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

    intf_info.set_ip_address(addr_.to_string());
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
