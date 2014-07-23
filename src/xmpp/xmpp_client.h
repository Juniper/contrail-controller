/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_CLIENT_H__
#define __XMPP_CLIENT_H__

#include <boost/asio/ip/tcp.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include "io/tcp_server.h"
#include "io/tcp_session.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_connection.h"

class LifetimeActor;
class LifetimeManager;
class XmppSession;

// Class to represent Xmpp Client
// We derive from the common TCP server base class
// which abstracts both server & client side methods.
class XmppClient : public TcpServer {
public:
    typedef boost::asio::ip::tcp::endpoint Endpoint;

    explicit XmppClient(EventManager *evm);
    virtual ~XmppClient();

    void Shutdown();

    typedef boost::function<void(XmppChannelMux *, xmps::PeerState)> 
        ConnectionEventCb;
    void RegisterConnectionEvent(xmps::PeerId, ConnectionEventCb);
    void UnRegisterConnectionEvent(xmps::PeerId);
    void NotifyConnectionEvent(XmppChannelMux *, xmps::PeerState);
    size_t ConnectionEventCount() const;

    virtual TcpSession *CreateSession();
    virtual bool Initialize(short port) ;

    XmppClientConnection *CreateConnection(const XmppChannelConfig *config);
    XmppClientConnection *FindConnection(const std::string &address);
    void InsertConnection(XmppClientConnection *connection);
    void RemoveConnection(XmppClientConnection *connection);
    size_t ConnectionCount() const;

    XmppChannel *FindChannel(const std::string &address);
    void ConfigUpdate(const XmppConfigData *cfg);
    XmppConfigManager *xmpp_config_mgr() { return config_mgr_.get(); }

    LifetimeManager *lifetime_manager();
    LifetimeActor *deleter();

protected:
    virtual TcpSession *AllocSession(Socket *socket);

private:
    class DeleteActor;
    friend class XmppSessionTest; 
    friend class XmppStreamMessageTest; 
    friend class DeleteActor; 

    typedef std::map<Endpoint, XmppClientConnection *> ConnectionMap;
    typedef std::map<xmps::PeerId, ConnectionEventCb> ConnectionEventCbMap;

    void ProcessConfigUpdate(XmppConfigManager::DiffType delta,
        const XmppChannelConfig *current, const XmppChannelConfig *future);

    ConnectionMap connection_map_;
    ConnectionEventCbMap connection_event_map_;

    boost::scoped_ptr<XmppConfigManager> config_mgr_;
    boost::scoped_ptr<LifetimeManager> lifetime_manager_;
    boost::scoped_ptr<DeleteActor> deleter_;

    DISALLOW_COPY_AND_ASSIGN(XmppClient);
};

#endif
