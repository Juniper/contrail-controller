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

bool Interface::test_mode_;
InterfaceTable *InterfaceTable::interface_table_;

/////////////////////////////////////////////////////////////////////////////
// Interface Table routines
/////////////////////////////////////////////////////////////////////////////
void InterfaceTable::Init(OperDB *oper) { 
    operdb_ = oper;
    agent_ = oper->agent();
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
    intf->GetOsParams();
    intf->SendTrace(Interface::ADD);
    return intf;
}

bool InterfaceTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    InterfaceKey *key = static_cast<InterfaceKey *>(req->key.get());

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
            intf->GetOsParams();
            intf->OnChange(static_cast<InetInterfaceData *>(req->data.get()));
            ret = true;
        }
        break;
    }
    default:
        break;
    }

    return ret;
}

// RESYNC supported only for VM_INTERFACE
bool InterfaceTable::Resync(DBEntry *entry, DBRequest *req) {
    InterfaceKey *key = static_cast<InterfaceKey *>(req->key.get());
    assert(key->type_ == Interface::VM_INTERFACE);

    VmInterfaceData *vm_data = static_cast<VmInterfaceData *>(req->data.get());
    VmInterface *intf = static_cast<VmInterface *>(entry);
    return intf->Resync(vm_data);
}

void InterfaceTable::Delete(DBEntry *entry, const DBRequest *req) {
    Interface *intf = static_cast<Interface *>(entry);
    intf->Delete();
    intf->SendTrace(Interface::DELETE);
}

VrfEntry *InterfaceTable::FindVrfRef(const string &name) const {
    VrfKey key(name);
    return static_cast<VrfEntry *>
        (agent_->GetVrfTable()->FindActiveEntry(&key));
}

VmEntry *InterfaceTable::FindVmRef(const uuid &uuid) const {
    VmKey key(uuid);
    return static_cast<VmEntry *>(agent_->GetVmTable()->FindActiveEntry(&key));
}

VnEntry *InterfaceTable::FindVnRef(const uuid &uuid) const {
    VnKey key(uuid);
    return static_cast<VnEntry *>(agent_->GetVnTable()->FindActiveEntry(&key));
}

MirrorEntry *InterfaceTable::FindMirrorRef(const string &name) const {
    MirrorEntryKey key(name);
    return static_cast<MirrorEntry *>
        (agent_->GetMirrorTable()->FindActiveEntry(&key));
}

DBTableBase *InterfaceTable::CreateTable(DB *db, const std::string &name) {
    interface_table_ = new InterfaceTable(db, name);
    (static_cast<DBTable *>(interface_table_))->Init();
    return interface_table_;
};

Interface *InterfaceTable::FindInterfaceFromMetadataIp(const Ip4Address &ip) {
    uint32_t addr = ip.to_ulong();
    if ((addr & 0xFFFF0000) != (METADATA_IP_ADDR & 0xFFFF0000))
        return NULL;
    return index_table_.At(addr & 0xFF);
}

bool InterfaceTable::FindVmUuidFromMetadataIp(const Ip4Address &ip,
                                              std::string *vm_ip,
                                              std::string *vm_uuid) {
    Interface *intf = FindInterfaceFromMetadataIp(ip);
    if (intf && intf->type() == Interface::VM_INTERFACE) {
        const VmInterface *vintf = static_cast<const VmInterface *>(intf);
        *vm_ip = vintf->ip_addr().to_string();
        if (vintf->vm()) {
            *vm_uuid = UuidToString(vintf->vm()->GetUuid());
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

bool InterfaceTable::L2VmInterfaceWalk(DBTablePartBase *partition,
                                     DBEntryBase *entry) {
    Interface *intf = static_cast<Interface *>(entry);
    if ((intf->type() != Interface::VM_INTERFACE) || intf->IsDeleted())
        return true;

    VmInterface *vm_intf = static_cast<VmInterface *>(entry);
    if (!vm_intf->l2_active())
        return true;

    const VnEntry *vn = vm_intf->vn();
    if (vm_intf->layer2_forwarding() && 
        (vn->GetVxLanId() != vm_intf->vxlan_id())) {
        vm_intf->UpdateL2();
    }
    return true;
}

void InterfaceTable::VmInterfaceWalkDone(DBTableBase *partition) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
}

void InterfaceTable::UpdateVxLanNetworkIdentifierMode() {
    DBTableWalker *walker = agent_->GetDB()->GetWalker();
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
    os_index_(kInvalidIndex) { 
}

Interface::~Interface() { 
    if (id_ != kInvalidIndex) {
        static_cast<InterfaceTable *>(get_table())->FreeInterfaceId(id_);
    }
}

void Interface::GetOsParams() {
    if (test_mode_) {
        static int dummy_ifindex = 0;
        os_index_ = ++dummy_ifindex;
        bzero(&mac_, sizeof(mac_));
        mac_.ether_addr_octet[5] = os_index_;
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
        os_index_ = Interface::kInvalidIndex;
        bzero(&mac_, sizeof(mac_));
        close(fd);
        return;
    }
    close(fd);

    memcpy(mac_.ether_addr_octet, ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN);
    os_index_ = if_nametoindex(name_.c_str());
}

void Interface::SetKey(const DBRequestKey *key) { 
    const InterfaceKey *k = static_cast<const InterfaceKey *>(key);
    type_ = k->type_;
    uuid_ = k->uuid_;
    name_ = k->name_;
}

uint32_t Interface::GetVrfId() const {
    if (vrf_ == NULL) {
        return VrfEntry::kInvalidIndex;
    }

    return vrf_->GetVrfId();
}

/////////////////////////////////////////////////////////////////////////////
// Pkt Interface routines
/////////////////////////////////////////////////////////////////////////////
DBEntryBase::KeyPtr PacketInterface::GetDBRequestKey() const {
    InterfaceKey *key = new PacketInterfaceKey(uuid_, name_);
    return DBEntryBase::KeyPtr(key);
}

// Enqueue DBRequest to create a Pkt Interface
void PacketInterface::CreateReq(InterfaceTable *table,
                                const std::string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PacketInterfaceKey(nil_uuid(), ifname));
    req.data.reset(new PacketInterfaceData());
    table->Enqueue(&req);
}

// Enqueue DBRequest to delete a Pkt Interface
void PacketInterface::DeleteReq(InterfaceTable *table,
                                const std::string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new PacketInterfaceKey(nil_uuid(), ifname));
    req.data.reset(NULL);
    table->Enqueue(&req);
}

/////////////////////////////////////////////////////////////////////////////
// Ethernet Interface routines
/////////////////////////////////////////////////////////////////////////////
DBEntryBase::KeyPtr PhysicalInterface::GetDBRequestKey() const {
    InterfaceKey *key = new PhysicalInterfaceKey(name_);
    return DBEntryBase::KeyPtr(key);
}

// Enqueue DBRequest to create a Host Interface
void PhysicalInterface::CreateReq(InterfaceTable *table, const string &ifname,
                                  const string &vrf_name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PhysicalInterfaceKey(ifname));
    req.data.reset(new PhysicalInterfaceData(vrf_name));
    table->Enqueue(&req);
}

// Enqueue DBRequest to delete a Host Interface
void PhysicalInterface::DeleteReq(InterfaceTable *table, const string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new PhysicalInterfaceKey(ifname));
    req.data.reset(NULL);
    table->Enqueue(&req);
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
            if (vintf->vn() == NULL) {
                common_reason += "vn-null ";
            }

            if (vintf->vm() == NULL) {
                common_reason += "vm-null ";
            }

            if (vintf->vrf() == NULL) {
                common_reason += "vrf-null ";
            }

            if (vintf->os_index() == Interface::kInvalidIndex) {
                common_reason += "no-dev ";
            }

            if (!ipv4_active_) {
                string reason = "Inactive< " + common_reason;
                if (vintf->ip_addr().to_ulong() == 0) {
                    reason += "no-ip-addr ";
                }
                reason += " >";
                data.set_active(reason);
            }

            if (!l2_active_) {
                string reason = "Inactive L2< " + common_reason;
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
}

bool Interface::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    ItfResp *resp = static_cast<ItfResp *>(sresp);

    if (name_.find(name) != std::string::npos) {
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

