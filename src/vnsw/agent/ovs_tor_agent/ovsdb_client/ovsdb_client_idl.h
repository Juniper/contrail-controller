/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_IDL_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_IDL_H_

#include <assert.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>

extern SandeshTraceBufferPtr OvsdbTraceBuf;
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
class VrfOvsdbObject;
class OvsdbEntryBase;

class OvsdbClientIdl {
public:
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
    typedef boost::function<void(OvsdbClientIdl::Op, struct ovsdb_idl_row *)> NotifyCB;
    typedef std::map<struct ovsdb_idl_txn *, OvsdbEntryBase *> PendingTxnMap;

    OvsdbClientIdl(OvsdbClientSession *session, Agent *agent, OvsPeerManager *manager);
    virtual ~OvsdbClientIdl();

    // Send request to start monitoring OVSDB server
    void SendMointorReq();
    // Send encode json rpc messgage to OVSDB server
    void SendJsonRpc(struct jsonrpc_msg *msg);
    // Process the recevied message and trigger update to ovsdb client
    void MessageProcess(const u_int8_t *buf, std::size_t len);
    // Create a OVSDB transaction to start encoding an update
    struct ovsdb_idl_txn *CreateTxn(OvsdbEntryBase *entry);
    // Delete the OVSDB transaction
    void DeleteTxn(struct ovsdb_idl_txn *txn);
    void Register(EntryType type, NotifyCB cb) {callback_[type] = cb;}
    void UnRegister(EntryType type) {callback_[type] = NULL;}
    // Get TOR Service Node IP
    Ip4Address tsn_ip();

    KSyncObjectManager *ksync_obj_manager();
    OvsPeer *route_peer();
    Agent *agent() {return agent_;}
    VMInterfaceKSyncObject *vm_interface_table();
    PhysicalSwitchTable *physical_switch_table();
    LogicalSwitchTable *logical_switch_table();
    PhysicalPortTable *physical_port_table();
    PhysicalLocatorTable *physical_locator_table();
    VlanPortBindingTable *vlan_port_table();
    UnicastMacLocalOvsdb *unicast_mac_local_ovsdb();
    VrfOvsdbObject *vrf_ovsdb();

private:
    friend void ovsdb_wrapper_idl_callback(void *, int, struct ovsdb_idl_row *);
    friend void ovsdb_wrapper_idl_txn_ack(void *, struct ovsdb_idl_txn *);

    struct ovsdb_idl *idl_;
    struct json_parser * parser_;
    const struct vteprec_global *vtep_global_;
    OvsdbClientSession *session_;
    Agent *agent_;
    NotifyCB callback_[OVSDB_TYPE_COUNT];
    PendingTxnMap pending_txn_;
    std::auto_ptr<OvsPeer> route_peer_;
    std::auto_ptr<VMInterfaceKSyncObject> vm_interface_table_;
    std::auto_ptr<PhysicalSwitchTable> physical_switch_table_;
    std::auto_ptr<LogicalSwitchTable> logical_switch_table_;
    std::auto_ptr<PhysicalPortTable> physical_port_table_;
    std::auto_ptr<PhysicalLocatorTable> physical_locator_table_;
    std::auto_ptr<VlanPortBindingTable> vlan_port_table_;
    std::auto_ptr<UnicastMacLocalOvsdb> unicast_mac_local_ovsdb_;
    std::auto_ptr<VrfOvsdbObject> vrf_ovsdb_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientIdl);
};
};  // namespace OVSDB

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_IDL_H_

