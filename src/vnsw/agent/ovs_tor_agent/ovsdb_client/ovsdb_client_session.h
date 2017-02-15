/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SESSION_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SESSION_H_

#include <assert.h>

#include <base/timer.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <io/tcp_session.h>

class OvsPeer;
class OvsPeerManager;
class KSyncObjectManager;
class SandeshOvsdbClientSession;

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

    // maximum number of inflight txn messages allowed
    virtual bool ThrottleInFlightTxnMessages() { return false; }

    virtual const boost::system::error_code &ovsdb_close_reason() const = 0;
    virtual ConnectionStateTable *connection_table() = 0;
    virtual KSyncObjectManager *ksync_obj_manager() = 0;
    virtual Ip4Address remote_ip() const = 0;
    virtual uint16_t remote_port() const = 0;
    virtual Ip4Address tsn_ip() = 0;
    virtual std::string status() = 0;
    virtual void SendMsg(u_int8_t *buf, std::size_t len) = 0;
    void MessageProcess(const u_int8_t *buf, std::size_t len);
    // Send encode json rpc messgage to OVSDB server
    void SendJsonRpc(struct jsonrpc_msg *msg);
    void OnEstablish();
    void OnClose();
    OvsdbClientIdl *client_idl();

    void AddSessionInfo(SandeshOvsdbClientSession &session);

    // UT overrides this to allow concurrency check on NULL task
    virtual bool TestConcurrencyAllow() { return false; }

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
    std::string connection_time_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientSession);
};
};  // namespace OVSDB

#define OVSDB_SM_TRACE(obj, ...)\
do {\
    Ovsdb##obj::TraceMsg(OvsdbSMTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(false);

#define OVSDB_SESSION_TRACE(obj, session, ...)\
do {\
    Ovsdb##obj::TraceMsg(OvsdbSMTraceBuf, __FILE__, __LINE__,\
                         "Session " + session->remote_ip().to_string() +\
                         ":" + integerToString(session->remote_port()) +\
                         " - " + __VA_ARGS__);\
} while(false);

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SESSION_H_

