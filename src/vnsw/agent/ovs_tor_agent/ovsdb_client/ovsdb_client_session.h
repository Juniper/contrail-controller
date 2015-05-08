/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SESSION_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SESSION_H_

#include <assert.h>

#include <base/timer.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

class OvsPeer;
class OvsPeerManager;
class KSyncObjectManager;

namespace OVSDB {
class OvsdbClientIdl;
class OvsdbClientSession {
public:
    struct OvsdbSessionEvent {
        TcpSession::Event event;
    };

    OvsdbClientSession(Agent *agent, OvsPeerManager *manager);
    virtual ~OvsdbClientSession();

    // Callback triggered for session cleanup
    virtual void OnCleanup() = 0;

    // method to trigger close of session
    virtual void TriggerClose() = 0;

    virtual int keepalive_interval() = 0;

    virtual ConnectionStateTable *connection_table() = 0;
    virtual KSyncObjectManager *ksync_obj_manager() = 0;
    virtual Ip4Address remote_ip() { return Ip4Address(); }
    virtual uint16_t remote_port() { return 0; }
    virtual Ip4Address tsn_ip() = 0;
    virtual void SendMsg(u_int8_t *buf, std::size_t len) = 0;
    void MessageProcess(const u_int8_t *buf, std::size_t len);
    // Send encode json rpc messgage to OVSDB server
    void SendJsonRpc(struct jsonrpc_msg *msg);
    void OnEstablish();
    void OnClose();
    OvsdbClientIdl *client_idl();

protected:
    // ovsdb io task ID.
    static int ovsdb_io_task_id_;

private:
    friend class OvsdbClientIdl;
    OvsdbClientIdlPtr client_idl_;
    Agent *agent_;
    OvsPeerManager *manager_;
    struct json_parser * parser_;
    tbb::atomic<bool> idl_inited_;
    Timer *monitor_req_timer_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientSession);
};
};  // namespace OVSDB

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SESSION_H_

