/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SSL_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SSL_H_
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>

#include <base/timer.h>
#include <io/ssl_session.h>
#include <base/queue_task.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>
#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <ovsdb_client_tcp.h>

namespace OVSDB {
class OvsdbClientSsl;
class OvsdbClientSslSession : public OvsdbClientSession, public SslSession {
public:
    OvsdbClientSslSession(Agent *agent, OvsPeerManager *manager,
            OvsdbClientSsl *server, SslSocket *sock, bool async_ready = true);
    ~OvsdbClientSslSession();

    // Send message to OVSDB server
    void SendMsg(u_int8_t *buf, std::size_t len);
    // Receive message from OVSDB server
    bool RecvMsg(const u_int8_t *buf, std::size_t len);

    int keepalive_interval();

    // Throttle in flight message to ovsdb-server with ssl.
    // without throttling, during scaled config ssl message gets corrupted
    // in send resulting in connection drop by ovsdb-server and lots of
    // churn in processing of config and ovsdb data.
    // TODO(prabhjot) need to remove this throttling once we find the
    // appropriate reason for write message corruption in SSL.
    bool ThrottleInFlightTxnMessages() { return true; }

    const boost::system::error_code &ovsdb_close_reason() const;

    ConnectionStateTable *connection_table();
    KSyncObjectManager *ksync_obj_manager();
    Ip4Address tsn_ip();

    void set_status(std::string status) {status_ = status;}
    std::string status() {return status_;}

    void OnCleanup();

    // method to trigger close of session
    void TriggerClose();

    // method to return ip address of remoter endpoint
    virtual Ip4Address remote_ip() const;
    virtual uint16_t remote_port() const;

    // Dequeue event from workqueue for processing
    bool ProcessSessionEvent(OvsdbSessionEvent event);

    void EnqueueEvent(TcpSession::Event event);

protected:
    virtual void OnRead(Buffer buffer);

    // the default io::ReaderTask task for TCP session has task exclusion
    // defined with db::DBTable task, Overriding reader task id with
    // OVSDB::IO task to run the message receive and keep alive reply
    // independent of db::DBTable task.
    virtual int reader_task_id() const {
        return ovsdb_io_task_id_;
    }

private:
    friend class OvsdbClientSsl;
    std::string status_;
    OvsdbClientTcpSessionReader *reader_;
    WorkQueue<OvsdbSessionEvent> *session_event_queue_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientSslSession);
};

class OvsdbClientSsl : public SslServer, public OvsdbClient {
public:
    typedef std::pair<Ip4Address, uint16_t> SessionKey;
    typedef std::map<SessionKey, OvsdbClientSslSession *> SessionMap;

    OvsdbClientSsl(Agent *agent, IpAddress tor_ip, int tor_port,
            IpAddress tsn_ip, int keepalive_interval,
            int ha_stale_route_interval, const std::string &ssl_cert,
            const std::string &ssl_privkey, const std::string &ssl_cacert,
            OvsPeerManager *manager);
    virtual ~OvsdbClientSsl();

    virtual SslSession *AllocSession(SslSocket *socket);
    void RegisterClients();
    void OnSessionEvent(TcpSession *session, TcpSession::Event event);
    const std::string protocol();
    const std::string server();
    uint16_t port();
    Ip4Address tsn_ip();

    // API to shutdown the TCP server
    void shutdown();

    OvsdbClientSession *FindSession(Ip4Address ip, uint16_t port);
    OvsdbClientSession *NextSession(OvsdbClientSession *session);
    void AddSessionInfo(SandeshOvsdbClient &client);

private:
    friend class OvsdbClientSslSession;

    // return true to accept incoming session or reject.
    bool AcceptSession(TcpSession *session);

    Agent *agent_;
    uint32_t ssl_server_port_;
    Ip4Address tsn_ip_;
    bool shutdown_;
    SessionMap session_map_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientSsl);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SSL_H_

