/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_TCP_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_TCP_H_
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>

#include <base/timer.h>
#include <io/tcp_session.h>
#include <base/queue_task.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>
#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>

namespace OVSDB {
class OvsdbClientTcpSessionReader : public TcpMessageReader {
public:
     OvsdbClientTcpSessionReader(TcpSession *session, ReceiveCallback callback);
     virtual ~OvsdbClientTcpSessionReader();

protected:
    virtual int MsgLength(Buffer buffer, int offset);

    virtual const int GetHeaderLenSize() {
        // We don't have any header
        return 0;
    }

    virtual const int GetMaxMessageSize() {
        return kMaxMessageSize;
    }

private:
    static const int kMaxMessageSize = 4096;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientTcpSessionReader);
};

class OvsdbClientTcpSession : public OvsdbClientSession, public TcpSession {
public:
    static const uint32_t TcpReconnectWait = 2000;      // in msec

    struct queue_msg {
        u_int8_t *buf;
        std::size_t len;
    };

    struct OvsdbSessionEvent {
        TcpSession::Event event;
    };

    OvsdbClientTcpSession(Agent *agent, OvsPeerManager *manager,
            TcpServer *server, Socket *sock, bool async_ready = true);
    ~OvsdbClientTcpSession();

    // Send message to OVSDB server
    void SendMsg(u_int8_t *buf, std::size_t len);
    // Receive message from OVSDB server
    bool RecvMsg(const u_int8_t *buf, std::size_t len);
    // Dequeue received message from workqueue for processing
    bool ReceiveDequeue(queue_msg msg);

    int keepalive_interval();

    KSyncObjectManager *ksync_obj_manager();
    Ip4Address tsn_ip();

    void set_status(std::string status) {status_ = status;}
    std::string status() {return status_;}

    void OnCleanup();

    // method to trigger close of session
    void TriggerClose();

    // Dequeue event from workqueue for processing
    bool ProcessSessionEvent(OvsdbSessionEvent event);

    void EnqueueEvent(TcpSession::Event event);
    bool ReconnectTimerCb();

protected:
    virtual void OnRead(Buffer buffer);
private:
    friend class OvsdbClientTcp;
    std::string status_;
    Timer *client_reconnect_timer_;
    OvsdbClientTcpSessionReader *reader_;
    WorkQueue<queue_msg> *receive_queue_;
    WorkQueue<OvsdbSessionEvent> *session_event_queue_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientTcpSession);
};

class OvsdbClientTcp : public TcpServer, public OvsdbClient {
public:
    OvsdbClientTcp(Agent *agent, TorAgentParam *params,
            OvsPeerManager *manager);
    OvsdbClientTcp(Agent *agent, IpAddress tor_ip, int tor_port,
            IpAddress tsn_ip, OvsPeerManager *manager);
    virtual ~OvsdbClientTcp();

    virtual TcpSession *AllocSession(Socket *socket);
    void RegisterClients();
    void OnSessionEvent(TcpSession *session, TcpSession::Event event);
    const std::string protocol();
    const std::string server();
    uint16_t port();
    Ip4Address tsn_ip();
    const boost::asio::ip::tcp::endpoint &server_ep() const;

    // API to shutdown the TCP server
    void shutdown();

    OvsdbClientSession *next_session(OvsdbClientSession *session);
    void AddSessionInfo(SandeshOvsdbClient &client);

private:
    friend class OvsdbClientTcpSession;
    Agent *agent_;
    TcpSession *session_;
    boost::asio::ip::tcp::endpoint server_ep_;
    Ip4Address tsn_ip_;
    bool shutdown_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientTcp);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_TCP_H_

