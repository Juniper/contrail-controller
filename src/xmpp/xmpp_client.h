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
class XmppSessionTest;

// Class to represent Xmpp Client
// We derive from the common TCP server base class
// which abstracts both server & client side methods.
class XmppClient : public TcpServer {
public:
    explicit XmppClient(EventManager *evm);
    virtual ~XmppClient();

    void Shutdown();
    void SessionShutdown(); 
    typedef boost::function<void(XmppChannelMux *, xmps::PeerState)> 
        ConnectionEventCb;
    void RegisterConnectionEvent(xmps::PeerId, ConnectionEventCb);
    void UnRegisterConnectionEvent(xmps::PeerId);
    void NotifyConnectionEvent(XmppChannelMux *, xmps::PeerState);
    size_t ConnectionEventCount() const;

    virtual TcpSession *CreateSession();
    virtual bool Initialize(short port) ;
    XmppConnection *FindConnection(const std::string &server_addr);
    XmppChannel *FindChannel(const std::string &server_addr);
    void RemoveConnection(XmppConnection *);
    void ConfigUpdate(const XmppConfigData *cfg);
    XmppConfigManager *xmpp_config_mgr() { return config_mgr_.get(); }
    XmppConnection *CreateConnection(const XmppChannelConfig *config);

    LifetimeManager *lifetime_manager();
    LifetimeActor *deleter();

protected:
    virtual TcpSession *AllocSession(Socket *socket);

private:
    class DeleteActor;
    friend class XmppSessionTest; 
    friend class XmppStreamMessageTest; 
    friend class DeleteActor; 
    typedef boost::ptr_map<boost::asio::ip::tcp::endpoint, 
                           XmppConnection> XmppConnectionMap;
    typedef std::map<xmps::PeerId, ConnectionEventCb> ConnectionEventCbMap;

    void ProcessConfigUpdate(XmppConfigManager::DiffType delta,
                             const XmppChannelConfig *current,
                             const XmppChannelConfig *future);
    typedef boost::ptr_container_detail::ref_pair<
                   boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>, 
                   XmppConnection *const> XmppConnectionPair;
    bool Compare(const std::string &server, const XmppConnectionPair &) const;
    XmppConnectionMap connection_map_;
    std::auto_ptr<XmppConfigManager> config_mgr_;
    ConnectionEventCbMap connection_event_map_;

    boost::scoped_ptr<LifetimeManager> lifetime_manager_;
    boost::scoped_ptr<DeleteActor> deleter_;

    DISALLOW_COPY_AND_ASSIGN(XmppClient);
};

#endif
