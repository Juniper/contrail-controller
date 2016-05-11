/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_IDL_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_IDL_H_

#include <assert.h>
#include <queue>

#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>
#include <ksync/ksync_entry.h>

extern SandeshTraceBufferPtr OvsdbTraceBuf;
extern SandeshTraceBufferPtr OvsdbSMTraceBuf;
extern SandeshTraceBufferPtr OvsdbPktTraceBuf;

#define OVSDB_TRACE(obj, ...)\
do {\
    Ovsdb##obj::TraceMsg(OvsdbTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(false);

#define OVSDB_PKT_TRACE(obj, ...)\
do {\
    Ovsdb##obj::TraceMsg(OvsdbPktTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(false);

class OvsPeer;
class OvsPeerManager;
class KSyncObjectManager;

namespace OVSDB {
class OvsdbClientSession;
class VMInterfaceKSyncObject;
class PhysicalSwitchTable;
class LogicalSwitchTable;
class PhysicalPortTable;
class PhysicalLocatorTable;
class VlanPortBindingTable;
class PhysicalLocatorTable;
class UnicastMacLocalOvsdb;
class MulticastMacLocalOvsdb;
class VrfOvsdbObject;
class VnOvsdbObject;
class OvsdbEntryBase;
class ConnectionStateTable;
class OvsdbResourceVxLanIdTable;

class OvsdbClientIdl;
typedef boost::intrusive_ptr<OvsdbClientIdl> OvsdbClientIdlPtr;
typedef std::set<OvsdbEntryBase *> OvsdbEntryList;

class OvsdbClientIdl {
public:
    // OvsdbSessionState represent state of the session.
    // it starts with OvsdbSessionRcvWait representing that idl is waiting
    // for a message to come from ovsdb-server on message receive on session
    // it moves to OvsdbSessionActive state, if keepalive timer fires with
    // idl being in OvsdbSessionRcvWait state, it triggers an echo req (keep
    // alive message) and moves to OvsdbSessionEchoWait state waiting for
    // echo response, if there is no response till the next time timer fires
    // it triggers the close of session.
    enum OvsdbSessionState {
        OvsdbSessionActive = 0,  // Actively receiving messages
        OvsdbSessionRcvWait,     // Waiting to receive next message
        OvsdbSessionEchoWait     // Echo Req sent waiting for reply
    };

    static const std::size_t OVSDBMaxInFlightPendingTxn = 25;
    static const std::size_t OVSDBEntriesInBulkTxn = 4;

    enum Op {
        OVSDB_DEL = 0,
        OVSDB_ADD,
        OVSDB_INVALID_OP
    };

    enum EntryType {
        OVSDB_PHYSICAL_SWITCH = 0,
        OVSDB_LOGICAL_SWITCH,
        OVSDB_PHYSICAL_PORT,
        OVSDB_PHYSICAL_LOCATOR,
        OVSDB_UCAST_MAC_LOCAL,
        OVSDB_UCAST_MAC_REMOTE,
        OVSDB_PHYSICAL_LOCATOR_SET,
        OVSDB_MCAST_MAC_LOCAL,
        OVSDB_MCAST_MAC_REMOTE,
        OVSDB_TYPE_COUNT
    };

    struct OvsdbMsg {
        OvsdbMsg(struct jsonrpc_msg *m);
        ~OvsdbMsg();
        struct jsonrpc_msg *msg;
    };

    struct TxnStats {
        TxnStats();
        uint64_t txn_initiated;
        uint64_t txn_succeeded;
        uint64_t txn_failed;
    };

    typedef boost::function<void(OvsdbClientIdl::Op, struct ovsdb_idl_row *)> NotifyCB;
    typedef std::map<struct ovsdb_idl_txn *, OvsdbEntryList> PendingTxnMap;
    typedef std::queue<struct jsonrpc_msg *> ThrottledTxnMsgs;

    OvsdbClientIdl(OvsdbClientSession *session, Agent *agent, OvsPeerManager *manager);
    virtual ~OvsdbClientIdl();

    // Callback from receive_queue to process the OVSDB Messages
    bool ProcessMessage(OvsdbMsg *msg);

    // Send request to start monitoring OVSDB server
    void OnEstablish();

    // Encode and send json rpc message to OVSDB server
    // takes ownership of jsonrpc message, and free memory
    void TxnScheduleJsonRpc(struct jsonrpc_msg *msg);

    // Process the recevied message and trigger update to ovsdb client
    void MessageProcess(const u_int8_t *buf, std::size_t len);
    // Create a OVSDB transaction to start encoding an update
    struct ovsdb_idl_txn *CreateTxn(OvsdbEntryBase *entry,
            KSyncEntry::KSyncEvent ack_event = KSyncEntry::ADD_ACK);
    // Create a Bulk OVSDB transaction to start encoding a bulk update
    struct ovsdb_idl_txn *CreateBulkTxn(OvsdbEntryBase *entry,
            KSyncEntry::KSyncEvent ack_event = KSyncEntry::ADD_ACK);

    // encode and send a transaction
    bool EncodeSendTxn(struct ovsdb_idl_txn *txn, OvsdbEntryBase *skip_entry);

    // Delete the OVSDB transaction
    void DeleteTxn(struct ovsdb_idl_txn *txn);
    void Register(EntryType type, NotifyCB cb) {callback_[type] = cb;}
    void UnRegister(EntryType type) {callback_[type] = NULL;}

    // Notify Delete followed by add for a given ovsdb_idl_row
    void NotifyDelAdd(struct ovsdb_idl_row *row);
    // Get TOR Service Node IP
    Ip4Address tsn_ip();

    // Process jsonrpc_msg for IDL, takes ownership of jsonrpc_msg
    void MessageProcess(struct jsonrpc_msg *msg);

    Ip4Address remote_ip() const;
    uint16_t remote_port() const;

    ConnectionStateTable *connection_table();
    KSyncObjectManager *ksync_obj_manager();
    OvsPeer *route_peer();
    bool deleted() { return deleted_; }
    Agent *agent() const {return agent_;}
    VMInterfaceKSyncObject *vm_interface_table();
    PhysicalSwitchTable *physical_switch_table();
    LogicalSwitchTable *logical_switch_table();
    PhysicalPortTable *physical_port_table();
    PhysicalLocatorTable *physical_locator_table();
    VlanPortBindingTable *vlan_port_table();
    UnicastMacLocalOvsdb *unicast_mac_local_ovsdb();
    MulticastMacLocalOvsdb *multicast_mac_local_ovsdb();
    VrfOvsdbObject *vrf_ovsdb();
    VnOvsdbObject *vn_ovsdb();
    OvsdbResourceVxLanIdTable *vxlan_table();

    // Used by Test case
    bool IsKeepAliveTimerActive();
    bool IsMonitorInProcess();

    bool KeepAliveTimerCb();
    void TriggerDeletion();
    bool IsDeleted() const { return deleted_; }
    int refcount() const { return refcount_; }

    const TxnStats &stats() const;
    uint64_t pending_txn_count() const;
    uint64_t pending_send_msg_count() const;

    // Concurrency Check to validate all idl transactions happen only in
    // db::DBTable or Agent::KSync task context
    bool ConcurrencyCheck() const;

private:
    friend void ovsdb_wrapper_idl_callback(void *, int, struct ovsdb_idl_row *);
    friend void ovsdb_wrapper_idl_txn_ack(void *, struct ovsdb_idl_txn *);
    friend void intrusive_ptr_add_ref(OvsdbClientIdl *p);
    friend void intrusive_ptr_release(OvsdbClientIdl *p);

    void ConnectOperDB();

    struct ovsdb_idl *idl_;
    const struct vteprec_global *vtep_global_;
    OvsdbClientSession *session_;
    Agent *agent_;
    NotifyCB callback_[OVSDB_TYPE_COUNT];
    PendingTxnMap pending_txn_;
    ThrottledTxnMsgs pending_send_msgs_;
    bool deleted_;
    // Queue for handling OVS messages. Message processing accesses many of the
    // OPER-DB and KSync structures. So, this queue will run in context KSync
    // task
    WorkQueue<OvsdbMsg *> *receive_queue_;
    OvsPeerManager *manager_;
    OvsdbSessionState connection_state_;
    Timer *keepalive_timer_;

    // json for the sent monitor request, used to identify response to the
    // request, reset to NULL once response if feed to idl for processing
    // as it free the json for monitor request id
    struct json *monitor_request_id_;

    // pointer to current buildup of bulk transaction
    struct ovsdb_idl_txn *bulk_txn_;
    // list of entries added to bulk txn
    OvsdbEntryList bulk_entries_;

    // transaction stats per IDL
    TxnStats stats_;

    tbb::atomic<int> refcount_;
    std::auto_ptr<OvsPeer> route_peer_;
    std::auto_ptr<VMInterfaceKSyncObject> vm_interface_table_;
    std::auto_ptr<PhysicalSwitchTable> physical_switch_table_;
    std::auto_ptr<LogicalSwitchTable> logical_switch_table_;
    std::auto_ptr<PhysicalPortTable> physical_port_table_;
    std::auto_ptr<PhysicalLocatorTable> physical_locator_table_;
    std::auto_ptr<VlanPortBindingTable> vlan_port_table_;
    std::auto_ptr<UnicastMacLocalOvsdb> unicast_mac_local_ovsdb_;
    std::auto_ptr<MulticastMacLocalOvsdb> multicast_mac_local_ovsdb_;
    std::auto_ptr<VrfOvsdbObject> vrf_ovsdb_;
    std::auto_ptr<VnOvsdbObject> vn_ovsdb_;
    std::auto_ptr<OvsdbResourceVxLanIdTable> vxlan_table_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientIdl);
};
};  // namespace OVSDB

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_IDL_H_

