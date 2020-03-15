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
#include <oper/crypt_tunnel.h>

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

    //There is no match found in priority list of config,
    //pick the advertised Tunnel type according to the order.
    if (bmap & (1 <<  MPLS_GRE))
        return MPLS_GRE;
    else if (bmap & (1 << MPLS_UDP))
        return MPLS_UDP;
    else if (bmap & (1 << VXLAN))
        return VXLAN;
    else if (bmap & (1 << NATIVE))
        return NATIVE;
    else if (bmap & (1 << MPLS_OVER_MPLS))
        return MPLS_OVER_MPLS;

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
void NextHop::SendObjectLog(const NextHopTable *table,
                            AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);
    OPER_TRACE_ENTRY(NextHop, table, info);
}

NextHop::~NextHop() {
    if (id_ != kInvalidIndex) {
        static_cast<NextHopTable *>(get_table())->FreeInterfaceId(id_);
    }
}

void NextHop::SetKey(const DBRequestKey *key) {
    const NextHopKey *nh_key = static_cast<const NextHopKey *>(key);
    type_ = nh_key->type_;
    policy_ = nh_key->policy_;
};

// Allocate label for nexthop
MplsLabel *NextHop::AllocateLabel(Agent *agent, const NextHopKey *key) {
    return agent->mpls_table()->AllocLabel(key);
}

void NextHop::Add(Agent *agent, const DBRequest *req) {
    ChangeEntry(req);
}

void NextHop::Change(const DBRequest *req) {
    Agent *agent = static_cast<NextHopTable *>(get_table())->agent();
    // Allocate mpls label if required
    if (NeedMplsLabel() && (mpls_label() == NULL)) {
        const NextHopKey *key =
            static_cast<const NextHopKey *>(req->key.get());
        mpls_label_ = AllocateLabel(agent, key);
    }
}

void NextHop::PostAdd() {
    Agent *agent = static_cast<NextHopTable *>(get_table())->agent();
    DBEntryBase::KeyPtr key = GetDBRequestKey();
    const NextHopKey *key1 = static_cast<const NextHopKey *>(key.get());
    // Mpls Label stores a pointer to NH oper db entry. It uses db table Find
    // api to retrieve NH db entry. Hence Allocate Mpls label if required
    // in PostAdd api which ensures presence of NH db entry in db table.
    if (NeedMplsLabel()) {
        mpls_label_ = AllocateLabel(agent, key1);
    }
}

void NextHop::EnqueueResync() const {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key = GetDBRequestKey();
    (static_cast<NextHopKey *>(req.key.get()))->sub_op_ = AgentKey::RESYNC;
    get_table()->Enqueue(&req);
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

        case NextHop::PBB:
            type_str.assign("PBB_INDIRECT");
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
        case AgentLogEvent::DEL:
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

bool NextHop::NexthopToInterfacePolicy() const {
    if (GetType() == NextHop::INTERFACE) {
        const InterfaceNH *intf_nh =
            static_cast<const InterfaceNH *>(this);
        const VmInterface *intf = dynamic_cast<const VmInterface *>
            (intf_nh->GetInterface());
        if (intf && intf->policy_enabled()) {
            return true;
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////
// NextHopTable routines
/////////////////////////////////////////////////////////////////////////////
NextHopTable::NextHopTable(DB *db, const string &name) : AgentDBTable(db, name){
    // nh-index 0 is reserved by vrouter. So, pre-allocate the first index so
    // that nh added by agent use index 1 and above
    int id = index_table_.Insert(NULL);
    assert(id == 0);
}

NextHopTable::~NextHopTable() {
    FreeInterfaceId(0);
}

uint32_t NextHopTable::ReserveIndex() {
    return index_table_.Insert(NULL);
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
    CheckVrNexthopLimit();
    nh->set_id(index_table_.Insert(nh));
    nh->Add(agent(), req);
    nh->SendObjectLog(this, AgentLogEvent::ADD);
    return static_cast<DBEntry *>(nh);
}

bool NextHopTable::OnChange(DBEntry *entry, const DBRequest *req) {
    NextHop *nh = static_cast<NextHop *>(entry);
    bool delcleared = false;
    //Since as part of label addition later, active nh entries are looked
    //up. So makes sense to clear delete now so as to allow proper label 
    //binding. 
    if (entry->IsDeleted()) {
        entry->ClearDelete();
        delcleared = true;
    }
    nh->Change(req);
    bool ret = nh->ChangeEntry(req);
    nh->SendObjectLog(this, AgentLogEvent::CHANGE);
    return (ret | delcleared);
}

bool NextHopTable::Resync(DBEntry *entry, const DBRequest *req) {
    NextHop *nh = static_cast<NextHop *>(entry);
    bool ret = nh->ChangeEntry(req);
    nh->SendObjectLog(this, AgentLogEvent::RESYNC);
    return ret;
}

bool NextHopTable::Delete(DBEntry *entry, const DBRequest *req) {
    NextHop *nh = static_cast<NextHop *>(entry);
    nh->Delete(req);
    nh->SendObjectLog(this, AgentLogEvent::DEL);
    CheckVrNexthopLimit();
    return true;
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
    agent()->ConcurrencyCheck();
    DBTablePartition *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
    tpart->Process(NULL, &req);
}

void NextHopTable::OnZeroRefcount(AgentDBEntry *e) {
    NextHop *nh = static_cast<NextHop *>(e);

    // Release mpls db entry reference
    nh->ResetMplsRef();
    if (nh->DeleteOnZeroRefCount() == false) {
        return;
    }

    agent()->ConcurrencyCheck();
    nh->OnZeroRefCount();

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;
    DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
    req.key = key;
    req.data.reset(NULL);
    Process(req);
}

void NextHopTable::CheckVrNexthopLimit() {
    VrLimitExceeded &vr_limits = agent()->get_vr_limits_exceeded_map();
    VrLimitExceeded::iterator vr_limit_itr = vr_limits.find("vr_nexthops");
    if (vr_limit_itr->second == "Normal") {
        if (index_table_.InUseIndexCount() >= ((agent()->vr_limit_high_watermark() *
            agent()->vrouter_max_nexthops())/100) ) {
            vr_limit_itr->second.assign(std::string("Exceeded"));
            LOG(ERROR, "Vrouter Nexthop Index Exceeded.");
        }
    } else if ( vr_limit_itr->second == "Exceeded") {
        if (index_table_.InUseIndexCount() >= agent()->vrouter_max_nexthops()) {
            vr_limit_itr->second.assign(std::string("TableLimit"));
            LOG(ERROR, "Vrouter Nexthop Table Limit Reached. Skip NH Add.");
        } else if ( index_table_.InUseIndexCount() < ((agent()->vr_limit_low_watermark() *
            agent()->vrouter_max_nexthops())/100) ) {
            vr_limit_itr->second.assign(std::string("Normal"));
        }
    } else if ( vr_limit_itr->second == "TableLimit" ) {
        if (index_table_.InUseIndexCount() <
            ((agent()->vrouter_max_nexthops()*95)/100) ) {
            vr_limit_itr->second.assign(std::string("Exceeded"));
            LOG(ERROR, "Vrouter Nexthop Index Exceeded.");
        }
    }
    agent()->set_vr_limits_exceeded_map(vr_limits);
}

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

bool ArpNH::ChangeEntry(const DBRequest *req) {
    bool ret= false;
    //const ArpNHKey *key = static_cast<const ArpNHKey *>(req->key.get());
    const ArpNHData *data = static_cast<const ArpNHData *>(req->data.get());
    if (valid_ != data->resolved_) {
        valid_ = data->resolved_;
        ret =  true;
    }

    Interface *pinterface = NextHopTable::GetInstance()->FindInterface
        (*data->intf_key_.get());
    if (interface_.get() != pinterface) {
        interface_ = pinterface;
        ret = true;
    }

    if (!data->valid_) {
        mac_.Zero();
        return ret;
    }

    if (data->resolved_ != true) {
        // If ARP is not resolved mac will be invalid
        return ret;
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
    NextHopKey *key = new ArpNHKey(vrf_->GetName(), ip_, policy_);
    return DBEntryBase::KeyPtr(key);
}

const boost::uuids::uuid &ArpNH::GetIfUuid() const {
    return interface_->GetUuid();
}

void ArpNH::SendObjectLog(const NextHopTable *table,
                          AgentLogEvent::type event) const {
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

    OPER_TRACE_ENTRY(NextHop, table, info);
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
    return new InterfaceNH(intf, policy_, flags_, dmac_);
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

    if (flags_ != a.flags_) {
        return flags_ < a.flags_;
    }

    return dmac_ < a.dmac_;
}

InterfaceNH::KeyPtr InterfaceNH::GetDBRequestKey() const {
    NextHopKey *key =
        new InterfaceNHKey(static_cast<InterfaceKey *>(
                          interface_->GetDBRequestKey().release()),
                           policy_, flags_, dmac_);
    return DBEntryBase::KeyPtr(key);
}

void InterfaceNH::SetKey(const DBRequestKey *k) {
    const InterfaceNHKey *key = static_cast<const InterfaceNHKey *>(k);

    NextHop::SetKey(k);
    interface_ = NextHopTable::GetInstance()->FindInterface(*key->intf_key_.get());
    flags_ = key->flags_;
    dmac_ = key->dmac_;
}

bool InterfaceNH::ChangeEntry(const DBRequest *req) {
    const InterfaceNHData *data =
            static_cast<const InterfaceNHData *>(req->data.get());
    bool ret = false;

    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&data->vrf_key_));
    if (vrf_.get() != vrf) {
        vrf_ = vrf;
        ret = true;
    }

    if (learning_enabled_ != data->learning_enabled_) {
        learning_enabled_ = data->learning_enabled_;
        ret = true;
    }

    if (etree_leaf_ != data->etree_leaf_) {
        etree_leaf_ = data->etree_leaf_;
        ret = true;
    }

    if (layer2_control_word_ != data->layer2_control_word_) {
        layer2_control_word_ = data->layer2_control_word_;
        ret = true;
    }

    return ret;
}

const boost::uuids::uuid &InterfaceNH::GetIfUuid() const {
    return interface_->GetUuid();
}

static void AddInterfaceNH(const boost::uuids::uuid &intf_uuid,
                          const MacAddress &dmac, uint8_t flags,
                          bool policy, const string vrf_name,
                          bool learning_enabled, bool etree_leaf,
                          bool layer2_control_word, const string &name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InterfaceNHKey
                  (new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, name),
                   policy, flags, dmac));
    req.data.reset(new InterfaceNHData(vrf_name, learning_enabled,
                                       etree_leaf, layer2_control_word));
    Agent::GetInstance()->nexthop_table()->Process(req);
}

// Create 3 InterfaceNH for every Vm interface. One with policy another without
// policy, third one is for multicast.
void InterfaceNH::CreateL3VmInterfaceNH(const boost::uuids::uuid &intf_uuid,
                                        const MacAddress &dmac,
                                        const string &vrf_name,
                                        bool learning_enabled,
                                        const string &intf_name) {
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::INET4, true, vrf_name,
                   learning_enabled, false, false, intf_name);
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::INET4, false, vrf_name,
                   learning_enabled, false, false, intf_name);
}

void InterfaceNH::DeleteL3InterfaceNH(const boost::uuids::uuid &intf_uuid,
                                      const MacAddress &mac,
                                      const string &intf_name) {
    DeleteNH(intf_uuid, false, InterfaceNHFlags::INET4, mac, intf_name);
    DeleteNH(intf_uuid, true, InterfaceNHFlags::INET4, mac, intf_name);
}

void InterfaceNH::CreateL2VmInterfaceNH(const boost::uuids::uuid &intf_uuid,
                                        const MacAddress &dmac,
                                        const string &vrf_name,
                                        bool learning_enabled,
                                        bool etree_leaf,
                                        bool layer2_control_word,
                                        const string &intf_name) {
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::BRIDGE, false, vrf_name,
                   learning_enabled, etree_leaf, layer2_control_word, intf_name);
    AddInterfaceNH(intf_uuid, dmac, InterfaceNHFlags::BRIDGE, true, vrf_name,
                   learning_enabled, etree_leaf, layer2_control_word, intf_name);
}

void InterfaceNH::DeleteL2InterfaceNH(const boost::uuids::uuid &intf_uuid,
                                      const MacAddress &dmac,
                                      const string &intf_name) {
    DeleteNH(intf_uuid, false, InterfaceNHFlags::BRIDGE, dmac, intf_name);
    DeleteNH(intf_uuid, true, InterfaceNHFlags::BRIDGE, dmac, intf_name);
}

void InterfaceNH::CreateMulticastVmInterfaceNH(const boost::uuids::uuid &intf_uuid,
                                               const MacAddress &dmac,
                                               const string &vrf_name,
                                               const string &intf_name) {
    AddInterfaceNH(intf_uuid, dmac, (InterfaceNHFlags::INET4 |
                                     InterfaceNHFlags::MULTICAST), false,
                   vrf_name, false, false, false, intf_name);
}

void InterfaceNH::DeleteMulticastVmInterfaceNH(const boost::uuids::uuid &intf_uuid,
                                               const MacAddress &dmac,
                                               const string &intf_name) {
    DeleteNH(intf_uuid, false, (InterfaceNHFlags::INET4 |
                                InterfaceNHFlags::MULTICAST),
                                dmac, intf_name);
}

void InterfaceNH::DeleteNH(const boost::uuids::uuid &intf_uuid, bool policy,
                          uint8_t flags, const MacAddress &mac,
                          const string &intf_name) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InterfaceNHKey
                  (new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid,
                                      intf_name),
                   policy, flags, mac));
    req.data.reset(NULL);
    NextHopTable::GetInstance()->Process(req);
}

// Delete the 2 InterfaceNH. One with policy another without policy
void InterfaceNH::DeleteVmInterfaceNHReq(const boost::uuids::uuid &intf_uuid,
                                         const MacAddress &mac,
                                         const string &intf_name) {
    DeleteNH(intf_uuid, false, InterfaceNHFlags::BRIDGE, mac, intf_name);
    DeleteNH(intf_uuid, true, InterfaceNHFlags::BRIDGE, mac, intf_name);
    DeleteNH(intf_uuid, false, InterfaceNHFlags::INET4, mac, intf_name);
    DeleteNH(intf_uuid, true, InterfaceNHFlags::INET4, mac, intf_name);
    DeleteNH(intf_uuid, false, InterfaceNHFlags::MULTICAST,
             MacAddress::BroadcastMac(), intf_name);
}

void InterfaceNH::CreateInetInterfaceNextHop(const string &ifname,
                                             const string &vrf_name,
                                             const MacAddress &mac) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *key = new InterfaceNHKey(new InetInterfaceKey(ifname),
                                         false, InterfaceNHFlags::INET4,
                                         mac);
    req.key.reset(key);

    InterfaceNHData *data = new InterfaceNHData(vrf_name);
    req.data.reset(data);
    NextHopTable::GetInstance()->Process(req);
}

void InterfaceNH::DeleteInetInterfaceNextHop(const string &ifname,
                                             const MacAddress &mac) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    NextHopKey *key = new InterfaceNHKey
        (new InetInterfaceKey(ifname), false,
         InterfaceNHFlags::INET4, mac);
    req.key.reset(key);

    req.data.reset(NULL);
    NextHopTable::GetInstance()->Process(req);
}

void InterfaceNH::CreatePacketInterfaceNh(Agent *agent, const string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);

    // create nexthop without policy
    req.key.reset(new InterfaceNHKey(
        new PacketInterfaceKey(boost::uuids::nil_uuid(), ifname), false,
        InterfaceNHFlags::INET4, agent->pkt_interface_mac()));
    req.data.reset(new InterfaceNHData(""));
    agent->nexthop_table()->Process(req);

    // create nexthop with relaxed policy
    req.key.reset(new InterfaceNHKey(
        new PacketInterfaceKey(boost::uuids::nil_uuid(), ifname), true,
        InterfaceNHFlags::INET4, agent->pkt_interface_mac()));
    req.data.reset(new InterfaceNHData(""));
    agent->nexthop_table()->Process(req);
}

void InterfaceNH::DeleteHostPortReq(Agent *agent, const string &ifname) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    // delete NH without policy
    req.key.reset(new InterfaceNHKey(
        new PacketInterfaceKey(boost::uuids::nil_uuid(), ifname), false,
        InterfaceNHFlags::INET4, agent->pkt_interface_mac()));
    req.data.reset(NULL);
    agent->nexthop_table()->Enqueue(&req);

    // delete NH with policy
    req.key.reset(new InterfaceNHKey(new PacketInterfaceKey(
        boost::uuids::nil_uuid(), ifname), true,
        InterfaceNHFlags::INET4, agent->pkt_interface_mac()));
    req.data.reset(NULL);
    agent->nexthop_table()->Enqueue(&req);
}

void InterfaceNH::CreatePhysicalInterfaceNh(const string &ifname,
                                            const MacAddress &mac) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InterfaceNHKey(new PhysicalInterfaceKey(ifname),
                                     false, InterfaceNHFlags::INET4, mac));
    req.data.reset(new InterfaceNHData(Agent::GetInstance()->fabric_vrf_name()));
    NextHopTable::GetInstance()->Process(req);
}

void InterfaceNH::DeletePhysicalInterfaceNh(const string &ifname,
                                            const MacAddress &mac) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InterfaceNHKey(new PhysicalInterfaceKey(ifname),
                                     false, InterfaceNHFlags::INET4, mac));
    req.data.reset(NULL);
    NextHopTable::GetInstance()->Process(req);
}

void InterfaceNH::SendObjectLog(const NextHopTable *table,
                                AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);

    const Interface *intf = GetInterface();
    FillObjectLogIntf(intf, info);

    const unsigned char *m = (unsigned char *)GetDMac().GetData();
    FillObjectLogMac(m, info);

    OPER_TRACE_ENTRY(NextHop, table, info);
}

bool InterfaceNH::NeedMplsLabel() {
    const Interface *itf = GetInterface();
    //Label is required only for VMInterface and InetInterface
    if (dynamic_cast<const VmInterface *>(itf) ||
        dynamic_cast<const InetInterface *>(itf)) {
        return true;
    }
    return false;
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
    return new VrfNH(vrf, policy_, bridge_nh_);
}

bool VrfNH::NextHopIsLess(const DBEntry &rhs) const {
    const VrfNH &a = static_cast<const VrfNH &>(rhs);

    if (vrf_.get() != a.vrf_.get()) {
        return (vrf_.get() < a.vrf_.get());
    }

    return bridge_nh_ < a.bridge_nh_;
}

void VrfNH::SetKey(const DBRequestKey *k) {
    const VrfNHKey *key = static_cast<const VrfNHKey *>(k);
    NextHop::SetKey(k);
    vrf_ = NextHopTable::GetInstance()->FindVrfEntry(key->vrf_key_);
    bridge_nh_ = key->bridge_nh_;
}

VrfNH::KeyPtr VrfNH::GetDBRequestKey() const {
    NextHopKey *key = new VrfNHKey(vrf_->GetName(), policy_, bridge_nh_);
    return DBEntryBase::KeyPtr(key);
}

bool VrfNH::ChangeEntry(const DBRequest *req) {
    bool ret = false;
    const VrfNHData *data = static_cast<const VrfNHData *>(req->data.get());

    if (data->flood_unknown_unicast_ != flood_unknown_unicast_) {
        flood_unknown_unicast_ = data->flood_unknown_unicast_;
        ret = true;
    }

    if (learning_enabled_ != data->learning_enabled_) {
        learning_enabled_ = data->learning_enabled_;
        ret = true;
    }

    if (layer2_control_word_ != data->layer2_control_word_) {
        layer2_control_word_ = data->layer2_control_word_;
        ret = true;
    }

    return ret;
}

void VrfNH::SendObjectLog(const NextHopTable *table,
                          AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;
    FillObjectLog(event, info);

    const VrfEntry *vrf = GetVrf();
    if (vrf) {
        info.set_vrf(vrf->GetName());
    }
    OPER_TRACE_ENTRY(NextHop, table, info);
}

/////////////////////////////////////////////////////////////////////////////
// Tunnel NH routines
/////////////////////////////////////////////////////////////////////////////
TunnelNH::TunnelNH(VrfEntry *vrf, const Ip4Address &sip, const Ip4Address &dip,
                   bool policy, TunnelType type, const MacAddress &rewrite_dmac) :
    NextHop(NextHop::TUNNEL, false, policy), vrf_(vrf, this), sip_(sip),
    dip_(dip), tunnel_type_(type), arp_rt_(this), interface_(NULL), dmac_(),
    crypt_(false), crypt_tunnel_available_(false), crypt_interface_(NULL),
    rewrite_dmac_(rewrite_dmac) {
}

TunnelNH::~TunnelNH() {
}

bool TunnelNH::CanAdd() const {
    if (vrf_ == NULL) {
        LOG(ERROR, "Invalid VRF in TunnelNH. Skip Add");
        return false;
    }

    if (dip_.to_ulong() == 0) {
        LOG(ERROR, "Invalid tunnel-destination in TunnelNH");
    }

    return true;
}

NextHop *TunnelNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->Find(&vrf_key_, true));
    return new TunnelNH(vrf, sip_, dip_, policy_, tunnel_type_,
                        rewrite_dmac_);
}

bool TunnelNH::NextHopIsLess(const DBEntry &rhs) const {
    const TunnelNH &a = static_cast<const TunnelNH &>(rhs);

    bool ret;
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

    if (rewrite_dmac_ != a.rewrite_dmac_) {
        return rewrite_dmac_ < a.rewrite_dmac_;
    }
    ret = TunnelNextHopIsLess(rhs);
    return ret;
}

void TunnelNH::SetKey(const DBRequestKey *k) {
    const TunnelNHKey *key = static_cast<const TunnelNHKey *>(k);
    NextHop::SetKey(k);
    vrf_ = NextHopTable::GetInstance()->FindVrfEntry(key->vrf_key_);
    sip_ = key->sip_;
    dip_ = key->dip_;
    tunnel_type_ = key->tunnel_type_;
    policy_ = key->policy_;
    rewrite_dmac_ = key->rewrite_dmac_;
}

TunnelNH::KeyPtr TunnelNH::GetDBRequestKey() const {
    NextHopKey *key = new TunnelNHKey(vrf_->GetName(), sip_, dip_, policy_,
                                      tunnel_type_, rewrite_dmac_);
    return DBEntryBase::KeyPtr(key);
}

const uint32_t TunnelNH::vrf_id() const {
    return vrf_->vrf_id();
}

bool TunnelNH::ChangeEntry(const DBRequest *req) {
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

        const ResolveNH *nh =
            static_cast<const ResolveNH *>(rt->GetActiveNextHop());
        std::string nexthop_vrf = nh->get_interface()->vrf()->GetName();
        if (nh->get_interface()->vrf()->forwarding_vrf()) {
            nexthop_vrf = nh->get_interface()->vrf()->forwarding_vrf()->GetName();
        }

        InetUnicastAgentRouteTable::AddArpReq(GetVrf()->GetName(), dip_,
                                              nexthop_vrf,
                                              nh->get_interface(),
                                              nh->PolicyEnabled(),
                                              rt->GetActivePath()->dest_vn_list(),
                                              rt->GetActivePath()->sg_list(),
                                              rt->GetActivePath()->tag_list());
        rt = NULL;
    } else {
        valid = rt->GetActiveNextHop()->IsValid();
    }

    if (valid != valid_) {
        valid_ = valid;
        ret = true;
    }

    if (arp_rt_.get() != rt) {
        arp_rt_.reset(rt);
        ret = true;
    }

    Agent *agent = Agent::GetInstance();
    if (agent->crypt_interface() != crypt_interface_) {
        crypt_interface_ = agent->crypt_interface();
        ret = true;
    }
    bool crypt, crypt_tunnel_available;
    agent->crypt_tunnel_table()->CryptAvailability(dip_.to_string(),
                                                   crypt, crypt_tunnel_available);
    if (crypt_tunnel_available != crypt_tunnel_available_) {
        crypt_tunnel_available_ = crypt_tunnel_available;
        ret = true;
    }
    if (crypt != crypt_) {
        crypt_ = crypt;
        ret = true;
    }

    //If route is present, check if the interface or mac
    //address changed for the dependent route
    if (valid_ && rt) {
        //Check if the interface or mac
        //of the dependent route has changed
        //only then notify the route
        const NextHop *active_nh = rt->GetActiveNextHop();
        const Interface *intf = NULL;
        MacAddress dmac;
        if (active_nh->GetType() == NextHop::ARP) {
            const ArpNH *arp_nh = static_cast<const ArpNH *>(active_nh);
            dmac = arp_nh->GetMac();
            intf = arp_nh->GetInterface();
        } else if (active_nh->GetType() == NextHop::INTERFACE) {
            const InterfaceNH *intf_nh =
                static_cast<const InterfaceNH *>(active_nh);
            intf = intf_nh->GetInterface();
            dmac_.Zero();
        }

        if (dmac_ != dmac) {
            dmac_ = dmac;
            ret = true;
        }

        if (interface_ != intf) {
            interface_ = intf;
            ret = true;
        }
    }

    return ret;
}

void TunnelNH::Delete(const DBRequest *req) {
    InetUnicastAgentRouteTable *rt_table =
        (GetVrf()->GetInet4UnicastRouteTable());
    if (rt_table)
        rt_table->RemoveUnresolvedNH(this);
}

void TunnelNH::SendObjectLog(const NextHopTable *table,
                             AgentLogEvent::type event) const {
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
    if (crypt_)
        info.set_crypt_traffic("All");
    if (crypt_tunnel_available_)
        info.set_crypt_tunnel_available("Yes");
    if (crypt_interface_)
        info.set_crypt_interface(GetCryptInterface()->name());
    info.set_rewrite_dmac(rewrite_dmac_.ToString());
    OPER_TRACE_ENTRY(NextHop, table, info);
}

/////////////////////////////////////////////////////////////////////////////
// Labelled Tunnel  NH routines
/////////////////////////////////////////////////////////////////////////////

LabelledTunnelNH::LabelledTunnelNH(VrfEntry *vrf, const Ip4Address &sip, const Ip4Address &dip,
                   bool policy, TunnelType type, const MacAddress &rewrite_dmac,
                   uint32_t label) :
    TunnelNH(vrf, sip, dip, policy, type, rewrite_dmac), transport_mpls_label_(label) {
}

LabelledTunnelNH::~LabelledTunnelNH() {
}

NextHop *LabelledTunnelNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->Find(&vrf_key_, true));
    return new LabelledTunnelNH(vrf, sip_, dip_, policy_, tunnel_type_,
                        rewrite_dmac_, transport_mpls_label_);
}

LabelledTunnelNH::KeyPtr LabelledTunnelNH::GetDBRequestKey() const {
    NextHopKey *key = new LabelledTunnelNHKey(vrf_->GetName(), sip_, dip_,
                        policy_,tunnel_type_, rewrite_dmac_, transport_mpls_label_);
    return DBEntryBase::KeyPtr(key);
}

bool LabelledTunnelNH::TunnelNextHopIsLess(const DBEntry &rhs) const {
    const LabelledTunnelNH &a = static_cast<const LabelledTunnelNH &>(rhs);

    return (transport_mpls_label_ < a.transport_mpls_label_);
}

bool LabelledTunnelNH::ChangeEntry(const DBRequest *req) {
    bool ret = false;
    ret = TunnelNH::ChangeEntry(req);
    TunnelType::Type transport_tunnel_type = 
                TunnelType::ComputeType(TunnelType::MplsType());
    if (transport_tunnel_type != transport_tunnel_type_) {
        transport_tunnel_type_ = transport_tunnel_type;
        ret = true;
    }
    return ret;
}

void LabelledTunnelNH::SendObjectLog(const NextHopTable *table,
                             AgentLogEvent::type event) const {
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
    if (crypt_)
        info.set_crypt_traffic("All");
    if (crypt_tunnel_available_)
        info.set_crypt_tunnel_available("Yes");
    if (crypt_interface_)
        info.set_crypt_interface(GetCryptInterface()->name());
    info.set_rewrite_dmac(rewrite_dmac_.ToString());
    info.set_transport_mpls_label(transport_mpls_label_);
    OPER_TRACE_ENTRY(NextHop, table, info);
}
/////////////////////////////////////////////////////////////////////////////
// Mirror NH routines
/////////////////////////////////////////////////////////////////////////////
MirrorNH::MirrorNH(const VrfKey &vkey, const IpAddress &sip, uint16_t sport,
                   const IpAddress &dip, uint16_t dport):
        NextHop(NextHop::MIRROR, false, false), vrf_name_(vkey.name_),
        sip_(sip), sport_(sport), dip_(dip), dport_(dport), arp_rt_(this),
        interface_(NULL), dmac_() {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->Find(&vkey, true));
    vrf_ = VrfEntryRef(vrf, this);
}

bool MirrorNH::CanAdd() const {
    /* For service-chain based mirroring, vrf will always be empty. In this
     * case we should create MirrorNH even when VRF is NULL */
    if (!vrf_name_.empty() && vrf_ == NULL) {
        LOG(ERROR, "Invalid VRF in mirror NH");
        return false;
    }

    return true;
}

NextHop *MirrorNHKey::AllocEntry() const {
    return new MirrorNH(vrf_key_, sip_, sport_, dip_, dport_);
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

InetUnicastAgentRouteTable *MirrorNH::GetRouteTable() {
    InetUnicastAgentRouteTable *rt_table = NULL;
    if (dip_.is_v4()) {
        rt_table = GetVrf()->GetInet4UnicastRouteTable();
    } else {
        rt_table = GetVrf()->GetInet6UnicastRouteTable();
    }
    return rt_table;
}

bool MirrorNH::ChangeEntry(const DBRequest *req) {
    bool ret = false;
    bool valid = false;

    if (GetVrf() == NULL) {
        valid_ = true;
        return true;
    }
    InetUnicastAgentRouteTable *rt_table = GetRouteTable();
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

        const ResolveNH *nh =
            static_cast<const ResolveNH *>(rt->GetActiveNextHop());
        std::string nexthop_vrf = nh->get_interface()->vrf()->GetName();
        if (nh->get_interface()->vrf()->forwarding_vrf()) {
            nexthop_vrf = nh->get_interface()->vrf()->forwarding_vrf()->GetName();
        }

        InetUnicastAgentRouteTable::AddArpReq(GetVrf()->GetName(), dip_.to_v4(),
                                              nexthop_vrf,
                                              nh->get_interface(),
                                              nh->PolicyEnabled(),
                                              rt->GetActivePath()->dest_vn_list(),
                                              rt->GetActivePath()->sg_list(),
                                              rt->GetActivePath()->tag_list());
        rt = NULL;
    } else {
        valid = rt->GetActiveNextHop()->IsValid();
    }

    if (valid != valid_) {
        valid_ = valid;
        ret = true;
    }

    if (dip_.is_v4() && (arp_rt_.get() != rt)) {
        arp_rt_.reset(rt);
        ret = true;
    }

    //If route is present, check if the interface or mac
    //address changed for the dependent route
    if (valid_ && rt) {
        //Check if the interface or mac
        //of the dependent route has changed
        //only then notify the route
        const NextHop *active_nh = rt->GetActiveNextHop();
        const Interface *intf = NULL;
        MacAddress dmac;
        if (active_nh->GetType() == NextHop::ARP) {
            const ArpNH *arp_nh = static_cast<const ArpNH *>(active_nh);
            dmac = arp_nh->GetMac();
            intf = arp_nh->GetInterface();
        } else if (active_nh->GetType() == NextHop::INTERFACE) {
            const InterfaceNH *intf_nh =
                static_cast<const InterfaceNH *>(active_nh);
            intf = intf_nh->GetInterface();
            dmac_.Zero();
        }

        if (dmac_ != dmac) {
            dmac_ = dmac;
            ret = true;
        }

        if (interface_ != intf) {
            interface_ = intf;
            ret = true;
        }
    }

    return ret;
}

void MirrorNH::Delete(const DBRequest *req) {
    if (!GetVrf()) {
        return;
    }
    InetUnicastAgentRouteTable *rt_table = GetRouteTable();
    rt_table->RemoveUnresolvedNH(this);
}

void MirrorNH::SendObjectLog(const NextHopTable *table,
                             AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);

    const VrfEntry *vrf = GetVrf();
    if (vrf) {
        info.set_vrf(vrf->GetName());
    }
    const IpAddress *sip = GetSip();
    info.set_source_ip(sip->to_string());
    const IpAddress *dip = GetDip();
    info.set_dest_ip(dip->to_string());
    info.set_source_port((short int)GetSPort());
    info.set_dest_port((short int)GetDPort());
    OPER_TRACE_ENTRY(NextHop, table, info);
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
void ReceiveNH::Create(NextHopTable *table, const Interface *intf,
                       bool policy) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    InterfaceKey *key =
        static_cast<InterfaceKey *>(intf->GetDBRequestKey().get())->Clone();
    req.key.reset(new ReceiveNHKey(key, policy));
    req.data.reset(new ReceiveNHData());
    table->Process(req);

    key = static_cast<InterfaceKey *>(intf->GetDBRequestKey().get())->Clone();
    req.key.reset(new ReceiveNHKey(key, true));
    table->Process(req);
}

void ReceiveNH::Delete(NextHopTable *table, const Interface *intf,
                       bool policy) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    InterfaceKey *key =
        static_cast<InterfaceKey *>(intf->GetDBRequestKey().get())->Clone();
    req.key.reset(new ReceiveNHKey(key, policy));
    req.data.reset(NULL);
    table->Process(req);

    key = static_cast<InterfaceKey *>(intf->GetDBRequestKey().get())->Clone();
    req.key.reset(new ReceiveNHKey(key, true));
    table->Process(req);
}

void ReceiveNH::SendObjectLog(const NextHopTable *table,
                              AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);

    const Interface *intf = GetInterface();
    FillObjectLogIntf(intf, info);

    OPER_TRACE_ENTRY(NextHop, table, info);
}

/////////////////////////////////////////////////////////////////////////////
// ResolveNH routines
/////////////////////////////////////////////////////////////////////////////
NextHop *ResolveNHKey::AllocEntry() const {
    Interface *intf = static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->Find(intf_key_.get(), true));
    return new ResolveNH(intf, policy_);
}

bool ResolveNH::CanAdd() const {
    return true;
}

void ResolveNH::Create(const InterfaceKey *intf, bool policy) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new ResolveNHKey(intf, policy));
    req.data.reset(new ResolveNHData());
    NextHopTable::GetInstance()->Process(req);
}

void ResolveNH::CreateReq(const InterfaceKey *intf, bool policy) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new ResolveNHKey(intf, policy));
    req.data.reset(new ResolveNHData());
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

void DiscardNH::Create( ) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new DiscardNHKey());
    req.data.reset(new DiscardNHData());
    NextHopTable::GetInstance()->Process(req);
}

/////////////////////////////////////////////////////////////////////////////
// L2ReceiveNH routines
/////////////////////////////////////////////////////////////////////////////
NextHop *L2ReceiveNHKey::AllocEntry() const {
    return new L2ReceiveNH();
}

bool L2ReceiveNH::CanAdd() const {
    return true;
}

void L2ReceiveNH::Create( ) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new L2ReceiveNHKey());
    req.data.reset(new L2ReceiveNHData());
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

bool VlanNH::ChangeEntry(const DBRequest *req) {
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

const boost::uuids::uuid &VlanNH::GetIfUuid() const {
    return interface_->GetUuid();
}

// Create VlanNH for a VPort
void VlanNH::Create(const boost::uuids::uuid &intf_uuid, uint16_t vlan_tag,
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

void VlanNH::Delete(const boost::uuids::uuid &intf_uuid, uint16_t vlan_tag) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    NextHopKey *key = new VlanNHKey(intf_uuid, vlan_tag);
    req.key.reset(key);

    req.data.reset(NULL);
    NextHopTable::GetInstance()->Process(req);
}

// Create VlanNH for a VPort
void VlanNH::CreateReq(const boost::uuids::uuid &intf_uuid, uint16_t vlan_tag,
                    const string &vrf_name, const MacAddress &smac,
                    const MacAddress &dmac) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VlanNHKey(intf_uuid, vlan_tag));
    req.data.reset(new VlanNHData(vrf_name, smac, dmac));
    NextHopTable::GetInstance()->Enqueue(&req);
}

void VlanNH::DeleteReq(const boost::uuids::uuid &intf_uuid, uint16_t vlan_tag) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    NextHopKey *key = new VlanNHKey(intf_uuid, vlan_tag);
    req.key.reset(key);

    req.data.reset(NULL);
    NextHopTable::GetInstance()->Enqueue(&req);
}


VlanNH *VlanNH::Find(const boost::uuids::uuid &intf_uuid, uint16_t vlan_tag) {
    VlanNHKey key(intf_uuid, vlan_tag);
    return static_cast<VlanNH *>(NextHopTable::GetInstance()->FindActiveEntry(&key));
}

void VlanNH::SendObjectLog(const NextHopTable *table,
                           AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;

    FillObjectLog(event, info);

    const Interface *intf = GetInterface();
    FillObjectLogIntf(intf, info);

    const unsigned char *m = GetDMac().GetData();
    FillObjectLogMac(m, info);

    info.set_vlan_tag((short int)GetVlanTag());
    OPER_TRACE_ENTRY(NextHop, table, info);
}

/////////////////////////////////////////////////////////////////////////////
// CompositeNH routines
/////////////////////////////////////////////////////////////////////////////
void CompositeNHKey::ReplaceLocalNexthop(const ComponentNHKeyList &lnh) {
    //Clear all local nexthop
    for (uint32_t i = 0; i < component_nh_key_list_.size();) {
        ComponentNHKeyPtr cnh = component_nh_key_list_[i];
        if (cnh->nh_key()->GetType() == NextHop::INTERFACE) {
            component_nh_key_list_.erase(component_nh_key_list_.begin() + i);
        } else {
            i++;
        }
    }

    component_nh_key_list_.insert(component_nh_key_list_.begin(), lnh.begin(),
                                  lnh.end());
}

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

bool CompositeNH::HasVmInterface(const VmInterface *vmi) const {
    ComponentNHList::const_iterator comp_nh_it =
        component_nh_list_.begin();
    for(;comp_nh_it != component_nh_list_.end(); comp_nh_it++) {
        if ((*comp_nh_it) == NULL) {
            continue;
        }

        if ((*comp_nh_it)->nh()->GetType() == NextHop::INTERFACE) {
            const InterfaceNH *intf_nh = dynamic_cast<const InterfaceNH *>
                ((*comp_nh_it)->nh());
            if (intf_nh->GetInterface() == vmi)
                return true;
        }
        if ((*comp_nh_it)->nh()->GetType() == NextHop::VLAN) {
            const VlanNH *vlan_nh = dynamic_cast<const VlanNH *>
                ((*comp_nh_it)->nh());
            if (vlan_nh->GetInterface() == vmi)
                return true;
        }
    }
    return false;
}

const Interface *CompositeNH::GetFirstLocalEcmpMemberInterface() const {
    if (composite_nh_type_ != Composite::LOCAL_ECMP) {
        return NULL;
    }
    ComponentNHList::const_iterator comp_nh_it =
        component_nh_list_.begin();
    for(;comp_nh_it != component_nh_list_.end(); comp_nh_it++) {
        if (*comp_nh_it == NULL) {
            continue;
        }
        if ((*comp_nh_it)->nh()->GetType() == NextHop::INTERFACE) {
            const InterfaceNH *intf_nh = dynamic_cast<const InterfaceNH *>
                ((*comp_nh_it)->nh());
            return (intf_nh->GetInterface());
        }
        if ((*comp_nh_it)->nh()->GetType() == NextHop::VLAN) {
            const VlanNH *vlan_nh = dynamic_cast<const VlanNH *>
                ((*comp_nh_it)->nh());
            return (vlan_nh->GetInterface());
        }
    }
    return NULL;
}

uint32_t CompositeNH::PickMember(uint32_t seed, uint32_t affinity_index,
                                 bool ingress) const {
    uint32_t idx = kInvalidComponentNHIdx;
    size_t size = component_nh_list_.size();
    if (size == 0) {
        return idx;
    }

    if (affinity_index != kInvalidComponentNHIdx) {
        const NextHop *nh = GetNH(affinity_index);
        if (nh != NULL && nh->IsActive()) {
            return affinity_index;
        }
    }

    idx = seed % size;
    if (component_nh_list_[idx].get() == NULL ||
        component_nh_list_[idx]->nh() == NULL ||
        component_nh_list_[idx]->nh()->IsActive() == false ||
        (ingress == false &&
         component_nh_list_[idx]->nh()->GetType() == NextHop::TUNNEL)) {

        std::vector<uint32_t> active_list;
        for (uint32_t i = 0; i < size; i++) {
            if (i == idx)
                continue;
            if (component_nh_list_[i].get() != NULL &&
                component_nh_list_[i]->nh() != NULL &&
                component_nh_list_[i]->nh()->IsActive()) {
                if (ingress == false) {
                    if (component_nh_list_[i]->nh()->GetType() != NextHop::TUNNEL) {
                        active_list.push_back(i);
                    }
                } else {
                    active_list.push_back(i);
                }
            }
        }
        idx = (active_list.size()) ?
                active_list.at(seed % active_list.size()) :
                kInvalidComponentNHIdx;
    }

    return idx;
}

NextHop *CompositeNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->Find(&vrf_key_, true));
    return new CompositeNH(composite_nh_type_, validate_mcast_src_, policy_,
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

bool CompositeNH::ChangeEntry(const DBRequest* req) {
    bool changed = false;
    CompositeNHData *data = static_cast<CompositeNHData *>(req->data.get());
    if (data && data->pbb_nh_ != pbb_nh_) {
        pbb_nh_ = data->pbb_nh_;
        changed = true;
    }

    if (data && data->learning_enabled_ != learning_enabled_) {
        learning_enabled_ = data->learning_enabled_;
        changed = true;
    }

    if (data && layer2_control_word_ != data->layer2_control_word_) {
        layer2_control_word_ = data->layer2_control_word_;
        changed = true;
    }

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
        //No Change
    } else {
        changed = true;
    }

    if (comp_ecmp_hash_fields_.IsFieldsInUseChanged()) {
        comp_ecmp_hash_fields_.SetHashFieldstoUse();
        changed = true;
    }
    component_nh_list_ = component_nh_list;
    return changed;
}

void CompositeNH::SendObjectLog(const NextHopTable *table,
                                AgentLogEvent::type event) const {
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
    OPER_TRACE_ENTRY(NextHop, table, info);
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
    NextHopKey *key = new CompositeNHKey(composite_nh_type_,
                                         validate_mcast_src_, policy_,
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
            if (type == TunnelType::MPLS_OVER_MPLS) {
                const LabelledTunnelNH *tnh =
                            static_cast<const LabelledTunnelNH *>(nh);
                if (tnh->GetTransportTunnelType() !=
                        TunnelType::ComputeType(TunnelType::MplsType())) {
                    DBRequest tnh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
                    tnh_req.key.reset(new LabelledTunnelNHKey(
                                                tnh->GetVrf()->GetName(),
                                                *(tnh->GetSip()),
                                                *(tnh->GetDip()),
                                                tnh->PolicyEnabled(),
                                                type,
                                                tnh->rewrite_dmac(),
                                                tnh->GetTransportLabel()));
                    tnh_req.data.reset(new LabelledTunnelNHData());
                    agent->nexthop_table()->Process(tnh_req);
                }
            } else {
                const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
                if (type != tnh->GetTunnelType().GetType()) {
                    DBRequest tnh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
                    tnh_req.key.reset(new TunnelNHKey(tnh->GetVrf()->GetName(),
                                                    *(tnh->GetSip()),
                                                    *(tnh->GetDip()),
                                                    tnh->PolicyEnabled(),
                                                    type,
                                                    tnh->rewrite_dmac()));
                    tnh_req.data.reset(new TunnelNHData());
                    agent->nexthop_table()->Process(tnh_req);
                }
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
    TunnelType::Type orig_type = type;
    for (;it != component_nh_key_list.end(); it++) {
        type = orig_type;
        if ((*it) == NULL) {
            continue;
        }

        if ((*it)->nh_key()->GetType() == NextHop::COMPOSITE) {
            CompositeNHKey *composite_nh_key =
                static_cast<CompositeNHKey *>((*it)->nh_key()->Clone());
            if (composite_nh_key->composite_nh_type() == Composite::TOR) {
                type = TunnelType::VXLAN;
            }
            if (composite_nh_key->composite_nh_type() == Composite::FABRIC ||
                composite_nh_key->composite_nh_type() == Composite::L3FABRIC) {
                type = TunnelType::ComputeType(TunnelType::MplsType());
            }
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
    if (composite_nh_type_ == Composite::TOR) {
        type = TunnelType::VXLAN;
    }
    if (composite_nh_type_ == Composite::FABRIC ||
        composite_nh_type_ == Composite::L3FABRIC) {
        type = TunnelType::ComputeType(TunnelType::MplsType());
    }
    //Create all component NH with new tunnel type
    CreateComponentNH(agent, type);

    //Change the tunnel type of all component NH key
    ComponentNHKeyList new_component_nh_key_list = component_nh_key_list_;
    ChangeComponentNHKeyTunnelType(new_component_nh_key_list, type);
    //Create the new nexthop
    CompositeNHKey *comp_nh_key = new CompositeNHKey(composite_nh_type_,
                                                     validate_mcast_src_,
                                                     policy_,
                                                     new_component_nh_key_list,
                                                     vrf_->GetName());
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(comp_nh_key);
    nh_req.data.reset(new CompositeNHData(pbb_nh_, learning_enabled_,
                                          layer2_control_word_));
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

        if (it->nh() && component_nh.nh()) {
            if (it->nh()->MatchEgressData(component_nh.nh())) {
                return true;
            } else if (it->nh() == component_nh.nh()) {
                return true;
            }
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

void CompositeNH::UpdateEcmpHashFieldsUponRouteDelete(Agent *agent,
                                                      const string &vrf_name) {
    if (comp_ecmp_hash_fields_.IsFieldsInUseChanged()) {
        DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
        DBEntryBase::KeyPtr key = GetDBRequestKey();
        NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
        nh_key->sub_op_ = AgentKey::RESYNC;
        nh_req.key = key;
        nh_req.data.reset(NULL);
        agent->nexthop_table()->Process(nh_req);
    }
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
    return new CompositeNHKey(composite_nh_type_, validate_mcast_src_, policy_,
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

bool CompositeNH::UpdateComponentNHKey(uint32_t label, NextHopKey *nh_key,
    ComponentNHKeyList &component_nh_key_list, bool &comp_nh_policy) const {
    bool ret = false;
    comp_nh_policy = false;
    BOOST_FOREACH(ComponentNHPtr it, component_nh_list_) {
        if (it.get() == NULL) {
            ComponentNHKeyPtr dummy_ptr;
            dummy_ptr.reset();
            component_nh_key_list.push_back(dummy_ptr);
            continue;
        }
        const ComponentNH *component_nh = it.get();
        uint32_t new_label = component_nh->label();
        DBEntryBase::KeyPtr key = component_nh->nh()->GetDBRequestKey();
        NextHopKey *lhs = static_cast<NextHopKey *>(key.release());

        if (new_label != label && lhs->IsEqual(*nh_key)) {
            new_label = label;
            ret = true;
        }
        std::auto_ptr<const NextHopKey> nh_key_ptr(lhs);
        ComponentNHKeyPtr component_nh_key(
            new ComponentNHKey(new_label, nh_key_ptr));
        component_nh_key_list.push_back(component_nh_key);
        if (!comp_nh_policy) {
            comp_nh_policy = component_nh->nh()->NexthopToInterfacePolicy();
        }
    }
    return ret;
}

ComponentNHKeyList CompositeNH::AddComponentNHKey(ComponentNHKeyPtr cnh,
                                                  bool &comp_nh_policy) const {
    Agent *agent = static_cast<NextHopTable *>(get_table())->agent();
    const NextHop *nh = static_cast<const NextHop *>(agent->nexthop_table()->
                                       FindActiveEntry(cnh->nh_key()));
    assert(nh);

    ComponentNHKeyList component_nh_key_list = component_nh_key_list_;
    int index = 0;

    comp_nh_policy = false;
    bool made_cnh_list = false;
    BOOST_FOREACH(ComponentNHPtr it, component_nh_list_) {
        const ComponentNH *component_nh = it.get();
        if (component_nh == NULL) {
            index++;
            continue;
        }
        if (component_nh->nh() == nh) {
            if (component_nh->label() == cnh->label()) {
                //Entry already present, return old component nh key list
                comp_nh_policy = PolicyEnabled();
                return component_nh_key_list;
            } else {
                if (nh->GetType() == NextHop::INTERFACE) {
                    component_nh_key_list[index] = cnh;
                    made_cnh_list = true;
                }
                if (!comp_nh_policy) {
                    comp_nh_policy = nh->NexthopToInterfacePolicy();
                }
            }
        } else if (!comp_nh_policy) {
            comp_nh_policy = component_nh->nh()->NexthopToInterfacePolicy();
        }
        if (comp_nh_policy && made_cnh_list) {
            break;
        }
        index++;
    }

    if (made_cnh_list) {
        return component_nh_key_list;
    }

    bool inserted = false;
    index = 0;
    ComponentNHKeyList::const_iterator key_it = component_nh_key_list.begin();
    for (;key_it != component_nh_key_list.end(); key_it++, index++) {
        //If there is a empty slot, in
        //component key list insert the element there.
        if ((*key_it) == NULL) {
            component_nh_key_list[index] = cnh;
            inserted = true;
            break;
        }
    }

    //No empty slots found, insert entry at last
    if (inserted == false) {
        component_nh_key_list.push_back(cnh);
    }
    comp_nh_policy = PolicyEnabled();
    if (!comp_nh_policy && (nh->GetType() == NextHop::INTERFACE)) {
        comp_nh_policy = nh->NexthopToInterfacePolicy();
    }
    return component_nh_key_list;
}

ComponentNHKeyList
CompositeNH::DeleteComponentNHKey(ComponentNHKeyPtr cnh,
                                  bool &comp_nh_new_policy) const {
    Agent *agent = static_cast<NextHopTable *>(get_table())->agent();
    const NextHop *nh = static_cast<const NextHop *>(agent->nexthop_table()->
                                       Find(cnh->nh_key(), true));
    assert(nh);

    ComponentNHKeyList component_nh_key_list = component_nh_key_list_;
    ComponentNHKeyPtr component_nh_key;
    ComponentNHList::const_iterator it = begin();
    comp_nh_new_policy = false;
    bool removed = false;
    int index = 0;
    for (;it != end(); it++, index++) {
        ComponentNHKeyPtr dummy_ptr;
        dummy_ptr.reset();
        if ((*it) && ((*it)->label() == cnh->label() && (*it)->nh() == nh)) {
            component_nh_key_list[index] = dummy_ptr;
            removed = true;
        } else {
            /* Go through all the component Interface Nexthops of this
             * CompositeNH to figure out the new policy status of this
             * CompositeNH. Ignore the component NH being deleted while
             * iterating. */
            if ((*it) && (*it)->nh() && !comp_nh_new_policy) {
                /* If any one of component NH's interface has policy enabled,
                 * the policy-status of compositeNH is true. So we need to
                 * look only until we find the first Interface which has
                 * policy enabled */
                comp_nh_new_policy = (*it)->nh()->NexthopToInterfacePolicy();
            }
        }
        if (removed && comp_nh_new_policy) {
            /* No need to iterate further if we done with both deleting key and
             * figuring out policy-status */
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

// Expand list of local composite members using the MPLS label in
// local-composite key
// Note, another alternative could be to use path created for local-composites
// However, in cases such as service-chain, the MPLS label can point to
// local composite created from different route (route for service-ip)
bool CompositeNHKey::ExpandLocalCompositeNH(Agent *agent) {
    uint32_t label = MplsTable::kInvalidLabel;
    //Find local composite ecmp label
    BOOST_FOREACH(ComponentNHKeyPtr component_nh_key,
                  component_nh_key_list_) {
        if (component_nh_key.get() &&
            component_nh_key->nh_key()->GetType() == NextHop::COMPOSITE) {
            const CompositeNHKey *composite_nh_key =
                static_cast<const CompositeNHKey *>(
                                component_nh_key->nh_key());
            if (composite_nh_key->composite_nh_type() ==
                                        Composite::LOCAL_ECMP) {
                label = component_nh_key->label();
                //Erase the entry from list, it will be replaced with
                //individual entries of this local composite NH
                erase(component_nh_key);
                break;
            }
        }
    }

    //In case of ECMP in fabric VRF there is no mpls
    //label, hence pick policy flag from corresponding
    //interface NH
    if (label ==  MplsTable::kInvalidLabel &&
        vrf_key_.IsEqual(VrfKey(agent->fabric_vrf_name()))) {
        BOOST_FOREACH(ComponentNHKeyPtr component_nh_key,
                component_nh_key_list_) {
            if (component_nh_key.get() &&
                    component_nh_key->nh_key()->GetType() == NextHop::INTERFACE) {
                //Interface NH wouldnt have policy hence pick from VMI
                const NextHop *nh =  static_cast<const NextHop *>(
                    agent->nexthop_table()->FindActiveEntry(component_nh_key->nh_key()));
                if (nh && nh->NexthopToInterfacePolicy()) {
                    return true;
                }
            }
        }
    }

     //No Local composite NH found
    if (label ==  MplsTable::kInvalidLabel) {
        return false;
    }

    MplsLabel *mpls = agent->mpls_table()->FindMplsLabel(label);
    if (mpls == NULL) {
        return false;
    }

    const NextHop *mpls_nh = mpls->nexthop();

    // FIXME: Its possible that the label we have got here is re-cycled one
    // We dont have a good scheme to handle recycled labels. For now ensure
    // that label points to COMPOSITE.
    //
    // If the label is really recyecled, then we will get a route update
    // shortly with new label or route delete
    if (mpls_nh->GetType() != NextHop::COMPOSITE) {
        component_nh_key_list_.clear();
        return false;
    }

    assert(mpls_nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *cnh = static_cast<const CompositeNH *>(mpls_nh);

    bool comp_nh_new_policy = false;
    BOOST_FOREACH(ComponentNHPtr it, cnh->component_nh_list()) {
        if (it.get() == NULL) {
            ComponentNHKeyPtr dummy_ptr;
            dummy_ptr.reset();
            insert(dummy_ptr);
            continue;
        }
        const ComponentNH *component_nh = it.get();
        DBEntryBase::KeyPtr key = component_nh->nh()->GetDBRequestKey();
        NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
        std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
        ComponentNHKeyPtr component_nh_key(
            new ComponentNHKey(component_nh->label(), nh_key_ptr));
        insert(component_nh_key);
        if (!comp_nh_new_policy) {
            comp_nh_new_policy = component_nh->nh()->NexthopToInterfacePolicy();
        }
    }
    return comp_nh_new_policy;
}

bool CompositeNHKey::Reorder(Agent *agent,
                             uint32_t label, const NextHop *nh) {
    //Enqueue request to create Tunnel NH
    CreateTunnelNH(agent);
    //First expand local composite NH, if any
    bool policy = ExpandLocalCompositeNH(agent);
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
        return policy;
    }

    if (nh->GetType() != NextHop::COMPOSITE) {
        DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
        NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
        nh_key->SetPolicy(false);
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
        return policy;
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
    return policy;
}

ComponentNHKey::ComponentNHKey(int label, Composite::Type type, bool policy,
    const ComponentNHKeyList &component_nh_list, const std::string &vrf_name):
    label_(label), nh_key_(new CompositeNHKey(type, policy, component_nh_list,
    vrf_name)) {
}

PBBNH::PBBNH(VrfEntry *vrf, const MacAddress &dest_bmac, uint32_t isid):
    NextHop(NextHop::PBB, true, false), vrf_(vrf, this), dest_bmac_(dest_bmac),
    isid_(isid), label_(MplsTable::kInvalidLabel), child_nh_(NULL){
}

PBBNH::~PBBNH() {
}

bool PBBNH::CanAdd() const {
    if (vrf_ == NULL) {
        LOG(ERROR, "Invalid VRF in PBBNH. Skip Add");
        return false;
    }

    if (dest_bmac_ == MacAddress::ZeroMac()) {
        LOG(ERROR, "Invalid tunnel-destination in PBBNH");
    }

    return true;
}

NextHop *PBBNHKey::AllocEntry() const {
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->Find(&vrf_key_, true));
    return new PBBNH(vrf, dest_bmac_, isid_);
}

bool PBBNH::NextHopIsLess(const DBEntry &rhs) const {
    const PBBNH &a = static_cast<const PBBNH &>(rhs);

    if (vrf_.get() != a.vrf_.get()) {
        return vrf_.get() < a.vrf_.get();
    }

    if (dest_bmac_ != a.dest_bmac_) {
        return dest_bmac_ < a.dest_bmac_;
    }

    return isid_ < a.isid_;
}

void PBBNH::SetKey(const DBRequestKey *k) {
    const PBBNHKey *key = static_cast<const PBBNHKey *>(k);
    NextHop::SetKey(k);
    vrf_ = NextHopTable::GetInstance()->FindVrfEntry(key->vrf_key_);
    dest_bmac_ = key->dest_bmac_;
    policy_ = key->policy_;
    isid_ = key->isid_;
}

PBBNH::KeyPtr PBBNH::GetDBRequestKey() const {
    NextHopKey *key = new PBBNHKey(vrf_->GetName(), dest_bmac_, isid_);
    return DBEntryBase::KeyPtr(key);
}

const uint32_t PBBNH::vrf_id() const {
    return vrf_->vrf_id();
}

bool PBBNH::ChangeEntry(const DBRequest *req) {
    bool ret = false;
    Agent *agent = Agent::GetInstance();
    BridgeAgentRouteTable *rt_table =
        static_cast<BridgeAgentRouteTable *>(vrf_->GetBridgeRouteTable());
    BridgeRouteEntry *rt = rt_table->FindRouteNoLock(dest_bmac_);

    uint32_t label = MplsTable::kInvalidLabel;
    const NextHop *nh = NULL;

    if (!rt) {
        DiscardNHKey key;
        nh = static_cast<NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&key));
    } else {
        nh = rt->GetActiveNextHop();
        label = rt->GetActiveLabel();
    }

    if (nh != child_nh_.get()) {
        child_nh_ = nh;
        ret = true;
    }

    if (label_ != label) {
        label_ = label;
        ret = true;
    }

    if (rt && etree_leaf_ != rt->GetActivePath()->etree_leaf()) {
        etree_leaf_ = rt->GetActivePath()->etree_leaf();
        ret = true;
    }

    return ret;
}

void PBBNH::Delete(const DBRequest *req) {
    child_nh_.reset(NULL);
}

void PBBNH::SendObjectLog(const NextHopTable *table,
                          AgentLogEvent::type event) const {
    NextHopObjectLogInfo info;
    FillObjectLog(event, info);

    if (vrf_) {
        info.set_vrf(vrf_->GetName());
    }
    info.set_mac(dest_bmac_.ToString());
    OPER_TRACE_ENTRY(NextHop, table, info);
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
    case Composite::TOR: {
        comp_str << "TOR Composite"  << " sub nh count: "
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
    case Composite::L3FABRIC: {
        comp_str << "L3 Fabric Composite"  << " sub nh count: "
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
    case Composite::LOCAL_ECMP:
    case Composite::LU_ECMP: {
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
    data.set_vxlan_flag(false);
    data.set_intf_flags(0);
    switch (type_) {
        case DISCARD:
            data.set_type("discard");
            break;
        case L2_RECEIVE:
            data.set_type("l2-receive");
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
            data.set_vxlan_flag(vrf->bridge_nh());
            data.set_flood_unknown_unicast(vrf->flood_unknown_unicast());
            data.set_layer2_control_word(vrf->layer2_control_word());
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
            data.set_layer2_control_word(itf->layer2_control_word());
            data.set_vxlan_flag(itf->IsVxlanRouting());
            data.set_intf_flags(itf->GetFlags());
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
            data.set_crypt_all_traffic(tun->GetCrypt());
            if (tun->GetCryptTunnelAvailable()) {
                data.set_crypt_path_available(tun->GetCryptTunnelAvailable());
                data.set_crypt_interface(tun->GetCryptInterface()->name());
            }
            if (tun->rewrite_dmac().IsZero() == false) {
                data.set_vxlan_flag(true);
                data.set_pbb_bmac(tun->rewrite_dmac().ToString());
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
            data.set_layer2_control_word(comp_nh->layer2_control_word());
            break;
        }

        case PBB: {
             data.set_type("PBB Tunnel");
             const PBBNH *pbb_nh = static_cast<const PBBNH *>(this);
             data.set_pbb_bmac(pbb_nh->dest_bmac().ToString());
             data.set_vrf(pbb_nh->vrf()->GetName());
             data.set_isid(pbb_nh->isid());
             std::vector<McastData> data_list;
             const TunnelNH *tnh =
                 dynamic_cast<const TunnelNH *>(pbb_nh->child_nh());
             if (tnh) {
                 McastData sdata;
                 sdata.set_type("Tunnel");
                 sdata.set_dip(tnh->GetDip()->to_string());
                 sdata.set_sip(tnh->GetSip()->to_string());
                 sdata.set_label(pbb_nh->label());
                 data_list.push_back(sdata);
             }
             data.set_mc_list(data_list);
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

    data.set_learning_enabled(learning_enabled_);
    data.set_etree_leaf(etree_leaf_);

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
    AgentSandeshPtr sand(new AgentNhSandesh(context(), get_type(),
                                  get_nh_index(), get_policy_enabled()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr NextHopTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                              const std::string &context) {
    return AgentSandeshPtr(new AgentNhSandesh(context,
                                              args->GetString("type"), args->GetString("nh_index"),
                                              args->GetString("policy_enabled")));
}
