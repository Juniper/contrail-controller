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
#include <ovsdb_types.h>

using OVSDB::UnicastMacRemoteEntry;
using OVSDB::UnicastMacRemoteTable;
using OVSDB::VrfOvsdbEntry;
using OVSDB::VrfOvsdbObject;
using OVSDB::OvsdbDBEntry;
using OVSDB::OvsdbDBObject;
using OVSDB::OvsdbClient;
using OVSDB::OvsdbClientSession;

UnicastMacRemoteEntry::UnicastMacRemoteEntry(UnicastMacRemoteTable *table,
        const std::string mac) : OvsdbDBEntry(table), mac_(mac),
    logical_switch_name_(table->logical_switch_name()),
    self_exported_route_(false) {
}

UnicastMacRemoteEntry::UnicastMacRemoteEntry(UnicastMacRemoteTable *table,
        const BridgeRouteEntry *entry) : OvsdbDBEntry(table),
    mac_(entry->mac().ToString()),
    logical_switch_name_(table->logical_switch_name()),
    self_exported_route_(false) {
}

UnicastMacRemoteEntry::UnicastMacRemoteEntry(UnicastMacRemoteTable *table,
        const UnicastMacRemoteEntry *entry) : OvsdbDBEntry(table),
    mac_(entry->mac_), logical_switch_name_(entry->logical_switch_name_),
    dest_ip_(entry->dest_ip_), self_exported_route_(false) {
}

UnicastMacRemoteEntry::UnicastMacRemoteEntry(UnicastMacRemoteTable *table,
        struct ovsdb_idl_row *entry) : OvsdbDBEntry(table, entry),
    mac_(ovsdb_wrapper_ucast_mac_remote_mac(entry)),
    logical_switch_name_(table->logical_switch_name()), dest_ip_(),
    self_exported_route_(false) {
    const char *dest_ip = ovsdb_wrapper_ucast_mac_remote_dst_ip(entry);
    if (dest_ip) {
        dest_ip_ = std::string(dest_ip);
    }
};

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

    if (logical_switch_.get() == NULL) {
        DeleteMsg(txn);
        return;
    }
    if (ovs_entry_ == NULL && !dest_ip_.empty() && !stale()) {
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
        obvsdb_wrapper_add_ucast_mac_remote(txn, mac_.c_str(),
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
        table_->NotifyEvent(this, KSyncEntry::ADD_CHANGE_REQ);
}

bool UnicastMacRemoteEntry::Sync(DBEntry *db_entry) {
    const BridgeRouteEntry *entry =
        static_cast<const BridgeRouteEntry *>(db_entry);
    std::string dest_ip;
    const NextHop *nh = entry->GetActiveNextHop();
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
        const TunnelNH *tunnel = static_cast<const TunnelNH *>(nh);
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

    bool self_exported_route =
        (evpn_rt != NULL &&
         evpn_rt->FindPath((Peer *)table_->client_idl()->route_peer()) != NULL);
    if (self_exported_route_ != self_exported_route) {
        self_exported_route_ = self_exported_route;
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
    LogicalSwitchTable *l_table = table_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(l_table, logical_switch_name_.c_str());
    LogicalSwitchEntry *l_switch =
        static_cast<LogicalSwitchEntry *>(l_table->GetReference(&key));
    if (!l_switch->IsResolved()) {
        return l_switch;
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

UnicastMacRemoteTable::UnicastMacRemoteTable(OvsdbClientIdl *idl,
        const std::string &logical_switch_name) :
    OvsdbDBObject(idl, true), logical_switch_name_(logical_switch_name),
    table_delete_ref_(this, NULL) {
}

UnicastMacRemoteTable::UnicastMacRemoteTable(OvsdbClientIdl *idl,
        AgentRouteTable *table, const std::string &logical_switch_name) :
    OvsdbDBObject(idl, table, true), logical_switch_name_(logical_switch_name),
    table_delete_ref_(this, table->deleter()) {
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

void UnicastMacRemoteTable::OvsdbNotify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    const char *mac = ovsdb_wrapper_ucast_mac_remote_mac(row);
    const char *logical_switch =
        ovsdb_wrapper_ucast_mac_remote_logical_switch(row);
    /* if logical switch is not available ignore nodtification */
    if (logical_switch == NULL)
        return;
    const char *dest_ip = ovsdb_wrapper_ucast_mac_remote_dst_ip(row);
    UnicastMacRemoteEntry key(this, mac);
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        NotifyDeleteOvsdb((OvsdbDBEntry*)&key, row);
        if (dest_ip)
            key.dest_ip_ = std::string(dest_ip);
        key.SendTrace(UnicastMacRemoteEntry::DEL_ACK);
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        NotifyAddOvsdb((OvsdbDBEntry*)&key, row);
        if (dest_ip)
            key.dest_ip_ = std::string(dest_ip);
        key.SendTrace(UnicastMacRemoteEntry::ADD_ACK);
    } else {
        assert(0);
    }
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
        KSyncObjectManager::Unregister(this);
    }
}

const std::string &UnicastMacRemoteTable::logical_switch_name() const {
    return logical_switch_name_;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class UnicastMacRemoteSandeshTask : public Task {
public:
    UnicastMacRemoteSandeshTask(std::string resp_ctx, const std::string &ip,
                                uint32_t port) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::KSync")), 0),
        resp_(new OvsdbUnicastMacRemoteResp()), resp_data_(resp_ctx),
        ip_(ip), port_(port) {
    }
    virtual ~UnicastMacRemoteSandeshTask() {}
    virtual bool Run() {
        std::vector<OvsdbUnicastMacRemoteEntry> macs;
        OvsdbClient *ovsdb_client = Agent::GetInstance()->ovsdb_client();
        OvsdbClientSession *session;
        if (ip_.empty()) {
            session = ovsdb_client->NextSession(NULL);
        } else {
            boost::system::error_code ec;
            Ip4Address ip_addr = Ip4Address::from_string(ip_, ec);
            session = ovsdb_client->FindSession(ip_addr, port_);
        }
        if (session != NULL && session->client_idl() != NULL) {
            VrfOvsdbObject *vrf_obj = session->client_idl()->vrf_ovsdb();
            VrfOvsdbEntry *vrf_entry =
                static_cast<VrfOvsdbEntry *>(vrf_obj->Next(NULL));
            while (vrf_entry != NULL) {
                UnicastMacRemoteTable *table = vrf_entry->route_table();
                UnicastMacRemoteEntry *entry =
                    static_cast<UnicastMacRemoteEntry *>(table->Next(NULL));
                while (entry != NULL) {
                    OvsdbUnicastMacRemoteEntry oentry;
                    oentry.set_state(entry->StateString());
                    oentry.set_mac(entry->mac());
                    oentry.set_logical_switch(entry->logical_switch_name());
                    oentry.set_dest_ip(entry->dest_ip());
                    oentry.set_self_exported(entry->self_exported_route());
                    macs.push_back(oentry);
                    entry =
                        static_cast<UnicastMacRemoteEntry *>(table->Next(entry));
                }
                vrf_entry =
                    static_cast<VrfOvsdbEntry *>(vrf_obj->Next(vrf_entry));
            }
        }
        resp_->set_macs(macs);
        SendResponse();
        return true;
    }
private:
    void SendResponse() {
        resp_->set_context(resp_data_);
        resp_->set_more(false);
        resp_->Response();
    }

    OvsdbUnicastMacRemoteResp *resp_;
    std::string resp_data_;
    std::string ip_;
    uint32_t port_;
    DISALLOW_COPY_AND_ASSIGN(UnicastMacRemoteSandeshTask);
};

void OvsdbUnicastMacRemoteReq::HandleRequest() const {
    UnicastMacRemoteSandeshTask *task =
        new UnicastMacRemoteSandeshTask(context(), get_session_remote_ip(),
                                        get_session_remote_port());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

