/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 sact*/

#include "base/os.h"
#include <sys/types.h>
#include <net/ethernet.h>
#include <boost/uuid/uuid_io.hpp>
#include <tbb/mutex.h>

#include "base/logging.h"
#include "db/db.h"
#include "db/db_entry.h"
#include "db/db_table.h"
#include "ifmap/ifmap_node.h"
#include "net/address.h"

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
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
#include <oper/ifmap_dependency_manager.h>

#include <vnc_cfg_types.h>
#include <oper/agent_sandesh.h>
#include <oper/sg.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>

using namespace std;
using namespace boost::uuids;
using boost::assign::map_list_of;
using boost::assign::list_of;

InterfaceTable *InterfaceTable::interface_table_;

/////////////////////////////////////////////////////////////////////////////
// Interface Table routines
/////////////////////////////////////////////////////////////////////////////
void InterfaceTable::Init(OperDB *oper) { 
    operdb_ = oper;
    agent_ = oper->agent();
}

void InterfaceTable::RegisterDBClients(IFMapDependencyManager *dep) {
    typedef IFMapDependencyTracker::PropagateList PropagateList;
    typedef IFMapDependencyTracker::ReactionMap ReactionMap;

    ReactionMap physical_port_react = map_list_of<std::string, PropagateList>
        ("self", list_of("self") ("physical-router-physical-interface"))
        ("physical-interface-logical-interface",
         list_of("physical-router-physical-interface"));
    dep->RegisterReactionMap("physical-interface", physical_port_react);
    dep->Register("physical-interface",
                  boost::bind(&InterfaceTable::ConfigEventHandler, this, _1));
    agent()->cfg()->Register("physical-interface", this,
                             autogen::PhysicalInterface::ID_PERMS);

    ReactionMap logical_port_react = map_list_of<std::string, PropagateList>
        ("self", list_of("self") ("physical-interface-logical-interface"))
        ("physical-interface-logical-interface",
         list_of("physical-router-physical-interface"))
        ("logical-interface-virtual-machine-interface",
         list_of("physical-interface-logical-interface"))
        ("logical-interface-virtual-machine-interface",
         list_of("self"));
    dep->RegisterReactionMap("logical-interface", logical_port_react);
    dep->Register("logical-interface",
                  boost::bind(&InterfaceTable::ConfigEventHandler, this,
                              _1));
    agent()->cfg()->Register("logical-interface", this,
                             autogen::LogicalInterface::ID_PERMS);
}

void InterfaceTable::ConfigEventHandler(DBEntry *entry) {
    Interface *intf = static_cast<Interface *>(entry);
    if (intf->ifmap_node_) {
        DBRequest req;
        if (IFNodeToReq(intf->ifmap_node_, req) == true) {
            Enqueue(&req);
        }
    }
    return;
}

bool InterfaceTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    if (strcmp(node->table()->Typename(), "physical-interface") == 0) {
        return PhysicalInterfaceIFNodeToReq(node, req);
    }

    if (strcmp(node->table()->Typename(), "logical-interface") == 0) {
        return LogicalInterfaceIFNodeToReq(node, req);
    }

    if (strcmp(node->table()->Typename(), "virtual-machine-interface") == 0) {
        return VmiIFNodeToReq(node, req);
    }

    return false;
}

std::auto_ptr<DBEntry> InterfaceTable::AllocEntry(const DBRequestKey *k) const{
    const InterfaceKey *key = static_cast<const InterfaceKey *>(k);
    
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>
                                  (key->AllocEntry(this)));
}

DBEntry *InterfaceTable::Add(const DBRequest *req) {
    InterfaceKey *key = static_cast<InterfaceKey *>(req->key.get());
    InterfaceData *data = static_cast<InterfaceData *>(req->data.get());

    Interface *intf = key->AllocEntry(this, data);
    if (intf == NULL)
        return NULL;

    intf->id_ = index_table_.Insert(intf);

    // Get the os-ifindex and mac of interface
    intf->GetOsParams(agent());

    intf->Add();
    intf->ifmap_node_ = data->ifmap_node_;
    if (intf->ifmap_node_) {
        operdb_->dependency_manager()->SetObject(intf->ifmap_node_, intf);
    }
    intf->SendTrace(Interface::ADD);
    return intf;
}

bool InterfaceTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    InterfaceKey *key = static_cast<InterfaceKey *>(req->key.get());
    InterfaceData *data = static_cast<InterfaceData *>(req->data.get());

    switch (key->type_) {
    case Interface::VM_INTERFACE: {
        VmInterface *intf = static_cast<VmInterface *>(entry);
        ret = intf->OnChange(static_cast<VmInterfaceData *>(req->data.get()));
        break;
    }
    case Interface::INET: {
        InetInterface *intf = static_cast<InetInterface *>(entry);
        if (intf) {
            // Get the os-ifindex and mac of interface
            intf->GetOsParams(agent());
            intf->OnChange(static_cast<InetInterfaceData *>(req->data.get()));
            ret = true;
        }
        break;
    }

    case Interface::PHYSICAL: {
        PhysicalInterface *intf = static_cast<PhysicalInterface *>(entry);
        ret = intf->OnChange(this, static_cast<PhysicalInterfaceData *>
                             (req->data.get()));
        break;
    }

    case Interface::REMOTE_PHYSICAL: {
        RemotePhysicalInterface *intf =
            static_cast<RemotePhysicalInterface *>(entry);
        ret = intf->OnChange(this, static_cast<RemotePhysicalInterfaceData *>
                             (req->data.get()));
        break;
    }

    case Interface::LOGICAL: {
        LogicalInterface *intf = static_cast<LogicalInterface *>(entry);
        ret = intf->OnChange(this, static_cast<LogicalInterfaceData *>
                             (req->data.get()));
        break;
    }

    default:
        break;
    }

    if (data->ifmap_node_) {
        Interface *intf = static_cast<Interface *>(entry);
        IFMapDependencyManager *dep = operdb()->dependency_manager();
        if (intf->ifmap_node_ != data->ifmap_node_) {
            if (intf->ifmap_node_ != NULL)
                dep->ResetObject(intf->ifmap_node_);
            intf->ifmap_node_ = data->ifmap_node_;
            if (intf->ifmap_node_)
                dep->SetObject(intf->ifmap_node_, intf);
        }
    }

    return ret;
}

// RESYNC supported only for VM_INTERFACE
bool InterfaceTable::Resync(DBEntry *entry, DBRequest *req) {
    InterfaceKey *key = static_cast<InterfaceKey *>(req->key.get());
    InterfaceData *data = static_cast<InterfaceData *>(req->data.get());
    if (key->type_ != Interface::VM_INTERFACE)
        return false;

    if (data->ifmap_node_) {
        Interface *intf = static_cast<Interface *>(entry);
        IFMapDependencyManager *dep = operdb()->dependency_manager();
        if (intf->ifmap_node_ != data->ifmap_node_) {
            if (intf->ifmap_node_ != NULL)
                dep->ResetObject(intf->ifmap_node_);
            intf->ifmap_node_ = data->ifmap_node_;
            if (intf->ifmap_node_)
                dep->SetObject(intf->ifmap_node_, intf);
        }
    }

    VmInterfaceData *vm_data = static_cast<VmInterfaceData *>(req->data.get());
    VmInterface *intf = static_cast<VmInterface *>(entry);
    return intf->Resync(this, vm_data);
}

bool InterfaceTable::Delete(DBEntry *entry, const DBRequest *req) {
    Interface *intf = static_cast<Interface *>(entry);
    bool ret = false;

    if (intf->Delete(req)) {
        intf->SendTrace(Interface::DELETE);
        ret = true;

        if (intf->ifmap_node_ != NULL) {
            operdb()->dependency_manager()->ResetObject(intf->ifmap_node_);
            intf->ifmap_node_ = NULL;
        }
    }
    return ret;
}

VrfEntry *InterfaceTable::FindVrfRef(const string &name) const {
    VrfKey key(name);
    return static_cast<VrfEntry *>
        (agent_->vrf_table()->FindActiveEntry(&key));
}

VmEntry *InterfaceTable::FindVmRef(const uuid &uuid) const {
    VmKey key(uuid);
    return static_cast<VmEntry *>(agent_->vm_table()->FindActiveEntry(&key));
}

VnEntry *InterfaceTable::FindVnRef(const uuid &uuid) const {
    VnKey key(uuid);
    return static_cast<VnEntry *>(agent_->vn_table()->FindActiveEntry(&key));
}

MirrorEntry *InterfaceTable::FindMirrorRef(const string &name) const {
    MirrorEntryKey key(name);
    return static_cast<MirrorEntry *>
        (agent_->mirror_table()->FindActiveEntry(&key));
}

DBTableBase *InterfaceTable::CreateTable(DB *db, const std::string &name) {
    interface_table_ = new InterfaceTable(db, name);
    (static_cast<DBTable *>(interface_table_))->Init();
    return interface_table_;
};

Interface *InterfaceTable::FindInterface(size_t index) {
    Interface *intf = index_table_.At(index);
    if (intf && intf->IsDeleted() != true) {
        return intf;
    }
    return NULL;
}

bool InterfaceTable::FindVmUuidFromMetadataIp(const Ip4Address &ip,
                                              std::string *vm_ip,
                                              std::string *vm_uuid,
                                              std::string *vm_project_uuid) {
    Interface *intf = FindInterfaceFromMetadataIp(ip);
    if (intf && intf->type() == Interface::VM_INTERFACE) {
        const VmInterface *vintf = static_cast<const VmInterface *>(intf);
        *vm_ip = vintf->ip_addr().to_string();
        if (vintf->vm()) {
            *vm_uuid = UuidToString(vintf->vm()->GetUuid());
            *vm_project_uuid = UuidToString(vintf->vm_project_uuid());
            return true;
        }
    }
    return false;
}

Interface *InterfaceTable::FindInterfaceFromMetadataIp(const Ip4Address &ip) {
    uint32_t addr = ip.to_ulong();
    if ((addr & 0xFFFF0000) != (METADATA_IP_ADDR & 0xFFFF0000))
        return NULL;
    return index_table_.At(addr & 0xFFFF);
}

void InterfaceTable::VmPortToMetaDataIp(uint16_t index, uint32_t vrfid,
                                        Ip4Address *addr) {
    uint32_t ip = METADATA_IP_ADDR & 0xFFFF0000;
    ip += (index & 0xFFFF);
    *addr = Ip4Address(ip);
}

bool InterfaceTable::L2VmInterfaceWalk(DBTablePartBase *partition,
                                       DBEntryBase *entry) {
    Interface *intf = static_cast<Interface *>(entry);
    if ((intf->type() != Interface::VM_INTERFACE) || intf->IsDeleted())
        return true;

    VmInterface *vm_intf = static_cast<VmInterface *>(entry);
    //In case VN passed is null it is tunnel mode change,
    //else its vxlan id change for a VN.
    if (!vm_intf->l2_active())
        return true;

    if (vm_intf->layer2_forwarding()) {
        bool force_update = false;
        if (vm_intf->vxlan_id() != vm_intf->vn()->GetVxLanId()) {
            force_update = true;
        }
        vm_intf->UpdateL2(force_update);
    }
    return true;
}

void InterfaceTable::VmInterfaceWalkDone(DBTableBase *partition) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
}

void InterfaceTable::UpdateVxLanNetworkIdentifierMode() {
    DBTableWalker *walker = agent_->db()->GetWalker();
    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        walker->WalkCancel(walkid_);
    }
    walkid_ = walker->WalkTable(this, NULL,
                      boost::bind(&InterfaceTable::L2VmInterfaceWalk, 
                                  this, _1, _2),
                      boost::bind(&InterfaceTable::VmInterfaceWalkDone, 
                                  this, _1));
}

/////////////////////////////////////////////////////////////////////////////
// Interface Base Entry routines
/////////////////////////////////////////////////////////////////////////////
Interface::Interface(Type type, const uuid &uuid, const string &name,
                     VrfEntry *vrf) :
    type_(type), uuid_(uuid), name_(name),
    vrf_(vrf), label_(MplsTable::kInvalidLabel),
    l2_label_(MplsTable::kInvalidLabel), ipv4_active_(true), l2_active_(true),
    id_(kInvalidIndex), dhcp_enabled_(true), dns_enabled_(true), mac_(),
    os_index_(kInvalidIndex), admin_state_(true), test_oper_state_(true),
    ifmap_node_(NULL) {
}

Interface::~Interface() {
    if (id_ != kInvalidIndex) {
        static_cast<InterfaceTable *>(get_table())->FreeInterfaceId(id_);
    }
}

void Interface::GetOsParams(Agent *agent) {
    if (agent->test_mode()) {
        static int dummy_ifindex = 0;
        if (os_index_ == kInvalidIndex) {
            os_index_ = ++dummy_ifindex;
            mac_.Zero();
            mac_.last_octet() = os_index_;
        }
        os_oper_state_ = test_oper_state_;
        return;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.c_str(), IF_NAMESIZE);
    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(fd >= 0);
    if (ioctl(fd, SIOCGIFHWADDR, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> querying mac-address for interface <" << name_ << ">");
        os_oper_state_ = false;
        close(fd);
        return;
    }


    if (ioctl(fd, SIOCGIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> querying mac-address for interface <" << name_ << ">");
        os_oper_state_ = false;
        close(fd);
        return;
    }

    os_oper_state_ = false;
    if ((ifr.ifr_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING)) {
        os_oper_state_ = true;
    }
    close(fd);

#if defined(__linux__)
    mac_ = ifr.ifr_hwaddr;
#elif defined(__FreeBSD__)
    mac_ = ifr.ifr_addr;
#endif

    if (os_index_ == kInvalidIndex) {
        int idx = if_nametoindex(name_.c_str());
        if (idx)
            os_index_ = idx;
    }
}

void Interface::SetKey(const DBRequestKey *key) {
    const InterfaceKey *k = static_cast<const InterfaceKey *>(key);
    type_ = k->type_;
    uuid_ = k->uuid_;
    name_ = k->name_;
}

uint32_t Interface::vrf_id() const {
    if (vrf_ == NULL) {
        return VrfEntry::kInvalidIndex;
    }

    return vrf_->vrf_id();
}

void InterfaceTable::set_update_floatingip_cb(UpdateFloatingIpFn fn) {
    update_floatingip_cb_ = fn;
}

const InterfaceTable::UpdateFloatingIpFn &InterfaceTable::update_floatingip_cb()
    const {
    return update_floatingip_cb_;
}

/////////////////////////////////////////////////////////////////////////////
// Pkt Interface routines
/////////////////////////////////////////////////////////////////////////////
PacketInterface::PacketInterface(const std::string &name) :
    Interface(Interface::PACKET, nil_uuid(), name, NULL) {
}

PacketInterface::~PacketInterface() {
}

DBEntryBase::KeyPtr PacketInterface::GetDBRequestKey() const {
    InterfaceKey *key = new PacketInterfaceKey(uuid_, name_);
    return DBEntryBase::KeyPtr(key);
}

void PacketInterface::PostAdd() {
    InterfaceNH::CreatePacketInterfaceNh(name_);
}

bool PacketInterface::Delete(const DBRequest *req) {
    flow_key_nh_= NULL;
    return true;
}

// Enqueue DBRequest to create a Pkt Interface
void PacketInterface::CreateReq(InterfaceTable *table,
                                const std::string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PacketInterfaceKey(nil_uuid(), ifname));
    req.data.reset(new PacketInterfaceData());
    table->Enqueue(&req);
}

void PacketInterface::Create(InterfaceTable *table, const std::string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PacketInterfaceKey(nil_uuid(), ifname));
    req.data.reset(new PacketInterfaceData());
    table->Process(req);
}

// Enqueue DBRequest to delete a Pkt Interface
void PacketInterface::DeleteReq(InterfaceTable *table,
                                const std::string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new PacketInterfaceKey(nil_uuid(), ifname));
    req.data.reset(NULL);
    table->Enqueue(&req);
}

void PacketInterface::Delete(InterfaceTable *table, const std::string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new PacketInterfaceKey(nil_uuid(), ifname));
    req.data.reset(NULL);
    table->Process(req);
}

/////////////////////////////////////////////////////////////////////////////
// DHCP Snoop routines
// DHCP Snoop entry can be added from 3 different places,
// - Interface added from config
// - Address learnt from DHCP Snoop on fabric interface
// - Address learnt from vrouter when agent restarts
//
// DHCP Snoop entry is deleted from 2 places
// - Interface deleted from config
// - Audit of entries read from vrouter on restart and config table
/////////////////////////////////////////////////////////////////////////////

// Get DHCP IP address. First try to find entry in DHCP Snoop table.
// If no entry in DHCP Snoop table, query the InterfaceKScan table.
//
// InterfaceKScan table is populated on agent restart
const Ip4Address InterfaceTable::GetDhcpSnoopEntry(const std::string &ifname) {
    tbb::mutex::scoped_lock lock(dhcp_snoop_mutex_);
    const DhcpSnoopIterator it = dhcp_snoop_map_.find(ifname);
    if (it != dhcp_snoop_map_.end()) {
        return it->second.addr_;
    }

    return Ip4Address(0);
}

void InterfaceTable::DeleteDhcpSnoopEntry(const std::string &ifname) {
    tbb::mutex::scoped_lock lock(dhcp_snoop_mutex_);
    const DhcpSnoopIterator it = dhcp_snoop_map_.find(ifname);
    if (it == dhcp_snoop_map_.end()) {
        return;
    }

    return dhcp_snoop_map_.erase(it);
}

// Set config_seen_ flag in DHCP Snoop entry.
// Create the DHCP Snoop entry, if not already present
void InterfaceTable::DhcpSnoopSetConfigSeen(const std::string &ifname) {
    tbb::mutex::scoped_lock lock(dhcp_snoop_mutex_);
    const DhcpSnoopIterator it = dhcp_snoop_map_.find(ifname);
    Ip4Address addr(0);

    if (it != dhcp_snoop_map_.end()) {
        addr = it->second.addr_;
    }
    dhcp_snoop_map_[ifname] = DhcpSnoopEntry(addr, true);
}

void InterfaceTable::AddDhcpSnoopEntry(const std::string &ifname,
                                       const Ip4Address &addr) {
    tbb::mutex::scoped_lock lock(dhcp_snoop_mutex_);
    DhcpSnoopEntry entry(addr, false);
    const DhcpSnoopIterator it = dhcp_snoop_map_.find(ifname);

    if (it != dhcp_snoop_map_.end()) {
        // Retain config_entry_ flag from old entry
        if (it->second.config_entry_) {
            entry.config_entry_ = true;
        }

        // If IP address is not specified, retain old IP address
        if (addr.to_ulong() == 0) {
            entry.addr_ = it->second.addr_;
        }
    }

    dhcp_snoop_map_[ifname] = entry;
}

// Audit DHCP Snoop table. Delete the entries which are not seen from config
void InterfaceTable::AuditDhcpSnoopTable() {
    tbb::mutex::scoped_lock lock(dhcp_snoop_mutex_);
    DhcpSnoopIterator it = dhcp_snoop_map_.begin();
    while (it != dhcp_snoop_map_.end()){
        DhcpSnoopIterator del_it = it++;
        if (del_it->second.config_entry_ == false) {
            dhcp_snoop_map_.erase(del_it);
        }
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

    if (ipv4_active_) {
        data.set_active("Active");
    } else {
        data.set_active("Inactive");
    }

    if (ipv6_active_) {
        data.set_ip6_active("Active");
    } else {
        data.set_ip6_active("Inactive");
    }

    if (l2_active_) {
        data.set_l2_active("L2 Active");
    } else {
        data.set_l2_active("L2 Inactive");
    }

    if (dhcp_enabled_) {
        data.set_dhcp_service("Enable");
    } else {
        data.set_dhcp_service("Disable");
    }

    if (dns_enabled_) {
        data.set_dns_service("Enable");
    } else {
        data.set_dns_service("Disable");
    }
    data.set_label(label_);
    data.set_l2_label(l2_label_);
    if (flow_key_nh()) {
        data.set_flow_key_idx(flow_key_nh()->id());
    }

    switch (type_) {
    case Interface::PHYSICAL:
        data.set_type("eth");
        // data.set_policy("Enable");
        break;
    case Interface::VM_INTERFACE: {
        data.set_type("vport");
        const VmInterface *vintf = static_cast<const VmInterface *>(this);
        if (vintf->vn())
            data.set_vn_name(vintf->vn()->GetName());
        if (vintf->vm())
            data.set_vm_uuid(UuidToString(vintf->vm()->GetUuid()));
        data.set_ip_addr(vintf->ip_addr().to_string());
        data.set_ip6_addr(vintf->ip6_addr().to_string());
        data.set_mac_addr(vintf->vm_mac());
        data.set_mdata_ip_addr(vintf->mdata_ip_addr().to_string());
        data.set_vxlan_id(vintf->vxlan_id());
        if (vintf->policy_enabled()) {
            data.set_policy("Enable");
        } else {
            data.set_policy("Disable");
        }

        if ((ipv4_active_ == false) ||
            (l2_active_ == false)) {
            string common_reason = "";

            if (!vintf->admin_state()) {
                common_reason += "admin-down ";
            }

            if (vintf->vn() == NULL) {
                common_reason += "vn-null ";
            } else if (!vintf->vn()->admin_state()) {
                common_reason += "vn-admin-down ";
            }

            if (vintf->vrf() == NULL) {
                common_reason += "vrf-null ";
            }

            if (vintf->subnet().is_unspecified() &&
                vintf->os_index() == Interface::kInvalidIndex) {
                common_reason += "no-dev ";
            }

            if (vintf->NeedDevice()) {
                if (vintf->os_index() == Interface::kInvalidIndex) {
                    common_reason += "no-dev ";
                }

                if (vintf->os_oper_state() == false) {
                    common_reason += "os-state-down ";
                }
            }

            if (!ipv4_active_) {
                if (vintf->layer3_forwarding() == false) {
                    common_reason += "l3-disabled";
                }

                string reason = "L3 Inactive < " + common_reason;
                if (vintf->ip_addr().to_ulong() == 0) {
                    reason += "no-ip-addr ";
                }
                reason += " >";
                data.set_active(reason);
            }

            if (!l2_active_) {
                if (vintf->layer2_forwarding() == false) {
                    common_reason += "l2-disabled";
                }
                string reason = "L2 Inactive < " + common_reason;
                reason += " >";
                data.set_l2_active(reason);
            }
        }
        
        std::vector<FloatingIpSandeshList> fip_list;
        VmInterface::FloatingIpSet::const_iterator it = 
            vintf->floating_ip_list().list_.begin();
        while (it != vintf->floating_ip_list().list_.end()) {
            const VmInterface::FloatingIp &ip = *it;
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
        VmInterface::ServiceVlanSet::const_iterator vlan_it = 
            vintf->service_vlan_list().list_.begin();
        while (vlan_it != vintf->service_vlan_list().list_.end()) {
            const VmInterface::ServiceVlan *vlan = vlan_it.operator->();
            ServiceVlanSandeshList entry;

            entry.set_tag(vlan->tag_);
            if (vlan->vrf_.get()) {
                entry.set_vrf_name(vlan->vrf_.get()->GetName());
            } else {
                entry.set_vrf_name("--ERROR--");
            }
            entry.set_ip_addr(vlan->addr_.to_string());
            entry.set_label(vlan->label_);

            if (vlan->installed_) {
                entry.set_installed("Y");
            } else {
                entry.set_installed("N");
            }
            vlan_list.push_back(entry);
            vlan_it++;
        }

        std::vector<StaticRouteSandesh> static_route_list;
        VmInterface::StaticRouteSet::iterator static_rt_it =
            vintf->static_route_list().list_.begin();
        while (static_rt_it != vintf->static_route_list().list_.end()) {
            const VmInterface::StaticRoute &rt = *static_rt_it;
            StaticRouteSandesh entry;
            entry.set_vrf_name(rt.vrf_);
            entry.set_ip_addr(rt.addr_.to_string());
            entry.set_prefix(rt.plen_);
            static_rt_it++;
            static_route_list.push_back(entry);
        }
        data.set_static_route_list(static_route_list);

        std::vector<StaticRouteSandesh> aap_list;
        VmInterface::AllowedAddressPairSet::iterator aap_it =
            vintf->allowed_address_pair_list().list_.begin();
        while (aap_it != vintf->allowed_address_pair_list().list_.end()) {
            const VmInterface::AllowedAddressPair &rt = *aap_it;
            StaticRouteSandesh entry;
            entry.set_vrf_name(rt.vrf_);
            entry.set_ip_addr(rt.addr_.to_string());
            entry.set_prefix(rt.plen_);
            aap_it++;
            aap_list.push_back(entry);
        }
        data.set_allowed_address_pair_list(aap_list);

        if (vintf->fabric_port()) {
            data.set_fabric_port("FabricPort");
        } else {
            data.set_fabric_port("NotFabricPort");
        }
        if (vintf->need_linklocal_ip()) {
            data.set_alloc_linklocal_ip("LL-Enable");
        } else {
            data.set_alloc_linklocal_ip("LL-Disable");
        }
        data.set_service_vlan_list(vlan_list);
        data.set_analyzer_name(vintf->GetAnalyzer());
        data.set_config_name(vintf->cfg_name());

        VmInterface::SecurityGroupEntrySet::const_iterator sgit;
        std::vector<VmIntfSgUuid> intf_sg_uuid_l;
        const VmInterface::SecurityGroupEntryList &sg_uuid_l = vintf->sg_list();
        for (sgit = sg_uuid_l.list_.begin(); sgit != sg_uuid_l.list_.end(); 
             ++sgit) {
            VmIntfSgUuid sg_id;
            sg_id.set_sg_uuid(UuidToString(sgit->uuid_));
            intf_sg_uuid_l.push_back(sg_id);
        }
        data.set_sg_uuid_list(intf_sg_uuid_l);
        data.set_vm_name(vintf->vm_name());
        data.set_vm_project_uuid(UuidToString(vintf->vm_project_uuid()));
        data.set_local_preference(vintf->local_preference());

        data.set_tx_vlan_id(vintf->tx_vlan_id());
        data.set_rx_vlan_id(vintf->rx_vlan_id());
        if (vintf->parent()) {
            data.set_parent_interface(vintf->parent()->name());
        }
        if (vintf->subnet().to_ulong() != 0) {
            std::ostringstream str;
            str << vintf->subnet().to_string() << "/"
                << (int)vintf->subnet_plen();
            data.set_subnet(str.str());
        }
        if (vintf->sub_type() == VmInterface::GATEWAY) {
            data.set_sub_type("Gateway");
        } else if (vintf->sub_type() == VmInterface::TOR) {
            data.set_sub_type("TOR");
        } else if (vintf->sub_type() == VmInterface::NOVA) {
             data.set_sub_type("Tap");
        } else {
            data.set_sub_type("None");
        }
        break;
    }
    case Interface::INET:
        data.set_type("vhost");
        break;
    case Interface::PACKET:
        data.set_type("pkt");
        break;
    default:
        data.set_type("invalid");
        break;
    }
    data.set_os_ifindex(os_index_);
    if (admin_state_) {
        data.set_admin_state("Enabled");
    } else {
        data.set_admin_state("Disabled");
    }
}

bool Interface::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    ItfResp *resp = static_cast<ItfResp *>(sresp);

    if (name.empty() || name_ == name) {
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

void Interface::SendTrace(Trace event) const {
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

bool Interface::ip_active(Address::Family family) const {
    if (family == Address::INET)
        return ipv4_active_;
    else if (family == Address::INET6)
        return ipv6_active_;
    else
        assert(0);
    return false;
}
