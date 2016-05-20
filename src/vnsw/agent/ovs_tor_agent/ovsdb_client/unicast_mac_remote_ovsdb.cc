/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};
#include <ovs_tor_agent/tor_agent_init.h>
#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <logical_switch_ovsdb.h>
#include <unicast_mac_remote_ovsdb.h>
#include <physical_locator_ovsdb.h>
#include <vrf_ovsdb.h>

#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/agent_path.h>
#include <oper/bridge_route.h>
#include <ovsdb_sandesh.h>
#include <ovsdb_types.h>

using namespace OVSDB;

UnicastMacRemoteEntry::UnicastMacRemoteEntry(UnicastMacRemoteTable *table,
        const std::string mac) : OvsdbDBEntry(table), mac_(mac),
    logical_switch_name_(table->logical_switch_name()),
    self_exported_route_(false), sequence_(0), self_sequence_(0),
    ecmp_suppressed_(false), logical_switch_(NULL, this) {
}

UnicastMacRemoteEntry::UnicastMacRemoteEntry(UnicastMacRemoteTable *table,
        const BridgeRouteEntry *entry) : OvsdbDBEntry(table),
    mac_(entry->mac().ToString()),
    logical_switch_name_(table->logical_switch_name()),
    self_exported_route_(false), sequence_(0), self_sequence_(0),
    ecmp_suppressed_(false), logical_switch_(NULL, this) {
}

UnicastMacRemoteEntry::UnicastMacRemoteEntry(UnicastMacRemoteTable *table,
        const UnicastMacRemoteEntry *entry) : OvsdbDBEntry(table),
    mac_(entry->mac_), logical_switch_name_(entry->logical_switch_name_),
    dest_ip_(entry->dest_ip_), self_exported_route_(false), sequence_(0),
    self_sequence_(0), ecmp_suppressed_(false), logical_switch_(NULL, this) {
}

UnicastMacRemoteEntry::UnicastMacRemoteEntry(UnicastMacRemoteTable *table,
        struct ovsdb_idl_row *entry) : OvsdbDBEntry(table, entry),
    mac_(ovsdb_wrapper_ucast_mac_remote_mac(entry)),
    logical_switch_name_(table->logical_switch_name()), dest_ip_(),
    self_exported_route_(false), sequence_(0), self_sequence_(0),
    ecmp_suppressed_(false), logical_switch_(NULL, this) {
    const char *dest_ip = ovsdb_wrapper_ucast_mac_remote_dst_ip(entry);
    if (dest_ip) {
        dest_ip_ = std::string(dest_ip);
    }
};

UnicastMacRemoteEntry::~UnicastMacRemoteEntry() {
    assert(pl_create_ref_.get() == NULL);
}

void UnicastMacRemoteEntry::NotifyAdd(struct ovsdb_idl_row *row) {
    if (ovs_entry() == NULL || ovs_entry() == row) {
        // this is the first idl row to use let the base infra
        // use it.
        OvsdbDBEntry::NotifyAdd(row);
        return;
    }
    dup_list_.insert(row);
    // trigger change to delete duplicate entries
    table_->Change(this);
}

void UnicastMacRemoteEntry::NotifyDelete(struct ovsdb_idl_row *row) {
    if (ovs_entry() == row) {
        OvsdbDBEntry::NotifyDelete(row);
        return;
    }
    dup_list_.erase(row);
}

void UnicastMacRemoteEntry::PreAddChange() {
    boost::system::error_code ec;
    Ip4Address dest_ip = Ip4Address::from_string(dest_ip_, ec);
    LogicalSwitchTable *l_table = table_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(l_table, logical_switch_name_.c_str());
    LogicalSwitchEntry *logical_switch =
        static_cast<LogicalSwitchEntry *>(l_table->GetReference(&key));

    if (self_exported_route_ ||
            dest_ip == logical_switch->physical_switch_tunnel_ip()) {
        // if the route is self exported or if dest tunnel end-point points to
        // the physical switch itself then donot export this route to OVSDB
        // release reference to logical switch to trigger delete.
        logical_switch_ = NULL;
        return;
    }
    logical_switch_ = logical_switch;
}

void UnicastMacRemoteEntry::PostDelete() {
    logical_switch_ = NULL;
}

void UnicastMacRemoteEntry::AddMsg(struct ovsdb_idl_txn *txn) {
    if (!dup_list_.empty()) {
        // if we have entries in duplicate list clean up all
        // by encoding a delete message and on ack re-trigger
        // Add Msg to return to sane state.
        DeleteMsg(txn);
        return;
    }

    if (logical_switch_.get() == NULL || (dest_ip_.empty() && !stale())) {
        DeleteMsg(txn);
        return;
    }

    if (!stale()) {
        // route was self exported but is not the active path
        if (self_sequence_ != 0 &&
            (ecmp_suppressed_ || sequence_ >= self_sequence_)) {
            // trigger delete of ovs row and add back later
            // to simulate MAC move.
            if (ovs_entry_ != NULL) {
                DeleteMsg(txn);
                return;
            }
        }


        PhysicalLocatorTable *pl_table =
            table_->client_idl()->physical_locator_table();
        PhysicalLocatorEntry pl_key(pl_table, dest_ip_);
        /*
         * we don't take reference to physical locator, just use if locator
         * is existing or we will create a new one.
         */
        PhysicalLocatorEntry *pl_entry =
            static_cast<PhysicalLocatorEntry *>(pl_table->Find(&pl_key));
        struct ovsdb_idl_row *pl_row = NULL;
        if (pl_entry)
            pl_row = pl_entry->ovs_entry();
        LogicalSwitchEntry *logical_switch =
            static_cast<LogicalSwitchEntry *>(logical_switch_.get());
        ovsdb_wrapper_add_ucast_mac_remote(txn, ovs_entry_, mac_.c_str(),
                logical_switch->ovs_entry(), pl_row, dest_ip_.c_str());
        SendTrace(UnicastMacRemoteEntry::ADD_REQ);
    }
}

void UnicastMacRemoteEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
    AddMsg(txn);
}

void UnicastMacRemoteEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
    DeleteDupEntries(txn);
    if (ovs_entry_) {
        ovsdb_wrapper_delete_ucast_mac_remote(ovs_entry_);
        SendTrace(UnicastMacRemoteEntry::DEL_REQ);
    }
}

void UnicastMacRemoteEntry::OvsdbChange() {
    if (!IsResolved())
        table_->SafeNotifyEvent(this, KSyncEntry::ADD_CHANGE_REQ);
}

bool UnicastMacRemoteEntry::Sync(DBEntry *db_entry) {
    const BridgeRouteEntry *entry =
        static_cast<const BridgeRouteEntry *>(db_entry);
    std::string dest_ip;

    const NextHop *nh = entry->GetActiveNextHop();
    const TunnelNH *tunnel = NULL;
    /* 
     * TOR Agent will not have any local VM so only tunnel nexthops
     * are to be looked into
     */
    if (nh && nh->GetType() == NextHop::TUNNEL) {
        /*
         * we don't care the about the tunnel type in nh and always program
         * the entry to ovsdb expecting vrouter to always handle
         * VxLAN encapsulation.
         */
        tunnel = static_cast<const TunnelNH *>(nh);
        dest_ip = tunnel->GetDip()->to_string();
    }
    bool change = false;
    if (dest_ip_ != dest_ip) {
        dest_ip_ = dest_ip;
        change = true;
    }

    // Since OVSDB exports routes to evpn table check for self exported route
    // path in the corresponding evpn route entry, instead of bridge entry
    VrfEntry *vrf = entry->vrf();
    EvpnAgentRouteTable *evpn_table =
        static_cast<EvpnAgentRouteTable *>(vrf->GetEvpnRouteTable());
    Ip4Address default_ip;
    EvpnRouteEntry *evpn_rt = evpn_table->FindRoute(entry->mac(), default_ip,
                                                    entry->GetActiveLabel());

    bool self_exported_route = false;
    uint32_t self_sequence = 0;
    if (evpn_rt != NULL) {
        const AgentPath *ovs_path =
            evpn_rt->FindPath((Peer *)table_->client_idl()->route_peer());
        if (ovs_path != NULL) {
            const NextHop *ovs_nh = ovs_path->nexthop();
            assert(ovs_nh != NULL && ovs_nh->GetType() == NextHop::TUNNEL);
            const TunnelNH *ovs_tunnel = static_cast<const TunnelNH *>(ovs_nh);
            if (tunnel != NULL &&
                *(tunnel->GetDip()) == *(ovs_tunnel->GetDip())) {
                self_exported_route = true;
            }
            self_sequence = ovs_path->sequence();
        }
    }

    if (self_exported_route_ != self_exported_route) {
        self_exported_route_ = self_exported_route;
        change = true;
    }

    if (self_sequence_ != self_sequence) {
        self_sequence_ = self_sequence;
        change = true;
    }

    const AgentPath *path = entry->GetActivePath();
    if (path && sequence_ != path->sequence()) {
        sequence_ = path->sequence();
        change = true;
    }

    if (ecmp_suppressed_ != path->ecmp_suppressed()) {
        ecmp_suppressed_ = path->ecmp_suppressed();
        change = true;
    }

    return change;
}

bool UnicastMacRemoteEntry::IsLess(const KSyncEntry &entry) const {
    const UnicastMacRemoteEntry &ucast =
        static_cast<const UnicastMacRemoteEntry&>(entry);
    if (mac_ != ucast.mac_)
        return mac_ < ucast.mac_;
    return logical_switch_name_ < ucast.logical_switch_name_;
}

KSyncEntry *UnicastMacRemoteEntry::UnresolvedReference() {
    assert(pl_create_ref_.get() == NULL);

    LogicalSwitchTable *l_table = table_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(l_table, logical_switch_name_.c_str());
    LogicalSwitchEntry *l_switch =
        static_cast<LogicalSwitchEntry *>(l_table->GetReference(&key));
    if (!l_switch->IsResolved()) {
        return l_switch;
    }

    if (!dest_ip_.empty()) {
        // check if physical locator is available
        PhysicalLocatorTable *pl_table =
            table_->client_idl()->physical_locator_table();
        PhysicalLocatorEntry pl_key(pl_table, dest_ip_);
        PhysicalLocatorEntry *pl_entry =
            static_cast<PhysicalLocatorEntry *>(pl_table->GetReference(&pl_key));
        if (!pl_entry->IsResolved()) {
            if (stale() || !pl_entry->AcquireCreateRequest(this)) {
                // failed to Acquire Create Request, wait for physical locator
                // we dont Acquire Create request for stale entry
                return pl_entry;
            }
            pl_create_ref_ = pl_entry;
        }
    }

    return NULL;
}

const std::string &UnicastMacRemoteEntry::mac() const {
    return mac_;
}

const std::string &UnicastMacRemoteEntry::logical_switch_name() const {
    return logical_switch_name_;
}

const std::string &UnicastMacRemoteEntry::dest_ip() const {
    return dest_ip_;
}

bool UnicastMacRemoteEntry::self_exported_route() const {
    return self_exported_route_;
}

uint32_t UnicastMacRemoteEntry::sequence() const {
    return sequence_;
}

uint32_t UnicastMacRemoteEntry::self_sequence() const {
    return self_sequence_;
}

bool UnicastMacRemoteEntry::ecmp_suppressed() const {
    return ecmp_suppressed_;
}

void UnicastMacRemoteEntry::Ack(bool success) {
    ReleaseLocatorCreateReference();
    OvsdbDBEntry::Ack(success);
}

void UnicastMacRemoteEntry::TxnDoneNoMessage() {
    ReleaseLocatorCreateReference();
}

void UnicastMacRemoteEntry::SendTrace(Trace event) const {
    SandeshUnicastMacRemoteInfo info;
    switch (event) {
    case ADD_REQ:
        info.set_op("Add Requested");
        break;
    case DEL_REQ:
        info.set_op("Delete Requested");
        break;
    case ADD_ACK:
        info.set_op("Add Received");
        break;
    case DEL_ACK:
        info.set_op("Delete Received");
        break;
    default:
        info.set_op("unknown");
    }
    info.set_mac(mac_);
    info.set_logical_switch(logical_switch_name_);
    info.set_dest_ip(dest_ip_);
    OVSDB_TRACE(UnicastMacRemote, info);
}

// Always called from any message encode Add/Change/Delete
// to trigger delete for deleted entries.
void UnicastMacRemoteEntry::DeleteDupEntries(struct ovsdb_idl_txn *txn) {
    OvsdbDupIdlList::iterator it = dup_list_.begin();
    for (; it != dup_list_.end(); it++) {
        ovsdb_wrapper_delete_ucast_mac_remote(*it);
    }
}

void UnicastMacRemoteEntry::ReleaseLocatorCreateReference() {
    // on Ack Release the physical locator create ref, if present
    if (pl_create_ref_.get() != NULL) {
        // release creator reference on txn complete
        PhysicalLocatorEntry *pl_entry =
            static_cast<PhysicalLocatorEntry *>(pl_create_ref_.get());
        pl_entry->ReleaseCreateRequest(this);
        pl_create_ref_ = NULL;
    }
}

UnicastMacRemoteTable::UnicastMacRemoteTable(OvsdbClientIdl *idl,
        const std::string &logical_switch_name, VrfOvsdbEntry *vrf) :
    OvsdbDBObject(idl, true), logical_switch_name_(logical_switch_name),
    table_delete_ref_(this, NULL), vrf_(vrf) {
}

UnicastMacRemoteTable::~UnicastMacRemoteTable() {
    // Table unregister will be done by Destructor of KSyncDBObject
    table_delete_ref_.Reset(NULL);
}

void UnicastMacRemoteTable::OvsdbRegisterDBTable(DBTable *tbl) {
    OvsdbDBObject::OvsdbRegisterDBTable(tbl);
    AgentRouteTable *table = dynamic_cast<AgentRouteTable *>(tbl);
    table_delete_ref_.Reset(table->deleter());
}

KSyncEntry *UnicastMacRemoteTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const UnicastMacRemoteEntry *k_entry =
        static_cast<const UnicastMacRemoteEntry *>(key);
    UnicastMacRemoteEntry *entry = new UnicastMacRemoteEntry(this, k_entry);
    return entry;
}

KSyncEntry *UnicastMacRemoteTable::DBToKSyncEntry(const DBEntry* db_entry) {
    const BridgeRouteEntry *entry =
        static_cast<const BridgeRouteEntry *>(db_entry);
    UnicastMacRemoteEntry *key = new UnicastMacRemoteEntry(this, entry);
    return static_cast<KSyncEntry *>(key);
}

OvsdbDBEntry *UnicastMacRemoteTable::AllocOvsEntry(struct ovsdb_idl_row *row) {
    UnicastMacRemoteEntry key(this, row);
    return static_cast<OvsdbDBEntry *>(CreateStale(&key));
}

KSyncDBObject::DBFilterResp UnicastMacRemoteTable::OvsdbDBEntryFilter(
        const DBEntry *db_entry, const OvsdbDBEntry *ovsdb_entry) {
    const BridgeRouteEntry *entry =
        static_cast<const BridgeRouteEntry *>(db_entry);
    //Locally programmed multicast route should not be added in
    //OVS.
    if (entry->is_multicast()) {
        return DBFilterIgnore;
    }

    if (entry->vrf()->IsDeleted()) {
        // if notification comes for a entry with deleted vrf,
        // trigger delete since we donot resue same vrf object
        // so this entry has to be deleted eventually.
        return DBFilterDelete;
    }

    return DBFilterAccept;
}

void UnicastMacRemoteTable::ManagedDelete() {
    // We do rely on follow up notification of VRF Delete
    // to handle delete of this route table
}

void UnicastMacRemoteTable::EmptyTable() {
    OvsdbDBObject::EmptyTable();
    // unregister the object if emptytable is called with
    // object being scheduled for delete
    if (delete_scheduled()) {
        // trigger Ack for Vrf entry and reset to NULL
        vrf_->TriggerAck(this);
        vrf_ = NULL;
        KSyncObjectManager::Unregister(this);
    }
}

const std::string &UnicastMacRemoteTable::logical_switch_name() const {
    return logical_switch_name_;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
UnicastMacRemoteSandeshTask::UnicastMacRemoteSandeshTask(
        std::string resp_ctx, AgentSandeshArguments &args) :
    OvsdbSandeshTask(resp_ctx, args) {
    if (args.Get("ls_name", &ls_name_) == false) {
        ls_name_ = "";
    }
    if (args.Get("mac", &mac_) == false) {
        mac_ = "";
    }
}

UnicastMacRemoteSandeshTask::UnicastMacRemoteSandeshTask(
        std::string resp_ctx, const std::string &ip, uint32_t port,
        const std::string &ls, const std::string &mac) :
    OvsdbSandeshTask(resp_ctx, ip, port), ls_name_(ls), mac_(mac) {
}

UnicastMacRemoteSandeshTask::~UnicastMacRemoteSandeshTask() {
}

void UnicastMacRemoteSandeshTask::EncodeArgs(AgentSandeshArguments &args) {
    args.Add("ls_name", ls_name_);
    if (!mac_.empty()) {
        args.Add("mac", mac_);
    }
}

OvsdbSandeshTask::FilterResp
UnicastMacRemoteSandeshTask::Filter(KSyncEntry *kentry) {
    if (!mac_.empty()) {
        UnicastMacRemoteEntry *entry =
            static_cast<UnicastMacRemoteEntry *>(kentry);
        if (entry->mac().find(mac_) != std::string::npos) {
            return FilterAllow;
        }
        return FilterDeny;
    }
    return FilterAllow;
}

void UnicastMacRemoteSandeshTask::UpdateResp(KSyncEntry *kentry,
                                             SandeshResponse *resp) {
    UnicastMacRemoteEntry *entry = static_cast<UnicastMacRemoteEntry *>(kentry);
    OvsdbUnicastMacRemoteEntry oentry;
    oentry.set_state(entry->StateString());
    oentry.set_mac(entry->mac());
    oentry.set_logical_switch(entry->logical_switch_name());
    oentry.set_dest_ip(entry->dest_ip());
    oentry.set_self_exported(entry->self_exported_route());
    oentry.set_sequence(entry->sequence());
    oentry.set_self_sequence(entry->self_sequence());
    oentry.set_ecmp_suppressed(entry->ecmp_suppressed());
    OvsdbUnicastMacRemoteResp *u_resp =
        static_cast<OvsdbUnicastMacRemoteResp *>(resp);
    std::vector<OvsdbUnicastMacRemoteEntry> &macs =
        const_cast<std::vector<OvsdbUnicastMacRemoteEntry>&>(
                u_resp->get_macs());
    macs.push_back(oentry);
}

SandeshResponse *UnicastMacRemoteSandeshTask::Alloc() {
    return static_cast<SandeshResponse *>(new OvsdbUnicastMacRemoteResp());
}

KSyncObject *UnicastMacRemoteSandeshTask::GetObject(
        OvsdbClientSession *session) {
    VrfOvsdbObject *vrf_obj = session->client_idl()->vrf_ovsdb();
    VrfOvsdbEntry vrf_key(vrf_obj, ls_name_);
    VrfOvsdbEntry *vrf_entry =
        static_cast<VrfOvsdbEntry *>(vrf_obj->Find(&vrf_key));
    return static_cast<KSyncObject *>(
            (vrf_entry != NULL) ? vrf_entry->route_table() : NULL);
}

void OvsdbUnicastMacRemoteReq::HandleRequest() const {
    UnicastMacRemoteSandeshTask *task =
        new UnicastMacRemoteSandeshTask(context(), get_session_remote_ip(),
                                        get_session_remote_port(),
                                        get_logical_switch(), get_mac());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

