/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_SERVER_H__
#define __XMPP_SERVER_H__

#include <boost/asio/ip/tcp.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/lifetime.h"
#include "base/queue_task.h"
#include "io/tcp_server.h"
#include "net/address.h"
#include "xmpp/xmpp_session.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_channel_mux.h"

class LifetimeActor;
class LifetimeManager;
class TcpSession;
class XmppConnectionEndpoint;
class XmppServerConnection;

// Class to represent Xmpp Server
class XmppServer : public TcpServer {
public:
    typedef boost::asio::ip::tcp::endpoint Endpoint;

    XmppServer(EventManager *evm, const std::string &server_addr);
    explicit XmppServer(EventManager *evm);
    virtual ~XmppServer();
    virtual bool IsPeerCloseGraceful();

    typedef boost::function<void(XmppChannelMux *, xmps::PeerState)> ConnectionEventCb;
    void RegisterConnectionEvent(xmps::PeerId, ConnectionEventCb);
    void UnRegisterConnectionEvent(xmps::PeerId);
    void NotifyConnectionEvent(XmppChannelMux *, xmps::PeerState);
    size_t ConnectionEventCount() const;

    LifetimeManager *lifetime_manager();
    LifetimeActor *deleter();

    virtual TcpSession *CreateSession();
    virtual bool Initialize(short port);
    virtual void Initialize(short port, bool logUVE);
    void SessionShutdown();
    void Shutdown();
    void Terminate();

    virtual XmppServerConnection *CreateConnection(XmppSession *session);
    virtual XmppServerConnection *FindConnection(Endpoint remote_endpoint);
    virtual XmppServerConnection *FindConnection(const std::string &address);
    virtual void InsertConnection(XmppServerConnection *connection);
    virtual void RemoveConnection(XmppServerConnection *connection);

    virtual void InsertDeletedConnection(XmppServerConnection *connection);
    virtual void RemoveDeletedConnection(XmppServerConnection *connection);

    bool ClearConnection(const std::string &hostname);
    void ClearAllConnections();

    const std::string &ServerAddr() const { return server_addr_; }
    size_t ConnectionCount() const;

    const XmppConnectionEndpoint *FindConnectionEndpoint(
        Ip4Address address) const;
    XmppConnectionEndpoint *LocateConnectionEndpoint(Ip4Address address);

protected:
    virtual TcpSession *AllocSession(Socket *socket);
    virtual bool AcceptSession(TcpSession *session);

private:
    class DeleteActor;
    friend class BgpXmppBasicTest;
    friend class DeleteActor;
    friend class XmppStateMachineTest;

    typedef std::map<Endpoint, XmppServerConnection *> ConnectionMap;
    typedef std::set<XmppServerConnection *> ConnectionSet;
    typedef std::map<Ip4Address, XmppConnectionEndpoint *> ConnectionEndpointMap;
    typedef std::map<xmps::PeerId, ConnectionEventCb> ConnectionEventCbMap;

    bool DequeueConnection(XmppServerConnection *connection);
    size_t GetQueueSize() const;
    void SetQueueDisable(bool disabled);

    ConnectionMap connection_map_;
    ConnectionSet deleted_connection_set_;
    ConnectionEndpointMap connection_endpoint_map_;
    void *bgp_server_;

    tbb::mutex deletion_mutex_;
    boost::scoped_ptr<LifetimeManager> lifetime_manager_;
    boost::scoped_ptr<DeleteActor> deleter_;

    ConnectionEventCbMap connection_event_map_;
    std::string server_addr_;
    bool log_uve_;
    WorkQueue<XmppServerConnection *> work_queue_;

    DISALLOW_COPY_AND_ASSIGN(XmppServer);
};

#endif
