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
#include <physical_switch_ovsdb.h>
#include <logical_switch_ovsdb.h>
#include <physical_locator_ovsdb.h>

#include <oper/vn.h>
#include <oper/physical_device.h>
#include <oper/physical_device_vn.h>
#include <ovsdb_types.h>

using OVSDB::LogicalSwitchEntry;
using OVSDB::LogicalSwitchTable;
using OVSDB::OvsdbDBEntry;
using OVSDB::OvsdbDBObject;
using OVSDB::OvsdbClient;
using OVSDB::OvsdbClientSession;

LogicalSwitchEntry::LogicalSwitchEntry(OvsdbDBObject *table,
        const PhysicalDeviceVn *entry) : OvsdbDBEntry(table),
        name_(UuidToString(entry->vn()->GetUuid())), mcast_local_row_(NULL),
        mcast_remote_row_(NULL), old_mcast_remote_row_(NULL) {
    vxlan_id_ = entry->vn()->GetVxLanId();
    device_name_ = entry->device()->name();
}

LogicalSwitchEntry::LogicalSwitchEntry(OvsdbDBObject *table,
        const LogicalSwitchEntry *entry) : OvsdbDBEntry(table),
        mcast_local_row_(NULL), mcast_remote_row_(NULL),
        old_mcast_remote_row_(NULL) {
    name_ = entry->name_;
    vxlan_id_ = entry->vxlan_id_;;
    device_name_ = entry->device_name_;
}

LogicalSwitchEntry::LogicalSwitchEntry(OvsdbDBObject *table,
        struct ovsdb_idl_row *entry) : OvsdbDBEntry(table, entry),
        name_(ovsdb_wrapper_logical_switch_name(entry)), device_name_(""),
        vxlan_id_(ovsdb_wrapper_logical_switch_tunnel_key(entry)),
        mcast_local_row_(NULL), mcast_remote_row_(NULL),
        old_mcast_remote_row_(NULL) {
}

Ip4Address &LogicalSwitchEntry::physical_switch_tunnel_ip() {
    PhysicalSwitchEntry *p_switch =
        static_cast<PhysicalSwitchEntry *>(physical_switch_.get());
    return p_switch->tunnel_ip();
}

void LogicalSwitchEntry::AddMsg(struct ovsdb_idl_txn *txn) {
    PhysicalSwitchTable *p_table = table_->client_idl()->physical_switch_table();
    PhysicalSwitchEntry key(p_table, device_name_.c_str());
    physical_switch_ = p_table->GetReference(&key);
    struct ovsdb_idl_row *row =
        ovsdb_wrapper_add_logical_switch(txn, ovs_entry_, name_.c_str(),
                vxlan_id_);
    /* Add remote multicast entry if not already present */
    if (mcast_remote_row_ == NULL) {
        std::string dest_ip = table_->client_idl()->tsn_ip().to_string();
        PhysicalLocatorTable *pl_table =
            table_->client_idl()->physical_locator_table();
        PhysicalLocatorEntry pl_key(pl_table, dest_ip);
        /*
         * we don't take reference to physical locator, just use if locator
         * is existing or we will create a new one.
         */
        PhysicalLocatorEntry *pl_entry =
            static_cast<PhysicalLocatorEntry *>(pl_table->Find(&pl_key));
        struct ovsdb_idl_row *pl_row = NULL;
        if (pl_entry)
            pl_row = pl_entry->ovs_entry();
        ovsdb_wrapper_add_mcast_mac_remote(txn, NULL, "unknown-dst", row,
                pl_row, dest_ip.c_str());
    }
    if (old_mcast_remote_row_ != NULL) {
        ovsdb_wrapper_delete_mcast_mac_remote(old_mcast_remote_row_);
    }
    SendTrace(LogicalSwitchEntry::ADD_REQ);
}

void LogicalSwitchEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
    AddMsg(txn);
}

void LogicalSwitchEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
    physical_switch_ = NULL;
    if (mcast_local_row_ != NULL) {
        ovsdb_wrapper_delete_mcast_mac_local(mcast_local_row_);
    }
    if (mcast_remote_row_ != NULL) {
        ovsdb_wrapper_delete_mcast_mac_remote(mcast_remote_row_);
    }
    if (old_mcast_remote_row_ != NULL) {
        ovsdb_wrapper_delete_mcast_mac_remote(old_mcast_remote_row_);
    }
    if (ovs_entry_ != NULL) {
        ovsdb_wrapper_delete_logical_switch(ovs_entry_);
    }
    SendTrace(LogicalSwitchEntry::DEL_REQ);
}

void LogicalSwitchEntry::OvsdbChange() {
    if (!IsResolved())
        table_->NotifyEvent(this, KSyncEntry::ADD_CHANGE_REQ);
}

const std::string &LogicalSwitchEntry::name() const {
    return name_;
}

const std::string &LogicalSwitchEntry::device_name() const {
    return device_name_;
}

int64_t LogicalSwitchEntry::vxlan_id() const {
    return vxlan_id_;
}

bool LogicalSwitchEntry::Sync(DBEntry *db_entry) {
    PhysicalDeviceVn *entry =
        static_cast<PhysicalDeviceVn *>(db_entry);
    bool change = false;
    if (vxlan_id_ != entry->vn()->GetVxLanId()) {
        vxlan_id_ = entry->vn()->GetVxLanId();
        change = true;
    }
    if (device_name_ != entry->device()->name()) {
        device_name_ = entry->device()->name();
        change = true;
    }
    return change;
}

bool LogicalSwitchEntry::IsLess(const KSyncEntry &entry) const {
    const LogicalSwitchEntry &ps_entry =
        static_cast<const LogicalSwitchEntry&>(entry);
    return (name_.compare(ps_entry.name_) < 0);
}

KSyncEntry *LogicalSwitchEntry::UnresolvedReference() {
    PhysicalSwitchTable *p_table = table_->client_idl()->physical_switch_table();
    PhysicalSwitchEntry key(p_table, device_name_.c_str());
    PhysicalSwitchEntry *p_switch =
        static_cast<PhysicalSwitchEntry *>(p_table->GetReference(&key));
    if (!p_switch->IsResolved()) {
        return p_switch;
    }
    return NULL;
}

void LogicalSwitchEntry::SendTrace(Trace event) const {
    SandeshLogicalSwitchInfo info;
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
    info.set_name(name_);
    info.set_device_name(device_name_);
    info.set_vxlan(vxlan_id_);
    OVSDB_TRACE(LogicalSwitch, info);
}

LogicalSwitchTable::LogicalSwitchTable(OvsdbClientIdl *idl, DBTable *table) :
    OvsdbDBObject(idl, table) {
    idl->Register(OvsdbClientIdl::OVSDB_LOGICAL_SWITCH,
                  boost::bind(&LogicalSwitchTable::OvsdbNotify, this, _1, _2));
    idl->Register(OvsdbClientIdl::OVSDB_MCAST_MAC_LOCAL,
                  boost::bind(&LogicalSwitchTable::OvsdbMcastLocalMacNotify,
                      this, _1, _2));
    idl->Register(OvsdbClientIdl::OVSDB_MCAST_MAC_REMOTE,
                  boost::bind(&LogicalSwitchTable::OvsdbMcastRemoteMacNotify,
                      this, _1, _2));
}

LogicalSwitchTable::~LogicalSwitchTable() {
    client_idl_->UnRegister(OvsdbClientIdl::OVSDB_LOGICAL_SWITCH);
    client_idl_->UnRegister(OvsdbClientIdl::OVSDB_MCAST_MAC_LOCAL);
    client_idl_->UnRegister(OvsdbClientIdl::OVSDB_MCAST_MAC_REMOTE);
}

void LogicalSwitchTable::OvsdbNotify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    LogicalSwitchEntry key(this, row);
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        NotifyDeleteOvsdb((OvsdbDBEntry*)&key);
        key.SendTrace(LogicalSwitchEntry::DEL_ACK);
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        NotifyAddOvsdb((OvsdbDBEntry*)&key, row);
        key.SendTrace(LogicalSwitchEntry::ADD_ACK);
    } else {
        assert(0);
    }
}

void LogicalSwitchTable::OvsdbMcastLocalMacNotify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    const char *mac = ovsdb_wrapper_mcast_mac_local_mac(row);
    const char *ls = ovsdb_wrapper_mcast_mac_local_logical_switch(row);
    LogicalSwitchEntry *entry = NULL;
    if (ls) {
        LogicalSwitchEntry key(this, ls);
        entry = static_cast<LogicalSwitchEntry *>(Find(&key));
    }
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        OVSDB_TRACE(Trace, "Delete : Local Mcast MAC " + std::string(mac) +
                ", logical switch " + (ls ? std::string(ls) : ""));
        if (entry) {
            entry->mcast_local_row_ = NULL;
        }
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        OVSDB_TRACE(Trace, "Add : Local Mcast MAC " + std::string(mac) +
                ", logical switch " + (ls ? std::string(ls) : ""));
        if (entry) {
            entry->mcast_local_row_ = row;
        }
    } else {
        assert(0);
    }
}

void LogicalSwitchTable::OvsdbMcastRemoteMacNotify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    const char *mac = ovsdb_wrapper_mcast_mac_remote_mac(row);
    const char *ls = ovsdb_wrapper_mcast_mac_remote_logical_switch(row);
    LogicalSwitchEntry *entry = NULL;
    if (ls) {
        LogicalSwitchEntry key(this, ls);
        entry = static_cast<LogicalSwitchEntry *>(Find(&key));
    }
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        OVSDB_TRACE(Trace, "Delete : Remote Mcast MAC " + std::string(mac) +
                ", logical switch " + (ls ? std::string(ls) : ""));
        if (entry) {
            if (entry->old_mcast_remote_row_ == row)
                entry->old_mcast_remote_row_ = NULL;
            if (entry->mcast_remote_row_ == row)
                entry->mcast_remote_row_ = NULL;
        }
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        OVSDB_TRACE(Trace, "Add : Remote Mcast MAC " + std::string(mac) +
                ", logical switch " + (ls ? std::string(ls) : ""));
        if (entry) {
            if (entry->mcast_remote_row_ != row) {
                entry->old_mcast_remote_row_ = entry->mcast_remote_row_;
                entry->mcast_remote_row_ = row;
                if (entry->old_mcast_remote_row_ != NULL)
                    entry->OvsdbChange();
            }
        }
    } else {
        assert(0);
    }
}

KSyncEntry *LogicalSwitchTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const LogicalSwitchEntry *k_entry =
        static_cast<const LogicalSwitchEntry *>(key);
    LogicalSwitchEntry *entry = new LogicalSwitchEntry(this, k_entry);
    return entry;
}

KSyncEntry *LogicalSwitchTable::DBToKSyncEntry(const DBEntry* db_entry) {
    const PhysicalDeviceVn *entry =
        static_cast<const PhysicalDeviceVn *>(db_entry);
    LogicalSwitchEntry *key = new LogicalSwitchEntry(this, entry);
    return static_cast<KSyncEntry *>(key);
}

OvsdbDBEntry *LogicalSwitchTable::AllocOvsEntry(struct ovsdb_idl_row *row) {
    LogicalSwitchEntry key(this, row);
    return static_cast<OvsdbDBEntry *>(Create(&key));
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class LogicalSwitchSandeshTask : public Task {
public:
    LogicalSwitchSandeshTask(std::string resp_ctx) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::KSync")), -1),
        resp_(new OvsdbLogicalSwitchResp()), resp_data_(resp_ctx) {
    }
    virtual ~LogicalSwitchSandeshTask() {}
    virtual bool Run() {
        std::vector<OvsdbLogicalSwitchEntry> lswitch;
        TorAgentInit *init =
            static_cast<TorAgentInit *>(Agent::GetInstance()->agent_init());
        OvsdbClientSession *session = init->ovsdb_client()->next_session(NULL);
        LogicalSwitchTable *table =
            session->client_idl()->logical_switch_table();
        LogicalSwitchEntry *entry =
            static_cast<LogicalSwitchEntry *>(table->Next(NULL));
        while (entry != NULL) {
            OvsdbLogicalSwitchEntry lentry;
            lentry.set_state(entry->StateString());
            lentry.set_name(entry->name());
            lentry.set_physical_switch(entry->device_name());
            lentry.set_vxlan_id(entry->vxlan_id());
            lswitch.push_back(lentry);
            entry = static_cast<LogicalSwitchEntry *>(table->Next(entry));
        }
        resp_->set_lswitch(lswitch);
        SendResponse();
        return true;
    }
private:
    void SendResponse() {
        resp_->set_context(resp_data_);
        resp_->set_more(false);
        resp_->Response();
    }

    OvsdbLogicalSwitchResp *resp_;
    std::string resp_data_;
    DISALLOW_COPY_AND_ASSIGN(LogicalSwitchSandeshTask);
};

void OvsdbLogicalSwitchReq::HandleRequest() const {
    LogicalSwitchSandeshTask *task = new LogicalSwitchSandeshTask(context());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}
