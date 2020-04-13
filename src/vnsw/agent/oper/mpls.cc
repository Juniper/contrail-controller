/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <base/task_annotations.h>
#include <oper/interface_common.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/agent_sandesh.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>

using namespace std;

SandeshTraceBufferPtr MplsTraceBuf(SandeshTraceBufferCreate("MplsTrace", 1000));

/****************************************************************************
 * MplsLabel routines
 ***************************************************************************/
MplsLabel::MplsLabel(Agent *agent, uint32_t label) :
    agent_(agent), label_(label), free_label_(false) {
}

MplsLabel::~MplsLabel() {
    if (free_label_) {
        if (label_ != MplsTable::kInvalidLabel) {
            MplsTable *table = static_cast<MplsTable *>(get_table());
            table->FreeMplsLabelIndex(label_);
        }
        agent_->resource_manager()->Release(Resource::MPLS_INDEX, label_);
    }
}

bool MplsLabel::IsLess(const DBEntry &rhs) const {
        const MplsLabel &mpls = static_cast<const MplsLabel &>(rhs);
        return label_ < mpls.label_;
}

std::string MplsLabel::ToString() const {
    return "MPLS";
}

DBEntryBase::KeyPtr MplsLabel::GetDBRequestKey() const {
    MplsLabelKey *key = new MplsLabelKey(label_);
    return DBEntryBase::KeyPtr(key);
}

void MplsLabel::SetKey(const DBRequestKey *k) {
    const MplsLabelKey *key = static_cast<const MplsLabelKey *>(k);
    label_ = key->label();
}

uint32_t MplsLabel::GetRefCount() const {
    return AgentRefCount<MplsLabel>::GetRefCount();
}

void MplsLabel::Add(const DBRequest *req) {
    free_label_ = true;
    ChangeInternal(req);
    SendObjectLog(agent_->mpls_table(), AgentLogEvent::ADD);
    return;
}

bool MplsLabel::Change(const DBRequest *req) {
    const AgentDBTable *table = static_cast<const AgentDBTable *>(get_table());
    bool ret = ChangeInternal(req);
    SendObjectLog(table, AgentLogEvent::CHANGE);
    return ret;
}

void MplsLabel::Delete(const DBRequest *req) {
    const AgentDBTable *table = static_cast<const AgentDBTable *>(get_table());
    SendObjectLog(table, AgentLogEvent::DEL);
    return;
}

bool MplsLabel::ChangeInternal(const DBRequest *req) {
    NextHopTable *nh_table = agent_->nexthop_table();
    MplsLabelData *data = static_cast<MplsLabelData *>(req->data.get());
    NextHop *nh =
        static_cast<NextHop *>(nh_table->FindActiveEntry(data->nh_key()));
    if (!nh) {
        // NextHop not found, point mpls label to discard
        DiscardNH key;
        nh = static_cast<NextHop *>(nh_table->FindActiveEntry(&key));
    }

    return ChangeNH(nh);
}

bool MplsLabel::ChangeNH(NextHop *nh) {
    if (nh_ == nh)
        return false;

    assert(nh);
    nh_ = nh;

    if (IsFabricMulticastReservedLabel()) {
        CompositeNH *cnh = dynamic_cast<CompositeNH*>(nh);
        if (cnh && cnh->vrf()) {
            fmg_nh_list_[cnh->vrf()->GetName()] = nh;
        }
    }

    SyncDependentPath();
    return true;
}

void MplsLabel::SyncDependentPath() {
    MPLS_TRACE(MplsTrace, "Syncing routes for label ", label());
    for (DependentPathList::iterator iter =
         mpls_label_.begin(); iter != mpls_label_.end(); iter++) {
        AgentRoute *rt = iter.operator->();
        rt->EnqueueRouteResync();
    }
}

bool MplsLabel::IsFabricMulticastReservedLabel() const {
    //MplsTable *table = static_cast<MplsTable *>(get_table());
    MplsTable *table = static_cast<MplsTable *>(agent_->mpls_table());
    return table->IsFabricMulticastLabel(label_);
}

/****************************************************************************
 * MplsLabel Sandesh routines
 ***************************************************************************/
bool MplsLabel::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    MplsResp *resp = static_cast<MplsResp *>(sresp);

    MplsSandeshData data;
    data.set_label(label_);
    nh_->SetNHSandeshData(data.nh);
    std::vector<MplsSandeshData> &list =
            const_cast<std::vector<MplsSandeshData>&>(resp->get_mpls_list());
    list.push_back(data);

    return true;
}

void MplsLabel::SendObjectLog(const AgentDBTable *table,
                              AgentLogEvent::type event) const {
    MplsObjectLogInfo info;
    string str, type_str, nh_type;
    info.set_type(type_str);
    info.set_label((int)label_);
    switch (event) {
    case AgentLogEvent::ADD:
        str.assign("Addition ");
        break;
    case AgentLogEvent::DEL:
        str.assign("Deletion ");
        info.set_event(str);
        OPER_TRACE_ENTRY(Mpls, table, info);
        return;
    case AgentLogEvent::CHANGE:
        str.assign("Modification ");
        break;
    default:
        str.assign("Unknown");
        break;
    }
    info.set_event(str);
    const NextHop *nh = nexthop();
    const Interface *intf = NULL;
    /* Mpls is not expected to have any other nexthop apart from Interface
       or Vlan */
    if (nh != NULL) {
        string policy_str("Disabled");
        const InterfaceNH *if_nh;
        const VlanNH *vlan_nh;

        switch(nh->GetType()) {
        case NextHop::INTERFACE:
            nh_type.assign("INTERFACE");
            if_nh = static_cast<const InterfaceNH *>(nh);
            intf = if_nh->GetInterface();
            if (if_nh->PolicyEnabled()) {
                policy_str.assign("Enabled");
            }
            info.set_policy(policy_str);
            break;
        case NextHop::VLAN:
            nh_type.assign("VLAN");
            vlan_nh = static_cast<const VlanNH *>(nh);
            intf = vlan_nh->GetInterface();
            info.set_vlan_tag(vlan_nh->GetVlanTag());
            break;
        case NextHop::COMPOSITE:
            nh_type.assign("Composite");
            break;
        default:
            nh_type.assign("unknown");
            break;
        }
    }
    info.set_nh_type(nh_type);
    /* Interface Nexthop pointed by Mpls object will always be of type VMPORT */
    if (intf) {
        string if_type_str;
        switch(intf->type()) {
        case Interface::VM_INTERFACE:
            if_type_str.assign("VM_INTERFACE");
            break;
        default:
            if_type_str.assign("Invalid");
            break;
        }
        info.set_intf_type(if_type_str);
        info.set_intf_uuid(UuidToString(intf->GetUuid()));
        info.set_intf_name(intf->name());
    }
    OPER_TRACE_ENTRY(Mpls, table, info);
}

/****************************************************************************
 * MplsTable routines
 ***************************************************************************/
MplsTable::MplsTable(DB *db, const std::string &name) :
    AgentDBTable(db, name) {
}

MplsTable::~MplsTable() {
}

DBTableBase *MplsTable::CreateTable(DB *db, const std::string &name) {
    MplsTable *table = new MplsTable(db, name);
    table->Init();
    return table;
};

void MplsTable::Process(DBRequest &req) {
    agent()->ConcurrencyCheck();
    DBTablePartition *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
    tpart->Process(NULL, &req);
}

/*
 * Allocates label from resource manager, currently used for evpn and
 * ecmp labels.
 */
uint32_t MplsTable::AllocLabel(ResourceManager::KeyPtr key) {
    uint32_t label = ((static_cast<IndexResourceData *>(agent()->resource_manager()->
                                      Allocate(key).get()))->index());
    assert(label != MplsTable::kInvalidLabel);
    return label;
}

//Free label from resource manager and delete db entry
void MplsTable::FreeLabel(uint32_t label) {
    FreeLabel(label, std::string());
}

void MplsTable::FreeLabel(uint32_t label, const std::string &vrf_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    MplsLabelKey *key = new MplsLabelKey(label);
    MplsLabelData *data = new MplsLabelData(NULL);
    data->set_vrf_name(vrf_name);
    req.key.reset(key);
    req.data.reset(data);

    Process(req);
}

std::auto_ptr<DBEntry> MplsTable::AllocEntry(const DBRequestKey *k) const {
    const MplsLabelKey *key = static_cast<const MplsLabelKey *>(k);
    MplsLabel *mpls = new MplsLabel(agent(), key->label());
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(mpls));
}

DBEntry *MplsTable::Add(const DBRequest *req) {
    CheckVrLabelLimit();
    MplsLabelKey *key = static_cast<MplsLabelKey *>(req->key.get());
    assert(key->label() != MplsTable::kInvalidLabel);

    MplsLabel *mpls = new MplsLabel(agent(), key->label());
    label_table_.InsertAtIndex(mpls->label(), mpls);
    mpls->Add(req);
    return mpls;
}

bool MplsTable::OnChange(DBEntry *entry, const DBRequest *req) {
    MplsLabel *mpls = static_cast<MplsLabel *>(entry);
    return mpls->Change(req);
}

bool MplsTable::Delete(DBEntry *entry, const DBRequest *req) {
    MplsLabel *mpls = static_cast<MplsLabel *>(entry);
    if (IsFabricMulticastLabel(mpls->label())) {
        MplsLabelData *data = static_cast<MplsLabelData *>(req->data.get());
        // For multicast labels we not expect to be here
        // via MplsTable::OnZeroRefcount where data is not set.
        assert(data);
        mpls->fmg_nh_list().erase(data->vrf_name());
        if (mpls->fmg_nh_list().empty() == false) {
            if (mpls->ChangeNH(mpls->fmg_nh_list().begin()->second.get())) {
                DBTablePartBase *tpart =
                    static_cast<DBTablePartition *>(GetTablePartition(mpls));
                tpart->Notify(mpls);
            }
            return false;
        }
    }
    mpls->Delete(req);
    CheckVrLabelLimit();
    return true;
}

void MplsTable::OnZeroRefcount(AgentDBEntry *e) {
    agent()->ConcurrencyCheck();

    //Delete db entry
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key = e->GetDBRequestKey();
    req.data.reset(NULL);
    Process(req);
}

uint32_t MplsTable::CreateRouteLabel(uint32_t label, const NextHopKey *nh_key,
                                const std::string &vrf_name,
                                const std::string &route) {
    if (label == MplsTable::kInvalidLabel) {
        ResourceManager::KeyPtr key(new RouteMplsResourceKey(agent()->
                                resource_manager(), vrf_name,
                                route));
        label = ((static_cast<IndexResourceData *>(agent()->resource_manager()->
                                      Allocate(key).get()))->index());
        assert(label != MplsTable::kInvalidLabel);
        assert(FindMplsLabel(label) == NULL);
    }

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    MplsLabelKey *key = new MplsLabelKey(label);
    req.key.reset(key);

    MplsLabelData *data = new MplsLabelData(nh_key->Clone());
    data->set_vrf_name(vrf_name);
    req.data.reset(data);

    Process(req);
    return label;
}

bool MplsTable::IsFabricMulticastLabel(uint32_t label) const {
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        if ((label >= multicast_label_start_[count]) &&
            (label <= multicast_label_end_[count])) return true;
    }
    return false;
}

void MplsTable::ReserveLabel(uint32_t start, uint32_t end) {
    // We want to allocate labels from an offset
    // Pre-allocate entries
    for (uint32_t i = start; i <= end;  i++) {
        agent()->resource_manager()->ReserveIndex(Resource::MPLS_INDEX, i);
    }
}

void MplsTable::FreeReserveLabel(uint32_t start, uint32_t end) {
    // We want to allocate labels from an offset
    // Pre-allocate entries
    for (uint32_t i = start; i <= end;  i++) {
        agent()->resource_manager()->ReleaseIndex(Resource::MPLS_INDEX, i);
    }
}

void MplsTable::ReserveMulticastLabel(uint32_t start, uint32_t end,
                                      uint8_t idx) {
    multicast_label_start_[idx] = start;
    multicast_label_end_[idx] = end;
    ReserveLabel(start, end);
}

MplsLabel *MplsTable::FindMplsLabel(uint32_t label) {
    MplsLabelKey key(label);
    return static_cast<MplsLabel *>(Find(&key, false));
}

// Allocate label for next-hop(interface, vrf, vlan)
MplsLabel *MplsTable::AllocLabel(const NextHopKey *nh_key) {
    switch(nh_key->GetType()) {
    case NextHop::INTERFACE:
    case NextHop::VLAN:
    case NextHop::VRF:
        break;
    default:
        assert(0);
    }

    // Allocate label from resource manager
    ResourceManager::KeyPtr rkey(new NexthopIndexResourceKey(
                                         agent()->resource_manager(),
                                         nh_key->Clone()));
    uint32_t label = ((static_cast<IndexResourceData *>(agent()->resource_manager()->
                                      Allocate(rkey).get()))->index());
    assert(label != MplsTable::kInvalidLabel);

    // Add MplsLabel db entry
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    MplsLabelKey *key = new MplsLabelKey(label);
    req.key.reset(key);

    MplsLabelData *data = new MplsLabelData(nh_key->Clone());
    req.data.reset(data);

    agent()->mpls_table()->Process(req);

    // Return MplsLabel db entry for nh to hold reference
    MplsLabel *mpls_label = static_cast<MplsLabel *>
        (agent()->mpls_table()->FindActiveEntry(key));
    assert(mpls_label);

    return mpls_label;
}

void MplsTable::CheckVrLabelLimit() {
    VrLimitExceeded &vr_limits = agent()->get_vr_limits_exceeded_map();
    VrLimitExceeded::iterator vr_limit_itr = vr_limits.find("vr_mpls_labels");
    if (vr_limit_itr->second == "Normal") {
        if (label_table_.InUseIndexCount() >= ((agent()->vr_limit_high_watermark() *
            agent()->vrouter_max_labels())/100) ) {
            vr_limit_itr->second.assign(std::string("Exceeded"));
            LOG(ERROR, "Vrouter Mpls Labels Exceeded.");
        }
    } else if ( vr_limit_itr->second == "Exceeded") {
        if (label_table_.InUseIndexCount() >= agent()->vrouter_max_labels()) {
            vr_limit_itr->second.assign(std::string("TableLimit"));
            LOG(ERROR, "Vrouter Mpls Lablels Table Limit Reached. Skip Label Add.");
        } else if ( label_table_.InUseIndexCount() < ((agent()->vr_limit_low_watermark() *
            agent()->vrouter_max_labels())/100) ) {
            vr_limit_itr->second.assign(std::string("Normal"));
        }
    } else if ( vr_limit_itr->second == "TableLimit" ) {
        if (label_table_.InUseIndexCount() <
            ((agent()->vrouter_max_labels()*95)/100) ) {
            vr_limit_itr->second.assign(std::string("Exceeded"));
            LOG(ERROR, "Vrouter Mpls Labels Exceeded.");
        }
    }
    agent()->set_vr_limits_exceeded_map(vr_limits);
}

AgentSandeshPtr MplsTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                           const std::string &context) {
    return AgentSandeshPtr(new AgentMplsSandesh
                           (context, args->GetString("type"),
                            args->GetString("label")));
}

void MplsReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentMplsSandesh(context(), get_type(),
                                              get_label()));
    sand->DoSandesh(sand);
}
