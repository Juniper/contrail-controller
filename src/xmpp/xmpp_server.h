/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_SERVER_H__
#define __XMPP_SERVER_H__

#include <boost/asio/ip/tcp.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/scoped_ptr.hpp>
#include "base/lifetime.h"
#include "io/tcp_server.h"
#include "base/queue_task.h"
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
    virtual XmppConnection *FindConnection(const std::string &peer_addr);
    virtual XmppConnection *FindConnectionbyHostName(const std::string hostname);
    virtual void RemoveConnection(XmppConnection *);
    virtual void InsertConnection(XmppConnection *);
                             
    //Clear a connection
    void ClearConnection(XmppConnection *);
    void ClearAllConnections();

    const std::string &ServerAddr() const { return server_addr_; }
    size_t ConnectionsCount() { return connection_map_.size(); }
protected:
    virtual TcpSession *AllocSession(Socket *socket);
    virtual bool AcceptSession(TcpSession *session);

private:
    class DeleteActor;
    friend class XmppStateMachineTest;
    friend class DeleteActor;

    typedef boost::asio::ip::tcp::endpoint endpoint;
    typedef boost::ptr_map<endpoint, XmppConnection> XmppConnectionMap;
    typedef std::map<xmps::PeerId, ConnectionEventCb> ConnectionEventCbMap;

    typedef boost::ptr_container_detail::ref_pair<
                   boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>, 
                   XmppConnection *const> XmppConnectionPair;
    bool Compare(const std::string &peer_addr, const XmppConnectionPair &) const;
    XmppConnection *CreateConnection(XmppSession *session);

    XmppConnectionMap connection_map_;
    void *bgp_server_;

    boost::scoped_ptr<LifetimeManager> lifetime_manager_;
    boost::scoped_ptr<DeleteActor> deleter_;

    ConnectionEventCbMap connection_event_map_;
    std::string server_addr_; // xmpp server addr
    bool log_uve_;
    bool DequeueSession(XmppConnection *connection);
    WorkQueue<XmppConnection *> work_queue_;

    DISALLOW_COPY_AND_ASSIGN(XmppServer);
};

#endif
