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
#include "xmpp/xmpp_session.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_channel_mux.h"

class TcpSession;
class XmppConnection;
class LifetimeActor;
class LifetimeManager;

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

    void Shutdown();
    void SessionShutdown();

    LifetimeManager *lifetime_manager();
    LifetimeActor *deleter();

    virtual TcpSession *CreateSession();
    virtual bool Initialize(short port);
    virtual void Initialize(short port, bool logUVE);
    virtual XmppConnection *FindConnection(Endpoint remote_endpoint);
    virtual XmppConnection *FindConnection(const std::string &peer_addr);
    virtual XmppConnection *FindConnectionbyHostName(const std::string hostname);
    virtual bool RemoveConnection(XmppConnection *);
    virtual void InsertConnection(XmppConnection *);
    virtual void DeleteConnection(XmppConnection *);
    virtual void DestroyConnection(XmppConnection *);
    virtual XmppConnection *CreateConnection(XmppSession *session);

    //Clear a connection
    void ClearConnection(XmppConnection *);
    void ClearAllConnections();

    const std::string &ServerAddr() const { return server_addr_; }
    size_t ConnectionsCount();

protected:
    virtual TcpSession *AllocSession(Socket *socket);
    virtual bool AcceptSession(TcpSession *session);

private:
    class DeleteActor;
    friend class XmppStateMachineTest;
    friend class DeleteActor;

    typedef std::map<Endpoint, XmppConnection *> XmppConnectionMap;
    typedef std::set<XmppConnection *> XmppConnectionSet;
    typedef std::map<xmps::PeerId, ConnectionEventCb> ConnectionEventCbMap;

    typedef boost::ptr_container_detail::ref_pair<
                   boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>, 
                   XmppConnection *const> XmppConnectionPair;
    bool Compare(const std::string &peer_addr, const XmppConnectionPair &) const;

    XmppConnectionMap connection_map_;
    XmppConnectionSet deleted_connection_set_;
    void *bgp_server_;

    boost::scoped_ptr<LifetimeManager> lifetime_manager_;
    boost::scoped_ptr<DeleteActor> deleter_;

    ConnectionEventCbMap connection_event_map_;
    std::string server_addr_; // xmpp server addr
    bool log_uve_;
    bool DequeueConnection(XmppConnection *connection);
    WorkQueue<XmppConnection *> work_queue_;

    DISALLOW_COPY_AND_ASSIGN(XmppServer);
};

#endif
