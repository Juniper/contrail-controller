/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/foreach.hpp>
#include <cmn/agent_cmn.h>
#include "ifmap/ifmap_node.h"
#include <vnc_cfg_types.h>
#include "base/task_annotations.h"
#include <base/logging.h>
#include <oper/route_common.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/mirror_table.h>
#include <oper/multicast.h>
#include <oper/tunnel_nh.h>
#include <oper/vrf.h>
#include <oper/agent_sandesh.h>

using namespace std;

NextHopTable *NextHopTable::nexthop_table_;

/////////////////////////////////////////////////////////////////////////////
// TunnelTyperoutines
/////////////////////////////////////////////////////////////////////////////
TunnelType::Type TunnelType::default_type_;
TunnelType::PriorityList TunnelType::priority_list_;

TunnelType::Type TunnelType::ComputeType(TunnelType::TypeBmap bmap) {
    for (PriorityList::const_iterator it = priority_list_.begin();
         it != priority_list_.end(); it++) {
        if (bmap & (1 << *it)) {
            return *it;
        }
    }

    return DefaultType();
}

// Confg triggers for change in encapsulation priority
bool TunnelType::EncapPrioritySync(const std::vector<std::string> &cfg_list) {
    PriorityList l;

    for (std::vector<std::string>::const_iterator it = cfg_list.begin();
         it != cfg_list.end(); it++) {
        if (*it == "MPLSoGRE")
            l.push_back(MPLS_GRE);
        if (*it == "MPLSoUDP")
            l.push_back(MPLS_UDP);
        if (*it == "VXLAN")
            l.push_back(VXLAN);
    }

    bool encap_changed = (priority_list_ != l);
    priority_list_ = l;

    return encap_changed;
}

void TunnelType::DeletePriorityList() {
    priority_list_.clear();
}

/////////////////////////////////////////////////////////////////////////////
// NextHop routines
/////////////////////////////////////////////////////////////////////////////
NextHopTable::NextHopTable(DB *db, const string &name) : AgentDBTable(db, name){
}

void NextHop::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);
    OPER_TRACE(NextHop, info);
}

NextHop::~NextHop() {
    if (id_ != kInvalidIndex) {
        static_cast<NextHopTable *>(get_table())->FreeInterfaceId(id_);
    }
}

void NextHop::FillObjectLog(AgentLogEvent::type event, 
                            NextHopObjectLogInfo &info) const {
    string type_str, policy_str("Disabled"), valid_str("Invalid"), str;
    switch (type_) {
        case NextHop::DISCARD:
            type_str.assign("DISCARD");
            break;

        case NextHop::RECEIVE:
            type_str.assign("RECEIVE");
            break;

        case NextHop::RESOLVE: 
            type_str.assign("RESOLVE");
            break;

        case NextHop::ARP:
            type_str.assign("ARP");
            break;

        case NextHop::INTERFACE:
            type_str.assign("INTERFACE");
            break;

        case NextHop::VRF:
            type_str.assign("VRF");
            break;

        case NextHop::TUNNEL:
            type_str.assign("TUNNEL");
            break;

        case NextHop::MIRROR:
            type_str.assign("MIRROR");
            break;

        case NextHop::VLAN:
            type_str.assign("VLAN");
            break;

        case NextHop::COMPOSITE:
            type_str.assign("COMPOSITE");
            break;

        default:
            type_str.assign("unknown");
    }
    if (policy_) {
        policy_str.assign("Enabled");
    }
    if (valid_) {
        valid_str.assign("Valid");
    }
    switch (event) {
        case AgentLogEvent::ADD:
            str.assign("Addition ");
            break;
        case AgentLogEvent::DELETE:
            str.assign("Deletion ");
            break;
        case AgentLogEvent::CHANGE:
            str.assign("Modification ");
            break;
        case AgentLogEvent::RESYNC:
            str.assign("Resync ");
            break;
        default:
            str.assign("unknown");
    }
    info.set_event(str);
    info.set_type(type_str);
    info.set_policy(policy_str);
    info.set_valid(valid_str);
    info.set_id(id_);
}

void NextHop::FillObjectLogIntf(const Interface *intf, 
                                NextHopObjectLogInfo &info) {
    if (intf) {
        string if_type_str;
        switch(intf->type()) {
        case Interface::VM_INTERFACE:
            if_type_str.assign("VM_INTERFACE");
            break;
        case Interface::PHYSICAL:
            if_type_str.assign("ETH");
            break;
        case Interface::INET:
            if_type_str.assign("VIRTUAL_HOST");
            break;
        case Interface::PACKET:
            if_type_str.assign("PKT");
            break;
        default:
            if_type_str.assign("Invalid");
            break;
        }
        info.set_intf_type(if_type_str);
        info.set_intf_uuid(UuidToString(intf->GetUuid()));
        info.set_intf_name(intf->name());
    }
}

void NextHop::FillObjectLogMac(const unsigned char *m, 
                               NextHopObjectLogInfo &info) {
    char mstr[32];
    snprintf(mstr, 32, "%02x:%02x:%02x:%02x:%02x:%02x", 
             m[0], m[1], m[2], m[3], m[4], m[5]);
    string mac(mstr);
    info.set_mac(mac);
}

std::auto_ptr<DBEntry> NextHopTable::AllocEntry(const DBRequestKey *k) const {
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(AllocWithKey(k)));
}

NextHop *NextHopTable::AllocWithKey(const DBRequestKey *k) const {
    const NextHopKey *key = static_cast<const NextHopKey *>(k);
    return key->AllocEntry();
}

std::auto_ptr<DBEntry> NextHopTable::GetEntry(const DBRequestKey *key) const {
    return std::auto_ptr<DBEntry>(AllocWithKey(key));
}

DBEntry *NextHopTable::Add(const DBRequest *req) {
    const NextHopKey *key = static_cast<const NextHopKey *>(req->key.get());
    NextHop *nh = AllocWithKey(key);

    if (nh->CanAdd() == false) {
        delete nh;
        return NULL;
    }
    nh->set_id(index_table_.Insert(nh));
    nh->Change(req);
    nh->SendObjectLog(AgentLogEvent::ADD);
    return static_cast<DBEntry *>(nh);
}

DBTableBase *NextHopTable::CreateTable(DB *db, const std::string &name) {
    nexthop_table_ = new NextHopTable(db, name);
    nexthop_table_->Init();
    return nexthop_table_;
};

Interface *NextHopTable::FindInterface(const InterfaceKey &key) const {
    return static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
}

VrfEntry *NextHopTable::FindVrfEntry(const VrfKey &key) const {
    return static_cast<VrfEntry *>(Agent::GetInstance()->vrf_table()->FindActiveEntry(&key));
}

void NextHopTable::Process(DBRequest &req) {
    CHECK_CONCURRENCY("db::DBTable");
    DBTablePartition *tpart = 
        static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
    tpart->Process(NULL, &req);
}

void NextHopTable::OnZeroRefcount(AgentDBEntry *e) {
    NextHop *nh = static_cast<NextHop *>(e);

    if (nh->DeleteOnZeroRefCount() == false) {
        return;
    }

    CHECK_CONCURRENCY("db::DBTable");
    nh->OnZeroRefCount();
    
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    req.key = key;
    req.data.reset(NULL);
    Process(req);
}

void NextHop::SetKey(const DBRequestKey *key) {
    const NextHopKey *nh_key = static_cast<const NextHopKey *>(key);
    type_ = nh_key->type_;
    policy_ = nh_key->policy_;
};

/////////////////////////////////////////////////////////////////////////////
// ARP NH routines
/////////////////////////////////////////////////////////////////////////////
NextHop *ArpNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->Find(&vrf_key_, true));
    return new ArpNH(vrf, dip_);
}

bool ArpNH::CanAdd() const {
    if (vrf_ == NULL) {
        LOG(ERROR, "Invalid VRF in ArpNH. Skip Add"); 
        return false;
    }

    return true;
}

bool ArpNH::NextHopIsLess(const DBEntry &rhs) const {
    const ArpNH &a = static_cast<const ArpNH &>(rhs);

    if (vrf_.get() != a.vrf_.get()) {
        return  vrf_.get() < a.vrf_.get();
    }
    
    return (ip_ < a.ip_);
}

void ArpNH::SetKey(const DBRequestKey *k) {
    const ArpNHKey *key = static_cast<const ArpNHKey *>(k);

    NextHop::SetKey(k);
    vrf_ = NextHopTable::GetInstance()->FindVrfEntry(key->vrf_key_);
    ip_ = key->dip_;
}

bool ArpNH::Change(const DBRequest *req) {
    bool ret= false;
    const ArpNHData *data = static_cast<const ArpNHData *>(req->data.get());

    if (!data->valid_) {
        return ret;
    }

    if (valid_ != data->resolved_) {
        valid_ = data->resolved_;
        ret =  true;
    }

    if (data->resolved_ != true) {
        // If ARP is not resolved, interface and mac will be invalid
        interface_ = NULL;
        return ret;
    }

    Interface *interface = NextHopTable::GetInstance()->FindInterface(*data->intf_key_.get());
    if (interface_.get() != interface) {
        interface_ = interface;
        ret = true;
    }

    if (mac_.CompareTo(data->mac_) != 0) {
        mac_ = data->mac_;
        ret = true;
    }

    return ret;
}

const uint32_t ArpNH::vrf_id() const {
    return vrf_->vrf_id();
}

ArpNH::KeyPtr ArpNH::GetDBRequestKey() const {
    NextHopKey *key = new ArpNHKey(vrf_->GetName(), ip_);
    return DBEntryBase::KeyPtr(key);
}

const uuid &ArpNH::GetIfUuid() const {
    return interface_->GetUuid();
}

void ArpNH::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);

    const Interface *intf = GetInterface();
    FillObjectLogIntf(intf, info);

    const VrfEntry *vrf = GetVrf();
    if (vrf) {
        info.set_vrf(vrf->GetName());
    }
    const Ip4Address *ip = GetIp();
    info.set_dest_ip(ip->to_string());

    const unsigned char *m = GetMac().GetData();
    FillObjectLogMac(m, info);

    OPER_TRACE(NextHop, info);
}

/////////////////////////////////////////////////////////////////////////////
// Interface NH routines
/////////////////////////////////////////////////////////////////////////////
NextHop *InterfaceNHKey::AllocEntry() const {
    Interface *intf = static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->Find(intf_key_.get(), true));
    if (intf && intf->IsDeleted() && intf->GetRefCount() == 0) {
        //Ignore interface which are  deleted, and there are no reference to it
        //taking reference on deleted interface with refcount 0, would result
        //in DB state set on deleted interface entry
        intf = NULL;
    }
    return new InterfaceNH(intf, policy_, flags_);
}

bool InterfaceNH::CanAdd() const {
    if (interface_ == NULL) {
        LOG(ERROR, "Invalid Interface in InterfaceNH. Skip Add"); 
        return false;
    }

    return true;
}

bool InterfaceNH::NextHopIsLess(const DBEntry &rhs) const {
    const InterfaceNH &a = static_cast<const InterfaceNH &>(rhs);

    if (interface_.get() != a.interface_.get()) {
        return interface_.get() < a.interface_.get();
    }

    if (policy_ != a.policy_) {
        return policy_ < a.policy_;
    }

    return flags_ < a.flags_;
}

InterfaceNH::KeyPtr InterfaceNH::GetDBRequestKey() const {
    NextHopKey *key =
        new InterfaceNHKey(static_cast<InterfaceKey *>(
                          interface_->GetDBRequestKey().release()),
                           policy_, flags_);
    return DBEntryBase::KeyPtr(key);
}

void InterfaceNH::SetKey(const DBRequestKey *k) {
    const InterfaceNHKey *key = static_cast<const InterfaceNHKey *>(k);

    NextHop::SetKey(k);
    interface_ = NextHopTable::GetInstance()->FindInterface(*key->intf_key_.get());
    flags_ = key->flags_;
}

bool InterfaceNH::Change(const DBRequest *req) {
    const InterfaceNHData *data =
            static_cast<const InterfaceNHData *>(req->data.get());
    bool ret = false;

    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&data->vrf_key_));
    if (vrf_.get() != vrf) {
        vrf_ = vrf;
        ret = true;
    }
    if (dmac_.CompareTo(data->dmac_) != 0) {
        dmac_ = data->dmac_;
        ret = true;
    }
    if (is_multicastNH()) {
        dmac_ = MacAddress::BroadcastMac();
    }

    return ret;
}

const uuid &InterfaceNH::GetIfUuid() const {
    return interface_->GetUuid();
}

static void AddInterfaceNH(const uuid &intf_uuid, const MacAddress &dmac,
                          uint8_t flags, bool policy, const string vrf_name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InterfaceNHKey
                  (new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""),
                   policy, flags));
    req.data.reset(new InterfaceNHData(vrf_name, dmac));
    Agent::GetInstance()->nexthop_table()->Process(req);
}

// Create 3 InterfaceNH for every Vm interface. One with policy another without
// policy, third one is for multicast.
void InterfaceNH::CreateL3VmInterfaceNH(const uuid &intf_uuid,
                                        const MacAddress &dmac,
                                        const string &vrf_name) {
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::INET4, true, vrf_name);
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::INET4, false, vrf_name);
}

void InterfaceNH::DeleteL3InterfaceNH(const uuid &intf_uuid) {
    DeleteNH(intf_uuid, false, InterfaceNHFlags::INET4);
    DeleteNH(intf_uuid, true, InterfaceNHFlags::INET4);
}

void InterfaceNH::CreateL2VmInterfaceNH(const uuid &intf_uuid,
                                        const MacAddress &dmac,
                                        const string &vrf_name) {
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::LAYER2, false, vrf_name);
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::LAYER2, true, vrf_name);
}

void InterfaceNH::DeleteL2InterfaceNH(const uuid &intf_uuid) {
    DeleteNH(intf_uuid, false, InterfaceNHFlags::LAYER2);
    DeleteNH(intf_uuid, true, InterfaceNHFlags::LAYER2);
}

void InterfaceNH::CreateMulticastVmInterfaceNH(const uuid &intf_uuid,
                                               const MacAddress &dmac,
                                               const string &vrf_name) {
    AddInterfaceNH(intf_uuid, dmac, (InterfaceNHFlags::INET4 |
                                     InterfaceNHFlags::MULTICAST), false,
                   vrf_name);
}

void InterfaceNH::DeleteMulticastVmInterfaceNH(const uuid &intf_uuid) {
    DeleteNH(intf_uuid, false, (InterfaceNHFlags::MULTICAST |
                                InterfaceNHFlags::INET4));
}

void InterfaceNH::DeleteNH(const uuid &intf_uuid, bool policy,
                          uint8_t flags) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InterfaceNHKey
                  (new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""),
                   policy, flags));
    req.data.reset(NULL);
    NextHopTable::GetInstance()->Process(req);
}

// Delete the 2 InterfaceNH. One with policy another without policy
void InterfaceNH::DeleteVmInterfaceNHReq(const uuid &intf_uuid) {
    DeleteNH(intf_uuid, false, InterfaceNHFlags::LAYER2);
    DeleteNH(intf_uuid, true, InterfaceNHFlags::LAYER2);
    DeleteNH(intf_uuid, false, InterfaceNHFlags::INET4);
    DeleteNH(intf_uuid, true, InterfaceNHFlags::INET4);
    DeleteNH(intf_uuid, false, InterfaceNHFlags::MULTICAST);
}

void InterfaceNH::CreateInetInterfaceNextHop(const string &ifname,
                                             const string &vrf_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *key = new InterfaceNHKey(new InetInterfaceKey(ifname),
                                         false, InterfaceNHFlags::INET4);
    req.key.reset(key);

    MacAddress mac;
    mac.last_octet() = 1;
    InterfaceNHData *data = new InterfaceNHData(vrf_name, mac);
    req.data.reset(data);
    NextHopTable::GetInstance()->Process(req);
}

void InterfaceNH::DeleteInetInterfaceNextHop(const string &ifname) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    NextHopKey *key = new InterfaceNHKey
        (new InetInterfaceKey(ifname), false,
         InterfaceNHFlags::INET4);
    req.key.reset(key);

    req.data.reset(NULL);
    NextHopTable::GetInstance()->Process(req);
}

void InterfaceNH::CreatePacketInterfaceNh(const string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    MacAddress mac;
    mac.last_octet() = 1;
    req.key.reset(new InterfaceNHKey(new PacketInterfaceKey(nil_uuid(), ifname),
                                     false, InterfaceNHFlags::INET4));
    req.data.reset(new InterfaceNHData(Agent::GetInstance()->fabric_vrf_name(),
                                       mac));
    NextHopTable::GetInstance()->Process(req);
}

void InterfaceNH::DeleteHostPortReq(const string &ifname) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    NextHopKey *key = new InterfaceNHKey(new PacketInterfaceKey(nil_uuid(), ifname),
                                         false, InterfaceNHFlags::INET4);
    req.key.reset(key);

    req.data.reset(NULL);
    NextHopTable::GetInstance()->Enqueue(&req);
}

void InterfaceNH::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);

    const Interface *intf = GetInterface();
    FillObjectLogIntf(intf, info);

    const unsigned char *m = (unsigned char *)GetDMac().GetData();
    FillObjectLogMac(m, info);

    OPER_TRACE(NextHop, info);
}

/////////////////////////////////////////////////////////////////////////////
// VRF NH routines
/////////////////////////////////////////////////////////////////////////////
bool VrfNH::CanAdd() const {
    if (vrf_ == NULL) {
        LOG(ERROR, "Invalid VRF in VrfNH. Skip Add"); 
        return false;
    }

    return true;
}

NextHop *VrfNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->Find(&vrf_key_, true));
    return new VrfNH(vrf, policy_);
}

bool VrfNH::NextHopIsLess(const DBEntry &rhs) const {
    const VrfNH &a = static_cast<const VrfNH &>(rhs);

    if (vrf_.get() != a.vrf_.get()) {
        return (vrf_.get() < a.vrf_.get());
    }

    return policy_ < a.policy_;
}

void VrfNH::SetKey(const DBRequestKey *k) {
    const VrfNHKey *key = static_cast<const VrfNHKey *>(k);
    NextHop::SetKey(k);
    vrf_ = NextHopTable::GetInstance()->FindVrfEntry(key->vrf_key_);
}

VrfNH::KeyPtr VrfNH::GetDBRequestKey() const {
    NextHopKey *key = new VrfNHKey(vrf_->GetName(), false);
    return DBEntryBase::KeyPtr(key);
}

void VrfNH::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;
    FillObjectLog(event, info);

    const VrfEntry *vrf = GetVrf();
    if (vrf) {
        info.set_vrf(vrf->GetName());
    }
    OPER_TRACE(NextHop, info);
}

/////////////////////////////////////////////////////////////////////////////
// Tunnel NH routines
/////////////////////////////////////////////////////////////////////////////
TunnelNH::TunnelNH(VrfEntry *vrf, const Ip4Address &sip, const Ip4Address &dip,
                   bool policy, TunnelType type) :
    NextHop(NextHop::TUNNEL, false, policy), vrf_(vrf), sip_(sip),
    dip_(dip), tunnel_type_(type), arp_rt_(this) {
}

TunnelNH::~TunnelNH() {
}

bool TunnelNH::CanAdd() const {
    if (vrf_ == NULL) {
        LOG(ERROR, "Invalid VRF in TunnelNH. Skip Add"); 
        return false;
    }

    return true;
}

NextHop *TunnelNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->Find(&vrf_key_, true));
    return new TunnelNH(vrf, sip_, dip_, policy_, tunnel_type_);
}

bool TunnelNH::NextHopIsLess(const DBEntry &rhs) const {
    const TunnelNH &a = static_cast<const TunnelNH &>(rhs);

    if (vrf_.get() != a.vrf_.get()) {
        return vrf_.get() < a.vrf_.get();
    }

    if (sip_ != a.sip_) {
        return sip_ < a.sip_;
    }

    if (dip_ != a.dip_) {
        return dip_ < a.dip_;
    }

    if (!tunnel_type_.Compare(a.tunnel_type_)) {
        return tunnel_type_.IsLess(a.tunnel_type_);
    }

    return policy_ < a.policy_;
}

void TunnelNH::SetKey(const DBRequestKey *k) {
    const TunnelNHKey *key = static_cast<const TunnelNHKey *>(k);
    NextHop::SetKey(k);
    vrf_ = NextHopTable::GetInstance()->FindVrfEntry(key->vrf_key_);
    sip_ = key->sip_;
    dip_ = key->dip_;
    tunnel_type_ = key->tunnel_type_;
    policy_ = key->policy_;
}

TunnelNH::KeyPtr TunnelNH::GetDBRequestKey() const {
    NextHopKey *key = new TunnelNHKey(vrf_->GetName(), sip_, dip_, policy_,
                                      tunnel_type_);
    return DBEntryBase::KeyPtr(key);
}

const uint32_t TunnelNH::vrf_id() const {
    return vrf_->vrf_id();
}

bool TunnelNH::Change(const DBRequest *req) {
    bool ret = false;
    bool valid = false;

    InetUnicastAgentRouteTable *rt_table =
        (GetVrf()->GetInet4UnicastRouteTable());
    InetUnicastRouteEntry *rt = rt_table->FindLPM(dip_);
    if (!rt) {
        //No route to reach destination, add to unresolved list
        valid = false;
        rt_table->AddUnresolvedNH(this);
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        //Trigger ARP resolution
        valid = false;
        rt_table->AddUnresolvedNH(this);
        InetUnicastAgentRouteTable::AddArpReq(GetVrf()->GetName(), dip_);
        rt = NULL;
    } else {
        valid = rt->GetActiveNextHop()->IsValid();
    }

    if (valid != valid_) {
        valid_ = valid;
        ret = true;
    }

    arp_rt_.reset(rt);
    ret = true; 

    return ret;
}

void TunnelNH::Delete(const DBRequest *req) {
    InetUnicastAgentRouteTable *rt_table =
        (GetVrf()->GetInet4UnicastRouteTable());
    rt_table->RemoveUnresolvedNH(this);
}

void TunnelNH::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;
    FillObjectLog(event, info);

    const VrfEntry *vrf = GetVrf();
    if (vrf) {
        info.set_vrf(vrf->GetName());
    }
    const Ip4Address *sip = GetSip();
    info.set_source_ip(sip->to_string());
    const Ip4Address *dip = GetDip();
    info.set_dest_ip(dip->to_string());
    info.set_tunnel_type(tunnel_type_.ToString());
    OPER_TRACE(NextHop, info);
}

/////////////////////////////////////////////////////////////////////////////
// Mirror NH routines
/////////////////////////////////////////////////////////////////////////////
bool MirrorNH::CanAdd() const {
    return true;
}

NextHop *MirrorNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->Find(&vrf_key_, true));
    return new MirrorNH(vrf, sip_, sport_, dip_, dport_);
}

bool MirrorNH::NextHopIsLess(const DBEntry &rhs) const {
    const MirrorNH &a = static_cast<const MirrorNH &>(rhs);

    if ((vrf_.get() != a.vrf_.get())) {
        return vrf_.get() < a.vrf_.get();
    }

    if (dip_ != a.dip_) {
        return dip_ < a.dip_;
    }

    return (dport_ < a.dport_);
}

const uint32_t MirrorNH::vrf_id() const {
    return (vrf_ ? vrf_->vrf_id() : (uint32_t)-1);
}

void MirrorNH::SetKey(const DBRequestKey *k) {
    const MirrorNHKey *key = static_cast<const MirrorNHKey *>(k);
    NextHop::SetKey(k);
    vrf_ = NextHopTable::GetInstance()->FindVrfEntry(key->vrf_key_);
    sip_ = key->sip_;
    sport_ = key->sport_;
    dip_ = key->dip_;
    dport_ = key->dport_;
}

MirrorNH::KeyPtr MirrorNH::GetDBRequestKey() const {
    NextHopKey *key = new MirrorNHKey((vrf_ ? vrf_->GetName() : ""),
                                      sip_, sport_, dip_, dport_);
    return DBEntryBase::KeyPtr(key);
}

bool MirrorNH::Change(const DBRequest *req) {
    bool ret = false;
    bool valid = false;

    if (GetVrf() == NULL) {
        valid_ = true;
        return true;
    }
    InetUnicastAgentRouteTable *rt_table =
        (GetVrf()->GetInet4UnicastRouteTable());
    InetUnicastRouteEntry *rt = rt_table->FindLPM(dip_);
    if (!rt) {
        //No route to reach destination, add to unresolved list
        valid = false;
        rt_table->AddUnresolvedNH(this);
    } else if ((rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) &&
               (GetVrf()->GetName() == Agent::GetInstance()->fabric_vrf_name())) {
        //Trigger ARP resolution
        valid = false;
        rt_table->AddUnresolvedNH(this);
        InetUnicastAgentRouteTable::AddArpReq(GetVrf()->GetName(), dip_);
        rt = NULL;
    } else {
        valid = rt->GetActiveNextHop()->IsValid();
    }

    if (valid != valid_) {
        valid_ = valid;
        ret = true;
    }

    arp_rt_.reset(rt);
    ret = true; 

    return ret;
}

void MirrorNH::Delete(const DBRequest *req) {
    if (!GetVrf()) {
        return;
    }
    InetUnicastAgentRouteTable *rt_table =
        (GetVrf()->GetInet4UnicastRouteTable());
    rt_table->RemoveUnresolvedNH(this);
}

void MirrorNH::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);

    const VrfEntry *vrf = GetVrf();
    if (vrf) {
        info.set_vrf(vrf->GetName());
    }
    const Ip4Address *sip = GetSip();
    info.set_source_ip(sip->to_string());
    const Ip4Address *dip = GetDip();
    info.set_dest_ip(dip->to_string());
    info.set_source_port((short int)GetSPort());
    info.set_dest_port((short int)GetDPort());
    OPER_TRACE(NextHop, info);
}

/////////////////////////////////////////////////////////////////////////////
// ReceiveNH routines
/////////////////////////////////////////////////////////////////////////////
bool ReceiveNH::CanAdd() const {
    if (interface_ == NULL) {
        LOG(ERROR, "Invalid Interface in ReceiveNH. Skip Add"); 
        return false;
    }

    return true;
}

NextHop *ReceiveNHKey::AllocEntry() const {
    Interface *intf = static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->Find(intf_key_.get(), true));
    if (intf && intf->IsDeleted() && intf->GetRefCount() == 0) {
        // Ignore interface which are  deleted, and there are no reference to it
        // taking reference on deleted interface with refcount 0, would result 
        // in DB state set on deleted interface entry
        intf = NULL;
    }
    return new ReceiveNH(intf, policy_);
}

void ReceiveNH::SetKey(const DBRequestKey *key) {
    const ReceiveNHKey *nh_key = static_cast<const ReceiveNHKey *>(key);
    NextHop::SetKey(key);
    interface_ = NextHopTable::GetInstance()->FindInterface(*nh_key->intf_key_.get());
};

// Create 2 ReceiveNH for every VPort. One with policy another without 
// policy
void ReceiveNH::Create(NextHopTable *table, const string &interface) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    InetInterfaceKey *intf_key = new InetInterfaceKey(interface);
    req.key.reset(new ReceiveNHKey(intf_key, false));
    req.data.reset(new ReceiveNHData());
    table->Process(req);

    intf_key = new InetInterfaceKey(interface);
    req.key.reset(new ReceiveNHKey(intf_key, true));
    table->Process(req);
}

void ReceiveNH::Delete(NextHopTable *table, const string &interface) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    InetInterfaceKey *intf_key = new InetInterfaceKey(interface);
    req.key.reset(new ReceiveNHKey(intf_key, false));
    req.data.reset(NULL);
    table->Process(req);

    intf_key = new InetInterfaceKey(interface);
    req.key.reset(new ReceiveNHKey(intf_key, true));
    table->Process(req);
}


void ReceiveNH::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);

    const Interface *intf = GetInterface();
    FillObjectLogIntf(intf, info);

    OPER_TRACE(NextHop, info);
}

/////////////////////////////////////////////////////////////////////////////
// ResolveNH routines
/////////////////////////////////////////////////////////////////////////////
NextHop *ResolveNHKey::AllocEntry() const {
    return new ResolveNH();
}

bool ResolveNH::CanAdd() const {
    return true;
}

void ResolveNH::Create( ) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new ResolveNHKey());
    req.data.reset(new ResolveNHData());
    NextHopTable::GetInstance()->Process(req);
}

/////////////////////////////////////////////////////////////////////////////
// DiscardNH routines
/////////////////////////////////////////////////////////////////////////////
NextHop *DiscardNHKey::AllocEntry() const {
    return new DiscardNH();
}

bool DiscardNH::CanAdd() const {
    return true;
}

void DiscardNH::Create( ) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new DiscardNHKey());
    req.data.reset(new DiscardNHData());
    NextHopTable::GetInstance()->Process(req);
}

/////////////////////////////////////////////////////////////////////////////
// VLAN NH routines
/////////////////////////////////////////////////////////////////////////////
bool VlanNH::CanAdd() const {
    if (interface_ == NULL) {
        LOG(ERROR, "Invalid Interface in VlanNH. Skip Add"); 
        return false;
    }

    return true;
}

NextHop *VlanNHKey::AllocEntry() const {
    Interface *intf = static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->Find(intf_key_.get(), true));
    return new VlanNH(intf, vlan_tag_);
}

bool VlanNH::NextHopIsLess(const DBEntry &rhs) const {
    const VlanNH &a = static_cast<const VlanNH &>(rhs);

    if (interface_.get() != a.interface_.get()) {
        return interface_.get() < a.interface_.get();
    }

    return vlan_tag_ < a.vlan_tag_;
}

VlanNH::KeyPtr VlanNH::GetDBRequestKey() const {
    VlanNHKey *key = new VlanNHKey(interface_->GetUuid(), vlan_tag_);
    return DBEntryBase::KeyPtr(key);
}

void VlanNH::SetKey(const DBRequestKey *k) {
    const VlanNHKey *key = static_cast<const VlanNHKey *>(k);

    NextHop::SetKey(k);
    interface_ = NextHopTable::GetInstance()->FindInterface(*key->intf_key_.get());
    vlan_tag_ = key->vlan_tag_;
}

bool VlanNH::Change(const DBRequest *req) {
    const VlanNHData *data = static_cast<const VlanNHData *>(req->data.get());
    bool ret = false;

    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&data->vrf_key_));
    if (vrf_.get() != vrf) {
        vrf_ = vrf;
        ret = true;
    }

    if (smac_.CompareTo(data->smac_) != 0) {
        smac_ = data->smac_;
        ret = true;
    }

    if (dmac_.CompareTo(data->dmac_) != 0) {
        dmac_ = data->dmac_;
        ret = true;
    }

    return ret;
}

const uuid &VlanNH::GetIfUuid() const {
    return interface_->GetUuid();
}

// Create VlanNH for a VPort
void VlanNH::Create(const uuid &intf_uuid, uint16_t vlan_tag,
                    const string &vrf_name, const MacAddress &smac,
                    const MacAddress &dmac) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *key = new VlanNHKey(intf_uuid, vlan_tag);
    req.key.reset(key);

    VlanNHData *data = new VlanNHData(vrf_name, smac, dmac);
    req.data.reset(data);
    NextHopTable::GetInstance()->Process(req);
}

void VlanNH::Delete(const uuid &intf_uuid, uint16_t vlan_tag) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    NextHopKey *key = new VlanNHKey(intf_uuid, vlan_tag);
    req.key.reset(key);

    req.data.reset(NULL);
    NextHopTable::GetInstance()->Process(req);
}

// Create VlanNH for a VPort
void VlanNH::CreateReq(const uuid &intf_uuid, uint16_t vlan_tag,
                    const string &vrf_name, const MacAddress &smac,
                    const MacAddress &dmac) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VlanNHKey(intf_uuid, vlan_tag));
    req.data.reset(new VlanNHData(vrf_name, smac, dmac));
    NextHopTable::GetInstance()->Enqueue(&req);
}

void VlanNH::DeleteReq(const uuid &intf_uuid, uint16_t vlan_tag) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    NextHopKey *key = new VlanNHKey(intf_uuid, vlan_tag);
    req.key.reset(key);

    req.data.reset(NULL);
    NextHopTable::GetInstance()->Enqueue(&req);
}


VlanNH *VlanNH::Find(const uuid &intf_uuid, uint16_t vlan_tag) {
    VlanNHKey key(intf_uuid, vlan_tag);
    return static_cast<VlanNH *>(NextHopTable::GetInstance()->FindActiveEntry(&key));
}

void VlanNH::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);

    const Interface *intf = GetInterface();
    FillObjectLogIntf(intf, info);

    const unsigned char *m = GetDMac().GetData();
    FillObjectLogMac(m, info);

    info.set_vlan_tag((short int)GetVlanTag());
    OPER_TRACE(NextHop, info);
}

/////////////////////////////////////////////////////////////////////////////
// CompositeNH routines
/////////////////////////////////////////////////////////////////////////////
bool CompositeNH::CanAdd() const {
    if (vrf_ == NULL || vrf_->IsDeleted()) {
        LOG(ERROR, "Invalid VRF in composite NH. Skip Add");
        return false;
    }
    return true;
}

const NextHop* CompositeNH::GetLocalNextHop() const {
    ComponentNHList::const_iterator comp_nh_it =
        component_nh_list_.begin();
    for(;comp_nh_it != component_nh_list_.end(); comp_nh_it++) {
        if ((*comp_nh_it) == NULL) {
            continue;
        }

        if ((*comp_nh_it)->nh()->GetType() != NextHop::TUNNEL) {
            return (*comp_nh_it)->nh();
        }
    }
    return NULL;
}

NextHop *CompositeNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->Find(&vrf_key_, true));
    return new CompositeNH(composite_nh_type_, policy_,
                           component_nh_key_list_, vrf);
}

void CompositeNHKey::ChangeTunnelType(TunnelType::Type tunnel_type) {
    ComponentNHKeyList::iterator it = component_nh_key_list_.begin();
    for (;it != component_nh_key_list_.end(); it++) {
        if ((*it) == NULL) {
            continue;
        }
        if ((*it)->nh_key()->GetType() == NextHop::TUNNEL) {
            TunnelNHKey *tunnel_nh_key =
                static_cast<TunnelNHKey *>((*it)->nh_key()->Clone());
            tunnel_nh_key->set_tunnel_type(tunnel_type);
            std::auto_ptr<const NextHopKey> nh_key(tunnel_nh_key);
            ComponentNHKeyPtr new_tunnel_nh(new ComponentNHKey((*it)->label(),
                                                               nh_key));
            (*it) = new_tunnel_nh;
        }
    }
}

bool CompositeNH::Change(const DBRequest* req) {
    ComponentNHList component_nh_list;
    ComponentNHKeyList::const_iterator it = component_nh_key_list_.begin();
    for (;it != component_nh_key_list_.end(); it++) {
        if ((*it) == NULL) {
            ComponentNHPtr nh_key;
            nh_key.reset();
            component_nh_list.push_back(nh_key);
            continue;
        }

        const NextHop *nh = static_cast<const NextHop *>
            (NextHopTable::GetInstance()->FindActiveEntry((*it)->nh_key()));
        if (nh) {
            ComponentNHPtr nh_key(new ComponentNH((*it)->label(), nh));
            component_nh_list.push_back(nh_key);
        } else {
            //Nexthop not active
            //Insert a empty entry
            ComponentNHPtr nh_key;
            nh_key.reset();
            component_nh_list.push_back(nh_key);
        }
    }

    bool changed = false;
    //Check if new list and old list are same
    ComponentNHList::const_iterator new_comp_nh_it =
        component_nh_list.begin();
    ComponentNHList::const_iterator old_comp_nh_it =
       component_nh_list_.begin();
    for(;new_comp_nh_it != component_nh_list.end() &&
         old_comp_nh_it != component_nh_list_.end();
         new_comp_nh_it++, old_comp_nh_it++) {
        //Check if both component NH are NULL
        if ((*old_comp_nh_it) == NULL &&
            (*new_comp_nh_it) == NULL) {
            continue;
        }

        //check if one of the component NH is NULL
        if ((*old_comp_nh_it) == NULL || (*new_comp_nh_it) == NULL) {
            changed = true;
            break;
        }

        //Check if component NH are same
        if ((**old_comp_nh_it) == (**new_comp_nh_it)) {
            continue;
        }

        changed = true;
        break;
    }

    if (new_comp_nh_it == component_nh_list.end() &&
        old_comp_nh_it == component_nh_list_.end()) {
        changed = false;
    } else {
        changed = true;
    }

    component_nh_list_ = component_nh_list;
    return changed;
}

void CompositeNH::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;
    FillObjectLog(event, info);

    const VrfEntry *vrf_entry = vrf();
    if (vrf_entry) {
        info.set_vrf(vrf_entry->GetName());
    }

    std::vector<ComponentNHLogInfo> comp_nh_log_list;
    ComponentNHList::const_iterator component_nh_it = begin();
    for (;component_nh_it != end(); component_nh_it++) {
        ComponentNHLogInfo component_nh_info;
        const ComponentNH *comp_nh = (*component_nh_it).get();
        if (comp_nh == NULL) {
            continue;
        }
        const NextHop *nh = comp_nh->nh();
        component_nh_info.set_component_nh_id(nh->id());
        switch(nh->GetType()) {
        case TUNNEL: {
            const TunnelNH *tun_nh = static_cast<const TunnelNH *>(nh);
            component_nh_info.set_type("Tunnel");
            component_nh_info.set_label(comp_nh->label());
            component_nh_info.set_server_ip(tun_nh->GetDip()->to_string());
            break;
        }    

        case INTERFACE: {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            component_nh_info.set_type("Interface");
            component_nh_info.set_label(comp_nh->label());
            const Interface *intf =
                static_cast<const Interface *>(intf_nh->GetInterface());
            component_nh_info.set_intf_name(intf->name());
            break;
        }

        case VLAN: {
           const VlanNH *vlan_nh = static_cast<const VlanNH *>(nh);
            component_nh_info.set_type("Vlan");
            component_nh_info.set_label(comp_nh->label());
            const Interface *intf = 
                static_cast<const Interface *>(vlan_nh->GetInterface());
            component_nh_info.set_intf_name(intf->name());
            break;
        }

        case COMPOSITE: {
            const CompositeNH *cnh = static_cast<const CompositeNH *>(nh);
            std::stringstream str;
            str << "Composite; Type: " << cnh->composite_nh_type() <<
                   " comp_nh_count" << cnh->ComponentNHCount();
            component_nh_info.set_type(str.str());
            break;
        }
        default:
            break;
        }
        comp_nh_log_list.push_back(component_nh_info);
    }

    info.set_nh_list(comp_nh_log_list);
    OPER_TRACE(NextHop, info);
}

//Key for composite NH is list of component NH
//Some of the component NH may be NULL, in case of ECMP, as deletion of
//component NH resulting in addition of invalid component NH at that location,
//so that kernel can trap packet hitting such component NH
void CompositeNH::SetKey(const DBRequestKey *k) {
    const CompositeNHKey *key = static_cast<const CompositeNHKey *>(k);
    NextHop::SetKey(k);
    composite_nh_type_ = key->composite_nh_type_;
    component_nh_key_list_ = key->component_nh_key_list_;
}

bool CompositeNH::NextHopIsLess(const DBEntry &rhs_db) const {
    const CompositeNH &rhs = static_cast<const CompositeNH &>(rhs_db);
    if (composite_nh_type_ != rhs.composite_nh_type_) {
        return composite_nh_type_ < rhs.composite_nh_type_;
    }

    if (vrf_ != rhs.vrf_) {
        return vrf_ < rhs.vrf_;
    }

    //Parse thought indivial key entries and compare if they are same
    ComponentNHKeyList::const_iterator left_component_nh_it =
        component_nh_key_list_.begin();
    ComponentNHKeyList::const_iterator right_component_nh_it =
        rhs.component_nh_key_list_.begin();

    for (;left_component_nh_it != component_nh_key_list_.end() &&
          right_component_nh_it != rhs.component_nh_key_list_.end();
          left_component_nh_it++, right_component_nh_it++) {
        //If both component NH are empty, nothing to compare
        if (*left_component_nh_it == NULL &&
            *right_component_nh_it == NULL) {
            continue;
        }
        //One of the component NH is NULL
        if ((*left_component_nh_it) == NULL ||
            (*right_component_nh_it) == NULL) {
            return (*left_component_nh_it) < (*right_component_nh_it);
        }

        //Check if the label is different
        if ((*left_component_nh_it)->label() !=
            (*right_component_nh_it)->label()) {
            return (*left_component_nh_it)->label() <
                   (*right_component_nh_it)->label();
        }

        //Check if the nexthop key is different
        //Ideally we could find the nexthop and compare pointer alone
        //it wont work because this is called from Find context itself,
        //and it would result in deadlock
        //Hence compare nexthop key alone
        const NextHopKey *left_nh = (*left_component_nh_it)->nh_key();
        const NextHopKey *right_nh = (*right_component_nh_it)->nh_key();

        if (left_nh->IsEqual(*right_nh) == false) {
            return left_nh->IsLess(*right_nh);
        }
    }

    //Both composite nexthop are same
    if (left_component_nh_it == component_nh_key_list_.end() &&
        right_component_nh_it == rhs.component_nh_key_list_.end()) {
        return false;
    }

    //Right composite nexthop entry has more entries, hence
    //left composite nexthop is lesser then right composite nh
    if (left_component_nh_it == component_nh_key_list_.end()) {
        return true;
    }
    return false;
}

CompositeNH::KeyPtr CompositeNH::GetDBRequestKey() const {
    ComponentNHKeyList component_nh_key_list;
    component_nh_key_list = component_nh_key_list_;
    NextHopKey *key = new CompositeNHKey(composite_nh_type_, policy_,
                                         component_nh_key_list,
                                         vrf_->GetName());
    return DBEntryBase::KeyPtr(key);
}

void CompositeNH::Delete(const DBRequest* req) {
    component_nh_list_.clear();
}

void CompositeNH::CreateComponentNH(Agent *agent,
                                    TunnelType::Type type) const {
    //Create all component NH
    for (ComponentNHList::const_iterator it = component_nh_list_.begin();
         it != component_nh_list_.end(); it++) {
        if ((*it) == NULL) {
            continue;
        }
        const NextHop *nh = (*it)->nh();
        switch (nh->GetType()) {
        case NextHop::TUNNEL: {
            const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
            if (type != tnh->GetTunnelType().GetType()) {
                DBRequest tnh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
                tnh_req.key.reset(new TunnelNHKey(tnh->GetVrf()->GetName(),
                                                  *(tnh->GetSip()),
                                                  *(tnh->GetDip()),
                                                  tnh->PolicyEnabled(),
                                                  type));
                tnh_req.data.reset(new TunnelNHData());
                agent->nexthop_table()->Process(tnh_req);
            }
            break;
        }
        case NextHop::COMPOSITE: {
            const CompositeNH *cnh =
                static_cast<const CompositeNH *>(nh);
            //Create new composite NH
            cnh->ChangeTunnelType(agent, type);
            break;
        }
        default: {
            break;
        }
        }
    }
}

//Changes the component NH key list to contain new NH keys as per new
//tunnel type
void CompositeNH::ChangeComponentNHKeyTunnelType(
        ComponentNHKeyList &component_nh_key_list, TunnelType::Type type) const {

    ComponentNHKeyList::iterator it = component_nh_key_list.begin();
    for (;it != component_nh_key_list.end(); it++) {
        if ((*it) == NULL) {
            continue;
        }

        if ((*it)->nh_key()->GetType() == NextHop::COMPOSITE) {
            CompositeNHKey *composite_nh_key =
                static_cast<CompositeNHKey *>((*it)->nh_key()->Clone());
            ChangeComponentNHKeyTunnelType(
                    composite_nh_key->component_nh_key_list_, type);
            std::auto_ptr<const NextHopKey> nh_key(composite_nh_key);
            ComponentNHKeyPtr new_comp_nh(new ComponentNHKey((*it)->label(),
                                                             nh_key));
            (*it) = new_comp_nh;
        }

        if ((*it)->nh_key()->GetType() == NextHop::TUNNEL) {
            TunnelNHKey *tunnel_nh_key =
                static_cast<TunnelNHKey *>((*it)->nh_key()->Clone());
            tunnel_nh_key->set_tunnel_type(type);
            std::auto_ptr<const NextHopKey> nh_key(tunnel_nh_key);
            ComponentNHKeyPtr new_tunnel_nh(new ComponentNHKey((*it)->label(),
                                                               nh_key));
            (*it) = new_tunnel_nh;
        }
    }
}

//This API recursively goes thru composite NH and creates
//all the component NH upon tunnel type change
//CreateComponentNH() API which creates new tunnel NH and composite NH,
//would call ChangeTunnelType() API which would result in recursion
CompositeNH *CompositeNH::ChangeTunnelType(Agent *agent,
                                           TunnelType::Type type) const {
    //Create all component NH with new tunnel type
    CreateComponentNH(agent, type);

    //Change the tunnel type of all component NH key
    ComponentNHKeyList new_component_nh_key_list = component_nh_key_list_;
    ChangeComponentNHKeyTunnelType(new_component_nh_key_list, type);

    //Create the new nexthop
    CompositeNHKey *comp_nh_key = new CompositeNHKey(composite_nh_type_,
                                                     policy_,
                                                     new_component_nh_key_list,
                                                     vrf_->GetName());
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(comp_nh_key);
    nh_req.data.reset(new CompositeNHData());
    agent->nexthop_table()->Process(nh_req);

    CompositeNH *comp_nh = static_cast<CompositeNH *>(
            agent->nexthop_table()->FindActiveEntry(comp_nh_key));
    assert(comp_nh);
    return comp_nh;
}

bool CompositeNH::GetIndex(ComponentNH &component_nh, uint32_t &idx) const {
    idx = 0;
    BOOST_FOREACH(ComponentNHPtr it, component_nh_list_) {
        if (it.get() == NULL) {
            idx++;
            continue;
        }

        if (it->nh() == component_nh.nh() &&
            it->label() == component_nh.label()) {
            return true;
        }
        idx++;
    }
    return false;
}

uint32_t CompositeNH::GetRemoteLabel(const Ip4Address &ip) const {
    BOOST_FOREACH(ComponentNHPtr component_nh,
                  component_nh_list_) {
        if (component_nh.get() == NULL) {
            continue;
        }
        const NextHop *nh = component_nh->nh();
        if (nh->GetType() != NextHop::TUNNEL) {
            continue;
        }
        const TunnelNH *tun_nh = static_cast<const TunnelNH *>(nh);
        if (*(tun_nh->GetDip()) == ip) {
            return component_nh->label();
        }
    }
    return -1;
}

void CompositeNHKey::CreateTunnelNH(Agent *agent) {
    BOOST_FOREACH(ComponentNHKeyPtr component_nh_key, component_nh_key_list_) {
        if (component_nh_key.get() &&
                component_nh_key->nh_key()->GetType() == NextHop::TUNNEL) {
            DBRequest req;
            // First enqueue request to create Tunnel NH
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            req.key.reset(component_nh_key->nh_key()->Clone());
            TunnelNHData *data = new TunnelNHData();
            req.data.reset(data);
            agent->nexthop_table()->Process(req);
        }
    }
}

void CompositeNHKey::CreateTunnelNHReq(Agent *agent) {
    BOOST_FOREACH(ComponentNHKeyPtr component_nh_key, component_nh_key_list_) {
        if (component_nh_key.get() &&
                component_nh_key->nh_key()->GetType() == NextHop::TUNNEL) {
            DBRequest req;
            // First enqueue request to create Tunnel NH
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            req.key.reset(component_nh_key->nh_key()->Clone());
            TunnelNHData *data = new TunnelNHData();
            req.data.reset(data);
            agent->nexthop_table()->Enqueue(&req);
        }
    }
}

CompositeNHKey* CompositeNHKey::Clone() const {
    return new CompositeNHKey(composite_nh_type_, policy_,
                              component_nh_key_list_, vrf_key_.name_);
}

bool CompositeNHKey::find(ComponentNHKeyPtr new_component_nh_key) {
    BOOST_FOREACH(ComponentNHKeyPtr component_nh_key,
                  component_nh_key_list_) {
        if (component_nh_key == NULL) {
            continue;
        }
        if (*component_nh_key == *new_component_nh_key) {
            return true;
        }
    }
    return false;
}

void CompositeNHKey::insert(ComponentNHKeyPtr new_component_nh_key) {
    if (new_component_nh_key == NULL) {
        component_nh_key_list_.push_back(new_component_nh_key);
        return;
    }

    if (find(new_component_nh_key)) {
        return;
    }

    ComponentNHKeyList::iterator it;
    for (it = component_nh_key_list_.begin();
         it != component_nh_key_list_.end(); it++) {
        //Insert at empty spot
        if ((*it) == NULL) {
            *it = new_component_nh_key;
            return;
        }
    }
    component_nh_key_list_.push_back(new_component_nh_key);
}

void CompositeNHKey::erase(ComponentNHKeyPtr nh_key) {
    ComponentNHKeyList::iterator it;
    for (it = component_nh_key_list_.begin();
         it != component_nh_key_list_.end(); it++) {
        if ((*it) == NULL) {
            continue;
        }
        //Find the nexthop and compare
        if (**it == *nh_key) {
            (*it).reset();
            return;
        }
    }
}

ComponentNHKeyList CompositeNH::AddComponentNHKey(ComponentNHKeyPtr cnh) const {
    Agent *agent = static_cast<NextHopTable *>(get_table())->agent();
    const NextHop *nh = static_cast<const NextHop *>(agent->nexthop_table()->
                                       FindActiveEntry(cnh->nh_key()));
    assert(nh);

    ComponentNHKeyList component_nh_key_list = component_nh_key_list_;
    ComponentNHList::const_iterator it = begin();
    //Make sure new entry is not already present
    for (;it != end(); it++) {
        if((*it) && (*it)->label() == cnh->label() && (*it)->nh() == nh) {
            //Entry already present, return old component nh key list
            return component_nh_key_list;
        }
    }

    bool inserted = false;
    ComponentNHKeyList::const_iterator key_it = component_nh_key_list.begin();
    for (;key_it != component_nh_key_list.end(); key_it++) {
        //If there is a empty slot, in
        //component key list insert the element there.
        if ((*key_it) == NULL) {
            component_nh_key_list.push_back(cnh);
            inserted = true;
            break;
        }
    }

    //No empty slots found, insert entry at last
    if (inserted == false) {
        component_nh_key_list.push_back(cnh);
    }
    return component_nh_key_list;
}

ComponentNHKeyList
CompositeNH::DeleteComponentNHKey(ComponentNHKeyPtr cnh) const {
    Agent *agent = static_cast<NextHopTable *>(get_table())->agent();
    const NextHop *nh = static_cast<const NextHop *>(agent->nexthop_table()->
                                       FindActiveEntry(cnh->nh_key()));
    assert(nh);

    ComponentNHKeyList component_nh_key_list = component_nh_key_list_;
    ComponentNHKeyPtr component_nh_key;
    ComponentNHList::const_iterator it = begin();
    int index = 0;
    for (;it != end(); it++, index++) {
        ComponentNHKeyPtr dummy_ptr;
        dummy_ptr.reset();
        if ((*it) && ((*it)->label() == cnh->label() && (*it)->nh() == nh)) {
            component_nh_key_list[index] = dummy_ptr;
            break;
        }
    }
    return component_nh_key_list;
}

bool CompositeNHKey::NextHopKeyIsLess(const NextHopKey &rhs) const {
    const CompositeNHKey *comp_rhs = static_cast<const CompositeNHKey *>(&rhs);
    if (vrf_key_.name_ != comp_rhs->vrf_key_.name_) {
        return vrf_key_.name_ < comp_rhs->vrf_key_.name_;
    }

    if (composite_nh_type_ != comp_rhs->composite_nh_type_) {
        return composite_nh_type_ < comp_rhs->composite_nh_type_;
    }

    ComponentNHKeyList::const_iterator key_it = begin();
    ComponentNHKeyList::const_iterator rhs_key_it = comp_rhs->begin();
    for (;key_it != end() && rhs_key_it != comp_rhs->end();
          key_it++, rhs_key_it++) {
        const ComponentNHKey *lhs_component_nh_ptr = (*key_it).get();
        const ComponentNHKey *rhs_component_nh_ptr = (*rhs_key_it).get();
        if (lhs_component_nh_ptr == NULL &&
            rhs_component_nh_ptr == NULL) {
            continue;
        }

        if (lhs_component_nh_ptr == NULL ||
            rhs_component_nh_ptr == NULL) {
            return lhs_component_nh_ptr < rhs_component_nh_ptr;
        }

        if (lhs_component_nh_ptr->label() !=
            rhs_component_nh_ptr->label()) {
            return lhs_component_nh_ptr->label() < rhs_component_nh_ptr->label();
        }

        const NextHopKey *left_nh_key = lhs_component_nh_ptr->nh_key();
        const NextHopKey *right_nh_key = rhs_component_nh_ptr->nh_key();
        if (left_nh_key->IsEqual(*right_nh_key) == false) {
            if (left_nh_key->GetType() != right_nh_key->GetType()) {
                return left_nh_key->GetType() < right_nh_key->GetType();
            }
            return left_nh_key->IsLess(*right_nh_key);
        }
    }

    if (key_it == end() && rhs_key_it == comp_rhs->end()) {
        return false;
    }

    if (key_it == end()) {
        return true;
    }
    return false;
}

void CompositeNHKey::ExpandLocalCompositeNH(Agent *agent) {
    uint32_t label = MplsTable::kInvalidLabel;
    //Find local composite ecmp label
    BOOST_FOREACH(ComponentNHKeyPtr component_nh_key,
                  component_nh_key_list_) {
        if (component_nh_key.get() &&
                component_nh_key->nh_key()->GetType() == NextHop::COMPOSITE) {
            label = component_nh_key->label();
            //Erase the entry from list, it will be replaced with
            //individual entries of this local composite NH
            erase(component_nh_key);
            break;
        }
    }

     //No Local composite NH found
    if (label ==  MplsTable::kInvalidLabel) {
        return;
    }

    MplsLabel *mpls = agent->mpls_table()->FindMplsLabel(label);
    if (mpls == NULL) {
        return;
    }

    DBEntryBase::KeyPtr key = mpls->nexthop()->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
    assert(nh_key->GetType() == NextHop::COMPOSITE);
    CompositeNHKey *local_composite_nh_key =
        static_cast<CompositeNHKey *>(nh_key);
    //Insert individual entries
    BOOST_FOREACH(ComponentNHKeyPtr component_nh_key,
                  local_composite_nh_key->component_nh_key_list()) {
        insert(component_nh_key);
    }
}

void CompositeNHKey::Reorder(Agent *agent,
                             uint32_t label, const NextHop *nh) {
    //Enqueue request to create Tunnel NH
    CreateTunnelNH(agent);
    //First expand local composite NH, if any
    ExpandLocalCompositeNH(agent);
    //Order the component NH entries, so that previous position of
    //component NH are maintained.
    //For example, if previous composite NH consisted of A, B and C
    //as component NH, and the new array of component NH is B, A and C
    //or any combination of the three entries, the result should be A, B and C
    //only, so that previous position are mainatined.
    //If the new key list is C and A, then the end result would be A <NULL> C,
    //so that A and C component NH position are maintained
    //
    //Example 2
    //Let old component NH member be A, B, C
    //And new component NH member be D, A, B, C in any of 24 combination
    //Then new composite NH has to be A, B, C, D in that order, such that
    //A, B, C nexthop retain there position
    if (!nh) {
        return;
    }

    if (nh->GetType() != NextHop::COMPOSITE) {
        DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
        NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
        std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
        //Insert exisiting nexthop at first slot
        //This ensures that old flows are not disturbed
        ComponentNHKeyPtr component_nh_key(new ComponentNHKey(label,
                                                              nh_key_ptr));
        if (find(component_nh_key)) {
            //Swap first entry and previous nexthop which
            //route would have been pointing to
            ComponentNHKeyPtr first_entry = component_nh_key_list_[0];
            erase(first_entry);
            erase(component_nh_key);
            insert(component_nh_key);
            insert(first_entry);
        }
        return;
    }

    CompositeNHKey *composite_nh_key;
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    composite_nh_key = static_cast<CompositeNHKey *>(key.get());
    //Delete entries not present in the new composite NH key
    BOOST_FOREACH(ComponentNHKeyPtr component_nh_key,
                  composite_nh_key->component_nh_key_list()) {
        if (component_nh_key != NULL &&
                find(component_nh_key) == false) {
            composite_nh_key->erase(component_nh_key);
        }
    }

    //Add new entries
    BOOST_FOREACH(ComponentNHKeyPtr component_nh_key,
                  component_nh_key_list()) {
        if (component_nh_key != NULL) {
            composite_nh_key->insert(component_nh_key);
        }
    }
    //Copy over the list
    component_nh_key_list_ = composite_nh_key->component_nh_key_list();
}

ComponentNHKey::ComponentNHKey(int label, Composite::Type type, bool policy,
    const ComponentNHKeyList &component_nh_list, const std::string &vrf_name):
    label_(label), nh_key_(new CompositeNHKey(type, policy, component_nh_list,
    vrf_name)) {
}

/////////////////////////////////////////////////////////////////////////////
// NextHop Sandesh routines
/////////////////////////////////////////////////////////////////////////////
static void FillComponentNextHop(const CompositeNH *comp_nh,
                                 std::vector<McastData> &list) 
{
    for (ComponentNHList::const_iterator it = comp_nh->begin();
         it != comp_nh->end(); it++) {
        const ComponentNH *component_nh = (*it).get();
        McastData sdata;
        if (component_nh == NULL) {
            sdata.set_type("NULL");
            list.push_back(sdata);
            continue;
        }
        switch (component_nh->nh()->GetType()) {
        case NextHop::INTERFACE: {
            sdata.set_type("Interface");
            const InterfaceNH *sub_nh = 
                static_cast<const InterfaceNH *>(component_nh->nh());
            if (sub_nh && sub_nh->GetInterface())
                sdata.set_label(component_nh->label());
            sdata.set_itf(sub_nh->GetInterface()->name());
            list.push_back(sdata);
            break;
        }
        case NextHop::TUNNEL: {
            sdata.set_type("Tunnel");
            const TunnelNH *tnh = 
                static_cast<const TunnelNH *>(component_nh->nh());
            sdata.set_dip(tnh->GetDip()->to_string());
            sdata.set_sip(tnh->GetSip()->to_string());
            sdata.set_label(component_nh->label());
            list.push_back(sdata);
            break;
        }
        case NextHop::VLAN: {
            sdata.set_type("Vlan");
            const VlanNH *vlan_nh = 
                static_cast<const VlanNH *>(component_nh->nh());
            sdata.set_itf(vlan_nh->GetInterface()->name());
            sdata.set_vlan_tag(vlan_nh->GetVlanTag());
            list.push_back(sdata);
            break;
        }
        case NextHop::COMPOSITE: {
            sdata.set_type("Composite");
            const CompositeNH *child_component_nh =
                static_cast<const CompositeNH *>(component_nh->nh());
            std::vector<McastData> comp_list;
            FillComponentNextHop(child_component_nh, comp_list);
            list.insert(list.begin(), comp_list.begin(), comp_list.end());
            break;
        }
        default:
            std::stringstream s;
            s << "UNKNOWN<" << component_nh->nh()->GetType()
                << ">";
            sdata.set_type(s.str());
            list.push_back(sdata);
            break;
        }
    }
}

static void FillL2CompositeNextHop(const CompositeNH *comp_nh,
                                   L2CompositeData &data) 
{
    std::stringstream str;
    str << "L2 Composite, subnh count : " 
        << comp_nh->ComponentNHCount();
    data.set_type(str.str());
    if (comp_nh->ComponentNHCount() == 0)
        return;
    std::vector<McastData> data_list;
    FillComponentNextHop(comp_nh, data_list);
    data.set_mc_list(data_list);
}

static void FillL3CompositeNextHop(const CompositeNH *comp_nh,
                                   L3CompositeData &data) 
{
    std::stringstream str;
    str << "L3 Composite, subnh count : " 
        << comp_nh->ComponentNHCount();
    data.set_type(str.str());
    if (comp_nh->ComponentNHCount() == 0)
        return;
    std::vector<McastData> data_list;                      
    FillComponentNextHop(comp_nh, data_list);                          
    data.set_mc_list(data_list);
}

static void FillMultiProtoCompositeNextHop(const CompositeNH *comp_nh,
                                           NhSandeshData &data) 
{
    std::stringstream str;
    str << "Multi Proto Composite, subnh count : " 
        << comp_nh->ComponentNHCount();
    data.set_type(str.str());
    if (comp_nh->ComponentNHCount() == 0)
        return;
    for (ComponentNHList::const_iterator it = comp_nh->begin();
            it != comp_nh->end(); it++) {
        const ComponentNH *component_nh = (*it).get();
        if (component_nh == NULL) {
            continue;
        }
        const CompositeNH *sub_cnh = 
            static_cast<const CompositeNH *>(component_nh->nh());
        if (sub_cnh->composite_nh_type() == Composite::L2COMP) {
            L2CompositeData l2_data;
            FillL2CompositeNextHop(sub_cnh, l2_data);
            data.set_l2_comp(l2_data);
        }
        if (sub_cnh->composite_nh_type() == Composite::L3COMP) {
            L3CompositeData l3_data;
            FillL3CompositeNextHop(sub_cnh, l3_data);
            data.set_l3_comp(l3_data);
        }
    }
}

static void ExpandCompositeNextHop(const CompositeNH *comp_nh, 
                                   NhSandeshData &data)
{
    stringstream comp_str;
    switch (comp_nh->composite_nh_type()) {
    case Composite::EVPN: {
        comp_str << "evpn Composite"  << " sub nh count: "
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        if (comp_nh->ComponentNHCount() == 0)
            break;
        std::vector<McastData> data_list;
        FillComponentNextHop(comp_nh, data_list);
        data.set_mc_list(data_list);
        break;
    }
    case Composite::FABRIC: {
        comp_str << "fabric Composite"  << " sub nh count: " 
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        if (comp_nh->ComponentNHCount() == 0)
            break;
        std::vector<McastData> data_list;
        FillComponentNextHop(comp_nh, data_list);
        data.set_mc_list(data_list);
        break;
    }    
    case Composite::L3COMP: {
        comp_str << "L3 Composite"  << " sub nh count: " 
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        if (comp_nh->ComponentNHCount() == 0)
            break;
        std::vector<McastData> data_list;                      
        FillComponentNextHop(comp_nh, data_list);                          
        data.set_mc_list(data_list);
        break;
    }
    case Composite::L2COMP: {
        comp_str << "L2 Composite"  << " sub nh count: " 
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        if (comp_nh->ComponentNHCount() == 0)
            break;
        std::vector<McastData> data_list;
        FillComponentNextHop(comp_nh, data_list);
        data.set_mc_list(data_list);
        break;
    }
    case Composite::L2INTERFACE: {
        comp_str << "L2 interface Composite"  << " sub nh count: "
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        if (comp_nh->ComponentNHCount() == 0)
            break;
        std::vector<McastData> data_list;
        FillComponentNextHop(comp_nh, data_list);
        data.set_mc_list(data_list);
        break;
    }
    case Composite::L3INTERFACE: {
        comp_str << "L3 interface Composite"  << " sub nh count: "
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        if (comp_nh->ComponentNHCount() == 0)
            break;
        std::vector<McastData> data_list;                      
        FillComponentNextHop(comp_nh, data_list);                          
        data.set_mc_list(data_list);
        break;
    }    
    case Composite::MULTIPROTO: {
        comp_str << "Multiproto Composite"  << " sub nh count: " 
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        FillMultiProtoCompositeNextHop(comp_nh, data);
        break;
    }    
    case Composite::ECMP:
    case Composite::LOCAL_ECMP: {
        comp_str << "ECMP Composite"  << " sub nh count: " 
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        std::vector<McastData> data_list;                      
        FillComponentNextHop(comp_nh, data_list);                          
        data.set_mc_list(data_list);
        break;
    }    
    default: {
        comp_str << "UNKNOWN<" << comp_nh->composite_nh_type()
            << ">";
        data.set_type(comp_str.str());
        break;
    }                 
    }
}

void NextHop::SetNHSandeshData(NhSandeshData &data) const {
    data.set_nh_index(id());
    switch (type_) {
        case DISCARD:
            data.set_type("discard");
            break;
        case RECEIVE: {
            data.set_type("receive");
            const ReceiveNH *nh = static_cast<const ReceiveNH *>(this);
            if (nh->GetInterface()) {
                data.set_itf(nh->GetInterface()->name());
            } else {
                data.set_itf("<NULL>");
            }
            break;
        }
        case RESOLVE:
            data.set_type("resolve");
            break;
        case ARP: {
            data.set_type("arp");
            const ArpNH *arp = static_cast<const ArpNH *>(this);
            data.set_sip(arp->GetIp()->to_string());
            data.set_vrf(arp->GetVrf()->GetName());
            if (valid_ == false) {
                break;
            }
            data.set_itf(arp->GetInterface()->name());
            const unsigned char *m = arp->GetMac().GetData();
            char mstr[32];
            snprintf(mstr, 32, "%x:%x:%x:%x:%x:%x",
                     m[0], m[1], m[2], m[3], m[4], m[5]);
            std::string mac(mstr);
            data.set_mac(mac);
            break;
        }
        case VRF: {
            data.set_type("vrf");
            const VrfNH *vrf = static_cast<const VrfNH *>(this);
            data.set_vrf(vrf->GetVrf()->GetName());
            break;
        }
        case INTERFACE: {
            data.set_type("interface");
            const InterfaceNH *itf = static_cast<const InterfaceNH *>(this);
            data.set_itf(itf->GetInterface()->name());
            const unsigned char *m = itf->GetDMac().GetData();
            char mstr[32];
            snprintf(mstr, 32, "%x:%x:%x:%x:%x:%x",
                     m[0], m[1], m[2], m[3], m[4], m[5]);
            std::string mac(mstr);
            data.set_mac(mac);
            if (itf->is_multicastNH())
                data.set_mcast("enabled");
            else
                data.set_mcast("disabled");
            break;
        }
        case TUNNEL: {
            data.set_type("tunnel");
            const TunnelNH *tun = static_cast<const TunnelNH *>(this);
            data.set_sip(tun->GetSip()->to_string());
            data.set_dip(tun->GetDip()->to_string());
            data.set_vrf(tun->GetVrf()->GetName());
            data.set_tunnel_type(tun->GetTunnelType().ToString());
            if (valid_) {
                const NextHop *nh = static_cast<const NextHop *>
                                  (tun->GetRt()->GetActiveNextHop());
                if (nh->GetType() == NextHop::ARP) {
                    const ArpNH *arp_nh = static_cast<const ArpNH *>(nh);
                    const unsigned char *m = arp_nh->GetMac().GetData();
                    char mstr[32];
                    snprintf(mstr, 32, "%x:%x:%x:%x:%x:%x",
                            m[0], m[1], m[2], m[3], m[4], m[5]);
                    std::string mac(mstr);
                    data.set_mac(mac);
                }
            }
            break;
        }
        case MIRROR: {
            data.set_type("Mirror");
            const MirrorNH *mir_nh = static_cast<const MirrorNH *>(this);
            data.set_sip(mir_nh->GetSip()->to_string());
            data.set_dip(mir_nh->GetDip()->to_string());
            data.set_vrf(mir_nh->GetVrf() ? mir_nh->GetVrf()->GetName() : "");
            data.set_sport(mir_nh->GetSPort());
            data.set_dport(mir_nh->GetDPort());
            if (valid_ && mir_nh->GetVrf()) {
                const NextHop *nh = static_cast<const NextHop *>
                                  (mir_nh->GetRt()->GetActiveNextHop());
                if (nh->GetType() == NextHop::ARP) {
                    const ArpNH *arp_nh = static_cast<const ArpNH *>(nh);
                    (mir_nh->GetRt()->GetActiveNextHop());
                    const unsigned char *m = arp_nh->GetMac().GetData();
                    char mstr[32];
                    snprintf(mstr, 32, "%x:%x:%x:%x:%x:%x",
                            m[0], m[1], m[2], m[3], m[4], m[5]);
                    std::string mac(mstr);
                    data.set_mac(mac);
                } else if (nh->GetType() == NextHop::RECEIVE) {
                    const ReceiveNH *rcv_nh = static_cast<const ReceiveNH*>(nh);
                    data.set_itf(rcv_nh->GetInterface()->name());
                }
            }
            break;
        }
        case COMPOSITE: {
            const CompositeNH *comp_nh = static_cast<const CompositeNH *>(this);
            ExpandCompositeNextHop(comp_nh, data);
            break;
        }

        case VLAN: {
            data.set_type("vlan");
            const VlanNH *itf = static_cast<const VlanNH *>(this);
            data.set_itf(itf->GetInterface()->name());
            data.set_vlan_tag(itf->GetVlanTag());
            const unsigned char *m = itf->GetDMac().GetData();
            char mstr[32];
            snprintf(mstr, 32, "%x:%x:%x:%x:%x:%x",
                    m[0], m[1], m[2], m[3], m[4], m[5]);
            std::string mac(mstr);
            data.set_mac(mac);
            break;
        }

        case INVALID:
        default:
            data.set_type("invalid");
            break;
    }
    if (valid_) {
        data.set_valid("true");
    } else {
        data.set_valid("false");
    }

    if (policy_) {
        data.set_policy("enabled");
    } else {
        data.set_policy("disabled");
    }

    data.set_ref_count(GetRefCount());
}

NextHop *NextHopTable::FindNextHop(size_t index) {
    NextHop *nh = index_table_.At(index);
    if (nh && nh->IsDeleted() != true) {
        return nh;
    }
    return NULL;
}

bool NextHop::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    NhListResp *resp = static_cast<NhListResp *>(sresp);

    NhSandeshData data;
    SetNHSandeshData(data);
    std::vector<NhSandeshData> &list =
                const_cast<std::vector<NhSandeshData>&>(resp->get_nh_list());
    list.push_back(data);

    return true;
}

void NhListReq::HandleRequest() const {
    AgentNhSandesh *sand = new AgentNhSandesh(context());
    sand->DoSandesh();
}
