/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_LOGICAL_SWITCH_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_LOGICAL_SWITCH_OVSDB_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_resource_vxlan_id.h>
#include <base/intrusive_ptr_back_ref.h>

class PhysicalDeviceVn;

namespace OVSDB {
class LogicalSwitchEntry;
class OvsdbResourceVxLanId;

// Logical Switch reference pointer to maintain active references
// in OVSDB database that needs to be deleted before triggering
// delete of a logical switch
typedef IntrusivePtrRef<LogicalSwitchEntry> LogicalSwitchRef;

class LogicalSwitchTable : public OvsdbDBObject {
public:
    typedef std::map<struct ovsdb_idl_row *, LogicalSwitchEntry *> OvsdbIdlRowMap;
    LogicalSwitchTable(OvsdbClientIdl *idl);
    virtual ~LogicalSwitchTable();

    void OvsdbNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);
    void OvsdbMcastLocalMacNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);
    void OvsdbMcastRemoteMacNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);
    void OvsdbUcastLocalMacNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);
    OvsdbDBEntry *AllocOvsEntry(struct ovsdb_idl_row *row);
    DBFilterResp OvsdbDBEntryFilter(const DBEntry *entry,
                                    const OvsdbDBEntry *ovsdb_entry);
    void ProcessDeleteTableReq();

private:
    class ProcessDeleteTableReqTask : public Task {
    public:
        static const int kEntriesPerIteration = 32;
        ProcessDeleteTableReqTask(LogicalSwitchTable *table);
        virtual ~ProcessDeleteTableReqTask();

        bool Run();
        std::string Description() const {
            return "LogicalSwitchTable::ProcessDeleteTableReqTask";
        }

    private:
        LogicalSwitchTable *table_;
        KSyncEntry::KSyncEntryPtr entry_;
        DISALLOW_COPY_AND_ASSIGN(ProcessDeleteTableReqTask);
    };

    OvsdbIdlRowMap  idl_row_map_;
    DISALLOW_COPY_AND_ASSIGN(LogicalSwitchTable);
};

class LogicalSwitchEntry : public OvsdbDBEntry {
public:
    typedef std::set<struct ovsdb_idl_row *> OvsdbIdlRowList;
    enum Trace {
        ADD_REQ,
        DEL_REQ,
        ADD_ACK,
        DEL_ACK,
        DUP_TUNNEL_KEY_ADD,
    };
    LogicalSwitchEntry(OvsdbDBObject *table, const std::string &name);
    LogicalSwitchEntry(OvsdbDBObject *table, const LogicalSwitchEntry *key);
    LogicalSwitchEntry(OvsdbDBObject *table,
            const PhysicalDeviceVn *entry);
    LogicalSwitchEntry(OvsdbDBObject *table,
            struct ovsdb_idl_row *entry);

    virtual ~LogicalSwitchEntry();

    Ip4Address &physical_switch_tunnel_ip();
    void AddMsg(struct ovsdb_idl_txn *);
    void ChangeMsg(struct ovsdb_idl_txn *);
    void DeleteMsg(struct ovsdb_idl_txn *);

    void OvsdbChange();

    const std::string &name() const;
    const std::string &device_name() const;
    int64_t vxlan_id() const;
    std::string tor_service_node() const;
    const OvsdbResourceVxLanId &res_vxlan_id() const;
    bool IsDeleteOvsInProgress() const;

    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Logical Switch";}
    KSyncEntry* UnresolvedReference();

    bool IsLocalMacsRef() const;

    // Override Ack api to get trigger on Ack
    void Ack(bool success);

    // Override TxnDoneNoMessage to get triggers for no message
    // transaction complete
    void TxnDoneNoMessage();

    // API used to remove Logical Switch from OVSDB-database
    // this can be triggered when the relevance of logical switch
    // in OVSDB database is either not required (when physical
    // device VN doesnot exist) or when it is incomplete due to
    // unavailability of logical switch resource like VxLAN ID
    //
    // To trigger delete of Logical Switch from OVSDB server, we
    // must remove the vlan-port bindings and unicast/multicast
    // remote routes before triggering delete of logical switch
    // Logical Switch uses intrusive pointer back reference infra
    // to maintain back references to the objects that needs to be
    // deleted.
    // This API works with an assumption that while this API is
    // triggered Logical switch is either delete marked or deferred
    // so that DELADD_REQ triggers done on dependent entries will
    // release backreference pointer till the Logical switch becomes
    // active again and remains in deferred state
    // Once logical switch is available again in OVSDB-server
    // we program back all the deferred entries
    void DeleteOvs();

private:
    class ProcessDeleteOvsReqTask : public Task {
    public:
        static const int kEntriesPerIteration = 32;
        ProcessDeleteOvsReqTask(LogicalSwitchEntry *entry);
        virtual ~ProcessDeleteOvsReqTask();

        bool Run();
        std::string Description() const {
            return "LogicalSwitchEntry::ProcessDeleteOvsReqTask";
        }

    private:
        KSyncEntry::KSyncEntryPtr entry_;
        DISALLOW_COPY_AND_ASSIGN(ProcessDeleteOvsReqTask);
    };

    // API used to stop running Delete OVS task, this is always
    // called in conjuction with activation or free of the entry
    void CancelDeleteOvs();
    void SendTrace(Trace event) const;
    void DeleteOldMcastRemoteMac();

    void ReleaseLocatorCreateReference();

    friend class LogicalSwitchTable;
    // not defining ref add and ref release for LogicalSwitchEntry
    // will endup calling functions for KSyncEntry class, thus
    // provide base refence infra along with local back ref info
    friend void intrusive_ptr_add_back_ref(IntrusiveReferrer ref,
                                           LogicalSwitchEntry *p);
    friend void intrusive_ptr_del_back_ref(IntrusiveReferrer ref,
                                           LogicalSwitchEntry *p);

    std::string name_;
    std::string device_name_;
    KSyncEntryPtr physical_switch_;
    // self ref to account for local mac from ToR, we hold
    // the reference till timeout or when all the local
    // macs are withdrawn
    KSyncEntryPtr local_mac_ref_;

    // physical_locator create ref
    KSyncEntryPtr pl_create_ref_;

    int64_t vxlan_id_;
    OvsdbIdlRowList mcast_local_row_list_;
    struct ovsdb_idl_row *mcast_remote_row_;
    OvsdbIdlRowList old_mcast_remote_row_list_;
    OvsdbIdlRowList ucast_local_row_list_;

    // indicates deleting logical switch from ovsdb is in process
    // used to identify operation while Logical switch is waiting
    // for local macs to be deleted
    bool delete_ovs_;
    OvsdbResourceVxLanId res_vxlan_id_;
    ProcessDeleteOvsReqTask *del_task_;

    // set of back reference, which needs to be deleted before
    // triggering delete of logical switch
    std::set<IntrusiveReferrer> back_ref_set_;

    DISALLOW_COPY_AND_ASSIGN(LogicalSwitchEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_LOGICAL_SWITCH_OVSDB_H_

