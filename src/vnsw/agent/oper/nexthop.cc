/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include "ifmap/ifmap_node.h"
#include <vnc_cfg_types.h>
#include "base/task_annotations.h"
#include <base/logging.h>
#include <oper/agent_route.h>
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
void TunnelType::EncapPrioritySync(const std::vector<std::string> &cfg_list) {
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

    priority_list_ = l;

    return;
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
    nh->set_table(this);

    if (nh->CanAdd() == false) {
        delete nh;
        return NULL;
    }
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
        (Agent::GetInstance()->GetInterfaceTable()->FindActiveEntry(&key));
}

VrfEntry *NextHopTable::FindVrfEntry(const VrfKey &key) const {
    return static_cast<VrfEntry *>(Agent::GetInstance()->GetVrfTable()->FindActiveEntry(&key));
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
        (Agent::GetInstance()->GetVrfTable()->Find(&vrf_key_, true));
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

    if (memcmp(&mac_, &data->mac_, sizeof(mac_)) != 0) {
        mac_ = data->mac_;
        ret = true;
    }

    return ret;
}

const uint32_t ArpNH::GetVrfId() const {
    return vrf_->GetVrfId();
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

    const unsigned char *m = GetMac()->ether_addr_octet;
    FillObjectLogMac(m, info);

    OPER_TRACE(NextHop, info);
}

/////////////////////////////////////////////////////////////////////////////
// Interface NH routines
/////////////////////////////////////////////////////////////////////////////
NextHop *InterfaceNHKey::AllocEntry() const {
    Interface *intf = static_cast<Interface *>
        (Agent::GetInstance()->GetInterfaceTable()->Find(intf_key_.get(), true));
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
        (Agent::GetInstance()->GetVrfTable()->FindActiveEntry(&data->vrf_key_));
    if (vrf_.get() != vrf) {
        vrf_ = vrf;
        ret = true;
    }
    if (memcmp(&dmac_, &data->dmac_, sizeof(dmac_)) != 0) {
        dmac_ = data->dmac_;
        ret = true;
    }
    if (IsMulticastNH()) {
        dmac_.ether_addr_octet[0] = 0xFF;
        dmac_.ether_addr_octet[1] = 0xFF;
        dmac_.ether_addr_octet[2] = 0xFF;
        dmac_.ether_addr_octet[3] = 0xFF;
        dmac_.ether_addr_octet[4] = 0xFF;
        dmac_.ether_addr_octet[5] = 0xFF;
    }

    return ret;
}

const uuid &InterfaceNH::GetIfUuid() const {
    return interface_->GetUuid();
}

static void AddInterfaceNH(const uuid &intf_uuid, const struct ether_addr &dmac,
                          uint8_t flags, bool policy, const string vrf_name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InterfaceNHKey
                  (new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""),
                   policy, flags));
    req.data.reset(new InterfaceNHData(vrf_name, dmac));
    Agent::GetInstance()->GetNextHopTable()->Process(req);
}

// Create 3 InterfaceNH for every VPort. One with policy another without 
// policy, third one is for multicast.
void InterfaceNH::CreateVport(const uuid &intf_uuid,
                              const struct ether_addr &dmac, 
                              const string &vrf_name) {
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::INET4 | 
                  InterfaceNHFlags::MULTICAST, false,vrf_name);
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::INET4, true, vrf_name);
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::INET4, false, vrf_name);
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::LAYER2, false, vrf_name);
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::LAYER2, true, vrf_name);
}

static void DeleteNH(const uuid &intf_uuid, bool policy, 
                          uint8_t flags) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InterfaceNHKey
                  (new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""),
                   policy, flags));
    req.data.reset(NULL);
    NextHopTable::GetInstance()->Process(req);
}

// Delete the 2 InterfaceNH. One with policy another without policy
void InterfaceNH::DeleteVportReq(const uuid &intf_uuid) {
    DeleteNH(intf_uuid, false, InterfaceNHFlags::LAYER2);
    DeleteNH(intf_uuid, true, InterfaceNHFlags::LAYER2);
    DeleteNH(intf_uuid, false, InterfaceNHFlags::INET4);
    DeleteNH(intf_uuid, true, InterfaceNHFlags::INET4);
    DeleteNH(intf_uuid, false, InterfaceNHFlags::MULTICAST | 
             InterfaceNHFlags::INET4);
}

void InterfaceNH::CreateInetInterfaceNextHop(const string &ifname) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *key = new InterfaceNHKey(new InetInterfaceKey(ifname),
                                         false, InterfaceNHFlags::INET4);
    req.key.reset(key);

    struct ether_addr mac;
    memset(&mac, 0, sizeof(mac));
    mac.ether_addr_octet[ETHER_ADDR_LEN-1] = 1;
    InterfaceNHData *data = new InterfaceNHData(Agent::GetInstance()->GetDefaultVrf(), mac);
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

void InterfaceNH::CreatePacketInterfaceNhReq(const string &ifname) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *key = new InterfaceNHKey(new PacketInterfaceKey(nil_uuid(), ifname),
                                         false, InterfaceNHFlags::INET4);
    req.key.reset(key);

    struct ether_addr mac;
    memset(&mac, 0, sizeof(mac));
    mac.ether_addr_octet[ETHER_ADDR_LEN-1] = 1;
    InterfaceNHData *data = new InterfaceNHData(Agent::GetInstance()->GetDefaultVrf(), mac);
    req.data.reset(data);
    NextHopTable::GetInstance()->Enqueue(&req);
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

    const unsigned char *m = GetDMac().ether_addr_octet;
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
        (Agent::GetInstance()->GetVrfTable()->Find(&vrf_key_, true));
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
bool TunnelNH::CanAdd() const {
    if (vrf_ == NULL) {
        LOG(ERROR, "Invalid VRF in TunnelNH. Skip Add"); 
        return false;
    }

    return true;
}

NextHop *TunnelNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->GetVrfTable()->Find(&vrf_key_, true));
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

const uint32_t TunnelNH::GetVrfId() const {
    return vrf_->GetVrfId();
}

bool TunnelNH::Change(const DBRequest *req) {
    bool ret = false;
    bool valid = false;

    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>(GetVrf()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST));
    Inet4UnicastRouteEntry *rt = rt_table->FindLPM(dip_);
    if (!rt) {
        //No route to reach destination, add to unresolved list
        valid = false;
        rt_table->AddUnresolvedNH(this);
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        //Trigger ARP resolution
        valid = false;
        rt_table->AddUnresolvedNH(this);
        Inet4UnicastAgentRouteTable::AddArpReq(GetVrf()->GetName(), dip_);
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
    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>(GetVrf()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST));
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
        (Agent::GetInstance()->GetVrfTable()->Find(&vrf_key_, true));
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

const uint32_t MirrorNH::GetVrfId() const {
    return (vrf_ ? vrf_->GetVrfId() : (uint32_t)-1);
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
    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>(GetVrf()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST));
    Inet4UnicastRouteEntry *rt = rt_table->FindLPM(dip_);
    if (!rt) {
        //No route to reach destination, add to unresolved list
        valid = false;
        rt_table->AddUnresolvedNH(this);
    } else if ((rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) &&
               (GetVrf()->GetName() == Agent::GetInstance()->GetDefaultVrf())) {
        //Trigger ARP resolution
        valid = false;
        rt_table->AddUnresolvedNH(this);
        Inet4UnicastAgentRouteTable::AddArpReq(GetVrf()->GetName(), dip_);
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
    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>(GetVrf()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST));
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
        (Agent::GetInstance()->GetInterfaceTable()->Find(intf_key_.get(), true));
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

void ResolveNH::CreateReq( ) {
    DBRequest req;

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    NextHopKey *key = new ResolveNHKey();
    req.key.reset(key);

    ResolveNHData *data =new ResolveNHData();
    req.data.reset(data);
    NextHopTable::GetInstance()->Enqueue(&req);
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

void DiscardNH::CreateReq( ) {
    DBRequest req;

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    NextHopKey *key = new DiscardNHKey();
    req.key.reset(key);

    DiscardNHData *data =new DiscardNHData();
    req.data.reset(data);
    NextHopTable::GetInstance()->Enqueue(&req);
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
        (Agent::GetInstance()->GetInterfaceTable()->Find(intf_key_.get(), true));
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
        (Agent::GetInstance()->GetVrfTable()->FindActiveEntry(&data->vrf_key_));
    if (vrf_.get() != vrf) {
        vrf_ = vrf;
        ret = true;
    }

    if (memcmp(&smac_, &data->smac_, sizeof(smac_)) != 0) {
        smac_ = data->smac_;
        ret = true;
    }

    if (memcmp(&dmac_, &data->dmac_, sizeof(dmac_)) != 0) {
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
                    const string &vrf_name, const ether_addr &smac, 
                    const ether_addr &dmac) {
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

VlanNH *VlanNH::Find(const uuid &intf_uuid, uint16_t vlan_tag) {
    VlanNHKey key(intf_uuid, vlan_tag);
    return static_cast<VlanNH *>(NextHopTable::GetInstance()->FindActiveEntry(&key));
}

void VlanNH::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);

    const Interface *intf = GetInterface();
    FillObjectLogIntf(intf, info);

    const unsigned char *m = GetDMac().ether_addr_octet;
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

NextHop *CompositeNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->GetVrfTable()->Find(&vrf_key_, true));
    if (is_mcast_nh_) {
        return new CompositeNH(vrf, dip_, sip_, comp_type_);
    } else {
        return new CompositeNH(vrf, dip_, plen_, is_local_ecmp_nh_, comp_type_);
    }
}

void CompositeNH::SendObjectLog(AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;
    FillObjectLog(event, info);

    const VrfEntry *vrf = GetVrf();
    if (vrf) {
        info.set_vrf(vrf->GetName());
    }
    const Ip4Address dip = GetGrpAddr();
    info.set_dest_ip(dip.to_string());

    std::vector<ComponentNHLogInfo> comp_nh_log_list;
    ComponentNHList::const_iterator component_nh_it = begin();
    for (;component_nh_it != end(); component_nh_it++) {
        ComponentNHLogInfo component_nh_info;
        ComponentNH *comp_nh = *component_nh_it;
        if (comp_nh == NULL) {
            continue;
        }
        const NextHop *nh = comp_nh->GetNH();
        switch(nh->GetType()) {
        case TUNNEL: {
            const TunnelNH *tun_nh = static_cast<const TunnelNH *>(nh);
            component_nh_info.set_type("Tunnel");
            component_nh_info.set_label(comp_nh->GetLabel());
            component_nh_info.set_server_ip(tun_nh->GetDip()->to_string());
            break;
        }    

        case INTERFACE: {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            component_nh_info.set_type("Interface");
            component_nh_info.set_label(comp_nh->GetLabel());
            const Interface *intf = 
                static_cast<const Interface *>(intf_nh->GetInterface());
            component_nh_info.set_intf_name(intf->name());
            break;
        }

        case VLAN: {
           const VlanNH *vlan_nh = static_cast<const VlanNH *>(nh);
            component_nh_info.set_type("Vlan");
            component_nh_info.set_label(comp_nh->GetLabel());
            const Interface *intf = 
                static_cast<const Interface *>(vlan_nh->GetInterface());
            component_nh_info.set_intf_name(intf->name());
            break;
        }

        case COMPOSITE: {
            const CompositeNH *cnh = static_cast<const CompositeNH *>(nh);
            std::stringstream str;
            str << "Composite; Type: " << cnh->CompositeType() << 
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

void CompositeNH::SetKey(const DBRequestKey *k) {
    const CompositeNHKey *key = static_cast<const CompositeNHKey *>(k);
    NextHop::SetKey(k);
    vrf_ = NextHopTable::GetInstance()->FindVrfEntry(key->vrf_key_);
    src_addr_ = key->sip_;
    grp_addr_ = key->dip_;
    is_local_ecmp_nh_ = key->is_local_ecmp_nh_;
    comp_type_ = key->comp_type_;
}

bool CompositeNH::NextHopIsLess(const DBEntry &rhs) const {
    const CompositeNH &a = static_cast<const CompositeNH &>(rhs);

    if (vrf_.get() != a.vrf_.get()) {
        return vrf_.get() < a.vrf_.get();
    }

    if (grp_addr_ != a.grp_addr_) {
        return grp_addr_ < a.grp_addr_;
    }

    if (plen_ != a.plen_) {
        return plen_ < a.plen_;
    }

    if (is_local_ecmp_nh_ != a.is_local_ecmp_nh_) {
        return is_local_ecmp_nh_ < a.is_local_ecmp_nh_;
    }   

    return (comp_type_ < a.comp_type_);
}

void CompositeNH::Sync(bool deleted) {
    //Loop thru all the dependent composite NH
    CompositeNH::iterator iter = remote_comp_nh_list_.begin();
    while (iter != remote_comp_nh_list_.end()) {
        CompositeNH *remote_comp_nh = 
            static_cast<CompositeNH *>(iter.operator->());

        //Append newly added component NH to remote composite NH
        ComponentNHList::iterator it = begin();
        while (it != end()) {
            ComponentNH *component_nh = *it;
            if (component_nh && 
                    !remote_comp_nh->component_nh_list_.Find(*component_nh)) {
                remote_comp_nh->component_nh_list_.insert(*component_nh);
            }
            it++;
        }

        //Delete component NH not present in local composite NH
        it = remote_comp_nh->begin();
        while (it != remote_comp_nh->component_nh_list_.end()) {
            ComponentNH *component_nh = *it;
            it++;

            if (!component_nh || 
                    component_nh->GetNH()->GetType() == NextHop::TUNNEL) {
                continue;
            }
            if (!component_nh_list_.Find(*component_nh)) {
                remote_comp_nh->component_nh_list_.remove(*component_nh);
            }
        }

        iter++;
        //Reset the reference to local composite NH
        if (deleted) {
            remote_comp_nh->local_comp_nh_.reset(NULL);
        }
        DBTablePartBase *part = 
            Agent::GetInstance()->GetNextHopTable()->GetTablePartition(remote_comp_nh);
        part->Notify(remote_comp_nh);
    }
}

bool CompositeNH::GetOldNH(const CompositeNHData *data, 
                           ComponentNH &component_nh) {
    //In case of ECMP give preference to already existing nexthop
    //and make it first entry in composite NH, so that existing flow
    //can just migrate to same index
    Inet4UnicastRouteEntry *rt = 
        Inet4UnicastAgentRouteTable::FindRoute(vrf_->GetName(), grp_addr_);
    if (!rt || rt->IsDeleted()) {
        return false;
    }

    const NextHop *nh = rt->GetActiveNextHop();
    if (nh->GetType() == NextHop::COMPOSITE || 
        nh->GetType() == NextHop::DISCARD) {
        return false;
    }

    bool found = false;
    NextHop *list_nh = NULL;
    const std::vector<ComponentNHData> &key_list = data->data_;
    std::vector<ComponentNHData>::const_iterator it = key_list.begin();
    for (;it != key_list.end(); it++) {
        list_nh = static_cast<NextHop *>
            (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(it->nh_key_));
        if (!list_nh) {
            continue;
        }
        if (list_nh == nh && it->label_ == rt->GetMplsLabel()) {
            found = true;
            break;
        }
    }

    if (found) {
        component_nh.nh_ = list_nh;
        component_nh.label_ = rt->GetMplsLabel();
        return true;
    } 

    return false;
}


bool CompositeNH::Change(const DBRequest* req) {
    const CompositeNHData *data = 
        static_cast<const CompositeNHData *>(req->data.get());
    const std::vector<ComponentNHData> &key_list = data->data_;
    std::vector<ComponentNH> component_nh_list;
    NextHop *nh;

    if (comp_type_ == Composite::ECMP) {
        ComponentNH component_nh(0, NULL);
        if (GetOldNH(data, component_nh)) {
            component_nh_list.push_back(component_nh);
        }
    }

    //Add entries
    std::vector<ComponentNHData>::const_iterator it = key_list.begin();
    for (;it != key_list.end(); it++) {
        nh = static_cast<NextHop *>
            (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(it->nh_key_));
        if (!nh) {
            continue;
        }

        CompositeNH *comp_nh = static_cast<CompositeNH *>(nh);
        if ((nh->GetType() == NextHop::COMPOSITE) && comp_nh->IsEcmpNH()) {
            //Add all the members in composite NH
            ComponentNHList::iterator it = comp_nh->begin();
            for(;it != comp_nh->end(); it++) {
                ComponentNH *component_nh = *it;
                if (component_nh) {
                    component_nh_list.push_back(*component_nh);
                }
            }
            local_comp_nh_.reset(comp_nh);
        } else { 
            ComponentNH component_nh(it->label_, nh);
            component_nh_list.push_back(component_nh);
        }
    }

    if (IsMcastNH() == true) {
        component_nh_list_.clear();
    }

    if (data->op_ == CompositeNHData::ADD) {
        std::vector<ComponentNH>::iterator it = 
            component_nh_list.begin();
        while (it != component_nh_list.end()) {
            if (!component_nh_list_.Find(*it)) {
                component_nh_list_.insert(*it);
            }
            it++;
        }
    } else if (data->op_ == CompositeNHData::DELETE) {
        std::vector<ComponentNH>::iterator it = 
            component_nh_list.begin();
        while (it != component_nh_list.end()) {
            if (component_nh_list_.Find(*it)) {
                component_nh_list_.remove(*it);
            }
            it++;
        }
    } else if (data->op_ == CompositeNHData::REPLACE) {
        component_nh_list_.replace(component_nh_list);
    }

    if (is_local_ecmp_nh_ == true) {
        //Sync dependent remote NH
        Sync(false);
    }
    return true;
}

const NextHop* CompositeNH::GetLocalNextHop() const {
    const NextHop *nh = NULL;
    if ((nh = GetLocalCompositeNH()) == NULL) {
        //Get interface NH inside composite NH
        ComponentNHList::const_iterator component_nh_it = begin();
        while (component_nh_it != end()) {
            if (*component_nh_it &&
                ((*component_nh_it)->GetNH()->GetType() == NextHop::INTERFACE ||
                (*component_nh_it)->GetNH()->GetType() == NextHop::VLAN))  {
                nh = (*component_nh_it)->GetNH();
                break;
            }
            component_nh_it++;
        }
    }
    return nh;
}

CompositeNH::KeyPtr CompositeNH::GetDBRequestKey() const {
    NextHopKey *key = NULL;
    if (comp_type_ != Composite::ECMP) {
        key = new CompositeNHKey(vrf_->GetName(), grp_addr_, src_addr_, 
                                 false, comp_type_);
    } else {
        key = new CompositeNHKey(vrf_->GetName(), grp_addr_, plen_,
                                 is_local_ecmp_nh_);
    }
    return DBEntryBase::KeyPtr(key);
}

void CompositeNH::Delete(const DBRequest* req) {
    if (is_local_ecmp_nh_ == true) {
        component_nh_list_.clear();
        Sync(true);
    }
    local_comp_nh_.reset(NULL);
}

uint32_t CompositeNH::GetRemoteLabel(Ip4Address ip) const {
    ComponentNHList::const_iterator component_nh_it = begin();
    while(component_nh_it != end()) {
        if (*component_nh_it) {
            const NextHop *nh = (*component_nh_it)->GetNH();
            if (nh && nh->GetType() == NextHop::TUNNEL) {
                const TunnelNH *tun_nh = static_cast<const TunnelNH *>(nh);
                if (*(tun_nh->GetDip()) == ip) {
                    return (*component_nh_it)->GetLabel();
                }
            }
        }
        component_nh_it++;
    }
    return 0;
}

void CompositeNH::CreateComponentNH(std::vector<ComponentNHData> comp_nh_list) {
    //Go thru list of component NH, and if there a tunnel NH,
    //enqueue a request to create tunnel NH
    std::vector<ComponentNHData>::const_iterator it = comp_nh_list.begin();
    while (it != comp_nh_list.end()) {
        ComponentNHData nh_data = *it;
        if (nh_data.nh_key_->GetType() == NextHop::TUNNEL) {
            DBRequest req;
            // First enqueue request to add/change Interface NH
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

            req.key.reset(nh_data.nh_key_);
            nh_data.nh_key_ = NULL;

            TunnelNHData *data = new TunnelNHData();
            req.data.reset(data);
            Agent::GetInstance()->GetNextHopTable()->Enqueue(&req); 
        }
        it++;
    }
}

//Create composite NH of type ECMP
void CompositeNH::CreateCompositeNH(const string vrf_name,
                                    const Ip4Address ip,
                                    bool local_ecmp_nh,
                                    std::vector<ComponentNHData> comp_nh_list) {
    CreateComponentNH(comp_nh_list);
}

//Create composite NH of type ECMP
void CompositeNH::AppendComponentNH(const string vrf_name,
                                    const Ip4Address ip, uint8_t plen,
                                    bool local_ecmp_nh,
                                    ComponentNHData comp_nh_data) {
    std::vector<ComponentNHData> comp_nh_list;
    comp_nh_list.push_back(comp_nh_data);
    CreateComponentNH(comp_nh_list);
    DBRequest req;
    CompositeNHData *data;

    NextHopKey *key = new CompositeNHKey(vrf_name, ip, plen, local_ecmp_nh);
    key->sub_op_ = AgentKey::RESYNC;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    data = new CompositeNHData(comp_nh_list, CompositeNHData::ADD);
    req.data.reset(data);
    Agent::GetInstance()->GetNextHopTable()->Process(req);
}

//Delete composite NH of type ECMP
void CompositeNH::DeleteComponentNH(const string vrf_name,
                                    const Ip4Address ip, uint8_t plen,
                                    bool local_ecmp_nh,
                                    ComponentNHData comp_nh_data) {
    std::vector<ComponentNHData> comp_nh_list;
    comp_nh_list.push_back(comp_nh_data);
    DBRequest req;
    CompositeNHData *data;
    NextHopKey *key = new CompositeNHKey(vrf_name, ip, plen, local_ecmp_nh);

    key->sub_op_ = AgentKey::RESYNC;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    data = new CompositeNHData(comp_nh_list, CompositeNHData::DELETE);
    req.data.reset(data);
    Agent::GetInstance()->GetNextHopTable()->Process(req);
}

//Create composite NH of type multicast
void CompositeNH::CreateCompositeNH(const string vrf_name,
                                    const Ip4Address source_address,
                                    const Ip4Address group_address,
                                    COMPOSITETYPE type,
                                    std::vector<ComponentNHData> comp_nh_list) {
    CreateComponentNH(comp_nh_list);

    DBRequest req;
    NextHopKey *key = new CompositeNHKey(vrf_name, group_address,
                                         source_address, false, type);
    CompositeNHData *data;

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    data = new CompositeNHData(comp_nh_list, CompositeNHData::ADD);
    req.data.reset(data);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
}

/////////////////////////////////////////////////////////////////////////////
// NextHop Sandesh routines
/////////////////////////////////////////////////////////////////////////////
static void FillComponentNextHop(const CompositeNH *comp_nh,
                                 std::vector<McastData> &list) 
{
    for (CompositeNH::ComponentNHList::const_iterator it =
         comp_nh->begin(); it != comp_nh->end(); it++) {
        ComponentNH *component_nh = *it;
        if ((component_nh == NULL) || (component_nh->GetNH() == NULL)) {
            continue;
        }
        McastData sdata;
        switch (component_nh->GetNH()->GetType()) {
        case NextHop::INTERFACE: {
            sdata.set_type("Interface");
            const InterfaceNH *sub_nh = 
                static_cast<const InterfaceNH *>(component_nh->GetNH());
            if (sub_nh && sub_nh->GetInterface())
                sdata.set_label(component_nh->GetLabel());
            sdata.set_itf(sub_nh->GetInterface()->name());
            break;
        }
        case NextHop::TUNNEL: {
            sdata.set_type("Tunnel");
            const TunnelNH *tnh = 
                static_cast<const TunnelNH *>(component_nh->GetNH());
            sdata.set_dip(tnh->GetDip()->to_string());
            sdata.set_sip(tnh->GetSip()->to_string());
            sdata.set_label(component_nh->GetLabel());
            break;
        }
        case NextHop::VLAN: {
            sdata.set_type("Vlan");
            const VlanNH *vlan_nh = 
                static_cast<const VlanNH *>(component_nh->GetNH());
            sdata.set_itf(vlan_nh->GetInterface()->name());
            sdata.set_vlan_tag(vlan_nh->GetVlanTag());
            break;
        }
        case NextHop::COMPOSITE: {
            continue;                         
            break;                         
        }                    
        default:
            std::stringstream s;
            s << "UNKNOWN<" << component_nh->GetNH()->GetType() 
                << ">";
            sdata.set_type(s.str());
            break;                      
        }
        list.push_back(sdata);
    }
}

static void FillFabricCompositeNextHop(const CompositeNH *comp_nh,
                                   FabricCompositeData &data) 
{
    std::stringstream str;
    str << "Fabric  Composite, subnh count : " 
        << comp_nh->ComponentNHCount();
    data.set_type(str.str());
    data.set_sip(comp_nh->GetSrcAddr().to_string());
    data.set_dip(comp_nh->GetGrpAddr().to_string());
    if (comp_nh->ComponentNHCount() == 0)
        return;
    std::vector<McastData> data_list;                      
    FillComponentNextHop(comp_nh, data_list);                          
    data.set_mc_list(data_list);
}

static void FillL2CompositeNextHop(const CompositeNH *comp_nh,
                                   L2CompositeData &data) 
{
    std::stringstream str;
    str << "L2 Composite, subnh count : " 
        << comp_nh->ComponentNHCount();
    data.set_type(str.str());
    data.set_sip(comp_nh->GetSrcAddr().to_string());
    data.set_dip(comp_nh->GetGrpAddr().to_string());
    if (comp_nh->ComponentNHCount() == 0)
        return;
    CompositeNH::ComponentNHList::const_iterator it = comp_nh->begin();
    ComponentNH *component_nh = *it;
    
    const CompositeNH *sub_cnh = 
        static_cast<const CompositeNH *>(component_nh->GetNH());
    FabricCompositeData fab_data;
    FillFabricCompositeNextHop(sub_cnh, fab_data);
    data.set_fabric_comp(fab_data); 
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
    data.set_sip(comp_nh->GetSrcAddr().to_string());
    data.set_dip(comp_nh->GetGrpAddr().to_string());
    if (comp_nh->ComponentNHCount() == 0)
        return;
    CompositeNH::ComponentNHList::const_iterator it = comp_nh->begin();
    ComponentNH *component_nh = *it;
    
    const CompositeNH *sub_cnh = 
        static_cast<const CompositeNH *>(component_nh->GetNH());
    FabricCompositeData fab_data;
    FillFabricCompositeNextHop(sub_cnh, fab_data);
    data.set_fabric_comp(fab_data); 
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
    data.set_sip(comp_nh->GetSrcAddr().to_string());
    data.set_dip(comp_nh->GetGrpAddr().to_string());
    if (comp_nh->ComponentNHCount() == 0)
        return;
    for (CompositeNH::ComponentNHList::const_iterator it =
         comp_nh->begin(); it != comp_nh->end(); it++) {
        ComponentNH *component_nh = *it;
        if (component_nh == NULL) {
            continue;
        }
        const CompositeNH *sub_cnh = 
            static_cast<const CompositeNH *>(component_nh->GetNH());
        if (sub_cnh->CompositeType() == Composite::L2COMP) {
            L2CompositeData l2_data;
            FillL2CompositeNextHop(sub_cnh, l2_data);
            data.set_l2_comp(l2_data);
        }
        if (sub_cnh->CompositeType() == Composite::L3COMP) {
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
    switch (comp_nh->CompositeType()) {
    case Composite::FABRIC: {
        comp_str << "fabric Composite"  << " sub nh count: " 
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        if (comp_nh->ComponentNHCount() == 0)
            break;
        FabricCompositeData fab_data;
        FillFabricCompositeNextHop(comp_nh, fab_data);
        data.set_fabric_comp(fab_data); 
        break;
    }    
    case Composite::L3COMP: {
        comp_str << "L3 Composite"  << " sub nh count: " 
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        if (comp_nh->ComponentNHCount() == 0)
            break;
        CompositeNH::ComponentNHList::const_iterator it = comp_nh->begin();
        ComponentNH *component_nh = *it;
    
        const CompositeNH *sub_cnh = 
            static_cast<const CompositeNH *>(component_nh->GetNH());
        FabricCompositeData fab_data;
        FillFabricCompositeNextHop(sub_cnh, fab_data);
        data.set_fabric_comp(fab_data); 
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
        CompositeNH::ComponentNHList::const_iterator it = comp_nh->begin();
        ComponentNH *component_nh = *it;
    
        const CompositeNH *sub_cnh = 
            static_cast<const CompositeNH *>(component_nh->GetNH());
        FabricCompositeData fab_data;
        FillFabricCompositeNextHop(sub_cnh, fab_data);
        data.set_fabric_comp(fab_data); 
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
    case Composite::ECMP: {
        comp_str << "ECMP Composite"  << " sub nh count: " 
            << comp_nh->ComponentNHCount();
        data.set_type(comp_str.str());
        std::vector<McastData> data_list;                      
        FillComponentNextHop(comp_nh, data_list);                          
        data.set_mc_list(data_list);
        break;
    }    
    default: {
        comp_str << "UNKNOWN<" << comp_nh->CompositeType() 
            << ">";
        data.set_type(comp_str.str());
        break;
    }                 
    }
}

void NextHop::SetNHSandeshData(NhSandeshData &data) const {
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
            const unsigned char *m = arp->GetMac()->ether_addr_octet;
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
            const unsigned char *m = itf->GetDMac().ether_addr_octet;
            char mstr[32];
            snprintf(mstr, 32, "%x:%x:%x:%x:%x:%x", 
                     m[0], m[1], m[2], m[3], m[4], m[5]);
            std::string mac(mstr);
            data.set_mac(mac);
            if (itf->IsMulticastNH())
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
                    const unsigned char *m = arp_nh->GetMac()->ether_addr_octet;
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
                    const unsigned char *m = arp_nh->GetMac()->ether_addr_octet;
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
            const MulticastGroupObject *obj = MulticastHandler::GetInstance()->
                FindGroupObject(comp_nh->GetVrfName(),
                                comp_nh->GetGrpAddr());
            if (obj != NULL) {
                data.set_label(obj->GetSourceMPLSLabel());
            }

            data.set_dip(comp_nh->GetGrpAddr().to_string());
            if (comp_nh->IsMcastNH()) {
                data.set_sip(comp_nh->GetSrcAddr().to_string());
            }
            data.set_vrf(comp_nh->GetVrf() ? comp_nh->GetVrf()->GetName() : "");
            if (comp_nh->IsLocal()) {
                data.set_local_ecmp("true");
            }
            ExpandCompositeNextHop(comp_nh, data);
            break;
        }

        case VLAN: {
            data.set_type("vlan");
            const VlanNH *itf = static_cast<const VlanNH *>(this);
            data.set_itf(itf->GetInterface()->name());
            data.set_vlan_tag(itf->GetVlanTag());
            const unsigned char *m = itf->GetDMac().ether_addr_octet;
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

